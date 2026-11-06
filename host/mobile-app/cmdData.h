#ifndef CMDDATA_H
#define CMDDATA_H
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
struct cmddata{

    uint8_t door_status = 0;//bit0-3 左前 右前 左后 右后 ,1关门，0表示开门
    uint8_t air_con_status = 0;//bit0 开关(1开)，bit1制冷，bit2制热
    uint8_t air_temperature = 24;

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["door_status"] = static_cast<int>(door_status);
        obj["air_con_status"] = static_cast<int>(air_con_status);
        obj["air_temperature"] = static_cast<int>(air_temperature);
        obj["timestamp"] = QDateTime::currentMSecsSinceEpoch();
        return obj;
    }
};
#endif // CMDDATA_H
