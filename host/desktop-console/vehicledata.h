#ifndef VEHICLEDATA_H
#define VEHICLEDATA_H

#include <QMetaType>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

// 电池数据
struct s_bmsdata {
    int16_t batterySoc = 0; // 0-100

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["batterySoc"] = static_cast<int>(batterySoc);
        return obj;
    }

    static s_bmsdata fromJson(const QJsonObject &obj) {
        s_bmsdata data;
        data.batterySoc = static_cast<int16_t>(obj.value("batterySoc").toInt(0));
        return data;
    }
};

// 整车运行数据
struct s_vcudata {
    int16_t vcuSpeed = 0; // 0-180
    int16_t vcuTemp = 0;//-40°C ~ 150°C
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["vcuSpeed"] = static_cast<int>(vcuSpeed);
        obj["vcuTemp"] = static_cast<int>(vcuTemp);
        return obj;
    }

    static s_vcudata fromJson(const QJsonObject &obj) {
        s_vcudata data;
        data.vcuSpeed = static_cast<int16_t>(obj.value("vcuSpeed").toInt(0));
        return data;
    }
};

// 车门空调数据
struct s_bcmdata {
    uint8_t door_status = 0;      // bit0-3: 左前,右前,左后,右后 (0=开门,1=关门)
    uint8_t air_con_status = 0;   // bit0:开关, bit1:制冷, bit2:制热
    uint8_t air_temperature = 24; // 空调温度

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["door_status"] = static_cast<int>(door_status);
        obj["air_con_status"] = static_cast<int>(air_con_status);
        obj["air_temperature"] = static_cast<int>(air_temperature);
        return obj;
    }

    static s_bcmdata fromJson(const QJsonObject &obj) {
        s_bcmdata data;
        data.door_status = static_cast<uint8_t>(obj.value("door_status").toInt(0));
        data.air_con_status = static_cast<uint8_t>(obj.value("air_con_status").toInt(0));
        data.air_temperature = static_cast<uint8_t>(obj.value("air_temperature").toInt(24));
        return data;
    }
};

// 故障数据
struct s_dtcdata {
    uint8_t guzhangma = 0;   // 故障码，0=无故障
    uint8_t youxianji = 0;   // 优先级：0无，1警告(橙色)，2严重(红色)

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["guzhangma"] = static_cast<int>(guzhangma);
        obj["youxianji"] = static_cast<int>(youxianji);
        return obj;
    }

    static s_dtcdata fromJson(const QJsonObject &obj) {
        s_dtcdata data;
        data.guzhangma = static_cast<uint8_t>(obj.value("guzhangma").toInt(0));
        data.youxianji = static_cast<uint8_t>(obj.value("youxianji").toInt(0));
        return data;
    }
};
#endif // VEHICLEDATA_H
