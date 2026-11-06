# 通信契约（自动生成）

> 由 `protocol/contract.json` 生成，请勿手改。重新生成：`python tools/gen_contract.py`

## 1. 总线参数

| 项 | 值 |
|---|---|
| 标准 | CAN 2.0A |
| 位速率 | 250 kbps |
| 采样点 | 87.5% |
| 字节序 | big-endian |
| 心跳超时 | 3000 ms |

## 2. 节点与功能组

| 节点 | 编号 |
|---|---|
| BROADCAST | 0 |
| COLLECT | 1 |
| CONTROL | 2 |
| GATEWAY | 3 |

| 功能组 | 基址 |
|---|---|
| ESTOP | 0x000 |
| ALARM | 0x010 |
| PERIODIC | 0x100 |
| CTRL_MOTOR | 0x300 |
| CTRL_TARGET | 0x310 |
| CTRL_PID | 0x320 |
| CTRL_MODE | 0x330 |
| HEARTBEAT | 0x700 |

## 3. CAN 帧表

| ID | DLC | 名称 | 方向 | 字段 |
|---|---|---|---|---|
| `0x000` | 1 | ESTOP | ALL → BROADCAST | action[0:1] |
| `0x011` | 3 | ALARM_COLLECT | COLLECT → GATEWAY | code[0:1], extra[1:3] |
| `0x012` | 3 | ALARM_CONTROL | CONTROL → GATEWAY | code[0:1], extra[1:3] |
| `0x101` | 5 | COLLECT_SENSOR | COLLECT → BROADCAST | ir[0:2], ldr[2:4], seq[4:5] |
| `0x102` | 4 | CONTROL_STATUS | CONTROL → BROADCAST | motor[0:1], pwm[1:2], mode[2:3], flags[3:4] |
| `0x103` | 4 | GATEWAY_STATUS | GATEWAY → BROADCAST | wifi[0:1], mqtt[1:2], cache[2:4] |
| `0x301` | 3 | CTRL_MOTOR_COLLECT | GATEWAY → COLLECT | cmd[0:1], arg[1:2], cid[2:3] |
| `0x302` | 3 | CTRL_MOTOR_CONTROL | GATEWAY → CONTROL | cmd[0:1], arg[1:2], cid[2:3] |
| `0x311` | 3 | CTRL_TARGET_COLLECT | GATEWAY → COLLECT | target[0:2], cid[2:3] |
| `0x312` | 3 | CTRL_TARGET_CONTROL | GATEWAY → CONTROL | target[0:2], cid[2:3] |
| `0x322` | 7 | CTRL_PID_CONTROL | GATEWAY → CONTROL | kp[0:2], ki[2:4], kd[4:6], cid[6:7] |
| `0x332` | 2 | CTRL_MODE_CONTROL | GATEWAY → CONTROL | mode[0:1], cid[1:2] |
| `0x701` | 2 | COLLECT_HEARTBEAT | COLLECT → BROADCAST | magic[0:1], cnt[1:2] |
| `0x702` | 2 | CONTROL_HEARTBEAT | CONTROL → BROADCAST | magic[0:1], cnt[1:2] |
| `0x703` | 2 | GATEWAY_HEARTBEAT | GATEWAY → BROADCAST | magic[0:1], cnt[1:2] |

## 4. MQTT 发布主题

| 主题 | QoS | Retain | 载荷 | 来源 |
|---|---|---|---|---|
| `{prefix}/nodeA/telemetry` | 0 | 否 | `ir:uint16, ldr:uint16, seq:uint8` | CAN 0x101 |
| `{prefix}/nodeB/telemetry` | 0 | 否 | `motor:uint8, pwm:uint8, mode:uint8, flags:uint8` | CAN 0x102 |
| `{prefix}/nodeA/alert` | 1 | 否 | `code:uint8, extra:uint16` | CAN 0x011 |
| `{prefix}/nodeB/alert` | 1 | 否 | `code:uint8, extra:uint16` | CAN 0x012 |
| `{prefix}/gateway/status` | 0 | 否 | `wifi:uint8, mqtt:uint8, cache:uint16` | CAN 0x103 / 网况 |
| `{prefix}/gateway/stats` | 0 | 否 | `a_rx:uint32, a_lost:uint32, pub_total:uint32, pub_lost:uint32` | can_get_stats() |

## 5. MQTT 订阅主题

| 主题 | QoS | 说明 |
|---|---|---|
| `{prefix}/+/cmd/#` | 1 | + = 节点（nodeA/nodeB/all），# = 命令类型 |

## 6. 命令映射

| 命令 | 主题 | CAN 帧 | 载荷 |
|---|---|---|---|
| estop | `{prefix}/{node}/cmd/estop` | `0x000` | uint8 |
| motor | `{prefix}/{node}/cmd/motor` | `0x301|0x302` | cmd[,arg] |
| target | `{prefix}/{node}/cmd/target` | `0x311|0x312` | uint16 |
| pid | `{prefix}/{node}/cmd/pid` | `0x322` | kp,ki,kd |
| mode | `{prefix}/{node}/cmd/mode` | `0x332` | uint8 |
