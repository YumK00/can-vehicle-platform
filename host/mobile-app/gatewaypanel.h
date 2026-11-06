#ifndef GATEWAYPANEL_H
#define GATEWAYPANEL_H

#include <QWidget>
#include <QByteArray>
#include <QString>

class QComboBox;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

/*
 * 网关模式面板。
 *
 * 只负责“显示 + 交互”，不直接碰 MQTT：
 *   - 数据由 MainWindow 从 contract 主题解析 JSON 后调用 setXxx() 塞进来；
 *   - 用户点按钮时抛信号，由 MainWindow 按契约拼主题并发布命令。
 * 面板内部不出现任何 "gw0805/..." 字面量，节点名取自 contract::kNodeXxx。
 */
class GatewayPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GatewayPanel(QWidget *parent = nullptr);

    // 进入网关模式时清空上一轮残留的显示值
    void resetRuntime();

    void clearLog();
    void appendLog(const QString &line);                       // 面板自动加 HH:mm:ss 前缀
    void setLastCommand(const QString &topic, const QByteArray &payload);

    // 实时数据
    void setNodeOnline(bool nodeAOnline, bool nodeBOnline);
    void setCollectData(int ir, int ldr, int seq);
    void setControlState(int motor, int pwm, int mode, int flags);
    void setGatewayStatus(int wifi, int mqtt, int cache);
    void setGatewayStats(quint32 aRx, quint32 aLost, quint32 pubTotal, quint32 pubLost);
    void setAlert(int code, int extra, const QString &node);

    // 电机 / 模式命令所用的目标节点（nodeA / nodeB / all）
    QString commandNode() const;

signals:
    void estopRequested(bool engage);   // true = 急停，false = 解除
    void motorRequested(int cmd, int arg);
    void modeRequested(int mode);       // 0 手动，1 自动

private:
    QFrame *buildNodeCard();
    QFrame *buildGatewayCard();
    QFrame *buildControlCard();
    QFrame *buildLogCard();

    QLabel *m_lbNodeA = nullptr;
    QLabel *m_lbNodeB = nullptr;
    QLabel *m_lbIr = nullptr;
    QLabel *m_lbLdr = nullptr;
    QLabel *m_lbSeq = nullptr;
    QLabel *m_lbMotor = nullptr;
    QLabel *m_lbPwm = nullptr;
    QLabel *m_lbMode = nullptr;
    QLabel *m_lbFlags = nullptr;
    QLabel *m_lbAlert = nullptr;

    QLabel *m_lbWifi = nullptr;
    QLabel *m_lbMqtt = nullptr;
    QLabel *m_lbCache = nullptr;
    QLabel *m_lbCollectIo = nullptr;
    QLabel *m_lbPubIo = nullptr;

    QComboBox *m_cmbNode = nullptr;
    QSpinBox *m_spinSpeed = nullptr;
    QPushButton *m_btnEstop = nullptr;
    QPushButton *m_btnClearEstop = nullptr;
    QPushButton *m_btnStop = nullptr;
    QPushButton *m_btnFwd = nullptr;
    QPushButton *m_btnRev = nullptr;
    QLabel *m_lbLastCmd = nullptr;
    QPlainTextEdit *m_logView = nullptr;
};

#endif // GATEWAYPANEL_H
