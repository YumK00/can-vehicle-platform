#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QStringList>
#include"qmqtt.h"
#include"cmdData.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QComboBox;
class QLabel;
class QTimer;
class GatewayPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 两种互斥的运行模式
    enum Mode {
        ModeSim = 0,      // 仿真模式：8.138.233.135 + car/ecu/*、phone/cmd（原有 ECU 仿真链路）
        ModeGateway = 1   // 网关模式：contract::kBrokerHost + gw0805/*（真实 ESP32-S3 网关链路）
    };

    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnected();
    void onDisconnected();
    void onReceived(const QMQTT::Message& msg);

    void onModeChanged(int index);
    void onRefreshOnlineState();          // 1 s 心跳：按最近报文时间刷新节点在线状态

    void onEstopRequested(bool engage);   // 网关模式：急停 / 解除
    void onMotorRequested(int cmd, int arg);
    void onGatewayModeRequested(int mode); // 网关模式：0 手动，1 自动

    void on_ckbox_alldoor_stateChanged(int arg1);
    void onSingleDoorStateChanged(int arg1);

    void on_ckbox_kongtiaokaiguan_stateChanged(int arg1);

    void on_ckbox_lenrekaiguan_stateChanged(int arg1);

    void on_spinbox_wendu_valueChanged(int arg1);

private:
    Ui::MainWindow *ui;
    QMQTT::Client* m_client = nullptr;
    cmddata cmd;

    Mode m_mode = ModeSim;
    GatewayPanel* m_gatewayPanel = nullptr;
    QComboBox* m_cmbMode = nullptr;
    QLabel* m_labelConn = nullptr;
    QTimer* m_freshTimer = nullptr;
    QList<QWidget*> m_simWidgets;   // 仿真界面的原始控件，网关模式时整体隐藏

    qint64 m_lastNodeA = 0;         // 最近一次收到 nodeA 遥测的时间戳（ms）
    qint64 m_lastNodeB = 0;         // 最近一次收到 nodeB 遥测的时间戳（ms）

    const QString m_topic_cmd = "phone/cmd";
    const QString m_topic_car_bms = "car/ecu/bms";
    const QString m_topic_car_vcu = "car/ecu/vcu";
    const QString m_topic_car_bcm = "car/ecu/bcm";
    const QString m_topic_car_dtc = "car/ecu/dtc";

    void buildTopBar();             // 顶部模式切换栏（代码创建，不改 .ui）
    void enterMode(Mode mode);      // 互斥切换：断开旧连接、清理订阅、切换界面
    void startClient();             // 按当前模式新建并连接 MQTT client
    void teardownClient();          // 退订 + 断开 + 销毁旧 client
    void updateConnLabel();
    QString brokerText() const;
    QStringList subscriptionsForMode(Mode mode) const;
    void subscribeTopic(const QString &topic, quint8 qos);

    void handleGatewayMessage(const QString &topic, const QByteArray &payload);
    void publishGatewayCommand(const QString &node, const char *kind, const QByteArray &payload);
    void logGateway(const QString &line);   // qDebug + 面板日志

    void sendCmd(const cmddata& cmd);

    bool m_batchUpdate = false;   // 批量更新标志
    void updateAllDoorCheckBox(); // 根据四个门状态更新全门复选框

    void updateAirConUI();
};
#endif // MAINWINDOW_H
