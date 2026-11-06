#ifndef BCMECU_H
#define BCMECU_H

#include <QObject>
#include "baseecu.h"

class BcmECU : public BaseECU
{
    Q_OBJECT
public:
    explicit BcmECU(QCanBusDevice* canDev, QObject *parent = nullptr);
private slots:
    void simTick() override;
};

#endif // BCMECU_H
