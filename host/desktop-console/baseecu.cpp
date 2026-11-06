#include "baseecu.h"

BaseECU::BaseECU(QCanBusDevice* canDev, QObject *parent)
    : QObject(parent)
    ,m_canDev(canDev)
{
    m_simTimer = new QTimer(this);
    connect(m_simTimer,&QTimer::timeout,this,&BaseECU::simTick);
}

void BaseECU::startSim(int ms)
{
     m_simTimer->start(ms);
}

void BaseECU::stopSim()
{
    m_simTimer->stop();
}
