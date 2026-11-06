# mobile-app —— 手机端监控 App（仿真 / 真实网关 双模式）

Qt6 Widgets 写的手机端界面。原来只会连 `8.138.233.135` 收 `car/ecu/*`、发 `phone/cmd`，
那是"纯 ECU 仿真"那一套；现在同一个界面里多了一个**网关模式**，直连 ESP32-S3 网关所在
的 MQTT broker，按 `protocol/contract.json` 的契约收发 `gw0805/*` 主题。

两种模式**互斥**：同一时刻只有一条 MQTT 连接、一套订阅、一套界面。

## 文件

| 文件 | 说明 |
|---|---|
| `mainwindow.h` / `mainwindow.cpp` | 主窗口：模式切换、MQTT 连接管理、仿真模式的 JSON 解析、网关模式的报文分派 |
| `gatewaypanel.h` / `gatewaypanel.cpp` | 网关模式面板：节点/网关数据展示 + 远程控制按钮 + 报文日志，**不直接碰 MQTT** |
| `cmdData.h` | 仿真模式下车门/空调命令的 JSON 结构 |
| `contract_qt.h` | 由 `tools/gen_contract.py` 从 `protocol/contract.json` 生成，**勿手改**；主题、命令、节点名全用这里的常量 |
| `mainwindow.ui` | 原有界面，未加控件；模式栏和网关面板都由代码创建 |
| `qmqtt/` | 内置 QMQTT 0.3.1 静态库 |

## 两种模式

| | 仿真模式 | 网关模式 |
|---|---|---|
| broker | `8.138.233.135:1883` | `contract::kBrokerHost:kBrokerPort`（`broker.emqx.io:1883`） |
| client id | `myphone` | `qt_mobile_app`（与 `contract.json` 的 `client_ids.mobile` 一致） |
| 订阅 | `car/ecu/bms`、`car/ecu/vcu`、`car/ecu/bcm`、`car/ecu/dtc` | `gw0805/nodeA/telemetry`、`gw0805/nodeB/telemetry`、`gw0805/nodeA/alert`、`gw0805/nodeB/alert`、`gw0805/gateway/status`、`gw0805/gateway/stats` |
| 发布 | `phone/cmd`（车门/空调 JSON） | `gw0805/<node>/cmd/<kind>`（急停/电机/模式） |
| 界面 | 原车门锁 + 空调卡片 | 节点实时数据 / 网关状态 / 远程控制 / MQTT 报文日志 |

## 模式切换

界面顶部（代码创建的 `QComboBox`，未改 `.ui`）选择"仿真模式"或"网关模式"，切换动作是：

1. 对当前模式订阅过的主题逐个 `unsubscribe`；
2. `disconnectFromHost()`，并销毁旧 `QMQTT::Client`（连同它的 socket / 未完成的 QoS1 消息）；
3. 清空网关面板的上一轮显示值；
4. 按新模式重新 `new Client` → 设置 host/port/clientId → `connectToHost()`。

因此**不会出现两个 broker 同时在线**、也不会残留订阅。切换时旧 client 的信号已从本窗口断开，
避免了"切换瞬间迟到回调改错界面"。

## 主题映射

网关发布、App 订阅（QoS 与 `contract.json` 的 `publish` 表一致）：

| 常量（`contract_qt.h`） | 主题 | QoS | 载荷 |
|---|---|---|---|
| `kTelemetryCollect` | `gw0805/nodeA/telemetry` | 0 | `{"ir":1234,"ldr":567,"seq":42}` |
| `kTelemetryControl` | `gw0805/nodeB/telemetry` | 0 | `{"motor":1,"pwm":60,"mode":1,"flags":9}` |
| `kAlertCollect` | `gw0805/nodeA/alert` | 1 | `{"code":3,"extra":77}` |
| `kAlertControl` | `gw0805/nodeB/alert` | 1 | `{"code":4,"extra":12}` |
| `kGatewayStatus` | `gw0805/gateway/status` | 0 | `{"wifi":1,"mqtt":1,"cache":5}` |
| `kGatewayStats` | `gw0805/gateway/stats` | 0 | `{"a_rx":1234,"a_lost":2,"pub_total":999,"pub_lost":1}` |

App 发布、网关订阅 `gw0805/+/cmd/#`（**QoS1、retain=false**）：

| 按钮 | 主题 | 载荷 | 说明 |
|---|---|---|---|
| 急停 | `gw0805/all/cmd/estop` | `1` | 广播到 `all`，安全动作不做单节点收敛 |
| 解除 | `gw0805/all/cmd/estop` | `2` | 同上；B0 不是 `2` 时网关一律按急停处理 |
| 电机 停止 | `gw0805/<node>/cmd/motor` | `0,0` | `cmd,arg`：0 停 |
| 电机 正转 | `gw0805/<node>/cmd/motor` | `1,0` | 1 正转 |
| 电机 反转 | `gw0805/<node>/cmd/motor` | `2,0` | 2 反转 |
| 设速 | `gw0805/<node>/cmd/motor` | `3,<0~100>` | 3 设速，`arg` 为占空比百分比 |
| 模式 手动 | `gw0805/<node>/cmd/mode` | `0` | 0 手动 |
| 模式 自动 | `gw0805/<node>/cmd/mode` | `1` | 1 自动 |

`<node>` 取自面板上的"控制节点"下拉：`nodeB`（控制节点，默认）/ `nodeA`（采集节点）/ `all`（广播）。
主题一律由 `contract::kTopicPrefix` + 节点名 + `contract::kCmdXxx` 拼出，代码里没有 `gw0805/...` 字面量。

## JSON 字段

`nodeA/telemetry`（来自 CAN `0x101`）：

| 字段 | 类型 | 含义 |
|---|---|---|
| `ir` | uint16 | 红外 ADC 原始值 0~4095 |
| `ldr` | uint16 | 光敏 ADC 原始值 0~4095 |
| `seq` | uint8 | 序列号，回绕 256，用于算丢帧 |

`nodeB/telemetry`（来自 CAN `0x102`）：

| 字段 | 类型 | 取值 |
|---|---|---|
| `motor` | uint8 | 0 停止 / 1 正转 / 2 反转 |
| `pwm` | uint8 | 0~100 |
| `mode` | uint8 | 0 手动 / 1 自动 |
| `flags` | uint8 | bit0 `COMM_OK`、bit1 `THRESH`、bit2 `ALARM`、bit3 `ESTOP` |

`*/alert`：`code` 为报警码（1 传感器故障 / 2 通信丢失 / 3 阈值越界 / 4 执行器故障），`extra` 为附加数据。
`gateway/status`：`wifi`/`mqtt` 非 0 视为已连接，`cache` 为缓存条数。
`gateway/stats`：`a_rx`/`a_lost` 采集节点收发统计，`pub_total`/`pub_lost` 发布统计（uint32）。

## 界面映射

| 界面位置 | 数据来源 |
|---|---|
| 节点A（采集）在线/离线 | 最近 5 s 内收到 `gw0805/nodeA/telemetry` 即在线；超时转"离线"并把 `ir/ldr/seq` 显示成 `--` |
| 节点B（控制）在线/离线 | 最近 5 s 内收到 `gw0805/nodeB/telemetry` 即在线；超时把电机/PWM/模式/flags 显示成 `--` |
| 红外 / 光敏 / 序列 | `nodeA/telemetry` 的 `ir` / `ldr` / `seq` |
| 电机 / PWM / 模式 / 标志 | `nodeB/telemetry` 的 `motor` / `pwm` / `mode` / `flags`（flags 同时解码成 `COMM_OK, THRESH, ALARM, ESTOP`） |
| 最近告警 | `nodeA/alert`、`nodeB/alert` 的 `code` + `extra` |
| WiFi / MQTT / 缓存 | `gateway/status` |
| 采集 收/丢、发布 总/丢 | `gateway/stats` |
| MQTT 报文日志 | 所有订阅/退订/连接/收到/下发，最多保留 500 行，可"清空" |

在线判定是"看最近报文时间"，不是网关自己上报的心跳位，所以 **telemetry 停发 5 s 就会被判离线**。
`gateway/status`、`gateway/stats` 是快照，只在收到时刷新。

## 日志

- 控制台（`qDebug`）：收发全量，例如
  `[MQTT<-] gw0805/nodeA/telemetry qos=0 retain=false {"ir":1234,...}`、
  `[MQTT->] gw0805/nodeB/cmd/motor qos=1 retain=false 3,50`。
- 界面：网关模式的"MQTT 报文日志"卡片（`QPlainTextEdit`，只读），仿真模式该面板隐藏。

## 构建

构建目录**必须是纯 ASCII 路径**：中文路径下 Qt 的 moc/汇编会失败（`can't create ...obj`）。

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Windows\System32;C:\Windows"

cmake -S "C:\Users\Administrator\Desktop\项目规划\can-vehicle-platform\host\mobile-app" `
      -B C:\cvp_build\mobile2 -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 `
      -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe `
      -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe

cmake --build C:\cvp_build\mobile2 -j 8
```

产物：`C:\cvp_build\mobile2\myphone.exe`（需要 `C:\Qt\6.11.1\mingw_64\bin` 在 `PATH` 里，或自行 windeployqt）。

## 注意事项

- **改协议只改真源**：`contract_qt.h` 由 `protocol/contract.json` 生成，改主题/命令要先改 JSON 再
  `python tools/gen_contract.py`，然后同步各端副本。
- **公共 broker**：`broker.emqx.io` 是公网测试服务器，任何人可读写 `gw0805/*`。演示时别人也可能
  发命令；要固定环境就把 `contract.json` 的 broker 换成自建 Mosquitto 再重新生成。
- **client id 会互相顶号**：同一 broker 上相同 client id 会把旧连接踢下线。桌面监控台用
  `qt_desktop_console`、手机端用 `qt_mobile_app`，不要重复。
- **QoS1 / 不 retain**：命令发出后 broker 给 PUBACK，但"网关是否真的执行"要看 `nodeB/telemetry`
  是否变化；不要用 retain 主题存控制命令，否则新连上的客户端会重放旧指令。
- **急停语义**：网关侧 `0x000` 急停帧是 fail-safe —— 只要 B0 不是 `2`（解除）就按急停执行，
  解析失败/空载荷也一样。所以"解除"必须显式下发 `2`。
- **窗口高度**：顶部模式栏由代码插入，窗口比原来高 52 px（425×726 → 425×778）；原有控件整体
  下移，相对位置和 `.ui` 都没改。
- **仿真模式行为未变**：默认启动仍是仿真模式，`car/ecu/*`、`phone/cmd`、车门/空调 JSON 的
  收发逻辑、批量全门联动都保持原样；只是发送前多了一道"非仿真模式不发送"的互斥保护。
