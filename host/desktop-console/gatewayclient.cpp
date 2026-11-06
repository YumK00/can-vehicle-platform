#include "gatewayclient.h"

#include <QDebug>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

using namespace QMQTT;

namespace {

/* 与 contract.json → mqtt.client_ids.desktop 保持一致。
 * 生成头里没有导出 client id（契约里只是字符串说明），
 * 所以这里写死一个固定串，换个端要改这里一处。 */
const char *const kDesktopClientId = "qt_desktop_console";

/* 订阅时统一用 QoS1：
 *   - 遥测在网关侧是 QoS0 发布，broker 取 min(0,1)=0，不会因此变重；
 *   - 报警在网关侧是 QoS1 发布，订阅用 QoS0 会被降级，用 1 才不丢。
 * 一遍订完，不做逐主题分级。 */
const quint8 kSubscribeQos = 1;

/* 下行命令固定 QoS1、绝不 retain：
 * 命令是"动作"不是"状态"，retain 会让新上线的订阅者重放历史命令。 */
const quint8 kCommandQos = 1;

} // namespace

GatewayClient::GatewayClient(QObject *parent)
    : QObject(parent)
{
    m_client = new Client(QHostAddress::LocalHost,
                          static_cast<quint16>(contract::kBrokerPort), this);
    m_client->setHostName(QString::fromLatin1(contract::kBrokerHost));
    m_client->setPort(static_cast<quint16>(contract::kBrokerPort));
    m_client->setClientId(QString::fromLatin1(kDesktopClientId));
    m_client->setKeepAlive(30);
    m_client->setCleanSession(true);
    m_client->setAutoReconnect(true);
    m_client->setAutoReconnectInterval(3000);

    connect(m_client, &Client::connected, this, &GatewayClient::onConnected);
    connect(m_client, &Client::disconnected, this, &GatewayClient::onDisconnected);
    connect(m_client, &Client::received, this, &GatewayClient::onReceived);

    /* QMQTT 的 error 是 socket 级错误（含连接被拒/网络中断）。
     * 没连上时用它把"连接中…"更新成"重连中…"，避免界面一直悬着。 */
    connect(m_client, &Client::error, this,
            [this](ClientError error) {
                const QString detail =
                    QStringLiteral("链路异常(code=%1)，自动重连中…").arg(static_cast<int>(error));
                qWarning().noquote() << "[gateway] " << detail;
                if (!m_connected) {
                    emit connectionStateChanged(false, detail);
                }
            });

    /* 在线判断不依赖 MQTT keepalive：按契约，3 s 内没有该节点的 telemetry
     * 就算掉线。500 ms 轮询一次，误判窗口最多 500 ms。 */
    m_watchdog.setInterval(500);
    connect(&m_watchdog, &QTimer::timeout, this, &GatewayClient::checkNodeTimeouts);
    m_watchdog.start();
}

GatewayClient::~GatewayClient()
{
    if (m_client != nullptr) {
        /* 先关自动重连再断，否则 QMQTT 会在 disconnected 之后又连回来 */
        m_client->setAutoReconnect(false);
        if (m_client->isConnectedToHost()) {
            m_client->disconnectFromHost();
        }
        delete m_client;
        m_client = nullptr;
    }
}

QString GatewayClient::nodeNameCollect()
{
    return QString::fromLatin1(contract::kNodeCollect);
}

QString GatewayClient::nodeNameControl()
{
    return QString::fromLatin1(contract::kNodeControl);
}

QString GatewayClient::nodeNameAll()
{
    return QString::fromLatin1(contract::kNodeAll);
}

QString GatewayClient::commandTopic(const QString &node, const QString &kind)
{
    /* gw0805/<node>/cmd/<kind> —— 前缀和命令类型全部来自契约常量 */
    return QString::fromLatin1(contract::kTopicPrefix)
           + QLatin1Char('/') + node
           + QLatin1String("/cmd/") + kind;
}

void GatewayClient::connectBroker()
{
    if (m_client == nullptr) {
        return;
    }
    m_client->setAutoReconnect(true);
    if (m_client->isConnectedToHost()) {
        return;
    }
    qInfo().noquote() << QString("[gateway] 连接 %1:%2 clientId=%3")
                             .arg(QString::fromLatin1(contract::kBrokerHost))
                             .arg(contract::kBrokerPort)
                             .arg(QString::fromLatin1(kDesktopClientId));
    emit connectionStateChanged(false, QStringLiteral("连接中…"));
    m_client->connectToHost();
}

void GatewayClient::disconnectBroker()
{
    if (m_client == nullptr) {
        return;
    }
    const bool wasConnected = m_connected;
    m_client->setAutoReconnect(false);
    m_connected = false;
    setNodeOfflineAll();
    if (m_client->isConnectedToHost()) {
        m_client->disconnectFromHost();
    }
    if (wasConnected) {
        emit connectionStateChanged(false, QStringLiteral("已断开"));
    }
}

bool GatewayClient::isConnected() const
{
    return m_client != nullptr && m_connected;
}

void GatewayClient::subscribeAll()
{
    /* 6 条上行主题，全部来自 contract_qt.h，不写字符串字面量 */
    const char *topics[] = {
        contract::kTelemetryCollect,
        contract::kTelemetryControl,
        contract::kAlertCollect,
        contract::kAlertControl,
        contract::kGatewayStatus,
        contract::kGatewayStats
    };
    for (const char *t : topics) {
        const QString topic = QString::fromLatin1(t);
        m_client->subscribe(topic, kSubscribeQos);
        qInfo().noquote() << "[gateway] subscribe" << topic;
    }
}

bool GatewayClient::publishCommand(const QString &node, const QString &kind,
                                   const QByteArray &payload)
{
    if (!isConnected()) {
        qWarning().noquote() << "[gateway] 未连接 broker，命令未下发:" << kind;
        return false;
    }
    /* 节点名限定在契约的三个取值内，防止 UI 传错拼出别的主题 */
    if (node != nodeNameCollect() && node != nodeNameControl() && node != nodeNameAll()) {
        qWarning().noquote() << "[gateway] 未知目标节点:" << node;
        return false;
    }

    const QString topic = commandTopic(node, kind);
    m_client->publish(Message(m_nextMsgId++, topic, payload, kCommandQos, false));

    qInfo().noquote() << "[gateway] TX" << topic << QString::fromUtf8(payload);
    emit traffic(false, topic, payload);
    emit commandPublished(node, kind, payload);
    return true;
}

bool GatewayClient::isNodeOnline(const QString &node) const
{
    return m_online.value(node, false);
}

QDateTime GatewayClient::lastTelemetryTime(const QString &node) const
{
    return m_lastSeen.value(node);
}

void GatewayClient::handleTelemetry(const QString &node, const QDateTime &now)
{
    m_lastSeen[node] = now;
    if (!m_online.value(node, false)) {
        m_online[node] = true;
        emit nodeOnlineChanged(node, true);
    }
}

void GatewayClient::setNodeOfflineAll()
{
    m_lastSeen.clear();
    const QString nodes[] = { nodeNameCollect(), nodeNameControl() };
    for (const QString &node : nodes) {
        if (m_online.value(node, false)) {
            m_online[node] = false;
            emit nodeOnlineChanged(node, false);
        }
    }
}

void GatewayClient::checkNodeTimeouts()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 timeout = static_cast<qint64>(contract::kHbTimeoutMs);
    const QString nodes[] = { nodeNameCollect(), nodeNameControl() };

    for (const QString &node : nodes) {
        if (!m_online.value(node, false)) {
            continue;
        }
        const QDateTime last = m_lastSeen.value(node);
        /* last 无效 = 从没收到过 telemetry，不该被判成"在线后又掉线"；
         * 这里直接按超时处理，保持语义简单。 */
        const bool expired = !last.isValid() || (now - last.toMSecsSinceEpoch()) > timeout;
        if (expired) {
            m_online[node] = false;
            qWarning().noquote() << "[gateway] 节点掉线(超时):" << node;
            emit nodeOnlineChanged(node, false);
        }
    }
}

void GatewayClient::onConnected()
{
    m_connected = true;
    qInfo().noquote() << "[gateway] broker 已连接";
    subscribeAll();
    emit connectionStateChanged(true, QStringLiteral("已连接"));
}

void GatewayClient::onDisconnected()
{
    if (!m_connected) {
        /* 主动断开时由 disconnectBroker() 负责通知，这里不重复发 */
        m_online.clear();
        return;
    }
    m_connected = false;
    setNodeOfflineAll();
    qWarning().noquote() << "[gateway] broker 连接断开";
    emit connectionStateChanged(false, QStringLiteral("连接丢失"));
}

bool GatewayClient::parseObject(const QByteArray &payload, QJsonObject &obj) const
{
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning().noquote() << "[gateway] JSON 解析失败:" << err.errorString()
                             << QString::fromUtf8(payload);
        return false;
    }
    obj = doc.object();
    return true;
}

void GatewayClient::parseStatus(const QByteArray &payload)
{
    QJsonObject obj;
    if (!parseObject(payload, obj)) {
        return;
    }
    GatewayStatusData st;
    st.valid = true;
    st.wifi = static_cast<quint8>(obj.value(QLatin1String("wifi")).toInt());
    st.mqtt = static_cast<quint8>(obj.value(QLatin1String("mqtt")).toInt());
    st.cache = static_cast<quint16>(obj.value(QLatin1String("cache")).toInt());
    st.stamp = QDateTime::currentDateTime();
    emit gatewayStatusReceived(st);
}

void GatewayClient::parseStats(const QByteArray &payload)
{
    QJsonObject obj;
    if (!parseObject(payload, obj)) {
        return;
    }
    GatewayStatsData st;
    st.valid = true;
    /* 统计值是 uint32，用 toDouble 取值避免 toInt 在超过 2^31 时截断
     * （Qt5 的 QJsonValue 没有 toInteger） */
    st.aRx = static_cast<quint32>(obj.value(QLatin1String("a_rx")).toDouble());
    st.aLost = static_cast<quint32>(obj.value(QLatin1String("a_lost")).toDouble());
    st.pubTotal = static_cast<quint32>(obj.value(QLatin1String("pub_total")).toDouble());
    st.pubLost = static_cast<quint32>(obj.value(QLatin1String("pub_lost")).toDouble());
    st.stamp = QDateTime::currentDateTime();
    emit gatewayStatsReceived(st);
}

void GatewayClient::onReceived(const QMQTT::Message &msg)
{
    const QString topic = msg.topic();
    const QByteArray payload = msg.payload();

    /* 先把原始报文抛给 UI 记日志，再解析：
     * 解析失败时日志里仍然能看到到底收到了什么 */
    emit traffic(true, topic, payload);

    const QDateTime now = QDateTime::currentDateTime();
    const QString nodeA = nodeNameCollect();
    const QString nodeB = nodeNameControl();

    if (topic == QString::fromLatin1(contract::kTelemetryCollect)) {
        QJsonObject obj;
        if (!parseObject(payload, obj)) {
            return;
        }
        CollectNodeTelemetry data;
        data.valid = true;
        data.ir = static_cast<quint16>(obj.value(QLatin1String("ir")).toInt());
        data.ldr = static_cast<quint16>(obj.value(QLatin1String("ldr")).toInt());
        data.seq = static_cast<quint8>(obj.value(QLatin1String("seq")).toInt());
        data.stamp = now;
        handleTelemetry(nodeA, now);
        emit collectTelemetryReceived(data);
    } else if (topic == QString::fromLatin1(contract::kTelemetryControl)) {
        QJsonObject obj;
        if (!parseObject(payload, obj)) {
            return;
        }
        ControlNodeTelemetry data;
        data.valid = true;
        data.motor = static_cast<quint8>(obj.value(QLatin1String("motor")).toInt());
        data.pwm = static_cast<quint8>(obj.value(QLatin1String("pwm")).toInt());
        data.mode = static_cast<quint8>(obj.value(QLatin1String("mode")).toInt());
        data.flags = static_cast<quint8>(obj.value(QLatin1String("flags")).toInt());
        data.stamp = now;
        handleTelemetry(nodeB, now);
        emit controlTelemetryReceived(data);
    } else if (topic == QString::fromLatin1(contract::kAlertCollect)
               || topic == QString::fromLatin1(contract::kAlertControl)) {
        QJsonObject obj;
        if (!parseObject(payload, obj)) {
            return;
        }
        GatewayAlert alert;
        alert.node = (topic == QString::fromLatin1(contract::kAlertCollect)) ? nodeA : nodeB;
        alert.code = static_cast<quint8>(obj.value(QLatin1String("code")).toInt());
        alert.extra = static_cast<quint16>(obj.value(QLatin1String("extra")).toInt());
        alert.stamp = now;
        emit alertReceived(alert);
    } else if (topic == QString::fromLatin1(contract::kGatewayStatus)) {
        parseStatus(payload);
    } else if (topic == QString::fromLatin1(contract::kGatewayStats)) {
        parseStats(payload);
    } else {
        /* 网关理论上只发这 6 条，收到别的说明主题树上有第三方在发，
         * 记一笔但不解析（报文已经进日志了） */
        qDebug().noquote() << "[gateway] 忽略未订阅主题的报文:" << topic;
    }
}
