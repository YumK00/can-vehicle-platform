# 网关收发速查表（写代码时对照用）

> **本文职责**：**编码对照表** —— 不做原理说明，只回答"这个 ID / 这条命令该用哪个函数、发哪个主题"。
>
> **ESP32-S3 = CAN ⇄ JSON 翻译官**
> 在 CAN 侧它是普通节点（节点号 3）；在 MQTT 侧它是**唯一**和云对话的人。
> 全部工作 = **CAN 帧 ⇄ JSON 互相翻译**。
>
> 相关：入口 `项目总纲.md` ｜ 协议细节 `CAN协议表与接线图.md` ｜ 编解码函数 `protocol.h`

---

## 1. 总图

```
                    ┌─────────────────────────┐
   0x101 A传感器 ───▶│                         │───▶ {p}/gw01/nodeA/telemetry
   0x102 B状态   ───▶│                         │───▶ {p}/gw01/nodeB/telemetry
   0x701 A心跳   ───▶│      ESP32-S3 网关      │───▶ {p}/gw01/alert
   0x702 B心跳   ───▶│                         │───▶ {p}/gw01/gateway/status
   0x011 报警    ───▶│      （翻译官）          │───▶ {p}/gw01/gateway/stats
                    │                         │
   0x302 电机命令 ◀──│                         │◀─── {p}/gw01/+/cmd
   0x312 目标值   ◀──│                         │
   0x322 PID     ◀──│                         │
   0x001 急停    ◀──│                         │
   0x703 心跳    ───▶│（自己发出去）            │
                    └─────────────────────────┘
       CAN 总线 ↑                              ↑ MQTT (WiFi)
```

---

## 2. CAN **接收** → 干什么

| ID | 宏名 | DLC | 解包函数 | 处理动作 |
|----|------|-----|----------|----------|
| `0x101` | `CAN_ID_A_SENSOR` | 5 | `PROTO_UnpackASensor()` | 发 `nodeA/telemetry` |
| `0x102` | `CAN_ID_B_STATUS` | 4 | `PROTO_UnpackBStatus()` | 发 `nodeB/telemetry` |
| `0x701` | `CAN_ID_A_HEARTBEAT` | 2 | 看 `data[0]==HB_MAGIC_A` | **只刷新"A 在线"，不发 MQTT** |
| `0x702` | `CAN_ID_B_HEARTBEAT` | 2 | 看 `data[0]==HB_MAGIC_B` | **只刷新"B 在线"，不发 MQTT** |
| `0x011` | `CAN_ID_ALARM_A` | 3 | `PROTO_AlarmCode()` | 发 `alert`（阶段 2） |
| `0x012` | `CAN_ID_ALARM_B` | 3 | `PROTO_AlarmCode()` | 发 `alert`（阶段 2） |

> 心跳**三查**：ID 对 + `len == LEN_HEARTBEAT` + 魔数对，三个都满足才算有效心跳。

## 3. CAN **发送** ← 什么时候发

| ID | 宏名 | DLC | 打包函数 | 触发条件 |
|----|------|-----|----------|----------|
| `0x302` | `CAN_ID_CTRL_MOTOR_B` | 3 | `PROTO_PackCtrlMotor()` | MQTT `{"cmd":"motor"}` |
| `0x312` | `CAN_ID_CTRL_TARGET_B` | 3 | `PROTO_PackCtrlTarget()` | MQTT `{"cmd":"target"}` |
| `0x322` | `CAN_ID_CTRL_PID_B` | 7 | `PROTO_PackCtrlPid()` | MQTT `{"cmd":"pid"}`（阶段 2） |
| `0x332` | `CAN_ID_CTRL_MODE_B` | 2 | `PROTO_PackCtrlMode()` | MQTT `{"cmd":"mode"}`（阶段 2） |
| `0x001` | `CAN_ID_ESTOP` | 1 | 直接填 `0x01` | MQTT `{"cmd":"estop"}` |
| `0x703` | `CAN_ID_GW_HEARTBEAT` | 2 | `PROTO_PackHeartbeat(d, HB_MAGIC_GW, cnt)` | 每 1000ms |
| `0x103` | `CAN_ID_GW_STATUS` | 4 | `PROTO_PackGwStatus()` | 每 1000ms（阶段 2） |

---

## 4. MQTT **发布**（上行）

| 主题 | QoS | Retained | 触发 | payload |
|------|-----|----------|------|---------|
| `{p}/nodeA/telemetry` | 0 | ❌ | 收到 CAN `0x101` | `{"ir":3854,"ldr":1030,"seq":42}` |
| `{p}/nodeB/telemetry` | 0 | ❌ | 收到 CAN `0x102` | `{"motor":1,"pwm":60,"mode":0,"flags":1}` |
| `{p}/nodeA/alert` | 1 | ❌ | 收到 CAN `0x011` | `{"code":2,"extra":0}` |
| `{p}/nodeB/alert` | 1 | ❌ | 收到 CAN `0x012` | `{"code":2,"extra":0}` |
| `{p}/gateway/status` | 0 | ❌ | 每 5s | `{"wifi":1,"mqtt":1,"cache":0}` |
| `{p}/gateway/stats` | 0 | ❌ | 每 10s（**待做**） | `{"rx":1234,"lost":3,"lost_rate":0.0024}` |
| `{p}/nodeA/status` ⭐ | 1 | ✅ | 在线状态翻转（**待做**） | `{"online":true,"lastseen":…}` |
| `{p}/nodeB/status` ⭐ | 1 | ✅ | 同上（**待做**） | 同上 |

> **`{p}` = 完整主题前缀**，本项目固定为 **`gw01`**（代码里的 `MQTT_PREFIX` 宏）。
> ⭐ 带 retained 的主题是"待做"状态——现在的实现都是不 retained 的。

**LWT 遗嘱**（连接时声明，用同一个 `gateway/status` 主题）：
`{"online":false}` / QoS 1 / **retain = 1**

## 5. MQTT **订阅**（下行）—— 完整命令清单

**网关只订一个主题** → `{p}/+/cmd/#`

一层通配节点名（`+`）+ 多层通配命令类型（`#`），一条订阅收全部。

### 命令格式

```
{p}/{节点}/cmd/{命令类型}
     ↑              ↑
nodeA / nodeB / all   estop / motor / target / pid / mode
```

**Payload 是纯数据，不是 JSON**——命令类型放在主题里，MCU 就不用写 JSON 解析器。

### 全部命令总表

| # | 主题（`{p}` = 前缀） | Payload | 产生 CAN 帧 | 谁执行 |
|---|---------------------|---------|------------|--------|
| 1 | `{p}/all/cmd/estop` | 任意（建议 `1`） | `0x001` | **所有节点** |
| 2 | `{p}/nodeA/cmd/motor` | `命令,参数` 如 `1,50` | `0x301` | A |
| 3 | `{p}/nodeA/cmd/target` | 数字 如 `2000` | `0x311` | A |
| 4 | `{p}/nodeB/cmd/motor` | `命令,参数` 如 `1,50` | `0x302` | B |
| 5 | `{p}/nodeB/cmd/target` | 数字 如 `2000` | `0x312` | B |
| 6 | `{p}/nodeB/cmd/pid` | `kp,ki,kd` 如 `1.5,0.2,0` | `0x322` | B |
| 7 | `{p}/nodeB/cmd/mode` | `0` 或 `1` | `0x332` | B |

### Payload 格式与取值

| 命令类型 | Payload 格式 | 取值 |
|----------|--------------|------|
| `estop` | 任意 | payload 被忽略，固定发 `0x01` |
| `motor` | `命令` 或 `命令,参数` | 命令：0=停 1=正转 2=反转 3=设速<br>参数（速度）：0~100 |
| `target` | 单个数字 | 0~4095（光照 ADC 原始值） |
| `pid` | `kp,ki,kd` 三个浮点 | 内部 ×100 转 uint16；不允许负数 |
| `mode` | 单个数字 | 0=手动 ／ 1=自动(PID) |

### 照抄就能用的例子

| 想做什么 | 主题 | Payload |
|----------|------|---------|
| **急停**（广播） | `{p}/all/cmd/estop` | `1` |
| B 正转 50% | `{p}/nodeB/cmd/motor` | `1,50` |
| B 反转 30% | `{p}/nodeB/cmd/motor` | `2,30` |
| B 停 | `{p}/nodeB/cmd/motor` | `0` |
| B 设速 80 | `{p}/nodeB/cmd/motor` | `3,80` |
| B 目标光照 2000 | `{p}/nodeB/cmd/target` | `2000` |
| B 下 PID 参数 | `{p}/nodeB/cmd/pid` | `1.5,0.2,0` |
| B 切自动模式 | `{p}/nodeB/cmd/mode` | `1` |
| A 目标光照 2000 | `{p}/nodeA/cmd/target` | `2000` |

**MQTTX 里三项设置**：QoS = **1**、Retain = **关闭**、payload 照上表填。

### ⚠️ `all` 只建议用于 `estop`

`all` 会被映射成广播节点号 0，于是 `all/cmd/motor` 会生成 **`0x300`**——但协议里**没有定义广播电机命令**（只定义了 `0x301`/`0x302`）。

**广播控制动作是危险的**（"所有节点一起开电机"基本不是你想要的）。建议在 `can_bridge_mqtt` 里加一条限制：

```c
/* 只有急停允许广播，其他控制命令必须指名节点 */
if (target == NODE_BROADCAST && id != CAN_ID_ESTOP) {
    ESP_LOGW(TAG, "广播只支持 estop，拒绝 %s", kind);
    return;
}
```

这样 ID 空间里也不会多出没定义的 `0x300` / `0x310` / `0x320`。

### 批量测试（可选，比手点快）

装了 mosquitto 客户端的话，几条命令就能把全部命令过一遍：

```bash
P=gw01
for c in "nodeA/cmd/motor 1,50" "nodeA/cmd/target 2000" \
         "nodeB/cmd/motor 1,50" "nodeB/cmd/target 2000" \
         "nodeB/cmd/pid 1.5,0.2,0" "nodeB/cmd/mode 1" \
         "all/cmd/estop 1"; do
    set -- $c
    mosquitto_pub -h broker.emqx.io -t "$P/$1" -m "$2" -q 1
done
```

每发一条，看 ESP32 monitor 有没有对应的 `MQTT→CAN 0xXXX ...` 日志。

---

## 6. 三条铁律（最容易搞错）

### ① 心跳收进来，但**不转发** MQTT

```
收到 0x701 ──✗──▶ 不直接发 MQTT
            │
            └──▶ 只刷新内部状态 A_online
                      │
                      └──▶ 只在状态**翻转**时发一次 nodeA/status
```

理由：1Hz × N 个节点全转发到云是纯浪费。云端只看 `status` 的 `online` 字段。

### ② 网关**收不到自己发的帧**

CAN 正常模式下节点不回环自己的发送。所以 `0x703`/`0x103`/`0x302` **不用写接收处理**。

### ③ 命令主题**绝不能** retained

retained 会让每个新订阅者一连上就重放最后一条命令并执行。
→ 想象网关重启后立刻执行三天前的"电机正转"。
→ **发命令时 retain 必须传 0。**

---

## 7. 阶段 0 最小实现（别被上面表格吓到）

| 做什么 | 具体 |
|--------|------|
| ✅ CAN 收 | 只收 `0x101` |
| ✅ MQTT 发 | 只发 `nodeA/telemetry` |
| ⬜ 其他全部 | 往后放 |

> **最小版本就一句话**：收到 CAN `0x101` → `PROTO_UnpackASensor()` → 发一条 `nodeA/telemetry`。
> 跑通它（手机 MQTTX 看到真实数据），阶段 0 的核心就完成了。

---

## 8. 常量速查

| 项 | 值 |
|----|-----|
| 节点号 | 广播=0 ／ A=1 ／ B=2 ／ **网关=3** |
| 心跳魔数 | A=`0xA5` ／ B=`0x5A` ／ **网关=`0xC3`** |
| 波特率 | **250 kbps**（`TWAI_TIMING_CONFIG_250KBITS()`） |
| 网关 CAN 引脚 | TXD→GPIO17、RXD→GPIO18（**RXD 必须分压**） |
| 心跳超时 | **3 秒**（心跳周期的 3 倍） |
| 主题前缀 | **`gw01`**（代码里的 `MQTT_PREFIX` 宏） |

**电机命令 `cmd` 取值**：0=停 ／ 1=正转 ／ 2=反转 ／ 3=设速
**电机状态 `motor` 取值**：0=停 ／ 1=正转 ／ 2=反转
**模式 `mode` 取值**：0=手动 ／ 1=自动(PID)
