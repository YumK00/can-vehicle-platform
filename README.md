# 车规模拟 CAN 总线采集-网关-上位机闭环平台（can-vehicle-platform）

> 面向车规模拟的 **CAN 总线采集 — 网关 — 上位机** 闭环平台，构建「多节点 ECU → ESP32 智能网关 → 跨端上位机」的完整数据通路。
> 覆盖多源传感器高速采集、CAN 2.0A 可靠传输、边缘侧协议转换、云端/移动端数据可视化与远程控制，以及一套真正跑得起来的**跨端契约治理工具链**。
> 五个工程、两种运行模式、一份单一真源协议：改协议只改一处，三端自动同步，3500+ 条主机侧用例把"各端协议分叉"挡在联调之前。

---

## 一、项目概述

平台把一条真实的 **250 kbps CAN 总线**接到 MQTT 云端，两端各有一段硬骨头：

- **总线侧**：CAN 一帧只有 8 字节、ID 承载语义、没有"重传失败就报错"的抽象，
  节点掉线、重复帧、总线错误、急停兜底全要自己设计；
- **网络侧**：MQTT 需要可读的 JSON 负载与主题树，弱网下发布还可能长时间阻塞，
  一不留神就把实时收帧任务拖死。

平台的做法不是"把数据搬上云"，而是**在资源受限、不可靠、实时的约束下，
把多协议数据安全、可靠、可观测地送上云，并让下行控制同样可靠**。
具体落成三层：

| 层 | 组件 | 一句话职责 |
|---|---|---|
| **现场层** | `firmware/collect-node`、`firmware/control-node` | STM32F103 + FreeRTOS，一个采集、一个执行，挂在同一条 CAN 总线上 |
| **边缘层** | `firmware/gateway` | ESP32-S3 + ESP-IDF，TWAI 驱动总线，CAN ⇄ MQTT 双向桥接，做判活、统计、自愈 |
| **应用层** | `host/desktop-console`、`host/mobile-app` | Qt6 桌面监控台 + 手机端 App，实时可视化与远程控制 |
| **治理层** | `protocol/` + `tools/` | 单一真源协议与主题、代码生成、副本校验、主机侧自测与 CI |

---

## 二、系统架构

```text
┌───────────────────────── 应用层（Qt6 / C++） ─────────────────────────┐
│  桌面监控台 host/desktop-console        手机端 host/mobile-app          │
│  · 实时遥测 / 网关状态看板                · 远程控制（JSON + QoS1）      │
│  · 节点在线率 / 通信时延 / 丢帧率          · 网关模式 / 仿真模式切换      │
│  · 报文日志 / 状态快照导出                                              │
└────────────────────────────────┬──────────────────────────────────────┘
                                 │ MQTT（broker.emqx.io:1883，主题树 gw0805/…）
┌────────────────────────────────┴──────────────────────────────────────┐
│              边缘层：ESP32-S3 网关  firmware/gateway                   │
│  TWAI 250 kbps  │  CAN→MQTT 按 ID 查表打包  │  MQTT→CAN 按主题反查功能组  │
│  发布队列解耦（慢网络不占实时路径）｜心跳判活 ｜bus-off 自愈 ｜seq 丢帧统计 │
└────────────────────────────────┬──────────────────────────────────────┘
                                 │ CAN 2.0A 250 kbps（双绞线 + 两端 120 Ω）
        ┌────────────────────────┴────────────────────────┐
        │                                                 │
┌───────┴──────────────────┐                  ┌───────────┴──────────────┐
│ 采集节点 A  NODE_A = 1    │                  │ 执行节点 B  NODE_B = 2    │
│ firmware/collect-node    │                  │ firmware/control-node     │
│ ADC1 双通道 + DMA 循环    │                  │ 电机 H 桥 + TIM3 PWM      │
│ 0x101 周期上报 / 0x701 心跳│                  │ 0x102 状态回读 / 0x702 心跳│
└──────────────────────────┘                  └──────────────────────────┘
        ▲
        │ 独立扩展（不接 CAN）
┌───────┴──────────────────┐
│ 门锁节点 firmware/door-node │  ESP32-S3 + ESP-NOW + SG90 舵机
└──────────────────────────┘
```

**两种运行模式**（上位机可切换，共用同一套 MQTT 封装与界面组件）：

| 模式 | 链路 | 用途 |
|---|---|---|
| 真实链路模式 | STM32 节点 ↔ CAN ↔ ESP32 网关 ↔ MQTT ↔ Qt 桌面 / 手机 | 联调、演示、答辩 |
| 纯仿真模式 | 桌面内置 4 个 ECU 仿真（BMS/VCU/BCM/DTC）↔ MQTT ↔ 手机 | 无硬件时验证 MQTT 链路与 UI |

---

## 三、仓库结构

```text
can-vehicle-platform/
├── README.md                      # 本文件：平台总览
├── docs/                          # 架构、协议全量、调试实录、场景包装
│   ├── architecture.md            #   分层视图 / 数据流 / 可靠性设计
│   ├── can-protocol-and-wiring.md #   协议全量 + 接线图 + 上电检查
│   ├── can-mqtt-quick-reference.md#   编码时的收发速查表
│   ├── design-goals.md            #   需求基线：量化目标与功能清单
│   ├── scenario-and-demos.md      #   面向非技术听众的场景与演示脚本
│   └── troubleshooting/           #   4 个真实故障的完整排查实录
├── protocol/                      # ★ 通信契约唯一真源
│   ├── contract.json              #   CAN 帧表 + MQTT 主题 + 命令映射（改这里）
│   ├── protocol.h                 #   CAN 应用层协议 v3.0（C 编解码，三端同源）
│   └── generated/                 #   生成物：contract_c.h / contract_qt.h / contract.md
├── tools/                         # ★ 契约治理与主机侧自测
│   ├── gen_contract.py            #   真源 → 生成 C/Qt 头文件与文档
│   ├── sync_contract_copies.py    #   同步到各工程副本
│   ├── validate_copies.py         #   校验副本，拦住"只改自己那一份"
│   ├── test_contract.py           #   3500+ 条主机侧协议自测（无需硬件）
│   └── serial_logger.py           #   串口数据采集器
├── firmware/
│   ├── collect-node/              # STM32F103 采集节点 A（完整 CubeMX 工程，含 .ioc）
│   ├── control-node/              # STM32F103 执行节点 B（完整 CubeMX 工程，含 .ioc）
│   ├── gateway/                   # ESP32-S3 边缘网关（ESP-IDF 5.5，含 sdkconfig）
│   └── door-node/                 # ESP32-S3 门锁节点（ESP-NOW + SG90 舵机）
├── host/
│   ├── desktop-console/           # Qt6 车端监控台（真实链路 + 仿真双模式）
│   └── mobile-app/                # Qt6 手机端控制 App
└── .github/workflows/contract-ci.yml  # 每次提交自动跑契约校验 + 协议自测
```

---

## 四、核心能力（按简历功能内容逐条对应）

### 4.1 高可靠 CAN 应用层协议设计 ✅

- **11 位 ID 仲裁机制**：`ID = 功能组(高 7 位) | 节点号(低 4 位)`，功能组按用途分层
  （`0x0xx` 紧急/报警、`0x1xx` 周期上报、`0x3xx` 控制下发、`0x7xx` 心跳）。
  好处是**加节点只改低 4 位**，心跳滤波一个掩码就能只听全组，优先级天然分层。
- **统一业务帧结构**：15 种帧全部定长 DLC + 大端编解码，纯头文件实现（无 .c、无全局变量），
  STM32 与 ESP32 复制同一份即可互通。
- **三个可靠性机制**：
  - 心跳存活检测：ID + DLC + **魔数**三重校验，超时 3 s 判掉线并报警；
  - 控制命令**幂等**：每帧带自增 `cid`，执行端记住上一次值，重复帧直接丢弃
    （抵消 CAN 硬件自动重发导致的"电机反转执行两次"）；
  - 序号**丢帧统计**：`PROTO_SeqLost()` 用无符号减法天然处理 255→0 回绕，不用写分支。
- **Fail-Safe 急停**：`0x000` 广播帧 B0 只要不是 `2`（解除）就一律按急停执行——
  负载为空、写错、解析失败都停机。安全帧不允许"看不懂就放行"。

### 4.2 网关双向桥接与边缘决策 ✅ / 🧭

**已实现（双向桥接）**：

- TWAI 驱动 250 kbps 标准帧，**位定时手工对齐 STM32**（4 MHz 量化、`1+13+2 = 16 tq`、
  采样点 87.5%），而不是直接用 IDF 的 250 kbps 宏（80% 采样点，混跑会偶发丢帧）；
- `CAN → MQTT`：按 ID 查表分发，遥测打包 JSON、心跳只刷新在线状态不转发（1 Hz × N 节点全转发是浪费）；
- `MQTT → CAN`：**命令类型放在主题里、负载放纯数据**，MCU 端不需要 JSON 解析器，
  网关收到 `gw0805/nodeB/cmd/pid` 直接查表发 `0x322`；
- **发布队列解耦**：收帧任务只做一次拷贝 + `xQueueSend(超时 0)`，由低优先级任务真正发 MQTT。
  原因是 `esp_mqtt_client_publish()` 是同步写 + 全程持锁（网络卡住最长 10 s），
  跑在收帧任务里会把整条总线拖死（详见 `docs/troubleshooting/04`）；
- bus-off 自愈：`BUS_OFF → twai_initiate_recovery() → STOPPED → twai_start()`，
  驱动恢复后停在 STOPPED 是常见坑，必须补一次 `twai_start()`。

**设计目标（边缘决策，路线图见第十节）**：
网关侧独立承担边缘计算——带死区/回差/防抖的温控状态机、红外测速闭环、以及失效降级
（数据超时 → 降级到安全输出 + 上报告警）。这一层目前在架构与契约上预留了数据源
（`0x101` 传感器帧、`0x102` 执行状态帧）与下行通道（`0x302/0x312/0x332`），尚未落到代码。

### 4.3 多源采集与执行器精准控制 ✅

**采集节点 A**（`firmware/collect-node`）：

- FreeRTOS 划分 5 个任务：采集（100 ms 周期）、CAN 接收、心跳、串口发送、默认任务；
- **ADC1 双通道 + DMA 循环**：光敏/红外两路 12 位原始值由 DMA 直接搬进缓冲，
  任务侧零等待读取，CPU 不搬数；
- 中断只做搬运：CAN RX 回调把整帧拷进结构体再入队（超时 0），不在中断里解析、不阻塞。

**执行节点 B**（`firmware/control-node`）：

- **单写者邮箱**解耦"收 CAN"与"电机控制"：`canRxTask` 只解析并写邮箱
  （先写 `target` 再写 `cnt`，读侧以 `cnt` 变化判定新命令），`motorTask` 是
  `Motor_SetSpeed()` 的唯一调用者，避免两个任务同时改 PWM 与方向脚打乱换向时序；
- 执行器驱动：直流电机双 H 桥（IN1/IN2 方向组合 + TIM3_CH1 PWM 调速），
  **换向前先把占空比降到 0 并等一个 PWM 更新事件**，防止反向瞬间电流尖峰；
- **状态周期回读**：1 Hz 上报 `0x102`（电机状态、当前占空比、模式、状态位），
  上位机因此能确认命令**真的生效**，而不是"发出去就当成功"；
- **掉线自动保护**：连续 3 s 收不到网关心跳 `0x703` 即停车；
  急停广播 `0x000` 立即置急停态并丢弃积压命令。

**舵机**：门锁节点 `firmware/door-node` 已实现 SG90 舵机开锁/自动回锁（ESP-NOW 触发）。
多路舵机与 PWM 风扇扩展列在路线图中。

### 4.4 跨端 Qt 可视化监控与控制 ✅

- **桌面监控台**（`host/desktop-console`）：Qt6 Widgets + 信号槽解耦网络层与 UI 层，
  实时刷新节点上报数据；状态看板统计**节点在线率、通信时延、丢帧率**；
  报文日志记录收发（带时间戳与 RX/TX 标记）；支持车辆状态快照导出。
  提供"仿真 ECU"与"真实网关"两种数据源，切换只换数据源不改 UI。
- **手机端 App**（`host/mobile-app`）：把控制指令封装为 JSON，通过 **MQTT QoS 1**
  下发，弱网下保证指令可靠送达；提供网关模式/仿真模式切换，可远程急停与调速。

### 4.5 跨端契约治理与自动化工具链 ✅（本项目最有辨识度的一块）

多端（节点/网关/桌面/手机）协议不一致是联调里最贵的 bug：症状永远像"我发了你没收到"，
然后三端各查一遍。本项目把它做成**可自动拦截**的工程问题：

```text
protocol/contract.json（唯一真源：15 帧 + 6 主题 + 5 命令 + 5 报警码）
        │  python tools/gen_contract.py
        ├──────────────► protocol/generated/contract_c.h   → ESP32 网关
        ├──────────────► protocol/generated/contract_qt.h  → 桌面 / 手机
        └──────────────► protocol/generated/contract.md    → 文档表格
        │  python tools/sync_contract_copies.py
        └──────────────► 同步到 4 个工程副本（逐字节一致）
        │  python tools/validate_copies.py && python tools/test_contract.py
        └──────────────► CI 拦截：副本漂移 / 生成物过期 / 协议行为回归
```

- **单一真源**：帧 ID、DLC、字段偏移、MQTT 主题、QoS、命令映射全部写进 `contract.json`；
- **代码生成**：一键生成 C 与 Qt 头文件，网关的 `mqtt.h` 已改为从生成头派生主题，
  不再硬编码主题字符串；
- **副本校验**：`validate_copies.py` 比对 6 个副本的 SHA256，谁单独改了协议立刻报路径；
- **主机侧自测**：`tools/test_contract.py` 共 **3557 条断言**，覆盖帧布局、大端编解码、
  seq 丢帧全回绕、急停 256 种取值兜底、MQTT 通配符匹配、命令载荷解析与越界拒绝，
  **无硬件、无第三方依赖、1 秒内跑完**，已接入 GitHub Actions。

---

## 五、通信契约摘要

### 5.1 CAN 帧（CAN 2.0A，250 kbps，采样点 87.5%，大端）

| ID | DLC | 方向 | 名称 | 关键字段 |
|---|---|---|---|---|
| `0x000` | 1 | 广播 | ESTOP | action（1 急停 / 2 解除，其余按急停） |
| `0x011` / `0x012` | 3 | A / B → 网关 | ALARM | code + extra |
| `0x101` | 5 | A → 全体 | 传感器周期帧 | ir(2B) + ldr(2B) + seq(1B) |
| `0x102` | 4 | B → 全体 | 执行状态 | motor + pwm + mode + flags |
| `0x103` | 4 | 网关 → 全体 | 网关状态 | wifi + mqtt + cache(2B) |
| `0x301` / `0x302` | 3 | 网关 → A / B | 电机命令 | cmd + arg + cid |
| `0x311` / `0x312` | 3 | 网关 → A / B | 目标值 | target(2B) + cid |
| `0x322` | 7 | 网关 → B | PID 参数 | kp/ki/kd(×100) + cid |
| `0x332` | 2 | 网关 → B | 模式切换 | mode + cid |
| `0x701` / `0x702` / `0x703` | 2 | A / B / 网关 → 全体 | 心跳 | magic + cnt |

完整字段表见 [`protocol/generated/contract.md`](protocol/generated/contract.md)。

### 5.2 MQTT 主题（前缀 `gw0805`）

| 方向 | 主题 | QoS | 载荷 |
|---|---|---|---|
| 发布 | `gw0805/nodeA/telemetry` | 0 | `{"ir":..,"ldr":..,"seq":..}` |
| 发布 | `gw0805/nodeB/telemetry` | 0 | `{"motor":..,"pwm":..,"mode":..,"flags":..}` |
| 发布 | `gw0805/nodeA/alert`、`nodeB/alert` | 1 | `{"code":..,"extra":..}` |
| 发布 | `gw0805/gateway/status` | 0 | `{"wifi":..,"mqtt":..,"cache":..}` |
| 发布 | `gw0805/gateway/stats` | 0 | `{"a_rx":..,"a_lost":..,"pub_total":..,"pub_lost":..}` |
| 订阅 | `gw0805/+/cmd/#` | 1 | `<prefix>/<nodeA\|nodeB\|all>/cmd/<estop\|motor\|target\|pid\|mode>` |

**三条铁律**：控制命令主题绝不 retain（否则新订阅者一上线就重放历史命令）；
心跳不进 MQTT（只更新在线状态）；遥测 QoS 0、控制命令 QoS 1。

---

## 六、关键工程决策

| 决策 | 为什么 |
|---|---|
| **ID = 功能组 + 节点号** | 加节点只改低 4 位；按组过滤只需一个掩码；急停用最小 ID 拿最高仲裁优先级 |
| **命令类型放主题、负载放纯数据** | MCU 端不用写 JSON 解析器，省代码省内存，也不容易解析出错 |
| **收帧与发布队列解耦** | `esp_mqtt_client_publish` 同步持锁最长 10 s，放实时路径会把总线拖死 |
| **位定时手工对齐 STM32** | IDF 默认 250k 宏是 80% 采样点，与 STM32 的 87.5% 差 7.5%，偶发丢帧很难查 |
| **心跳判活用"网关心跳"而非"命令超时"** | 命令是状态不是事件；用命令超时会把 `cid` 逼成保活计数器 |
| **急停解析失败一律停机** | 安全帧的默认值必须是"停"，静默失效比编不过危险得多 |
| **协议单一真源 + 生成 + 校验** | 多端协议分叉是联调最贵的 bug，成本前移到主机侧自测几乎为零 |

---

## 七、构建与运行

### 7.1 环境

| 组件 | 环境 | 说明 |
|---|---|---|
| CAN 节点 | STM32CubeMX + STM32CubeIDE / Keil（ARM GCC） | 两个节点仓库都带完整 `.ioc`、`Drivers/`、`CMakeLists.txt`、链接脚本 |
| 网关 | ESP-IDF **5.5.x**，target `esp32s3` | 仓库带 `sdkconfig` 与 `partitions-16MiB.csv`，无需重新 menuconfig |
| 上位机 | **Qt 6.11**（Widgets）+ CMake ≥ 3.19 + MinGW/Ninja | QMQTT 以静态库形式 vendored 在 `qmqtt/` |
| 工具链 | Python 3.10+（仅标准库） | 契约生成与自测不依赖任何第三方包 |

### 7.2 烧录固件

```bash
# ESP32-S3 网关
cd firmware/gateway
idf.py set-target esp32s3
idf.py build
idf.py -p COM19 flash monitor

# STM32 采集节点 / 执行节点：用 CubeIDE 打开工程（含 .ioc）直接 Build & Flash
# 或 CMake：cmake --preset Debug && cmake --build build/Debug
```

### 7.3 构建上位机

> 构建目录请放在**纯 ASCII 路径**下：Qt 的 moc 在中文路径下会因无法创建中间文件而失败。

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$env:PATH"
cmake -S host/desktop-console -B C:/build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build C:/build/desktop -j 8
# 产物：C:/build/desktop/vehicle_gateway.exe
```

手机端同理，源目录换成 `host/mobile-app`，产物 `myphone.exe`。

### 7.4 跑契约工具链（无需硬件）

```bash
python tools/gen_contract.py --check   # 生成物是否与真源同步
python tools/validate_copies.py        # 6 个副本是否逐字节一致
python tools/test_contract.py          # 3557 条主机侧协议自测
```

一条命令跑全套：

```bash
python tools/gen_contract.py --check && python tools/validate_copies.py && python tools/test_contract.py
```

---

## 八、持续集成

`.github/workflows/contract-ci.yml` 在每次 push / PR 上自动执行：

1. 校验生成物与 `contract.json` 同步（防止"改了真源忘了重新生成"）；
2. 校验四端协议副本一致（防止"只改自己那一份"）；
3. 跑完整主机侧协议自测（3557 条断言）。

联调时最贵的"到底哪端写错了"问题，在提交阶段就会被拦下。

---

## 九、文档索引

| 文档 | 内容 | 什么时候看 |
|---|---|---|
| [`docs/architecture.md`](docs/architecture.md) | 分层架构、数据流、两种模式、可靠性设计矩阵 | 想理解整体设计时 |
| [`docs/bringup.md`](docs/bringup.md) | 上电检查清单、分步联调顺序、MQTTX 手动测试、现象→判据速查 | 第一次上电 / 联调排障 |
| [`docs/can-protocol-and-wiring.md`](docs/can-protocol-and-wiring.md) | 协议全量、字节示例、滤波器、接线图、上电检查 | 查协议细节 / 接线时 |
| [`docs/can-mqtt-quick-reference.md`](docs/can-mqtt-quick-reference.md) | CAN 收发 + MQTT 主题 + 下行命令速查 | 写代码时开着 |
| [`docs/design-goals.md`](docs/design-goals.md) | 需求基线：量化目标、8 个子系统功能清单、验收对应表 | 开工前 / 验收时 |
| [`docs/scenario-and-demos.md`](docs/scenario-and-demos.md) | 面向非技术听众的场景包装与演示脚本 | 面试 / 答辩 |
| [`docs/troubleshooting/`](docs/troubleshooting/) | 4 个真实故障的完整排查过程（丢帧 / 供电 / 总线 / 耦合） | 面试最能讲的部分 |
| [`protocol/README.md`](protocol/README.md) | 契约真源与生成流程 | 改协议前必读 |
| [`tools/README.md`](tools/README.md) | 工具链与自测覆盖范围 | 想加用例时 |

各组件另有自己的 README：`firmware/*/README.md`、`host/*/README.md`。

---

## 十、路线图

| 优先级 | 项 | 现状 |
|---|---|---|
| 高 | **网关边缘决策**：带死区/回差/防抖的温控状态机、红外测速闭环、失效降级 | 数据源与下行通道已就绪，控制逻辑待落地 |
| 高 | **断网缓存与掉电续传**：环形缓冲 + 按 seq 补传 + 云端幂等；先写数据后提交索引 + 魔数/CRC | 分区表（`vfs`/`storage`）与 `0x103` 缓存字段已预留 |
| 中 | 多路舵机与 PWM 风扇（执行节点扩展） | 单路直流电机 + SG90 舵机（door-node）已实现 |
| 中 | 桌面端增加实时曲线、离线数据回放 | 状态看板与日志已实现 |
| 低 | OTA、UDS 诊断（`0x7E0/0x7E8`）、边缘 AI | 协议表已预留 ID 段 |

---

## 十一、备注

- 编译产物、本机 IDE 配置（`*.user`、`build/`、`sdkconfig.old`、`managed_components/`）均已在 `.gitignore` 中排除。
- `firmware/door-node` 是独立扩展节点（ESP-NOW + 舵机），不接 CAN 总线，主链路不依赖它。
- 两个 STM32 工程都在 `SystemClock_Config()` 里做了 **AHB ÷2**（HCLK = 36 MHz），
  因此 PWM 实际载频与部分注释里按 72 MHz 推算的值不同，具体以代码与组件 README 为准。
