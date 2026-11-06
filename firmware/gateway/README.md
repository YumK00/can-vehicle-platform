# ESP32-S3 边缘网关（gateway）

> 平台的**边缘层**：一侧用 ESP32-S3 片内 TWAI 控制器接 250 kbps CAN 2.0A 总线，
> 另一侧经 WiFi 连 MQTT broker，做 **CAN ⇄ MQTT 双向桥接**，并负责节点判活、运行统计与 bus-off 自愈。
> 本工程在契约里占节点号 `NODE_GW = 3`，心跳帧 `0x703`、周期状态帧 `0x103`。

---

## 一、项目定位

网关把一条只有 8 字节一帧的 CAN 总线接到可读的 JSON 主题树上，三条数据路径各自有独立的执行体（见 [`component/MiddleWare/CAN/can.c`](component/MiddleWare/CAN/can.c) 文件头）：

| 路径 | 执行者 | 说明 |
|---|---|---|
| CAN 收到 → MQTT 发出 | `can_rx_task` 收帧分发 + `pub_telemetry()` 入队 + `can_pub_task` 实发 | 按 ID 查表解包成 JSON |
| MQTT 收到 → CAN 发出 | `esp-mqtt` 回调 → `can_bridge_mqtt()` | 命令类型放在主题里，负载放纯数据 |
| 网关自己周期上报 | `can_tx_task` | 每秒发 `0x103` 状态 + `0x703` 心跳 |

关键取舍写在代码注释里，也被本 README 沿用：**发布走队列解耦**。`esp_mqtt_client_publish()` 是在调用者任务里同步写 socket 并全程持锁的，若直接在收帧任务里调用，网络一卡就会把唯一排空 TWAI 接收队列的任务卡住，同时拖停 `0x703` 心跳，导致 A/B 判网关掉线。因此收帧任务只做一次内存拷贝 + `xQueueSend(超时 0)`，CAN 侧遥测的真正发布只由低优先级的 `can_pub_task` 承担（`gateway/status` 与 `gateway/stats` 由 `wifi_task` 自己发，不走这条队列）。

---

## 二、目录结构

```text
firmware/gateway/
├── CMakeLists.txt                  # 工程入口：EXTRA_COMPONENT_DIRS = component/BSP、component/MiddleWare；project(gateway)
├── sdkconfig                       # 已配置好的工程配置（target=esp32s3、16MB flash、自定义分区表、MQTT 5.0）
├── partitions-16MiB.csv            # 自定义分区表：nvs / phy-init / factory / vfs / storage
├── dependencies.lock               # 组件锁：idf 5.5.6、espressif/openai 1.1.0、cmake_utilities 0.5.3
├── main/
│   ├── CMakeLists.txt              # idf_component_register(SRCS "main.c")
│   ├── idf_component.yml           # 组件依赖清单（声明 espressif/openai）
│   └── main.c                      # app_main：只负责按依赖顺序初始化
└── component/
    ├── BSP/
    │   ├── CMakeLists.txt          # 注册 LED 组件，REQUIRES driver
    │   └── LED/
    │       ├── led.c / led.h       # WS2812 驱动（RMT 通道 + 拷贝编码器）
    │       └── ledtask.c           # 2 秒翻转一次的指示灯任务
    └── MiddleWare/
        ├── CMakeLists.txt          # SRC_DIRS "WIFI" "MQTT" "PROTOCOL" "CAN"，REQUIRES esp_wifi nvs_flash esp_event esp_netif mqtt driver
        ├── CAN/
        │   ├── can.c               # 网关核心：TWAI 初始化、收帧分发、MQTT→CAN 桥接、周期上报、统计
        │   └── can.h               # can_init / can_send / can_bridge_mqtt / can_get_stats
        ├── MQTT/
        │   ├── mqtt.c               # esp-mqtt 封装：连接、事件回调、发布、订阅
        │   └── mqtt.h               # 主题宏全部由 contract_c.h 派生
        ├── WIFI/
        │   ├── wifi.c / wifi.h      # STA 初始化、扫描、阻塞式连接、状态查询
        │   └── wifitask.c           # 等 WiFi → 起 MQTT → 5 秒一次上报网关状态与统计
        └── PROTOCOL/
            ├── protocol.h           # CAN 应用层协议 v3.0（与 protocol/protocol.h 逐字节一致）
            └── contract_c.h         # 由 protocol/contract.json 生成的契约常量
```

另有 `.clangd`、`.vscode/`、`.devcontainer/` 三份开发环境配置，不参与固件构建。

---

## 三、核心模块

| 文件 | 职责 | 关键点 |
|---|---|---|
| [`main/main.c`](main/main.c) | `app_main` 初始化顺序 | `LED_Init` → `LedTask_Start` → `can_init` → `wifi_sta_init`（内部阻塞到连上或超时）→ `wifiTask_Start` |
| [`component/MiddleWare/CAN/can.c`](component/MiddleWare/CAN/can.c) | 网关全部业务逻辑 | TWAI 安装/启动、收帧分发、MQTT→CAN 反查、周期帧、判活、统计、bus-off 看护 |
| [`component/MiddleWare/CAN/can.h`](component/MiddleWare/CAN/can.h) | 对外接口 + `can_stats_t` | 四个开机至今的累计值：`a_rx` / `a_lost` / `pub_total` / `pub_lost` |
| [`component/MiddleWare/MQTT/mqtt.c`](component/MiddleWare/MQTT/mqtt.c) | esp-mqtt 封装 | URI `mqtt://broker.emqx.io:1883`；`MQTT_PROTOCOL_V_5`；client_id 由 STA MAC 末 3 字节派生（`esp32s3_xxxxxx`）；连上即订阅命令通配主题 |
| [`component/MiddleWare/MQTT/mqtt.h`](component/MiddleWare/MQTT/mqtt.h) | 主题与 QoS 宏 | `#include "contract_c.h"` + `#define MQTT_PREFIX CONTRACT_MQTT_PREFIX`；遥测/报警/状态/统计主题与 QoS 全部起别名自契约 |
| [`component/MiddleWare/WIFI/wifi.c`](component/MiddleWare/WIFI/wifi.c) | WiFi STA | `nvs_flash_init` → `esp_netif_init` → 事件循环 → 默认 STA 网卡 → `esp_wifi_init` → `WIFI_MODE_STA` → `esp_wifi_connect`；用事件组 `WIFI_CONNECTED_BIT` / `WIFI_FAIL_BIT` 把异步连接包成同步函数，等待上限 15000 ms |
| [`component/MiddleWare/WIFI/wifitask.c`](component/MiddleWare/WIFI/wifitask.c) | 网络任务 | 1 秒轮询等 WiFi → `mqtt_init()` → 循环发布 `gateway/status`（`wifi`/`mqtt`/`cache`）与 `gateway/stats`（`a_rx`/`a_lost`/`pub_total`/`pub_lost`），周期 5000 ms |
| [`component/BSP/LED/led.c`](component/BSP/LED/led.c) | 板载 WS2812 | RMT 分辨率 10 MHz，`T0H=4`/`T0L=8`/`T1H=8`/`T1L=4` tick，复位 500 tick（50 µs），24 bit GRB 高位在前 |
| [`component/BSP/LED/ledtask.c`](component/BSP/LED/ledtask.c) | 指示灯 | 每 2000 ms 在 `(0,0,0)` 与 `(20,0,0)` 之间切换 |
| [`component/MiddleWare/PROTOCOL/protocol.h`](component/MiddleWare/PROTOCOL/protocol.h) | 协议副本 | 与 `protocol/protocol.h` 内容完全相同（由 `tools/sync_contract_copies.py` 同步） |
| [`component/MiddleWare/PROTOCOL/contract_c.h`](component/MiddleWare/PROTOCOL/contract_c.h) | 契约常量副本 | 顶部由 `tools/gen_contract.py` 写入"请勿手改"横幅 |

---

## 四、FreeRTOS 任务与中断

`CONFIG_FREERTOS_HZ = 1000`，所以全部 `pdMS_TO_TICKS(ms)` 与毫秒一一对应。任务栈单位是字节（`xTaskCreate` 的栈参数）。

| 任务名 | 优先级 | 栈（字节） | 周期 / 阻塞点 | 职责 |
|---|---|---|---|---|
| `can_rx` | 6 | 4096 | `twai_receive()` 等待 100 ms，超时即做一次总线状态看护 | 收帧 → 按 ID 分发 → `pub_telemetry()` 入队；每秒打一行"1s 收 N 帧 + 节点 A/B 在线状态"汇总 |
| `can_tx` | 4 | 4096 | 1000 ms | `can_send_periodic()`（`0x103` + `0x703`）+ `can_check_nodes()`（心跳超时判掉线） |
| `can_pub` | 3 | 4096 | 阻塞等待发布队列 | CAN 侧遥测/报警的唯一发布者（`gateway/status`、`gateway/stats` 由 `wifi_task` 自己发）；队满丢包日志最多每秒一行 |
| `wifi_task` | 5 | 4096 | 先 1000 ms 轮询等 WiFi，连上后 5000 ms | `mqtt_init()` 起客户端；周期发布 `gateway/status` 与 `gateway/stats` |
| `led_task` | 5 | 3072 | 2000 ms | 彩灯翻转 |
| esp-mqtt 内部任务 | 组件默认 | 组件默认 | 由 esp-mqtt 维护 | keepalive、断线自动重连、事件投递（`mqtt_event_handler` 跑在这里） |
| `main` 任务 | IDF 默认 | 3584（`CONFIG_ESP_MAIN_TASK_STACK_SIZE`） | 一次性 | 跑完 `app_main` 后由 IDF 回收 |

中断与事件：

| 来源 | 处理方式 |
|---|---|
| TWAI 收/发 | 应用层不注册中断，驱动内部把中断转成队列，`twai_receive()` 从队列取帧 |
| WiFi / IP 事件 | `wifi_event_handler`，在默认事件循环任务上下文执行，只置事件组位、不做耗时操作 |
| MQTT 事件 | `mqtt_event_handler`，在 esp-mqtt 内部任务上下文执行；收到 `MQTT_EVENT_DATA` 直接调 `can_bridge_mqtt()`（`can_send` 有 50 ms 发送超时） |

同步设施：

| 类型 | 名称 | 规格 |
|---|---|---|
| 队列 | `s_pub_q` | 长度 8，元素为 `{const char *topic; char payload[96];}`，创建失败则不启动 CAN 任务 |
| 事件组 | `s_wifi_events` | `WIFI_CONNECTED_BIT`（`BIT0`）、`WIFI_FAIL_BIT`（`BIT1`） |
| 计数 | `s_rx_count`、`s_a_rx`、`s_a_lost`、`s_pub_total`、`s_pub_lost`、`s_pub_drop`、`s_busoff_count` | 无锁；`s_pub_total` / `s_pub_lost` / `s_pub_drop` 为 `volatile`，代码注释明确接受"偶发少算一条" |

---

## 五、CAN 收发帧

本工程发出的帧：

| ID | DLC | 方向 | 名称 | 字段 | 触发方式 |
|---|---|---|---|---|---|
| `0x103` | 4 | 网关 → 广播 | GATEWAY_STATUS | `wifi`(1) + `mqtt`(1) + `cache`(2，大端) | `can_tx_task`，1000 ms |
| `0x703` | 2 | 网关 → 广播 | GATEWAY_HEARTBEAT | `magic = 0xC3` + `cnt`（每帧 +1，自然回绕） | `can_tx_task`，1000 ms |
| `0x000` | 1 | 网关 → 广播 | ESTOP | `action`（1 急停 / 2 解除，其余按急停） | MQTT `<prefix>/all/cmd/estop` |
| `0x301` / `0x302` | 3 | 网关 → A / B | CTRL_MOTOR | `cmd` + `arg` + `cid` | MQTT `<prefix>/nodeA/cmd/motor` 或 `<prefix>/nodeB/cmd/motor`，负载 `"cmd,arg"` |
| `0x311` / `0x312` | 3 | 网关 → A / B | CTRL_TARGET | `target`(2，大端) + `cid` | MQTT `.../cmd/target`，负载单个数字，超过 65535 按 65535 |
| `0x322` | 7 | 网关 → B | CTRL_PID | `kp`/`ki`/`kd` 各 2 字节（×100）+ `cid` | MQTT `.../cmd/pid`，负载 `"kp,ki,kd"` 浮点 |
| `0x332` | 2 | 网关 → B | CTRL_MODE | `mode` + `cid` | MQTT `.../cmd/mode`，负载 `0`/`1` |

`cid` 由 `static uint8_t s_cid` 每下发一帧自增一次，供执行端做幂等去重。

本工程接收并处理的帧：

| ID | DLC | 方向 | 处理动作 |
|---|---|---|---|
| `0x101` | 5 | A → 广播 | 解包 `ir`/`ldr`/`seq`，发布 `nodeA/telemetry`；用 `PROTO_SeqLost` 累计丢帧，`seq` 相同视为重复帧跳过，跳变 `>= 10` 判为疑似重启不计入丢包 |
| `0x102` | 4 | B → 广播 | 解包 `motor`/`pwm`/`mode`/`flags`，发布 `nodeB/telemetry` |
| `0x011` / `0x012` | 3 | A / B → 网关 | 解包 `code`/`extra`，发布 `nodeA/alert` 或 `nodeB/alert`，日志用 WARN 级别 |
| `0x000` | 1 | 任意 → 广播 | 只记日志（A/B 自己执行），不转发 |
| `0x701` / `0x702` | 2 | A / B → 广播 | 校验 DLC + 魔数（`0xA5` / `0x5A`），刷新 `last_seen` 与在线状态；只在状态变化时打日志 |
| 其它 ID | — | — | 打一条节流日志并打印原始 8 字节，便于排查 ID 拼错 |

判活口径：`0x703` 每秒无条件发送，同时兼任执行节点 B 的**电机保活**；A/B 超过 `CAN_HB_TIMEOUT_MS = 3000` 没有有效心跳即判掉线，并按 `ALM_COMM_LOST` 往对应 alert 主题补一条 `{"code":2,"extra":0}`。

MQTT 侧主题（前缀 `gw0805`，全部来自契约）：

| 方向 | 主题 | QoS |
|---|---|---|
| 发布 | `gw0805/nodeA/telemetry`、`gw0805/nodeB/telemetry` | 0 |
| 发布 | `gw0805/nodeA/alert`、`gw0805/nodeB/alert` | 1 |
| 发布 | `gw0805/gateway/status`、`gw0805/gateway/stats` | 0 |
| 订阅 | `gw0805/+/cmd/#` | 1 |

---

## 六、硬件与引脚

| 功能 | 引脚 | 说明 |
|---|---|---|
| TWAI TX | `GPIO5` | → 收发器 TXD（`CAN_TX_GPIO`） |
| TWAI RX | `GPIO4` | ← 收发器 RXD（`CAN_RX_GPIO`） |
| 板载 WS2812 | `GPIO48` | `LED_GPIO_PIN`，经 RMT 输出 |

代码注释里核对过的冲突清单：`GPIO4/5` 与 USB-Serial-JTAG（19/20）、控制台串口（43/44）、Strapping（0/3/45/46）、彩灯（48）都不冲突；两者同时是 ADC1_CH3/CH4，本工程不使用 ADC。ESP32 只有 CAN 控制器没有物理层，必须外挂 TJA1050 / SN65HVD230 / MCP2551 之类的收发器，且总线两端各一个 120 Ω 终端电阻、必须共地。

位定时（`twai_timing_config_t`，逐项对齐 STM32 而不是用 `TWAI_TIMING_CONFIG_250KBITS()`）：

| 参数 | 值 |
|---|---|
| `quanta_resolution_hz` | 4 000 000（`brp = 0`，由驱动反算分频） |
| `tseg_1` / `tseg_2` / `sjw` | 13 / 2 / 2 |
| 位时间 | `1 + 13 + 2 = 16 tq` |
| 位速率 | 4 MHz / 16 tq = **250 kbps**（与 STM32 侧同一套 tq 划分） |
| 采样点 | `(1 + 13) / 16 = **87.5%**` |
| `triple_sampling` | `false` |
| 接收过滤器 | `TWAI_FILTER_CONFIG_ACCEPT_ALL()`（网关要听全部帧） |
| 工作模式 | `CAN_MODE = TWAI_MODE_NORMAL` |

STM32 侧是 `Prescaler=9, BS1=13TQ, BS2=2TQ, SJW=2TQ`（APB1 36 MHz），两侧波特率与采样点完全一致；改用 IDF 的 250 kbps 宏会得到 5 MHz 量化、20 tq、80% 采样点，与 STM32 差 7.5%，混跑会偶发丢帧。

---

## 七、构建与烧录

环境：ESP-IDF **5.5.6**（见 [`dependencies.lock`](dependencies.lock) 中 `idf` 的版本），target `esp32s3`。仓库自带 `sdkconfig`，无需重新 `menuconfig`。

```bash
cd firmware/gateway
idf.py set-target esp32s3
idf.py build
idf.py -p COM19 flash monitor      # 串口按实际端口替换
```

`sdkconfig` 里与运行行为直接相关的项：

| 配置 | 值 |
|---|---|
| `CONFIG_IDF_TARGET` | `esp32s3` |
| `CONFIG_ESPTOOLPY_FLASHSIZE` | `16MB` |
| `CONFIG_PARTITION_TABLE_CUSTOM` | `y`，文件名 `partitions-16MiB.csv`，表偏移 `0x8000` |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` | `240` |
| `CONFIG_FREERTOS_HZ` | `1000` |
| `CONFIG_MQTT_PROTOCOL_5` | `y`（`mqtt.c` 用 `MQTT_PROTOCOL_V_5`，缺这项会在 `client_init` 时直接报错拒绝启动） |
| `CONFIG_ESP_TASK_WDT_TIMEOUT_S` | `5`（两个核的 IDLE 任务都纳入看护） |
| `CONFIG_SPIRAM` | `y`，Octal 模式，80 MHz |

分区表（[`partitions-16MiB.csv`](partitions-16MiB.csv)）：

| 名称 | 类型/子类型 | 偏移 | 大小 |
|---|---|---|---|
| `nvs` | data/nvs | `0x9000` | `0x6000` |
| `phy-init` | data/phy | `0xf000` | `0x1000` |
| `factory` | app/factory | `0x10000` | `0x1f0000` |
| `vfs` | data/fat | `0x200000` | `0xa00000` |
| `storage` | data/spiffs | `0xc00000` | `0x400000` |

运行前需要改的现场参数（均为源码硬编码）：

- WiFi：`WIFI_SSID` / `WIFI_PASSWORD` 在 [`component/MiddleWare/WIFI/wifi.c`](component/MiddleWare/WIFI/wifi.c)；
- MQTT：`MQTT_URI` 在 [`component/MiddleWare/MQTT/mqtt.c`](component/MiddleWare/MQTT/mqtt.c)。

---

## 八、与其他组件的关系

```text
                    ┌────────────────────────────┐
   MQTT (gw0805/…)  │  firmware/gateway  ← 本工程  │  CAN 250 kbps
  host/desktop-console ──────────────────────────── firmware/collect-node (0x101/0x701)
  host/mobile-app      └──────────────────────────── firmware/control-node (0x102/0x702)
                                                     firmware/door-node（不接 CAN）
```

| 对象 | 关系 |
|---|---|
| [`../../README.md`](../../README.md) | 平台总览、架构图、构建总说明 |
| [`../../protocol/contract.json`](../../protocol/contract.json) | 契约唯一真源：帧表、主题、QoS、命令映射 |
| [`../../protocol/generated/contract.md`](../../protocol/generated/contract.md) | 生成的可读协议表 |
| [`../../protocol/protocol.h`](../../protocol/protocol.h) | CAN 编解码（本工程副本在 `component/MiddleWare/PROTOCOL/protocol.h`） |
| [`../../protocol/README.md`](../../protocol/README.md) | 契约与生成流程说明 |
| [`../collect-node/README.md`](../collect-node/README.md) | 采集节点 A（`NODE_A = 1`） |
| [`../control-node/README.md`](../control-node/README.md) | 执行节点 B（`NODE_B = 2`） |
| [`../door-node/README.md`](../door-node/README.md) | 门锁节点（ESP-NOW，独立扩展，不接 CAN） |
| [`../../host/desktop-console`](../../host/desktop-console) | 订阅遥测主题的 Qt 桌面监控台 |
| [`../../docs/architecture.md`](../../docs/architecture.md) | 分层视图与数据流 |
| [`../../docs/troubleshooting/04-CAN故障排查-网络抖动拖垮CAN侧.md`](../../docs/troubleshooting/04-CAN故障排查-网络抖动拖垮CAN侧.md) | 发布队列解耦这一改动的完整复盘 |

### 契约治理：改主题 / 改帧的唯一正确姿势

本工程的主题、QoS、CAN 帧 ID 与 DLC **不写死在源码里**，而是从单一真源派生：

```text
protocol/contract.json
        │  python tools/gen_contract.py
        ├──► protocol/generated/contract_c.h  ──► firmware/gateway/component/MiddleWare/PROTOCOL/contract_c.h
        ├──► protocol/generated/contract_qt.h ──► host/desktop-console、host/mobile-app
        └──► protocol/generated/contract.md   ──► 文档表格
```

`mqtt.h` 的写法是 `#include "contract_c.h"` 再 `#define MQTT_PREFIX CONTRACT_MQTT_PREFIX`，其余主题宏（`TOPIC_TELEMETRY_A`、`TOPIC_ALERT_B`、`TOPIC_STATUS_GW`、`TOPIC_STATUS_STATS`、`TOPIC_CMD_SUB`）与 QoS（`MQTT_QOS_TELEMETRY`、`MQTT_QOS_CMD`）都是契约常量的别名。改主题或帧的流程固定为三步：

```bash
python tools/gen_contract.py            # 从 contract.json 重新生成
python tools/sync_contract_copies.py    # 同步到各工程副本（含本工程的 contract_c.h 与 protocol.h）
python tools/test_contract.py           # 主机侧协议自测，验证生成物行为
```

只改本工程的副本会被 `tools/validate_copies.py` 比对出来并报路径。

---

## 九、已知限制与注意事项

以下均为代码中确实存在的状态，不是待办清单：

1. **主题已派生，broker 地址仍硬编码。** `mqtt.c` 的 `MQTT_URI` 是字面量 `"mqtt://broker.emqx.io:1883"`，契约生成的 `CONTRACT_MQTT_HOST` / `CONTRACT_MQTT_PORT` 在源码中没有被引用；同文件里的 `MQTT_CLIENT_ID "esp32_001"` 宏也未被使用（实际 client_id 用 STA MAC 末 3 字节拼成 `esp32s3_xxxxxx`）。
2. **`0x103` 的 `cache` 字段是占位值。** `can_send_periodic()` 里 `PROTO_PackGwStatus(..., 12345)`，代码注释写明"本项目还没做断网缓存，先填 12345"；而 MQTT 侧 `gateway/status` 的 `cache` 固定发 `0`。同一个语义在两条出口上取值不同。分区表里预留的 `vfs` / `storage` 两个数据分区目前也没有任何代码使用。
3. **心跳逐帧日志已被注释。** `can_on_heartbeat()` 末尾保留了一个 `if (can_log_allow(n->log_id)) { ... }` 结构，内部两行 `ESP_LOGI` 是注释状态，该分支现在为空。
4. **应用层没有信号量 / 互斥量。** 任务间只靠队列与原子读写；`s_pub_total` / `s_pub_lost` / `s_pub_drop` 跨任务自增未加锁（注释明确说明"监控数不是账目"）；`can_get_stats()` 返回的是四个独立读取的拷贝，不是同一瞬间的快照。
5. **`WIFI_FAIL_BIT` 从未被置位。** `wifi.c` 的 `wifi_connect()` 会等"连上"或"彻底失败"，但事件回调只置 `WIFI_CONNECTED_BIT`（并且没有重试计数上限），所以失败路径实际靠 15000 ms 超时返回，`bits & WIFI_FAIL_BIT` 分支不可达。断线事件回调里是直接再调一次 `wifi_connect()`。
6. **`main/idf_component.yml` 声明了 `espressif/openai`。** 依赖清单与 `dependencies.lock`（`espressif/openai 1.1.0`、`espressif/cmake_utilities 0.5.3`）都有它，但工程源码没有任何 `#include` 引用该组件。
7. **`CAN_MODE` 改成 `TWAI_MODE_LISTEN_ONLY` 时发送会失败。** `twai_transmit()` 在该模式下返回 `ESP_ERR_NOT_SUPPORTED`，`can_tx_task` 仍会照常调用（代码注释已注明这是"单独测接收"的调试模式）。
8. **`0x000` 只记录不联动。** 收到广播急停/解除后网关只打日志，不触发任何其他动作（注释："以后想联动，在这里加"）；`0x011` / `0x012` 除转发 MQTT 外无本地处理。
9. **`sdkconfig` 未启用 `CONFIG_MQTT_USE_CUSTOM_CONFIG`。** 因此 `can.c` 注释里提到的"最长等 10 秒"用的是 esp-mqtt 默认网络超时，工程没有自定义该值。
