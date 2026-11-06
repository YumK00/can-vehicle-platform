# ESP32-S3 门锁节点（door-node）

> 平台的**独立扩展节点**：不接 CAN 总线、不连 MQTT，用 ESP-NOW 私有协议接收主控的开门命令，
> 驱动 SG90 舵机完成"开锁 → 保持 3 秒 → 自动回锁"。它服务于"车门"这类非 CAN 执行器的演示场景，
> **主链路（CAN ↔ 网关 ↔ 上位机）不依赖它**。

---

## 一、项目定位

| 项 | 内容 |
|---|---|
| 在链路中的角色 | 扩展执行器：把无线命令落成舵机角度，演示"非 CAN 执行器"的接入方式 |
| 通信方式 | ESP-NOW（WiFi 私有链路，信道 1~11 跳频搜索），**不使用 TWAI/CAN** |
| 与主链路耦合 | 无。既不订阅 `gw0805` 主题树，也不出现在 `protocol/contract.json` 的帧表或 MQTT 主题里 |
| 私有协议对端 | 主控板的 `espnow_link.cc`（按 `main.c` 注释）——该文件不在本仓库内 |
| 工程形态 | 精简 ESP-IDF 工程：`CMakeLists.txt` + `main/`（`main.c`、`CMakeLists.txt`）+ 已配好的 `sdkconfig` |

一句话说明它在平台中的位置：**主链路走 CAN，门锁走 ESP-NOW**。两者共享同一个 ESP32-S3 硬件家族与 ESP-IDF 工具链，但协议、数据通路、部署位置都是分开的。

---

## 二、目录结构

```text
firmware/door-node/
├── CMakeLists.txt                  # 工程入口：project(door_node)
├── sdkconfig                       # 已配置好的工程配置（target=esp32s3、16MB flash、单 app 分区）
├── main/
│   ├── CMakeLists.txt              # idf_component_register(SRCS "main.c")
│   └── main.c                      # 全部逻辑：ESP-NOW 私有协议 + LEDC 舵机
├── .vscode/                        # c_cpp_properties / launch / settings
└── .devcontainer/                  # devcontainer.json + Dockerfile
```

与网关工程相比，本工程**没有** `partitions-16MiB.csv`、`dependencies.lock`、`idf_component.yml`、`component/` 分层目录，
也没有 `.clangd`；分区表使用 IDF 默认的单 app 方案（见第七节）。

---

## 三、核心模块

本工程的 C 代码只有一个文件 [`main/main.c`](main/main.c)，按功能可分成五块：

| 区块 | 内容 | 关键点 |
|---|---|---|
| 私有协议定义 | `DOOR_MAGIC = 0x444F4F52`（即 ASCII `"DOOR"`）、`ROLE_CTRL = 0`、`ROLE_LOCK = 1`、`MSG_DISCOVER = 0`、`MSG_ANNOUNCE = 1`、`MSG_CMD = 2` | 全部宏硬编码在 `main.c`，不走 `protocol/contract.json` |
| 报文结构 | `door_msg_t`：`magic`(4) + `type`(1) + `role`(1) + `cmd`(1) + `seq`(1) + `counter`(4)，注释标注 12 字节 | 收发都用 `sizeof(door_msg_t)` 直接对结构体内存操作，没有字节序转换函数 |
| ESP-NOW 安全 | `ESPNOW_PMK[16]`、`ESPNOW_LMK[16]` 两组常量 | 广播 peer `encrypt = false`（明文）；主控 peer `encrypt = true` 并带 LMK（AES-128-CCMP） |
| 舵机驱动 | `servo_init()` 建 LEDC 定时器与通道，`servo_set_deg()` 把 0~180 度映射成 500~2500 µs 脉宽 | 50 Hz、14 bit 分辨率、`LEDC_LOW_SPEED_MODE`、`LEDC_TIMER_0` / `LEDC_CHANNEL_0` |
| 主流程与任务 | `wifi_init_for_espnow()`、`send_announce()`、`on_recv()`、`channel_scan_task()`、`app_main()` | 详见第四节 |

`app_main()` 的实际顺序：

```text
nvs_flash_init()
  → wifi_init_for_espnow()        # esp_netif_init / 默认事件循环 / esp_wifi_init / STA / start / set_channel(1)
  → esp_now_init()
  → esp_now_set_pmk(ESPNOW_PMK)   # 必须早于任何加密 peer 的添加
  → esp_now_register_recv_cb(on_recv)
  → 添加广播 peer（明文）
  → 打印本机 STA MAC
  → servo_init() + servo_set_deg(SERVO_LOCK_DEG)   # 上电先上锁
  → send_announce()               # 上电先广播一次，主控不必等发现周期
  → xTaskCreate(channel_scan_task, "ch_scan", 2048, NULL, 3, NULL)
  → while (true) vTaskDelay(1000)
```

---

## 四、FreeRTOS 任务与中断

`sdkconfig` 里 `CONFIG_FREERTOS_HZ = 100`（1 tick = 10 ms，与网关的 1000 Hz 不同），
`CONFIG_ESP_MAIN_TASK_STACK_SIZE = 3584`，`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ = 160`。

| 任务 / 上下文 | 优先级 | 栈（字节） | 周期 | 职责 |
|---|---|---|---|---|
| `main`（`app_main`） | IDF 默认 | 3584 | 一次性 + 末尾 1 s 空转 | 初始化 WiFi/ESP-NOW/舵机，广播一次 ANNOUNCE，最后只 `vTaskDelay(1000)` |
| `ch_scan` | 3 | 2048 | 每信道 200 ms | 信道 1→11 循环切换，收到第一个合法包后 `vTaskDelete(NULL)` 自行退出 |
| ESP-NOW 接收回调 `on_recv` | 运行在 WiFi/ESP-NOW 接收任务上下文 | 复用该任务栈 | 事件驱动 | 校验长度与 magic、学习主控 MAC、建加密 peer、处理 DISCOVER/CMD、驱动舵机 |
| `esp_timer` / WiFi 内部任务 | IDF 默认 | IDF 默认 | — | 由组件自行创建，工程未改动 |

中断：本工程**没有**注册任何应用级 ISR（舵机由 LEDC 硬件产生波形；收包由 ESP-NOW 协议栈转为回调），因此没有中断优先级表可列。

同步与队列：

| 类型 | 名称 | 说明 |
|---|---|---|
| 队列 / 信号量 | 无 | 工程没有创建任何消息队列、信号量或互斥量 |
| 原子标志 | `s_ch_locked`（`volatile bool`） | 收到合法包后置 true，`ch_scan` 据此退出 |
| 状态变量 | `s_ctrl_mac[6]`、`s_has_ctrl`、`s_last_cmd_counter`、`s_tx_counter` | 主控 MAC、是否已学到主控、防重放基线、本方发送计数 |

---

## 五、收发的 ESP-NOW 报文

本工程**不发送也不接收任何 CAN 帧**。下表是它在 ESP-NOW 上收发的私有报文（字段顺序即结构体顺序）：

| `type` | 名称 | 方向 | 发送方式 | 字段与处理 |
|---|---|---|---|---|
| `0` | `MSG_DISCOVER` | 主控 → 本节点 | 主控广播 | 收到后学习 `src_addr`、调用 `add_ctrl_peer_encrypted()` 建加密 peer，然后回一包 ANNOUNCE |
| `1` | `MSG_ANNOUNCE` | 本节点 → 主控 | 广播（`FF:FF:FF:FF:FF:FF`，明文） | 载荷 `type = 1`、`role = ROLE_LOCK`、`cmd = 0`、`seq = 0`、`counter = ++s_tx_counter`；上电发一次，收到 DISCOVER 再发一次 |
| `2` | `MSG_CMD` | 主控 → 本节点 | 单播 + 加密 | `cmd == 1` → 开锁（`SERVO_OPEN_DEG`），保持 3 秒后回锁；`cmd` 其他值 → 上锁（`SERVO_LOCK_DEG`）；`counter` 用于防重放 |

`door_msg_t` 的完整布局（按 `main.c` 的字段顺序）：

| 偏移 | 字段 | 类型 | 含义 |
|---|---|---|---|
| 0 | `magic` | `uint32_t` | 固定 `0x444F4F52` |
| 4 | `type` | `uint8_t` | `MSG_xxx` |
| 5 | `role` | `uint8_t` | `ROLE_CTRL` / `ROLE_LOCK` |
| 6 | `cmd` | `uint8_t` | `type == MSG_CMD` 时有效：1 开锁 / 0 上锁 |
| 7 | `seq` | `uint8_t` | 本节点发送时填 0，接收侧未使用 |
| 8 | `counter` | `uint32_t` | 发送方单调递增，收端只接受更大的值 |

接收侧的三道过滤（都在 `on_recv()` 里，顺序执行）：

1. `len != sizeof(door_msg_t)` → 直接丢弃；
2. `magic != DOOR_MAGIC` → 直接丢弃（不锁信道、不学 MAC）；
3. `MSG_CMD` 的 `counter <= s_last_cmd_counter`：差值 `> 50` 视为"主控重启"并接受，否则按重放/重复丢弃。

另有开锁去重：`msg->cmd == 1` 且距上次开锁不足 3500 ms 时直接返回（注释说明主控会连发几包保证送达）。

---

## 六、硬件与引脚

| 引脚 | 功能 | 说明 |
|---|---|---|
| `GPIO4` | SG90 舵机 PWM（`SERVO_GPIO`） | 由 LEDC 通道 0 输出，50 Hz |

舵机参数：

| 项 | 值 |
|---|---|
| LEDC 模式 / 定时器 / 通道 | `LEDC_LOW_SPEED_MODE` / `LEDC_TIMER_0` / `LEDC_CHANNEL_0` |
| 频率 / 分辨率 | 50 Hz / 14 bit |
| 角度 → 脉宽 | `pulse_us = 500 + deg * 2000 / 180`（0° → 500 µs，180° → 2500 µs） |
| 占空比换算 | `duty = pulse_us * (1 << 14) / 20000` |
| 上锁 / 开锁角 | `SERVO_LOCK_DEG = 10`、`SERVO_OPEN_DEG = 80` |

无线侧：ESP32-S3 片内 WiFi 既做 ESP-NOW 载波，也决定了信道；`ESPNOW_WIFI_CH = 1` 只是初始信道，实际靠 `ch_scan` 任务在 1~11 之间跳频，直到收到第一包合法报文为止。工程不使用 CAN 收发器、不占用 TWAI 引脚，也不使用板载 WS2812。

---

## 七、构建与烧录

环境：ESP-IDF（与网关同一套工具链即可），target `esp32s3`。仓库自带 `sdkconfig`，无需重新 `menuconfig`。

```bash
cd firmware/door-node
idf.py set-target esp32s3
idf.py build
idf.py -p COM20 flash monitor      # 串口按实际端口替换
```

`sdkconfig` 里与运行行为相关的项：

| 配置 | 值 |
|---|---|
| `CONFIG_IDF_TARGET` | `esp32s3` |
| `CONFIG_ESPTOOLPY_FLASHSIZE` | `16MB` |
| `CONFIG_PARTITION_TABLE_SINGLE_APP` | `y`（分区表为 IDF 默认的 `partitions_singleapp.csv`，工程未附带自定义 CSV） |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` | `160` |
| `CONFIG_FREERTOS_HZ` | `100` |
| `CONFIG_ESP_MAIN_TASK_STACK_SIZE` | `3584` |
| `CONFIG_LOG_DEFAULT_LEVEL` | `3`（INFO） |

串口里可用来确认状态的日志（TAG 为 `ESP_NOW_DOOR`）：`lock node MAC = …`、`scan channel N`、`channel locked`、`ctrl peer added (encrypted)`、`CMD cmd=1 from …`、`servo -> OPEN` / `servo -> LOCK (auto)`。

---

## 八、与其他组件的关系

| 对象 | 关系 |
|---|---|
| [`../../README.md`](../../README.md) | 平台总览；第十一节明确本节点是"独立扩展节点，不接 CAN 总线，主链路不依赖它" |
| [`../../docs/architecture.md`](../../docs/architecture.md) | 第 1 节末尾同样把它列为独立扩展 |
| [`../gateway/README.md`](../gateway/README.md) | 网关（TWAI ⇄ MQTT），与本节点无数据通路 |
| [`../collect-node/README.md`](../collect-node/README.md) | 采集节点 A（`NODE_A = 1`） |
| [`../control-node/README.md`](../control-node/README.md) | 执行节点 B（`NODE_B = 2`） |
| [`../../protocol/contract.json`](../../protocol/contract.json) | CAN 帧表与 MQTT 主题的单一真源；**不包含**本节点的 ESP-NOW 私有协议 |
| [`../../protocol/generated/contract.md`](../../protocol/generated/contract.md) | 同上，供对照"哪些协议受契约管理" |
| [`../../host/desktop-console`](../../host/desktop-console) | 上位机；本节点不发 MQTT，因此上位机不会出现它的数据 |

也就是说：改 `protocol/contract.json` 或跑 `tools/gen_contract.py` 不会影响本工程；反过来，改本工程的 `door_msg_t`、`DOOR_MAGIC`、PMK/LMK 也不会被任何副本校验拦住——**它必须与仓库外的主控 `espnow_link.cc` 手工保持一致**（`main.c` 的两处注释都写明了这一点）。

---

## 九、已知限制与注意事项

以下均为代码中确实存在的状态：

1. **接收回调里有 3 秒阻塞。** `on_recv()` 在开锁分支中直接 `vTaskDelay(pdMS_TO_TICKS(3000))` 等待回锁，而该回调运行在 WiFi/ESP-NOW 接收任务上下文。这 3 秒内该任务无法处理新报文；代码里没有把"延时回锁"交给独立任务或定时器。
2. **私有协议与密钥都硬编码。** `DOOR_MAGIC`、`ROLE_*`、`MSG_*` 以及 `ESPNOW_PMK[16]`、`ESPNOW_LMK[16]` 全部写死在 `main.c`；`main.c` 注释写明"量产要改成一机一密 + 配对派生"。这套报文不在 `protocol/contract.json` 内，没有生成器与副本校验覆盖。
3. **报文按结构体内存直接收发，没有序列化层。** `esp_now_send(..., (uint8_t *)&msg, sizeof(msg))` 发送、`(const door_msg_t *)data` 强转接收，意味着两端的字节序、字段对齐、结构体填充必须完全一致（ESP32 为小端）；对端是另一块 ESP32-S3 时成立，换成其他平台需要自己补编解码。
4. **`seq` 字段没有被使用。** `send_announce()` 里固定 `seq = 0`，接收侧从不读取 `msg->seq`；去重与时序完全依赖 `counter`。
5. **`nvs_flash_init()` 的返回值没有检查。** 网关工程里对 `ESP_ERR_NVS_NO_FREE_PAGES` / `ESP_ERR_NVS_NEW_VERSION_FOUND` 做了擦除重试，本工程直接调用后继续往下走。
6. **上电首次 ANNOUNCE 很可能发在错误信道上。** `send_announce()` 在 `ch_scan` 任务创建之前执行，此时信道固定为 `ESPNOW_WIFI_CH = 1`；主控若不在信道 1，这一包收不到，实际发现过程依赖主控发 `MSG_DISCOVER` 触发回应。
7. **工程缺少网关那套工程化文件。** 没有 `partitions-16MiB.csv`、`dependencies.lock`、`idf_component.yml`，也没有 `component/` 分层；`sdkconfig` 用的是单 app 分区表，`sdkconfig` 里声明的 16 MB flash 并没有被分区表用满。
8. **只有一路舵机、没有位置反馈。** `servo_set_deg()` 是开环输出脉宽，代码里没有舵机角度回读或堵转检测；`SERVO_LOCK_DEG` / `SERVO_OPEN_DEG` 两个角度也是写死的常量。
