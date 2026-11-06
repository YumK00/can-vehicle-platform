#include "bcmecu.h"
#include <QRandomGenerator>
#include <QDebug>
BcmECU::BcmECU(QCanBusDevice* canDev, QObject *parent)
    : BaseECU(canDev, parent)
{
}

void BcmECU::simTick()
{

    static uint8_t door = 0;//bit0-3 左前 右前 左后 右后 1表示开门
    static uint8_t air_con = 0;//bit0 开关，bit1制冷，bit2制热
    static uint8_t air_temperature = 24;//bit0 开关，bit1制冷，bit2制热

    //构造can帧
    QCanBusFrame frame;
    frame.setFrameId(0x200);
    QByteArray payload(8,0);

    payload[0] = door;
    payload[1] = air_con;
    payload[2]  =air_temperature;
    frame.setPayload(payload);
    if (m_canDev && m_canDev->state() == QCanBusDevice::ConnectedState) {
        if (!m_canDev->writeFrame(frame)) {
            qWarning() << "Write failed:" << m_canDev->errorString();
        } else {
            //qDebug() << "Write success, bcm =" << payload;
        }
    }
}

