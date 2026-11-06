#ifndef BASEECU_H
#define BASEECU_H

#include <QObject>
#include <QTimer>
#include <QCanBus>
#include <QCanBusDevice>
#include <QCanBusFrame>

class BaseECU : public QObject
{
    Q_OBJECT
public:
    explicit BaseECU(QCanBusDevice* canDev,QObject *parent = nullptr);
    void startSim(int ms);
    void stopSim();

private slots:
    virtual void simTick() = 0;
protected:
    QCanBusDevice* m_canDev;
    QTimer* m_simTimer;
};

#endif // BASEECU_H
