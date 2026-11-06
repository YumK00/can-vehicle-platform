#ifndef BMSECU_H
#define BMSECU_H

#include <QObject>
#include "baseecu.h"

//电池ecu 包括容量
class BmsECU : public BaseECU
{
    Q_OBJECT
public:
    explicit BmsECU(QCanBusDevice* canDev, QObject *parent = nullptr);

private slots:
    void simTick() override;
};

#endif // BMSECU_H
