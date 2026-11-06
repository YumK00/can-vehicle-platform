#include "vcuecu.h"
#include <QRandomGenerator>
#include <QDebug>
VcuECU::VcuECU(QCanBusDevice* canDev,QObject *parent)
    : BaseECU(canDev, parent)
{}
void VcuECU::simTick()
{
    static uint16_t speed = 80;
    int16_t vcutemp = 50;
    speed += QRandomGenerator::global()->bounded(-5, 6);
    if(speed <= 0)  speed = 0;
    else if(speed>= 180) speed = 180;
    vcutemp+=(speed/2);
    QCanBusFrame frame;
    frame.setFrameId(0x100);
    QByteArray payload(8,0);

    payload[0] = (speed>>8) & 0xFF;
    payload[1] = speed & 0xFF;
    payload[2] = (vcutemp>>8)&0xFF;
    payload[3] = (vcutemp) & 0xFF;

    frame.setPayload(payload);

    if (m_canDev && m_canDev->state() == QCanBusDevice::ConnectedState) {
        if (!m_canDev->writeFrame(frame)) {
            qWarning() << "Write failed:" << m_canDev->errorString();
        } else {
            //qDebug() << "Write success, Vcuspeed=" << payload;
        }
    }
}
