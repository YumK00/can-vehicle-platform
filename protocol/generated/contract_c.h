/*
 * 本文件由 tools/gen_contract.py 从 protocol/contract.json 自动生成，请勿手改。
 * 重新生成： python tools/gen_contract.py
 */

#ifndef CONTRACT_C_H
#define CONTRACT_C_H

#define CONTRACT_VERSION "1.0.0"
#define CONTRACT_PROTOCOL_VERSION "3.0"

/* ===== MQTT：Broker 与主题前缀 ===== */
#define CONTRACT_MQTT_HOST     "broker.emqx.io"
#define CONTRACT_MQTT_PORT     1883
#define CONTRACT_MQTT_TLS      0
#define CONTRACT_MQTT_URI      "mqtt://broker.emqx.io:1883"
#define CONTRACT_MQTT_PREFIX   "gw0805"

/* ===== MQTT：发布主题（网关 → 云端/上位机） ===== */
#define CONTRACT_TOPIC_TELEMETRY_COLLECT      CONTRACT_MQTT_PREFIX "/nodeA/telemetry"
#define CONTRACT_QOS_TELEMETRY_COLLECT        0
#define CONTRACT_RETAIN_TELEMETRY_COLLECT     0
#define CONTRACT_TOPIC_TELEMETRY_CONTROL      CONTRACT_MQTT_PREFIX "/nodeB/telemetry"
#define CONTRACT_QOS_TELEMETRY_CONTROL        0
#define CONTRACT_RETAIN_TELEMETRY_CONTROL     0
#define CONTRACT_TOPIC_ALERT_COLLECT          CONTRACT_MQTT_PREFIX "/nodeA/alert"
#define CONTRACT_QOS_ALERT_COLLECT            1
#define CONTRACT_RETAIN_ALERT_COLLECT         0
#define CONTRACT_TOPIC_ALERT_CONTROL          CONTRACT_MQTT_PREFIX "/nodeB/alert"
#define CONTRACT_QOS_ALERT_CONTROL            1
#define CONTRACT_RETAIN_ALERT_CONTROL         0
#define CONTRACT_TOPIC_GATEWAY_STATUS         CONTRACT_MQTT_PREFIX "/gateway/status"
#define CONTRACT_QOS_GATEWAY_STATUS           0
#define CONTRACT_RETAIN_GATEWAY_STATUS        0
#define CONTRACT_TOPIC_GATEWAY_STATS          CONTRACT_MQTT_PREFIX "/gateway/stats"
#define CONTRACT_QOS_GATEWAY_STATS            0
#define CONTRACT_RETAIN_GATEWAY_STATS         0

/* ===== MQTT：订阅主题（下发命令） ===== */
#define CONTRACT_COMMAND_TOPIC CONTRACT_MQTT_PREFIX "/+/cmd/#"
#define CONTRACT_COMMAND_QOS   1

/* ===== MQTT：命令类型 → CAN 帧 ===== */
#define CONTRACT_CMD_ESTOP      "/cmd/estop"
#define CONTRACT_CMD_ESTOP_FRAME   0x000u
#define CONTRACT_CMD_MOTOR      "/cmd/motor"
#define CONTRACT_CMD_MOTOR_FRAME   0x301u
#define CONTRACT_CMD_TARGET     "/cmd/target"
#define CONTRACT_CMD_TARGET_FRAME   0x311u
#define CONTRACT_CMD_PID        "/cmd/pid"
#define CONTRACT_CMD_PID_FRAME   0x322u
#define CONTRACT_CMD_MODE       "/cmd/mode"
#define CONTRACT_CMD_MODE_FRAME   0x332u

/* ===== 节点名 ↔ 节点号 ===== */
#define CONTRACT_NODE_NAME_COLLECT  "nodeA"
#define CONTRACT_NODE_COLLECT 1
#define CONTRACT_NODE_NAME_CONTROL  "nodeB"
#define CONTRACT_NODE_CONTROL 2
#define CONTRACT_NODE_NAME_BROADCAST "all"
#define CONTRACT_NODE_BROADCAST 0

/* ===== CAN：帧 ID 与 DLC（与 protocol.h 一致） ===== */
#define CONTRACT_CAN_ID_ESTOP                  0x000u
#define CONTRACT_CAN_DLC_ESTOP                 1u
#define CONTRACT_CAN_ID_ALARM_COLLECT          0x011u
#define CONTRACT_CAN_DLC_ALARM_COLLECT         3u
#define CONTRACT_CAN_ID_ALARM_CONTROL          0x012u
#define CONTRACT_CAN_DLC_ALARM_CONTROL         3u
#define CONTRACT_CAN_ID_COLLECT_SENSOR         0x101u
#define CONTRACT_CAN_DLC_COLLECT_SENSOR        5u
#define CONTRACT_CAN_ID_CONTROL_STATUS         0x102u
#define CONTRACT_CAN_DLC_CONTROL_STATUS        4u
#define CONTRACT_CAN_ID_GATEWAY_STATUS         0x103u
#define CONTRACT_CAN_DLC_GATEWAY_STATUS        4u
#define CONTRACT_CAN_ID_CTRL_MOTOR_COLLECT     0x301u
#define CONTRACT_CAN_DLC_CTRL_MOTOR_COLLECT    3u
#define CONTRACT_CAN_ID_CTRL_MOTOR_CONTROL     0x302u
#define CONTRACT_CAN_DLC_CTRL_MOTOR_CONTROL    3u
#define CONTRACT_CAN_ID_CTRL_TARGET_COLLECT    0x311u
#define CONTRACT_CAN_DLC_CTRL_TARGET_COLLECT   3u
#define CONTRACT_CAN_ID_CTRL_TARGET_CONTROL    0x312u
#define CONTRACT_CAN_DLC_CTRL_TARGET_CONTROL   3u
#define CONTRACT_CAN_ID_CTRL_PID_CONTROL       0x322u
#define CONTRACT_CAN_DLC_CTRL_PID_CONTROL      7u
#define CONTRACT_CAN_ID_CTRL_MODE_CONTROL      0x332u
#define CONTRACT_CAN_DLC_CTRL_MODE_CONTROL     2u
#define CONTRACT_CAN_ID_COLLECT_HEARTBEAT      0x701u
#define CONTRACT_CAN_DLC_COLLECT_HEARTBEAT     2u
#define CONTRACT_CAN_ID_CONTROL_HEARTBEAT      0x702u
#define CONTRACT_CAN_DLC_CONTROL_HEARTBEAT     2u
#define CONTRACT_CAN_ID_GATEWAY_HEARTBEAT      0x703u
#define CONTRACT_CAN_DLC_GATEWAY_HEARTBEAT     2u

/* ===== CAN：心跳参数 ===== */
#define CONTRACT_HB_PERIOD_MS   1000u
#define CONTRACT_HB_TIMEOUT_MS  3000u
#define CONTRACT_HB_MAGIC_COLLECT 0xA5u
#define CONTRACT_HB_MAGIC_CONTROL 0x5Au
#define CONTRACT_HB_MAGIC_GATEWAY 0xC3u

/* ===== CAN：总线参数 ===== */
#define CONTRACT_CAN_BITRATE_KBPS   250
#define CONTRACT_CAN_SAMPLE_POINT   0.875f

/* ===== JSON 字段名（上行遥测/状态） ===== */
#define CONTRACT_KEY_IR               "ir"
#define CONTRACT_KEY_LDR              "ldr"
#define CONTRACT_KEY_SEQ              "seq"
#define CONTRACT_KEY_MOTOR            "motor"
#define CONTRACT_KEY_PWM              "pwm"
#define CONTRACT_KEY_MODE             "mode"
#define CONTRACT_KEY_FLAGS            "flags"
#define CONTRACT_KEY_CODE             "code"
#define CONTRACT_KEY_EXTRA            "extra"
#define CONTRACT_KEY_WIFI             "wifi"
#define CONTRACT_KEY_MQTT             "mqtt"
#define CONTRACT_KEY_CACHE            "cache"
#define CONTRACT_KEY_A_RX             "a_rx"
#define CONTRACT_KEY_A_LOST           "a_lost"
#define CONTRACT_KEY_PUB_TOTAL        "pub_total"
#define CONTRACT_KEY_PUB_LOST         "pub_lost"

/* ===== 报警码 ===== */
#define CONTRACT_ALM_SENSOR_FAULT     1u
#define CONTRACT_ALM_COMM_LOST        2u
#define CONTRACT_ALM_THRESH_OVER      3u
#define CONTRACT_ALM_ACTUATOR_FAULT   4u

#endif /* CONTRACT_C_H */
