/*
 * 本文件由 tools/gen_contract.py 从 protocol/contract.json 自动生成，请勿手改。
 * 重新生成： python tools/gen_contract.py
 */

#ifndef CONTRACT_QT_H
#define CONTRACT_QT_H

/* 给 Qt 端用的契约常量：纯 const char*，不依赖任何 Qt 头文件，
 * 需要 QString 时用 QString::fromLatin1(contract::kXxx) 转换。 */

namespace contract {

inline constexpr const char *kVersion = "1.0.0";
inline constexpr const char *kProtocolVersion = "3.0";

inline constexpr const char *kBrokerHost = "broker.emqx.io";
inline constexpr int kBrokerPort = 1883;
inline constexpr bool kBrokerTls = false;
inline constexpr const char *kTopicPrefix = "gw0805";

// 网关发布、上位机订阅
inline constexpr const char *kTelemetryCollect             = "gw0805/nodeA/telemetry";
inline constexpr const char *kTelemetryControl             = "gw0805/nodeB/telemetry";
inline constexpr const char *kAlertCollect                 = "gw0805/nodeA/alert";
inline constexpr const char *kAlertControl                 = "gw0805/nodeB/alert";
inline constexpr const char *kGatewayStatus                = "gw0805/gateway/status";
inline constexpr const char *kGatewayStats                 = "gw0805/gateway/stats";

// 网关订阅、上位机发布
inline constexpr const char *kCommandSubscribe             = "gw0805/+/cmd/#";

// 命令类型（拼主题用：kTopicPrefix + "/" + node + "/cmd/" + kind）
inline constexpr const char *kCmdEstop      = "estop";
inline constexpr const char *kCmdMotor      = "motor";
inline constexpr const char *kCmdTarget     = "target";
inline constexpr const char *kCmdPid        = "pid";
inline constexpr const char *kCmdMode       = "mode";

// 节点名
inline constexpr const char *kNodeCollect = "nodeA";
inline constexpr const char *kNodeControl = "nodeB";
inline constexpr const char *kNodeAll     = "all";

// CAN 帧 ID / DLC
inline constexpr unsigned kCanIdESTOP                  = 0x000u;
inline constexpr unsigned kCanDlcESTOP                 = 1u;
inline constexpr unsigned kCanIdALARMCOLLECT           = 0x011u;
inline constexpr unsigned kCanDlcALARMCOLLECT          = 3u;
inline constexpr unsigned kCanIdALARMCONTROL           = 0x012u;
inline constexpr unsigned kCanDlcALARMCONTROL          = 3u;
inline constexpr unsigned kCanIdCOLLECTSENSOR          = 0x101u;
inline constexpr unsigned kCanDlcCOLLECTSENSOR         = 5u;
inline constexpr unsigned kCanIdCONTROLSTATUS          = 0x102u;
inline constexpr unsigned kCanDlcCONTROLSTATUS         = 4u;
inline constexpr unsigned kCanIdGATEWAYSTATUS          = 0x103u;
inline constexpr unsigned kCanDlcGATEWAYSTATUS         = 4u;
inline constexpr unsigned kCanIdCTRLMOTORCOLLECT       = 0x301u;
inline constexpr unsigned kCanDlcCTRLMOTORCOLLECT      = 3u;
inline constexpr unsigned kCanIdCTRLMOTORCONTROL       = 0x302u;
inline constexpr unsigned kCanDlcCTRLMOTORCONTROL      = 3u;
inline constexpr unsigned kCanIdCTRLTARGETCOLLECT      = 0x311u;
inline constexpr unsigned kCanDlcCTRLTARGETCOLLECT     = 3u;
inline constexpr unsigned kCanIdCTRLTARGETCONTROL      = 0x312u;
inline constexpr unsigned kCanDlcCTRLTARGETCONTROL     = 3u;
inline constexpr unsigned kCanIdCTRLPIDCONTROL         = 0x322u;
inline constexpr unsigned kCanDlcCTRLPIDCONTROL        = 7u;
inline constexpr unsigned kCanIdCTRLMODECONTROL        = 0x332u;
inline constexpr unsigned kCanDlcCTRLMODECONTROL       = 2u;
inline constexpr unsigned kCanIdCOLLECTHEARTBEAT       = 0x701u;
inline constexpr unsigned kCanDlcCOLLECTHEARTBEAT      = 2u;
inline constexpr unsigned kCanIdCONTROLHEARTBEAT       = 0x702u;
inline constexpr unsigned kCanDlcCONTROLHEARTBEAT      = 2u;
inline constexpr unsigned kCanIdGATEWAYHEARTBEAT       = 0x703u;
inline constexpr unsigned kCanDlcGATEWAYHEARTBEAT      = 2u;

// 心跳
inline constexpr unsigned kHbPeriodMs  = 1000u;
inline constexpr unsigned kHbTimeoutMs = 3000u;
inline constexpr unsigned kHbMagicCOLLECT    = 0xA5u;
inline constexpr unsigned kHbMagicCONTROL    = 0x5Au;
inline constexpr unsigned kHbMagicGATEWAY    = 0xC3u;

// 报警码与名称（contract.json → alarm_codes），显示层直接用，别再手抄一份
inline constexpr unsigned kAlarmSensorFault        = 1u;
inline constexpr const char *kAlarmSensorFaultName = "SENSOR_FAULT";
inline constexpr unsigned kAlarmCommLost           = 2u;
inline constexpr const char *kAlarmCommLostName = "COMM_LOST";
inline constexpr unsigned kAlarmThreshOver         = 3u;
inline constexpr const char *kAlarmThreshOverName = "THRESH_OVER";
inline constexpr unsigned kAlarmActuatorFault      = 4u;
inline constexpr const char *kAlarmActuatorFaultName = "ACTUATOR_FAULT";

// 品牌/工位信息
inline constexpr const char *kPlatform = "can-vehicle-platform";
} // namespace contract

#endif /* CONTRACT_QT_H */
