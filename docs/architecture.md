# 系统架构与数据流

> 本文说明"数据从现场传感器到手机屏幕"要经过哪些环节、每个环节谁负责、契约在哪里约束。
> 通信细节见 [`protocol/generated/contract.md`](../protocol/generated/contract.md)，接线与调试见 [`can-protocol-and-wiring.md`](can-protocol-and-wiring.md)。

## 1. 分层视图

```
┌──────────────────────────── 应用层 ────────────────────────────┐
│   Qt 桌面监控台 (host/desktop-console)      手机 App (host/mobile-app)  │
│   · 实时遥测 / 状态看板                       · 远程控制（JSON + QoS1）   │
│   · 节点在线率 / 通信时延 / 丢帧率             · 车门/空调指令             │
│   · 报文日志 / 状态快照导出                                               │
└───────────────────────────────┬─────────────────────────────────┘
                                │ MQTT over WiFi
                  broker.emqx.io:1883，主题树 gw0805/...
┌───────────────────────────────┴─────────────────────────────────┐
│                    边缘层：ESP32-S3 网关 (firmware/gateway)        │
│  · TWAI 驱动 250 kbps CAN 2.0A                                    │
│  · CAN → MQTT：按 ID 查表打包 JSON（遥测/报警/网况/统计）            │
│  · MQTT → CAN：命令类型放主题，反查功能组发帧                        │
│  · 发布队列解耦：收帧任务只在队列里放一份拷贝，慢网络不拖垮总线        │
│  · 心跳判活 / bus-off 自愈 / seq 丢帧统计                          │
└───────────────────────────────┬─────────────────────────────────┘
                                │ CAN 2.0A 250 kbps（双绞线 + 两端 120 Ω）
        ┌───────────────────────┴───────────────────────┐
        │                                               │
┌───────┴────────────────────┐            ┌─────────────┴──────────────┐
│ 采集节点 A (firmware/       │            │ 执行节点 B (firmware/      │
│ collect-node)              │            │ control-node)              │
│ STM32F103 + FreeRTOS       │            │ STM32F103 + FreeRTOS       │
│ · ADC1 双通道 + DMA 循环    │            │ · 电机 H 桥方向 + TIM3 PWM  │
│ · 红外 / 光敏采集           │            │ · cid 幂等 + 掉线自动停机   │
│ · 0x101 周期上报 / 0x701 心跳│            │ · 0x102 状态回读 / 0x702 心跳│
└────────────────────────────┘            └────────────────────────────┘
```

另有一个独立扩展节点：[`firmware/door-node`](../firmware/door-node)（ESP32-S3，ESP-NOW + SG90 舵机门锁），
用于"车门"这类非 CAN 执行器的演示，主链路不依赖它。

## 2. 数据流（真实链路模式）

以"采集节点上报一帧传感器数据"为例：

```
传感器 → ADC1(CH0/CH1) → DMA 循环写入 adc_dma_buf[2]
   → photoAdcTask（100 ms）读取缓冲、组 0x101 帧（5 字节大端 + seq）
   → CAN_Send(0x101) → 250 kbps 总线广播
   → 网关 can_rx_task 收到 → 校验 DLC → PROTO_UnpackASensor
   → pub_telemetry() 只做一次拷贝 + xQueueSend（超时 0）
   → can_pub_task（优先级垫底）真正调用 esp_mqtt_client_publish
   → MQTT gw0805/nodeA/telemetry {"ir":..,"ldr":..,"seq":..}
   → Qt 桌面监控台刷新数值、累计在线率/时延/丢帧率并写入报文日志
```

反方向（"手机下发一条电机命令"）：

```
手机 App 发布 gw0805/nodeB/cmd/motor，payload "1,50"，QoS 1
   → 网关 esp-mqtt 回调 → can_bridge_mqtt() 拆主题
   → 查表得功能组 GRP_CTRL_MOTOR + 目标节点 NODE_B → 0x302
   → 组帧 {cmd=1, arg=50, cid=自增} → CAN 发出
   → 执行节点 canRxTask 收帧 → cid 幂等判断 → 写命令邮箱
   → motorTask（10 ms 周期、优先级最高）执行换向 + 占空比
   → 1 s 后 0x102 回读实际状态 → 网关 → MQTT → 上位机确认
```

这套闭环里 **每一跳都有可观测证据**：CAN 侧有 `seq`/`cid`/心跳计数，
MQTT 侧有 `gateway/status` 与 `gateway/stats`，上位机侧有报文日志。
这也是"把系统黑盒变白盒"的落点。

## 3. 两种运行模式

| 模式 | 链路 | 用途 |
|---|---|---|
| **真实链路模式** | STM32 节点 ↔ CAN ↔ ESP32 网关 ↔ MQTT ↔ Qt 桌面监控台 / 手机 App | 联调、演示、面试现场 |
| **纯仿真模式** | Qt 桌面内置 4 个 ECU 仿真（BMS/VCU/BCM/DTC）↔ MQTT ↔ 手机 App | 没有硬件时演示 MQTT 链路与 UI |

两种模式共用同一套 MQTT 客户端封装与界面组件，切换只换数据源，用于对比"真实总线"与"模拟数据"的行为差异。

## 4. 契约在架构里的位置

```
              protocol/contract.json  ← 唯一真源（帧表 + 主题 + 命令映射）
                        │
        ┌───────────────┼────────────────┬──────────────────┐
        ▼               ▼                ▼                  ▼
 protocol.h      generated/        generated/         generated/
 (C 编解码)      contract_c.h      contract_qt.h      contract.md
        │               │                │                  │
        ▼               ▼                ▼                  ▼
 collect/control/   网关 MQTT 主题    桌面/手机主题常量    文档表格
 gateway 三端同源    从契约派生        从契约派生          自动同步
```

任意一端想改主题或帧结构，都必须改真源 → 重新生成 → 同步副本 → 跑自测，
`tools/validate_copies.py` 会在 CI 或本地拦住"只改了自己那一份"的情况。

## 5. 可靠性设计（按层）

| 层 | 机制 | 位置 |
|---|---|---|
| 帧级 | DLC 严格校验、大端编解码、`seq` 丢帧统计 | `protocol.h` |
| 命令级 | `cid` 命令序号幂等，抵消 CAN 硬件重发 | 执行节点 `freertos.c` |
| 节点级 | 心跳魔数 + 3 s 超时判活，掉线报警 | 网关 `can.c` / 执行节点 |
| 安全级 | 急停广播 Fail-Safe（解析失败一律停机）、优先级最高的广播 ID | `protocol.h` + 三端 |
| 任务级 | 收帧与发布队列解耦，慢网络不占实时路径 | 网关 `can.c` |
| 总线级 | bus-off 自动恢复（`twai_initiate_recovery` + 重新 `twai_start`） | 网关 `can.c` |
| 契约级 | 单一真源 + 生成 + 副本校验 + 主机侧自测 | `protocol/` + `tools/` |
