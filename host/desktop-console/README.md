# desktop-console —— Qt6 车端联调监控台

> 平台的**桌面应用层**：既是"真实链路"的监控台（订阅 ESP32 网关的 MQTT 遥测、下发控制命令），
> 也是"纯仿真"的演示端（本机跑 4 个 ECU 仿真，无需任何硬件）。
> 两种数据源在同一个界面里切换，UI 与网络层通过 Qt 信号槽解耦。
>
> 网络层主题、命令类型、节点名全部来自 `contract_qt.h`（由 `protocol/contract.json` 生成），
> 本工程不硬编码任何 `gw0805/...` 主题字符串。

---

## 一、两种数据源模式

| | 仿真模式（默认） | 真实车辆模式 |
|---|---|---|
| 数据源 | 本机 `BmsECU / VcuECU / BcmECU / DtcECU` 四个 QTimer 仿真 ECU | ESP32-S3 网关经 MQTT 上行的真实 CAN 数据 |
| CAN 层 | Qt SerialBus `virtualcan`（`can0`，Loopback + ReceiveOwn） | 不经过本机 CAN，直接收 MQTT |
| broker | `8.138.233.135:1883`（点「连接Broker」后由 `MqttClient` 上行） | `contract::kBrokerHost:kBrokerPort`（`broker.emqx.io:1883`） |
| 主题 | `car/ecu/{bms,vcu,bcm,dtc}` / `phone/cmd` | `gw0805/nodeA/telemetry`、`gw0805/nodeB/telemetry`、`alert`、`gateway/status`、`gateway/stats` |
| 用途 | 无硬件时演示 MQTT 链路与界面 | 与 STM32 节点 + ESP32 网关联调、验收 |

切换方式：界面「运行模式」单选按钮（`radionbtn_fangzhengmoshi` / `radiobtn_zhenshicheliang`）。
程序只监听「真实车辆」这一颗的 `toggled`，一次点击不会触发两遍逻辑：

```text
仿真模式 → 真实车辆
  1. 记录切换前仿真是否在跑；
  2. 停掉 4 个 ECU 仿真与虚拟 CAN（closeConnect + 停止 CAN 日志定时器）；
  3. 创建 GatewayClient，连 broker，订阅 6 条上行主题；
  4. 右列自动翻到「网关看板」Tab，看板指标清零后等新数据。

真实车辆 → 仿真模式
  1. 断开 GatewayClient（先关 autoReconnect 再 disconnect，避免它自己连回来）；
  2. 若切走前仿真在跑，自动按原流程恢复「启动CAN」；
  3. 右列自动切回原来的「MQTT 控制」Tab。
```

> 真实模式下按「启动CAN」= 明确要回到仿真：先退出真实模式再启动仿真；
> 按「停止CAN」只记一条日志，不会把网关看板改成"已停止"。

---

## 二、类结构与数据流

```text
                 ┌──────────────────────────────────────────────┐
                 │              MainWindow（UI 层）              │
                 │  仿真：CAN 帧解析 → update*Ui / 日志 / 快照    │
                 │  真实：GatewayClient 信号 → 看板 / 命令 / 日志 │
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

### 2.1 GatewayClient（`gatewayclient.h/.cpp`）

| 项 | 说明 |
|---|---|
| 基类 | `QObject`，底层是用仓库内 vendored 的 QMQTT 静态库 |
| broker | `contract::kBrokerHost : contract::kBrokerPort` |
| client id | 固定串 `qt_desktop_console`（对齐 `contract.json → mqtt.client_ids.desktop`） |
| 订阅 | 6 条上行主题，统一 QoS1（遥测在网关侧是 QoS0，broker 取 min 后仍是 0；报警保住 QoS1） |
| 命令 | `publishCommand()`：QoS1、`retain=false`，主题按契约拼 |
| 重连 | `setAutoReconnect(true)`，间隔 3 s；主动断开前先关自动重连 |
| 在线判断 | `contract::kHbTimeoutMs`（3000 ms）内没有该节点 telemetry 即判离线，500 ms 轮询一次 |

对外接口：

```cpp
void connectBroker();
void disconnectBroker();
bool isConnected() const;

bool publishCommand(const QString &node, const QString &kind,
                    const QByteArray &payload);   // QoS1 + retain=false

bool isNodeOnline(const QString &node) const;      // nodeA / nodeB
QDateTime lastTelemetryTime(const QString &node) const;
```

对外信号：

| 信号 | 触发 |
|---|---|
| `connectionStateChanged(bool, QString)` | broker 已连接 / 已断开 / 连接中 / 链路异常 |
| `collectTelemetryReceived(CollectNodeTelemetry)` | `nodeA/telemetry` → `{ir,ldr,seq}` |
| `controlTelemetryReceived(ControlNodeTelemetry)` | `nodeB/telemetry` → `{motor,pwm,mode,flags}` |
| `alertReceived(GatewayAlert)` | `nodeA/alert`、`nodeB/alert` → `{code,extra}` |
| `gatewayStatusReceived(GatewayStatusData)` | `gateway/status` → `{wifi,mqtt,cache}` |
| `gatewayStatsReceived(GatewayStatsData)` | `gateway/stats` → `{a_rx,a_lost,pub_total,pub_lost}` |
| `nodeOnlineChanged(QString, bool)` | 节点上线 / 掉线（超时判定） |
| `traffic(bool isRx, QString topic, QByteArray payload)` | 每条收发报文 → 写 `listWidgetmqtt` |

`publishCommand()` 内部拼主题，所有片段都来自契约常量：

```cpp
topic = QString::fromLatin1(contract::kTopicPrefix) + "/" + node + "/cmd/" + kind;
```

### 2.2 MainWindow 的接入点

| 函数 | 作用 |
|---|---|
| `setupGatewayPanel()` | **代码创建**看板与命令面板，`insertWidget` 进已有 `horizontalLayout_2` 右列；原「MQTT 控制」组原样搬入第 0 个 Tab，默认可见，因此原界面与行为不变 |
| `onRunModeChanged(bool)` | 单出口处理模式切换 |
| `enterRealMode()` / `enterSimMode()` | 停/启仿真、连/断网关、切 Tab、清指标 |
| `onGatewayTick()` | 1 s 采样在线率、命令超时兜底、断线兜底 |
| `sendGatewayCommand()` | 统一校验连接 → `publishCommand` → 记录下发时刻算时延 |
| `onGatewayTraffic()` | 每条报文交给 `writeLog()`：`[hh:mm:ss]RX/TX` + `scrollToBottom`，列表最多保留 500 条 |

---

## 三、主题映射

### 3.1 上行（网关发布 → 桌面订阅）

| 主题 | QoS | 载荷 | 看板落点 |
|---|---|---|---|
| `gw0805/nodeA/telemetry` | 0 | `{"ir":uint16,"ldr":uint16,"seq":uint8}` | 节点A在线、红外值、光敏值、帧序号 |
| `gw0805/nodeB/telemetry` | 0 | `{"motor":uint8,"pwm":uint8,"mode":uint8,"flags":uint8}` | 节点B在线、电机状态/PWM、模式、状态位、通信时延 |
| `gw0805/nodeA/alert` | 1 | `{"code":uint8,"extra":uint16}` | 最近报警 |
| `gw0805/nodeB/alert` | 1 | `{"code":uint8,"extra":uint16}` | 最近报警 |
| `gw0805/gateway/status` | 0 | `{"wifi":uint8,"mqtt":uint8,"cache":uint16}` | 网关 WiFi / MQTT / 缓存条数 |
| `gw0805/gateway/stats` | 0 | `{"a_rx":uint32,"a_lost":uint32,"pub_total":uint32,"pub_lost":uint32}` | 丢帧率、发布统计 |

### 3.2 下行（桌面发布 → 网关订阅 `gw0805/+/cmd/#`）

| 命令 | 主题 | 载荷 | 对应 CAN |
|---|---|---|---|
| 急停 | `gw0805/all/cmd/estop` | `1` | `0x000` 广播（B0≠2 一律按急停） |
| 解除急停 | `gw0805/all/cmd/estop` | `2` | `0x000` 广播 |
| 电机 停止/正转/反转 | `gw0805/<nodeB或nodeA>/cmd/motor` | `0` / `1` / `2` | `0x302` / `0x301` |
| 电机 设速 | `gw0805/<nodeB或nodeA>/cmd/motor` | `3,<0~100>` | `0x302` / `0x301` |
| 目标值 | `gw0805/<nodeB或nodeA>/cmd/target` | `<0~65535>` | `0x312` / `0x311` |
| 模式 手动/自动 | `gw0805/nodeB/cmd/mode` | `0` / `1` | `0x332`（只有执行节点支持） |

全部命令 `QoS1`、`retain=false`。急停固定广播 `all`，模式固定 `nodeB`；
「命令目标」下拉框只影响电机和目标值。

---

## 四、网关看板指标口径

看板在右列「网关看板」Tab（切到真实车辆模式自动选中），逐帧/每秒刷新。

| 指标 | 显示 | 口径 / 来源 |
|---|---|---|
| Broker | 未启动 / 连接中 / 已连接 / 链路异常 | QMQTT 连接状态 + `error` 信号 |
| 节点A / 节点B | 在线 / 离线 | 3 s 内是否收到该节点 `telemetry`（`contract::kHbTimeoutMs`） |
| 红外 ir | ADC 原始值 0~4095 | `nodeA/telemetry.ir` |
| 光敏 ldr | ADC 原始值 0~4095 | `nodeA/telemetry.ldr` |
| 帧序号 seq | 0~255 回绕 | `nodeA/telemetry.seq` |
| 电机 / PWM | 停止/正转/反转 · `PWM n%` | `nodeB/telemetry.motor/pwm` |
| 模式 | 手动 / 自动 | `nodeB/telemetry.mode` |
| 状态位 | `COMM_OK`/`THRESH`/`ALARM`/`ESTOP` 组合 | `nodeB/telemetry.flags` 位域 |
| 网关 WiFi / MQTT | 已连接 / 断开 | `gateway/status.wifi/mqtt` |
| 缓存条数 | 待补传条数 | `gateway/status.cache` |
| 最近报警 | `A · COMM_LOST`（tooltip 带 code/extra） | `nodeA/alert`、`nodeB/alert`；报警码名称来自 `contract.json → alarm_codes` |
| **丢帧率** | `2.31%（12/520）`，红/橙/绿分级 | `a_lost / (a_rx + a_lost)` |
| **发布统计** | `总 1234 · 丢 2（0.16%）` | `pub_lost / pub_total`，tooltip 带 4 个原始累计值 |
| **通信时延** | `xxx ms` / `等待下一帧 0x102…` / `超时（无 0x102）` | 命令下发时刻 → 下一帧 `nodeB/telemetry`（CAN `0x102`）到达的本地时间差 |
| **在线率** | `A 98.3% / B 100.0%（60 s 窗口）` | 每秒采样一次在线状态，60 s 窗口内在线占比，A/B 分别统计 |

> 丢帧率与发布统计是**网关开机以来的累计值**（`can_get_stats()`），不会清零；
> 通信时延用 1 Hz 的 0x102 周期帧近似"命令生效回执"，数值天然带 0~1 s 周期偏差。
> 所有指标旁边都有小字说明（看板底部），口径与网关 `can.h` 注释保持一致。

---

## 五、命令面板

在右列「命令下发」Tab；未连接 broker 时整块禁用（急停按钮也会压暗）。

| 控件 | Payload | 目标节点 |
|---|---|---|
| 急停（广播） | `1` | `all` |
| 解除急停 | `2` | `all` |
| 停止 / 正转 / 反转 | `0` / `1` / `2` | 命令目标下拉框（nodeB / nodeA） |
| 设速 0~100 + 设速下发 | `3,<spin>` | 命令目标下拉框 |
| 目标值（`QIntValidator` 0~65535）+ 下发目标 | `QByteArray::number(value)` | 命令目标下拉框 |
| 手动 / 自动 | `0` / `1` | 固定 `nodeB` |

命令发出后，看板「通信时延」进入"等待下一帧 0x102…"；收到回读后显示毫秒数，
5 s 内没等到则标红"超时（无 0x102）"。因此"命令有没有真的执行"在界面上可验证，
而不是发出去就当成功。

---

## 六、文件清单

| 文件 | 职责 |
|---|---|
| `gatewayclient.h/.cpp` | 真实网关数据源：连接/订阅/解析/下发/判活 |
| `mainwindow.h/.cpp` | 主窗口：模式切换、看板与命令面板（代码创建）、统计、日志、状态快照 |
| `mqttclient.h/.cpp` | 仿真模式下的 MQTT 上行（ECU JSON）+ `phone/cmd` 订阅 |
| `baseecu.h/.cpp` + `*ecu.*` | 仿真 ECU 基类与四个子类（BMS/VCU/BCM/DTC） |
| `vehicledata.h` | 仿真模式的数据模型（4 个 POD 结构 + `toJson`/`fromJson`） |
| `dark_gateway.qss` + `res.qrc` | 深色主题与资源（新增了 Tab 与急停按钮样式） |
| `contract_qt.h` | **生成物**，由 `python tools/gen_contract.py` 从 `protocol/contract.json` 生成，勿手改 |
| `mainwindow.ui` | 未改动；仅复用「运行模式」单选按钮与 `horizontalLayout_2` |

---

## 七、构建

> 构建目录必须在**纯 ASCII 路径**下：Qt 的 `moc` 在中文路径下会因无法创建中间目录而失败。

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Windows\System32;C:\Windows"

cmake -S "<repo>/host/desktop-console" -B C:\build\desktop -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 `
      -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe `
      -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe

cmake --build C:\build\desktop -j 8
# 产物：C:\build\desktop\vehicle_gateway.exe
```

依赖：Qt 6.11（Widgets / Network / SerialBus / Svg）+ `qmqtt/`（仓库内 vendored，构建时 `add_subdirectory`）。
CMake 用 `Qt${QT_VERSION_MAJOR}` 变量，Qt5 亦可，未硬编码 Qt6。

> 直接双击 exe 若报"缺少 Qt6xxx.dll"，把 `C:\Qt\6.11.1\mingw_64\bin` 加入 `PATH` 再启动，
> 或用 `windeployqt` 把依赖 DLL 拷到产物目录。

---

## 八、联调步骤（真实模式）

1. 先让 `firmware/gateway`、`firmware/collect-node`、`firmware/control-node` 上电并跑通
   （见 [`docs/bringup.md`](../../docs/bringup.md)）；
2. 打开本程序，选「真实车辆」模式；
3. 右侧「网关看板」应出现 Broker「已连接」、节点 A/B「在线」、红外/光敏数值、
   电机状态与 PWM、网关 WiFi/MQTT/缓存、丢帧率与发布统计；
4. 切到「命令下发」，点「正转」或设速下发，1 s 内应看到 `0x102` 回读变化、
   `MQTT收发日志` 出现 `TX gw0805/nodeB/cmd/motor ...`，时延数值刷新；
5. 点「急停（广播）」，两个节点应立即停车，`0x102` 的 `flags` 出现急停位（bit3）；
6. 断开网关电源：3 s 后节点 A/B 变「离线」、在线率下降；
   恢复后 GatewayClient 自动重连，无需重启桌面端。

> 公共 broker `broker.emqx.io` 上任何人都能读写 `gw0805/*`。演示前建议先改
> `protocol/contract.json` 里的 `mqtt.prefix`（例如带日期/工位后缀），
> 然后 `python tools/gen_contract.py && python tools/sync_contract_copies.py` 重新生成常量。

---

## 九、注意事项与已知限制

- 看板与命令面板全部由代码创建并插入现有布局，`mainwindow.ui` 未改动；
  右列用 Tab 承载「MQTT 控制 / 网关看板 / 命令下发」，不新增列，原三列几何关系不变。
- 网关模式支持 broker 断线自动重连（3 s 间隔）；主动切回仿真时会先关闭自动重连再断开。
- 无硬件时只能看到「Broker 已连接」，节点 A/B 一直离线——没有 telemetry 属正常现象。
- `listWidgetmqtt` 只保留最近 500 条：nodeA 遥测是 10 Hz，不设上限会持续增长。
- 在线率窗口固定 60 个 1 s 采样点，切换模式会清空，仅供联调参考。
- 模式命令 `0x332` 只由执行节点 B 处理，所以「模式」固定发 `nodeB`；
  若把命令目标选成 nodeA，网关会拒绝 motor/target 之外的 mode 命令。
- 报警码的码值与名称都来自 `contract_qt.h` 的 `kAlarmXxx` / `kAlarmXxxName` 常量
  （由 `contract.json → alarm_codes` 生成）；新增报警码只需改契约并重新生成，显示层无需改动。
- 契约变更后请重新生成并同步：`gen_contract.py` → `sync_contract_copies.py` → `test_contract.py`。

---

## 十、契约来源

```text
protocol/contract.json
        │ python tools/gen_contract.py
        └──► protocol/generated/contract_qt.h
                │ python tools/sync_contract_copies.py
                └──► host/desktop-console/contract_qt.h   （本目录，勿手改）
```
