#include "bmsecu.h"
#include <QRandomGenerator>
#include <QDebug>
BmsECU::BmsECU(QCanBusDevice* canDev, QObject *parent)
    : BaseECU(canDev, parent)
{
}

void BmsECU::simTick()
{
    static uint16_t soc = 80;
    soc += QRandomGenerator::global()->bounded(-1, 1);
    if(soc < 0) soc = 0;
    else if(soc >100) soc =100;

    //构造can帧
    QCanBusFrame frame;
    frame.setFrameId(0x120);
    QByteArray payload(8,0);

    payload[0] = (soc >> 8) & 0xFF;
    payload[1] = soc & 0xFF;

    frame.setPayload(payload);

    if (m_canDev && m_canDev->state() == QCanBusDevice::ConnectedState) {
        if (!m_canDev->writeFrame(frame)) {
            qWarning() << "Write failed:" << m_canDev->errorString();
        } else {
            //qDebug() << "Write success, barrerysoc =" << payload;
        }
    }
}
