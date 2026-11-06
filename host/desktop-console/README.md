# desktop-console —— Qt6 车端联调监控台

> 平台的**桌面应用层**：既是"真实链路"的监控台（订阅 ESP32 网关的 MQTT 遥测、下发控制命令），
> 也是"纯仿真"的演示端（本机跑 4 个 ECU 仿真，无需任何硬件）。
> 两种数据源在同一个界面里切换，UI 与网络层通过 Qt 信号槽解耦。
>
> 网络层主题、命令、节点名全部来自 `contract_qt.h`（由 `protocol/contract.json` 生成），本工程不硬编码任何 `gw0805/...` 字符串。

---

## 一、两种数据源模式

| | 仿真模式（默认） | 真实网关模式 |
|---|---|---|
| 数据源 | 本机 `BmsECU / VcuECU / BcmECU / DtcECU` 四个 QTimer 仿真 ECU | ESP32-S3 网关经 MQTT 上行的真实 CAN 数据 |
| CAN 层 | Qt SerialBus `virtualcan`（`can0`，Loopback + ReceiveOwn） | 不经过本机 CAN，直接收 MQTT |
| broker | `8.138.233.135:1883` | `contract::kBrokerHost:kBrokerPort`（`broker.emqx.io:1883`） |
| 主题 | `car/ecu/{bms,vcu,bcm,dtc}` / `phone/cmd` | `gw0805/nodeA|nodeB/telemetry`、`alert`、`gateway/status`、`gateway/stats` |
| 用途 | 无硬件时演示 MQTT 链路与界面 | 与 STM32 节点 + ESP32 网关联调、验收 |

切换方式：界面「运行模式」单选按钮（`radionbtn_fangzhengmoshi` / `radiobtn_zhenshicheliang`）。
切到真实模式时**停掉 ECU 仿真**（记下切换前是否在跑，切回时恢复），并启动网关客户端；
切回仿真时断开网关并重建虚拟 CAN 总线。

---

## 二、类结构与数据流

```text
                 ┌──────────────────────────────────────────────┐
                 │              MainWindow（UI 层）              │
                 │  仿真：CAN 帧解析 → update*Ui / 日志 / 快照    │
                 │  真实：GatewayClient 信号 → 看板 / 日志        │
                 └───────┬───────────────────────────┬──────────┘
                         │                           │
        ┌────────────────▼─────────┐      ┌──────────▼─────────────┐
        │  MqttClient（仿真上行）   │      │ GatewayClient（真实链路）│
        │  car/ecu/* + phone/cmd    │      │ gw0805/*，QoS1 命令下发  │
        └──────────────────────────┘      └──────────┬─────────────┘
                                                     │ MQTT
                                          ┌──────────▼─────────────┐
                                          │ broker.emqx.io:1883     │
                                          └──────────┬─────────────┘
                                                     │
                                          ┌──────────▼─────────────┐
                                          │ ESP32-S3 网关（CAN 侧）  │
                                          └────────────────────────┘
```

| 文件 | 职责 |
|---|---|
| `gatewayclient.h/.cpp` | 真实网关数据源：连 broker、订阅 6 条上行主题、JSON 解析成结构体、按契约拼下行主题发命令、3 s 心跳超时判定节点离线、每条报文抛 `traffic` 信号供 UI 记日志 |
| `mainwindow.h/.cpp` | 主窗口：模式切换、看板与命令面板（代码创建，未改 `.ui`）、时延/在线率/丢帧率统计、报文日志、状态快照导出 |
| `mqttclient.h/.cpp` | 仿真模式下的 MQTT 上行（ECU JSON）+ `phone/cmd` 订阅 |
| `baseecu.h/.cpp` + `*ecu.*` | 仿真 ECU 基类与四个子类（BMS/VCU/BCM/DTC） |
| `vehicledata.h` | 仿真模式的数据模型（4 个 POD 结构 + `toJson`/`fromJson`） |
| `dark_gateway.qss` + `res.qrc` | 深色主题与资源 |
| `contract_qt.h` | **生成物**，由 `python tools/gen_contract.py` 从 `protocol/contract.json` 生成，勿手改 |

---

## 三、网关看板指标口径

| 指标 | 计算方式 | 数据来源 |
|---|---|---|
| 节点 A/B 在线 | `contract::kHbTimeoutMs`（3 s）内收到过该节点 `telemetry` 判在线；500 ms 轮询一次超时 | `GatewayClient` 看门狗 |
| 在线率 | 1 s 采样一次在线状态，**60 s 滑动窗口**内在线采样数 / 总采样数，按 A / B 分别显示 | `onGatewayTick()` |
| 通信时延 | 下发命令的本地时刻 → 收到下一帧 `nodeB/telemetry` 的差值（ms）；5 s 内没等到则显示 `--` | `m_pendingCommandMs` |
| 丢帧率 | `a_lost / (a_rx + a_lost)`，来自网关 `gateway/stats` | CAN 侧 seq 丢帧统计 |
| 发布统计 | `pub_total` / `pub_lost`（网关 MQTT 发布总数与失败数） | `gateway/stats` |
| 网关网况 | `wifi` / `mqtt` / `cache`（未补传条数） | `gateway/status`，每 5 s |

界面右侧是页签结构：`MQTT 控制`（原有）、`网关看板`、`命令下发`。切到真实模式会自动翻到"网关看板"。

---

## 四、命令下发（QoS 1，绝不 retain）

命令主题 = `contract::kTopicPrefix + "/" + <节点> + "/cmd/" + <命令类型>`，
**命令类型只出现在主题里，负载是纯数据**——与网关 `can_bridge_mqtt()` 的约定一致。

| 按钮 | 节点 | 主题后缀 | Payload | 对应 CAN 帧 |
|---|---|---|---|---|
| 急停 / 解除 | `all`（广播） | `/cmd/estop` | `1` / `2` | `0x000` |
| 电机 停 / 正转 / 反转 | 下拉选择（默认 nodeB） | `/cmd/motor` | `0` / `1` / `2` | `0x301` / `0x302` |
| 设速 | 同上 | `/cmd/motor` | `3,<0~100>` | 同上 |
| 目标值 | 同上 | `/cmd/target` | `<0~65535>` | `0x311` / `0x312` |
| 模式 手动 / 自动 | 同上 | `/cmd/mode` | `0` / `1` | `0x332` |

所有下行报文都在报文日志里以 `TX` 标记记录，回读的 `0x102` 状态以 `RX` 标记记录，
因此"命令有没有真的执行"在界面上是可验证的，而不是发出去就当成功。

---

## 五、构建

> 构建目录必须在**纯 ASCII 路径**下：Qt 的 `moc` 在中文路径下会因无法创建中间目录而失败。

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$env:PATH"

cmake -S host/desktop-console -B C:/build/desktop -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64

cmake --build C:/build/desktop -j 8
# 产物：C:/build/desktop/vehicle_gateway.exe
```

依赖：Qt 6.11（Widgets / Network / SerialBus / Svg）+ `qmqtt/`（仓库内 vendored 的 QMQTT 静态库，构建时 `add_subdirectory`）。

---

## 六、联调步骤（真实模式）

1. 先让 `firmware/gateway`、`firmware/collect-node`、`firmware/control-node` 上电并跑通
   （见 [`docs/bringup.md`](../../docs/bringup.md)）；
2. 打开本程序，选「真实车辆」模式；
3. 右侧「网关看板」出现节点 A/B 在线、红外/光敏数值、电机状态与 PWM、网关网况；
4. 切到「命令下发」，点「电机正转」并设速，1 s 内应看到 `0x102` 回读变化，时延数值刷新；
5. 点「急停」，执行节点立即停车，`0x102` 的 `flags` 出现急停位（bit3）。

> 公共 broker `broker.emqx.io` 上任何人都能读写 `gw0805/*`。演示前建议先改
> `protocol/contract.json` 里的 `mqtt.prefix`（例如带日期/工位后缀），
> 然后 `python tools/gen_contract.py && python tools/sync_contract_copies.py` 重新生成常量。

---

## 七、注意事项

- 看板与命令面板全部由代码创建并插入现有布局，`mainwindow.ui` 未改动；原仿真界面结构保持不变。
- 网关模式暂不做断线自动重连（与仿真模式原行为一致），断线后重新选择一次模式即可。
- 仿真模式与真实模式共用同一套日志与快照控件，但数据互不串源（切换时清空上一轮显示）。
- 契约变更后请重新生成并同步：`gen_contract.py` → `sync_contract_copies.py` → `test_contract.py`。
