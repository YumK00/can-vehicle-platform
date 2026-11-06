#ifndef GATEWAYCLIENT_H
#define GATEWAYCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QTimer>

#include <qmqtt.h>

#include "contract_qt.h"

/* ============================================================
 * 【真实网关模式的数据源】—— ESP32 网关 ⇄ MQTT ⇄ 桌面监控台
 *
 * 本类只做三件事，UI 逻辑一律不放进来：
 *   1. 连 broker（contract::kBrokerHost:kBrokerPort），固定 client id；
 *   2. 订阅 6 条上行主题，把 JSON 解析成结构体后用信号抛出；
 *   3. 按契约拼下行主题 gw0805/<node>/cmd/<kind>，QoS1、不 retain。
 *
 * 所有主题都从 contract_qt.h 的常量拼，禁止在别处硬编码 "gw0805/…"。
 * 断线重连由 QMQTT 的 autoReconnect 负责（但主动断开时要先关掉，
 * 否则 disconnectFromHost() 之后它又会自己连回来）。
 * ============================================================ */

/* 采集节点 A 遥测：nodeA/telemetry  {ir,ldr,seq}（源自 CAN 0x101） */
struct CollectNodeTelemetry {
    bool      valid = false;
    quint16   ir = 0;      // 红外 ADC 原始值 0~4095
    quint16   ldr = 0;     // 光敏 ADC 原始值 0~4095
    quint8    seq = 0;     // 帧序号，255 后回绕
    QDateTime stamp;       // 本地收到时刻
};

/* 执行节点 B 遥测：nodeB/telemetry  {motor,pwm,mode,flags}（源自 CAN 0x102） */
struct ControlNodeTelemetry {
    bool      valid = false;
    quint8    motor = 0;   // 0 停止 / 1 正转 / 2 反转
    quint8    pwm = 0;     // 0~100
    quint8    mode = 0;    // 0 手动 / 1 自动
    quint8    flags = 0;   // bit0 COMM_OK, bit1 THRESH, bit2 ALARM, bit3 ESTOP
    QDateTime stamp;
};

/* 报警：nodeA/alert、nodeB/alert  {code,extra}（源自 CAN 0x011/0x012） */
struct GatewayAlert {
    QString  node;         // nodeA / nodeB
    quint8   code = 0;
    quint16  extra = 0;
    QDateTime stamp;
};

/* 网关自身状态：gateway/status  {wifi,mqtt,cache}（源自 CAN 0x103） */
struct GatewayStatusData {
    bool      valid = false;
    quint8    wifi = 0;
    quint8    mqtt = 0;
    quint16   cache = 0;
    QDateTime stamp;
};

/* 网关累计统计：gateway/stats  {a_rx,a_lost,pub_total,pub_lost} */
struct GatewayStatsData {
    bool      valid = false;
    quint32   aRx = 0;
    quint32   aLost = 0;
    quint32   pubTotal = 0;
    quint32   pubLost = 0;
    QDateTime stamp;
};

class GatewayClient : public QObject
{
    Q_OBJECT
public:
    explicit GatewayClient(QObject *parent = nullptr);
    ~GatewayClient() override;

    /* 按 contract::kNodeCollect / kNodeControl / kNodeAll 取节点名，
     * 避免在 MainWindow 里再写一遍 "nodeA"/"nodeB" 字面量 */
    static QString nodeNameCollect();
    static QString nodeNameControl();
    static QString nodeNameAll();

    /* 按契约拼 gw0805/<node>/cmd/<kind>，供界面显示/自测使用 */
    static QString commandTopic(const QString &node, const QString &kind);

    void connectBroker();
    void disconnectBroker();
    bool isConnected() const;

    /* 下发一条命令：QoS1、retain=false。
     * payload 是纯数据（如 estop 的 "1"、motor 的 "1,50"），
     * 命令类型只出现在主题里 —— 与网关侧 can_bridge_mqtt() 的约定一致。 */
    bool publishCommand(const QString &node, const QString &kind,
                        const QByteArray &payload);

    /* 在线判断：kHbTimeoutMs 内收到过该节点的 telemetry 才算在线 */
    bool isNodeOnline(const QString &node) const;
    QDateTime lastTelemetryTime(const QString &node) const;

signals:
    void connectionStateChanged(bool connected, const QString &detail);
    void collectTelemetryReceived(const CollectNodeTelemetry &data);
    void controlTelemetryReceived(const ControlNodeTelemetry &data);
    void alertReceived(const GatewayAlert &alert);
    void gatewayStatusReceived(const GatewayStatusData &status);
    void gatewayStatsReceived(const GatewayStatsData &stats);
    void nodeOnlineChanged(const QString &node, bool online);

    /* 每条收发报文都抛一次，MainWindow 把它写进 listWidgetmqtt。
     * isRx=true 表示从 broker 收到（RX），false 表示我们发出（TX）。 */
    void traffic(bool isRx, const QString &topic, const QByteArray &payload);
    void commandPublished(const QString &node, const QString &kind,
                          const QByteArray &payload);

private slots:
    void onConnected();
    void onDisconnected();
    void onReceived(const QMQTT::Message &msg);
    void checkNodeTimeouts();

private:
    void subscribeAll();
    void handleTelemetry(const QString &node, const QDateTime &now);
    void setNodeOfflineAll();
    void parseStatus(const QByteArray &payload);
    void parseStats(const QByteArray &payload);
    bool parseObject(const QByteArray &payload, QJsonObject &obj) const;

    QMQTT::Client *m_client = nullptr;
    QTimer         m_watchdog;                 // 500 ms 轮询一次在线超时
    QHash<QString, QDateTime> m_lastSeen;      // 节点 → 最近一次 telemetry 时刻
    QHash<QString, bool>      m_online;        // 节点 → 在线标志
    quint16        m_nextMsgId = 1;
    bool           m_connected = false;
};

#endif // GATEWAYCLIENT_H
