#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include"mqttclient.h"
#include"gatewayclient.h"
#include"bmsecu.h"
#include"vcuecu.h"
#include"bcmecu.h"
#include"dtcecu.h"
#include"vehicledata.h"
#include<QFile>
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QLabel;
class QGroupBox;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_timeout();//时间定时器
    void on_CanFramesReceived();   // CAN 接收

    void on_btn_qidongcan_clicked();

    void on_btn_tingzhican_clicked();

    void on_btn_mqttlianjie_clicked();

    void onPhoneCommandArrived(const QJsonObject& cmdObj); //处理手机下发指令

    void on_tim2out();
    void on_pushButton_clicked();

    // ---------- 运行模式：仿真 / 真实网关 ----------
    void onRunModeChanged(bool realModeChecked);

    // ---------- 真实网关数据（全部来自 GatewayClient 的信号） ----------
    void onGatewayConnectionChanged(bool connected, const QString &detail);
    void onGatewayCollectTelemetry(const CollectNodeTelemetry &data);
    void onGatewayControlTelemetry(const ControlNodeTelemetry &data);
    void onGatewayAlert(const GatewayAlert &alert);
    void onGatewayStatus(const GatewayStatusData &status);
    void onGatewayStats(const GatewayStatsData &stats);
    void onGatewayNodeOnline(const QString &node, bool online);
    void onGatewayTraffic(bool isRx, const QString &topic, const QByteArray &payload);
    void onGatewayTick();

    // ---------- 网关命令面板 ----------
    void onCmdEmergencyStop();
    void onCmdEmergencyClear();
    void onCmdMotorStop();
    void onCmdMotorForward();
    void onCmdMotorReverse();
    void onCmdMotorSetSpeed();
    void onCmdTargetSend();
    void onCmdModeManual();
    void onCmdModeAuto();

private:
    Ui::MainWindow *ui;
    QCanBusDevice* m_candevice = nullptr;

    s_bcmdata m_bcmcurinfo;
    s_bmsdata m_bmscurinfo;
    s_dtcdata m_dtccurinfo;
    s_vcudata m_vcucurinfo;

    BmsECU* m_bms = nullptr;
    VcuECU* m_vcu = nullptr;
    BcmECU* m_bcm = nullptr;
    DtcECU* m_dtc = nullptr;

    MqttClient* m_mqttClient = nullptr;

    void parseCanFrame(const QCanBusFrame &frame);

    void updateBmsUi(const s_bmsdata &data);
    void updateVcuUi(const s_vcudata &data);
    void updateBcmUi(const s_bcmdata &data);
    void updateDtcUi(const s_dtcdata &data);
    void closeConnect();
    QString getCurrentTimeString();
    void writeLog(bool isRecv, bool isCan,const QString &logstr);

    uint32_t m_currentWindowBit; // 当前1秒窗口内累计bit，收到帧累加
    uint32_t m_showBps;          // UI用来显示的bps值，每秒更新一次

    QTimer* tim2 ;
    QFile* m_logFile;

    // ================= 真实网关模式 =================
    void setupGatewayPanel();       // 代码方式创建"网关状态看板"和"命令面板"
    void connectGatewaySignals();
    void enterRealMode();
    void enterSimMode();
    void stopSimulation();
    void updateGatewayConnectionUi(bool connected, const QString &detail);
    void refreshNodeLabels();
    void setGatewayCommandsEnabled(bool enabled);
    void sendGatewayCommand(const QString &node, const QString &kind,
                            const QByteArray &payload, const QString &desc);
    void updateOnlineRate();
    QString selectedCommandNode() const;

    GatewayClient* m_gateway = nullptr;
    bool   m_simSuspendedForRealMode = false; // 切到真实模式前仿真是否在跑
    qint64 m_pendingCommandMs = -1;           // 最近一次下发命令的本地时刻（用于算时延）
    QTimer* m_gatewayTick = nullptr;          // 1 s 采样：在线率 / 超时兜底

    // 看板控件（全部代码创建，不改 .ui）
    QWidget*   m_gatewayPanel  = nullptr;
    QWidget*   m_commandPanel  = nullptr;
    QTabWidget* m_gatewayTabs  = nullptr;
    QLabel* m_gwConnLabel      = nullptr;
    QLabel* m_gwNodeALabel     = nullptr;
    QLabel* m_gwNodeBLabel     = nullptr;
    QLabel* m_gwIrLabel        = nullptr;
    QLabel* m_gwLdrLabel       = nullptr;
    QLabel* m_gwSeqLabel       = nullptr;
    QLabel* m_gwMotorLabel     = nullptr;
    QLabel* m_gwModeLabel      = nullptr;
    QLabel* m_gwFlagsLabel     = nullptr;
    QLabel* m_gwWifiLabel      = nullptr;
    QLabel* m_gwMqttLabel      = nullptr;
    QLabel* m_gwCacheLabel     = nullptr;
    QLabel* m_gwAlertLabel     = nullptr;
    QLabel* m_gwLostRateLabel  = nullptr;
    QLabel* m_gwPubStatsLabel  = nullptr;
    QLabel* m_gwLatencyLabel   = nullptr;
    QLabel* m_gwOnlineRateLabel= nullptr;

    QComboBox*  m_cmdNodeCombo  = nullptr;
    QSpinBox*   m_cmdSpeedSpin  = nullptr;
    QLineEdit*  m_cmdTargetEdit = nullptr;
    QVector<QPushButton*> m_cmdButtons;

    // 最新遥测缓存：定时器里只刷新"时效性"相关显示，不重复解析
    CollectNodeTelemetry m_nodeACur;
    ControlNodeTelemetry m_nodeBCur;
    GatewayStatusData    m_gwStatusCur;
    GatewayStatsData     m_gwStatsCur;

    QVector<int> m_onlineHistoryA; // 每秒采样一次，窗口 60 s
    QVector<int> m_onlineHistoryB;
};
#endif // MAINWINDOW_H
