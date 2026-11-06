#include "mqttclient.h"
#include "QDebug"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
using namespace QMQTT;

MqttClient::MqttClient(QObject *parent)
    : QObject{parent}
{
    m_client = new Client();
    m_client->setHostName("8.138.233.135");
    m_client->setPort(1883);
    m_client->setPassword("");
    m_client->setUsername("");
    m_client->setClientId("vehicle");

    connect(m_client, &Client::connected, this, &MqttClient::onConnected);
    connect(m_client, &Client::disconnected, this, &MqttClient::onDisconnected);
    connect(m_client, &Client::received, this, &MqttClient::onReceived);
}

void MqttClient::connectBroker()
{
    if(m_client)
    {
        m_client->connectToHost();
    }
}

void MqttClient::disconnectBroker()
{
    if(m_client && m_client->isConnectedToHost())
    {
        m_client->disconnectFromHost();
    }
}

bool MqttClient::mqttclientIsConnected() const
{
    if(!m_client) return false;
    return m_client->isConnectedToHost();
}

void MqttClient::sendBmsInfo(const s_bmsdata &data)
{
    if(!mqttclientIsConnected()) return;
    QJsonObject obj = data.toJson();
    obj["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    QJsonDocument doc(obj);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);
    qDebug()<<payload;
    m_client->publish(QMQTT::Message(0, "car/ecu/bms", payload, 0));
}

void MqttClient::sendVcuInfo(const s_vcudata &data)
{
    if(!mqttclientIsConnected()) return;
    QJsonObject obj = data.toJson();
    obj["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    QJsonDocument doc(obj);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);
    qDebug()<<payload;
    m_client->publish(QMQTT::Message(0, "car/ecu/vcu", payload, 0));
}

void MqttClient::sendBcmInfo(const s_bcmdata &data)
{
    if(!mqttclientIsConnected()) return;
    QJsonObject obj = data.toJson();
    obj["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    QJsonDocument doc(obj);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);
    qDebug()<<payload;
    m_client->publish(QMQTT::Message(0, "car/ecu/bcm", payload, 0));
}

void MqttClient::sendDtcInfo(const s_dtcdata &data)
{
    if(!mqttclientIsConnected()) return;
    QJsonObject obj = data.toJson();
    obj["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    QJsonDocument doc(obj);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);
    qDebug()<<payload;
    m_client->publish(QMQTT::Message(0, "car/ecu/dtc", payload, 0));
}

void MqttClient::onReceived(const QMQTT::Message& msg){
    QString topic = msg.topic();
    QByteArray payload = msg.payload();
    qDebug() << "mqtt recv [" << topic << "]:" << payload;
    if(topic == m_topic_cmd)
    {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if(!doc.isObject()) return;
        QJsonObject jo = doc.object();
        emit sigRecvPhoneCmd(jo);
    }
}

MqttClient::~MqttClient()
{
    if(m_client != nullptr)
    {
        if(m_client->isConnectedToHost())
        {
            m_client->disconnectFromHost();
        }
        delete m_client;
        m_client = nullptr;
    }
}

void MqttClient::onConnected(){
    qDebug()<<"vehicle connected success";
    m_client->subscribe(m_topic_cmd,1);
}

void MqttClient::onDisconnected(){
    qDebug()<<"vehicle disconnected .";
}
