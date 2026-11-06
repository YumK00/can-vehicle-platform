#ifndef DTCECU_H
#define DTCECU_H

#include <QObject>
#include "baseecu.h"

class DtcECU : public BaseECU
{
    Q_OBJECT
public:
    explicit DtcECU(QCanBusDevice* canDev, QObject *parent = nullptr);
private slots:
    void simTick() override;
};

#endif // DTCECU_H
