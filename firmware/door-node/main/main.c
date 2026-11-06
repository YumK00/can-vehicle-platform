/**
 * @file    main.c
 * @brief   门锁节点（第二块 ESP32-S3）：ESP-NOW 接收 + SG90 舵机
 *
 * 工作流程：
 *   1. 开机后 WiFi 只启动、不连路由器；ESP-NOW 初始化；
 *   2. 信道未知，先 1~11 跳频扫描，收到第一个合法包后锁定信道；
 *   3. 收到主控的 MSG_DISCOVER → 广播 MSG_ANNOUNCE（主控据此学到我方 MAC）；
 *   4. 收到 MSG_CMD → 驱动舵机开锁（3 秒后自动回锁）或上锁；
 *   5. 同时记住主控 MAC，便于后续扩展单播/ACK。
 */
#include <stdio.h>
#include <string.h>

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define DOOR_TAG "ESP_NOW_DOOR"

/* ==================== 协议定义（必须与主控 espnow_link.cc 完全一致） ==================== */
#define DOOR_MAGIC  0x444F4F52   /* "DOOR" */

#define ROLE_CTRL   0            /* 主控板 */
#define ROLE_LOCK   1            /* 锁节点（本机） */

#define MSG_DISCOVER 0           /* 主控广播：找锁节点 */
#define MSG_ANNOUNCE 1           /* 锁节点广播：我是锁节点 */
#define MSG_CMD      2           /* 主控发命令 */

typedef struct {
    uint32_t magic;
    uint8_t  type;               /* MSG_xxx */
    uint8_t  role;               /* ROLE_xxx */
    uint8_t  cmd;                /* type=MSG_CMD 时有效：1=开锁 0=上锁 */
    uint8_t  seq;
    uint32_t counter;            /* 防重放：发送方单调递增，收端只接受更大的值 */
} door_msg_t;                    /* 12 字节 */

/* ==================== AES-128 加密（ESP-NOW 内置 CCMP） ==================== */
/* 必须和主控 espnow_link.cc 里的两个密钥完全一致；量产要改成一机一密 + 配对派生。 */
static const uint8_t ESPNOW_PMK[16] = {
    0x5A, 0x11, 0x3C, 0x7E, 0x92, 0x04, 0xB8, 0x6D,
    0x21, 0xF5, 0x43, 0xA9, 0x0C, 0x88, 0xD7, 0x31,
};
static const uint8_t ESPNOW_LMK[16] = {
    0x9E, 0x27, 0xB4, 0x61, 0x0F, 0xDA, 0x55, 0x83,
    0x17, 0x6C, 0xE2, 0x90, 0x4B, 0xF8, 0x33, 0xAC,
};

/* ==================== 舵机参数 ==================== */
#define SERVO_GPIO       GPIO_NUM_4
#define SERVO_LOCK_DEG   10      /* 上锁角度 */
#define SERVO_OPEN_DEG   80      /* 开锁角度 */
#define SERVO_LEDC_CH    LEDC_CHANNEL_0
#define ESPNOW_WIFI_CH   1       /* 未连路由器时的初始信道 */

/* ==================== 内部状态 ==================== */
static const uint8_t BCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static volatile bool s_ch_locked = false;   /* 是否已锁定主控信道 */
static uint8_t s_ctrl_mac[6] = {0};         /* 主控 MAC（从收到的包学习） */
static bool    s_has_ctrl = false;
static uint32_t s_last_cmd_counter = 0;     /* 已处理的最大 CMD 计数器（防重放） */
static uint32_t s_tx_counter = 0;           /* 我方发送计数器 */

/* 把主控 MAC 加为加密单播 peer（AES-128-CCMP，LMK 与主控一致） */
static bool add_ctrl_peer_encrypted(void) {
    if (esp_now_is_peer_exist(s_ctrl_mac)) {
        return true;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_ctrl_mac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    memcpy(peer.lmk, ESPNOW_LMK, 16);
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(DOOR_TAG, "add encrypted ctrl peer failed: %d", (int)err);
        return false;
    }
    ESP_LOGI(DOOR_TAG, "ctrl peer added (encrypted)");
    return true;
}

/* 舵机初始化：LEDC 50 Hz，14 bit 分辨率 */
static void servo_init(void) {
    ledc_timer_config_t ledc_timer = {
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .freq_hz = 50,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t c = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SERVO_LEDC_CH,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&c);
}

/* 设置舵机角度：0~180 度映射到 500~2500us 脉宽 */
static void servo_set_deg(int deg) {
    if (deg < 0) deg = 0;
    if (deg > 180) deg = 180;
    uint32_t pulse_us = 500 + (uint32_t)deg * 2000 / 180;
    uint32_t duty = (uint32_t)((uint64_t)pulse_us * (1 << 14) / 20000);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_LEDC_CH);
}

/* 启动 WiFi 驱动（ESP-NOW 的前置条件，不需要连 AP） */
static void wifi_init_for_espnow(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_WIFI_CH, WIFI_SECOND_CHAN_NONE));
}

/* 广播一条 ANNOUNCE，告诉主控"我是锁节点，我的 MAC 是 src_addr" */
static void send_announce(void) {
    door_msg_t msg = {
        .magic = DOOR_MAGIC,
        .type = MSG_ANNOUNCE,
        .role = ROLE_LOCK,
        .cmd = 0,
        .seq = 0,
        .counter = ++s_tx_counter,
    };
    esp_now_send(BCAST_MAC, (uint8_t *)&msg, sizeof(msg));
}

/* ESP-NOW 接收回调：处理 DISCOVER / CMD 两类消息 */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != (int)sizeof(door_msg_t)) {
        return;
    }
    const door_msg_t *msg = (const door_msg_t *)data;
    if (msg->magic != DOOR_MAGIC) {
        return;
    }

    s_ch_locked = true;   /* 收到合法包就锁定信道，停止跳频 */

    /* 学习主控 MAC（谁发的包就认谁） */
    memcpy(s_ctrl_mac, info->src_addr, 6);
    s_has_ctrl = true;

    if (msg->type == MSG_DISCOVER) {
        ESP_LOGI(DOOR_TAG, "DISCOVER from " MACSTR " -> ANNOUNCE", MAC2STR(info->src_addr));
        /* 学到主控 MAC 后，立刻建立加密单播链路（之后的 CMD 都是密文） */
        add_ctrl_peer_encrypted();
        send_announce();
        return;
    }

    if (msg->type != MSG_CMD) {
        return;
    }

    /* 防重放：只接受比上次更大的 counter；
     * 若 counter 突然比上次小很多（主控重启后从 0 开始），按"重启"处理并重置基线。 */
    if (msg->counter <= s_last_cmd_counter) {
        if (s_last_cmd_counter - msg->counter > 50) {
            ESP_LOGW(DOOR_TAG, "counter reset detected (ctrl reboot?), old=%u new=%u",
                     (unsigned)s_last_cmd_counter, (unsigned)msg->counter);
        } else {
            ESP_LOGW(DOOR_TAG, "replay/dup dropped: counter=%u last=%u",
                     (unsigned)msg->counter, (unsigned)s_last_cmd_counter);
            return;
        }
    }
    s_last_cmd_counter = msg->counter;

    /* 主控会连发几包保证送达：3.5 秒内重复的开锁命令只执行一次 */
    static TickType_t s_last_open = 0;
    if (msg->cmd == 1) {
        TickType_t now = xTaskGetTickCount();
        if (s_last_open != 0 && (now - s_last_open) < pdMS_TO_TICKS(3500)) {
            return;
        }
        s_last_open = now;
    }

    ESP_LOGI(DOOR_TAG, "CMD cmd=%d from " MACSTR, msg->cmd, MAC2STR(info->src_addr));
    if (msg->cmd == 1) {
        servo_set_deg(SERVO_OPEN_DEG);
        ESP_LOGI(DOOR_TAG, "servo -> OPEN");
        vTaskDelay(pdMS_TO_TICKS(3000));   /* 开锁保持 3 秒 */
        servo_set_deg(SERVO_LOCK_DEG);
        ESP_LOGI(DOOR_TAG, "servo -> LOCK (auto)");
    } else {
        servo_set_deg(SERVO_LOCK_DEG);
        ESP_LOGI(DOOR_TAG, "servo -> LOCK");
    }
}

/* 信道扫描任务：不知道主控在哪个信道，1~11 循环切换，收到包后退出 */
static void channel_scan_task(void *arg) {
    (void)arg;
    uint8_t ch = 1;
    while (!s_ch_locked) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        ESP_LOGI(DOOR_TAG, "scan channel %d", ch);
        ch = (ch % 11) + 1;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ESP_LOGI(DOOR_TAG, "channel locked");
    vTaskDelete(NULL);
}

void app_main(void) {
    nvs_flash_init();

    /* 1) WiFi + ESP-NOW */
    wifi_init_for_espnow();

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(DOOR_TAG, "esp_now_init failed: %d", (int)err);
        return;
    }

    /* 设置 PMK：必须早于任何加密 peer 的添加 */
    err = esp_now_set_pmk(ESPNOW_PMK);
    if (err != ESP_OK) {
        ESP_LOGE(DOOR_TAG, "esp_now_set_pmk failed: %d", (int)err);
        return;
    }

    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    /* 2) 加广播 peer（发 ANNOUNCE 用）；广播不支持加密，保持明文 */
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BCAST_MAC, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    /* 3) 打印本机 MAC（调试/记录用） */
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(DOOR_TAG, "lock node MAC = " MACSTR, MAC2STR(mac));

    /* 4) 舵机初始化并上锁 */
    servo_init();
    servo_set_deg(SERVO_LOCK_DEG);

    /* 5) 上电先广播一次 ANNOUNCE，让主控不用等发现周期 */
    send_announce();
    ESP_LOGI(DOOR_TAG, "lock node ready");

    /* 6) 开始跳频找主控信道 */
    xTaskCreate(channel_scan_task, "ch_scan", 2048, NULL, 3, NULL);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
