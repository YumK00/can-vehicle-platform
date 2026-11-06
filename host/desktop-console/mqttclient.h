#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H
#include <QObject>
#include <qmqtt.h>
#include"vehicledata.h"
class MqttClient : public QObject
{
    Q_OBJECT
public:
    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient();

    void sendBmsInfo(const s_bmsdata& data);
    void sendVcuInfo(const s_vcudata& data);
    void sendBcmInfo(const s_bcmdata& data);
    void sendDtcInfo(const s_dtcdata& data);

    bool mqttclientIsConnected() const;
    void disconnectBroker();
    void connectBroker();

signals:
    void sigRecvPhoneCmd(const QJsonObject& cmdObj);

private slots:
    void onConnected();
    void onDisconnected();
    void onReceived(const QMQTT::Message &msg);

private:
    QMQTT::Client* m_client;
    const QString m_topic_cmd = "phone/cmd";
};
#endif // MQTTCLIENT_H
