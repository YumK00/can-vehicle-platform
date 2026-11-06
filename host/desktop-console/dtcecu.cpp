#include "dtcecu.h"
#include <QRandomGenerator>
#include <QDebug>
DtcECU::DtcECU(QCanBusDevice* canDev,QObject *parent)
    : BaseECU(canDev, parent)
{}

void DtcECU::simTick()
{
    static uint8_t guzhangma = 0;
    static uint8_t youxianji = 0;

    QCanBusFrame frame;
    frame.setFrameId(0x110);
    QByteArray payload(8,0);

    payload[0] = guzhangma & 0xFF;
    payload[1] = youxianji & 0xFF;

    frame.setPayload(payload);

    if (m_canDev && m_canDev->state() == QCanBusDevice::ConnectedState) {
        if (!m_canDev->writeFrame(frame)) {
            qWarning() << "Write failed:" << m_canDev->errorString();
        } else {
            //qDebug() << "Write success, dtc=" << payload;
        }
    }
}
