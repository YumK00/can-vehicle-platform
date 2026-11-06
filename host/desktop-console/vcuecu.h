#ifndef VCUECU_H
#define VCUECU_H

#include <QObject>
#include "baseecu.h"

//整车运行ecu 包括车速
class VcuECU : public BaseECU
{
    Q_OBJECT
public:
    explicit VcuECU(QCanBusDevice* canDev,QObject *parent = nullptr);

private slots:
    void simTick() override;
};

#endif // VCUECU_H
