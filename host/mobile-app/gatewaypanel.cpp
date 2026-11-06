#include "gatewaypanel.h"
#include "contract_qt.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTime>
#include <QVBoxLayout>

namespace {

// CAN 0x102 flags 位定义（见 protocol/contract.json）
constexpr int kFlagCommOk = 0x01;
constexpr int kFlagThresh = 0x02;
constexpr int kFlagAlarm  = 0x04;
constexpr int kFlagEstop  = 0x08;

const char *kStyleOn    = "color:#38a169;font-weight:bold;";   // 在线 / 已连接
const char *kStyleOff   = "color:#a0aec0;font-weight:bold;";   // 离线 / 未连接
const char *kStyleAlarm = "color:#dd6b20;font-weight:bold;";   // 告警
const char *kStyleMuted = "color:#718096;font-weight:bold;";

QLabel *makeCardTitle(const QString &text)
{
    auto *lb = new QLabel(text);
    QFont f = lb->font();
    f.setBold(true);
    f.setPointSize(10);
    lb->setFont(f);
    lb->setStyleSheet(QStringLiteral("color:#2d3748;"));
    return lb;
}

QLabel *makeName(const QString &text)
{
    auto *lb = new QLabel(text);
    lb->setStyleSheet(QStringLiteral("color:#4a5568;"));
    return lb;
}

QLabel *makeValue(const QString &text = QStringLiteral("--"))
{
    auto *lb = new QLabel(text);
    QFont f = lb->font();
    f.setBold(true);
    f.setPointSize(10);
    lb->setFont(f);
    lb->setStyleSheet(QStringLiteral("color:#2d3748;"));
    return lb;
}

QFrame *makeCard()
{
    auto *card = new QFrame;
    card->setFrameShape(QFrame::NoFrame);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return card;
}

QString alarmName(int code)
{
    switch (code) {
    case 1: return QStringLiteral("传感器故障");
    case 2: return QStringLiteral("通信丢失");
    case 3: return QStringLiteral("阈值越界");
    case 4: return QStringLiteral("执行器故障");
    default: return QStringLiteral("未知告警");
    }
}

QString motorName(int motor)
{
    switch (motor) {
    case 0: return QStringLiteral("停止");
    case 1: return QStringLiteral("正转");
    case 2: return QStringLiteral("反转");
    default: return QStringLiteral("未知(%1)").arg(motor);
    }
}

} // namespace

GatewayPanel::GatewayPanel(QWidget *parent)
    : QWidget(parent)
{
    QFont base(QStringLiteral("Microsoft YaHei UI"));
    base.setPointSize(9);
    setFont(base);

    setStyleSheet(QStringLiteral(
        "QPushButton{background:#5a9eff;color:#ffffff;border:none;border-radius:8px;padding:5px 12px;}"
        "QPushButton:hover{background:#4488ee;}"
        "QPushButton:pressed{background:#3b78d8;}"
        "QPushButton:disabled{background:#cbd5e0;}"
        "QPushButton#btnEstop{background:#e53e3e;font-weight:bold;}"
        "QPushButton#btnEstop:hover{background:#c53030;}"
        "QPushButton#btnClearEstop{background:#ed8936;}"
        "QPushButton#btnClearEstop:hover{background:#dd6b20;}"
        "QPushButton#btnClearLog{background:#a0aec0;padding:3px 10px;}"
        "QPushButton#btnClearLog:hover{background:#718096;}"
        "QComboBox{background:#ffffff;border:1px solid #e2e8f0;border-radius:8px;padding:4px 8px;}"
        "QComboBox:hover{border-color:#94bfff;}"
        "QComboBox QAbstractItemView{background:#ffffff;selection-background-color:#5a9eff;}"
        "QSpinBox{background:#ffffff;border:1px solid #e2e8f0;border-radius:8px;padding:4px 8px;}"
        "QPlainTextEdit{background:#ffffff;border:1px solid #e2e8f0;border-radius:8px;}"
    ));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);
    root->addWidget(buildNodeCard());
    root->addWidget(buildGatewayCard());
    root->addWidget(buildControlCard());
    root->addWidget(buildLogCard(), 1);

    resetRuntime();
}

QFrame *GatewayPanel::buildNodeCard()
{
    auto *card = makeCard();
    auto *lay = new QGridLayout(card);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setHorizontalSpacing(10);
    lay->setVerticalSpacing(3);

    lay->addWidget(makeCardTitle(QStringLiteral("节点实时数据")), 0, 0, 1, 4);

    lay->addWidget(makeName(QStringLiteral("节点A（采集）")), 1, 0);
    m_lbNodeA = makeValue();
    lay->addWidget(m_lbNodeA, 1, 1);
    lay->addWidget(makeName(QStringLiteral("节点B（控制）")), 1, 2);
    m_lbNodeB = makeValue();
    lay->addWidget(m_lbNodeB, 1, 3);

    lay->addWidget(makeName(QStringLiteral("红外 IR")), 2, 0);
    m_lbIr = makeValue();
    lay->addWidget(m_lbIr, 2, 1);
    lay->addWidget(makeName(QStringLiteral("电机")), 2, 2);
    m_lbMotor = makeValue();
    lay->addWidget(m_lbMotor, 2, 3);

    lay->addWidget(makeName(QStringLiteral("光敏 LDR")), 3, 0);
    m_lbLdr = makeValue();
    lay->addWidget(m_lbLdr, 3, 1);
    lay->addWidget(makeName(QStringLiteral("PWM")), 3, 2);
    m_lbPwm = makeValue();
    lay->addWidget(m_lbPwm, 3, 3);

    lay->addWidget(makeName(QStringLiteral("序列 seq")), 4, 0);
    m_lbSeq = makeValue();
    lay->addWidget(m_lbSeq, 4, 1);
    lay->addWidget(makeName(QStringLiteral("模式")), 4, 2);
    m_lbMode = makeValue();
    lay->addWidget(m_lbMode, 4, 3);

    lay->addWidget(makeName(QStringLiteral("标志 flags")), 5, 0);
    m_lbFlags = makeValue();
    lay->addWidget(m_lbFlags, 5, 1, 1, 3);

    lay->addWidget(makeName(QStringLiteral("最近告警")), 6, 0);
    m_lbAlert = makeValue();
    m_lbAlert->setWordWrap(true);
    lay->addWidget(m_lbAlert, 6, 1, 1, 3);

    lay->setColumnStretch(1, 1);
    lay->setColumnStretch(3, 1);
    return card;
}

QFrame *GatewayPanel::buildGatewayCard()
{
    auto *card = makeCard();
    auto *lay = new QGridLayout(card);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setHorizontalSpacing(10);
    lay->setVerticalSpacing(3);

    lay->addWidget(makeCardTitle(QStringLiteral("网关状态")), 0, 0, 1, 4);

    lay->addWidget(makeName(QStringLiteral("WiFi")), 1, 0);
    m_lbWifi = makeValue();
    lay->addWidget(m_lbWifi, 1, 1);
    lay->addWidget(makeName(QStringLiteral("MQTT")), 1, 2);
    m_lbMqtt = makeValue();
    lay->addWidget(m_lbMqtt, 1, 3);

    lay->addWidget(makeName(QStringLiteral("缓存")), 2, 0);
    m_lbCache = makeValue();
    lay->addWidget(m_lbCache, 2, 1);
    lay->addWidget(makeName(QStringLiteral("采集 收/丢")), 2, 2);
    m_lbCollectIo = makeValue();
    lay->addWidget(m_lbCollectIo, 2, 3);

    lay->addWidget(makeName(QStringLiteral("发布 总/丢")), 3, 0);
    m_lbPubIo = makeValue();
    lay->addWidget(m_lbPubIo, 3, 1, 1, 3);

    lay->setColumnStretch(1, 1);
    lay->setColumnStretch(3, 1);
    return card;
}

QFrame *GatewayPanel::buildControlCard()
{
    auto *card = makeCard();
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(5);

    lay->addWidget(makeCardTitle(QStringLiteral("远程控制（QoS1 / retain=false）")));

    auto *rowNode = new QHBoxLayout;
    rowNode->setSpacing(8);
    rowNode->addWidget(makeName(QStringLiteral("控制节点")));
    m_cmbNode = new QComboBox;
    m_cmbNode->addItem(QStringLiteral("nodeB（控制）"), QString::fromLatin1(contract::kNodeControl));
    m_cmbNode->addItem(QStringLiteral("nodeA（采集）"), QString::fromLatin1(contract::kNodeCollect));
    m_cmbNode->addItem(QStringLiteral("all（广播）"), QString::fromLatin1(contract::kNodeAll));
    m_cmbNode->setMinimumWidth(150);
    rowNode->addWidget(m_cmbNode);
    rowNode->addStretch(1);
    lay->addLayout(rowNode);

    auto *rowEstop = new QHBoxLayout;
    rowEstop->setSpacing(8);
    m_btnEstop = new QPushButton(QStringLiteral("急停"));
    m_btnEstop->setObjectName(QStringLiteral("btnEstop"));
    m_btnEstop->setToolTip(QStringLiteral("广播 all 节点：estop 载荷 1"));
    m_btnClearEstop = new QPushButton(QStringLiteral("解除"));
    m_btnClearEstop->setObjectName(QStringLiteral("btnClearEstop"));
    m_btnClearEstop->setToolTip(QStringLiteral("广播 all 节点：estop 载荷 2"));
    rowEstop->addWidget(m_btnEstop, 1);
    rowEstop->addWidget(m_btnClearEstop, 1);
    lay->addLayout(rowEstop);

    auto *rowMotor = new QHBoxLayout;
    rowMotor->setSpacing(8);
    rowMotor->addWidget(makeName(QStringLiteral("电机")));
    m_btnStop = new QPushButton(QStringLiteral("停止"));
    m_btnFwd = new QPushButton(QStringLiteral("正转"));
    m_btnRev = new QPushButton(QStringLiteral("反转"));
    rowMotor->addWidget(m_btnStop);
    rowMotor->addWidget(m_btnFwd);
    rowMotor->addWidget(m_btnRev);
    rowMotor->addStretch(1);
    lay->addLayout(rowMotor);

    auto *rowSpeed = new QHBoxLayout;
    rowSpeed->setSpacing(8);
    rowSpeed->addWidget(makeName(QStringLiteral("调速")));
    m_spinSpeed = new QSpinBox;
    m_spinSpeed->setRange(0, 100);
    m_spinSpeed->setValue(50);
    m_spinSpeed->setSuffix(QStringLiteral(" %"));
    rowSpeed->addWidget(m_spinSpeed);
    auto *btnSetSpeed = new QPushButton(QStringLiteral("设速"));
    btnSetSpeed->setToolTip(QStringLiteral("motor 载荷 3,<0~100>"));
    rowSpeed->addWidget(btnSetSpeed);
    rowSpeed->addStretch(1);
    lay->addLayout(rowSpeed);

    auto *rowMode = new QHBoxLayout;
    rowMode->setSpacing(8);
    rowMode->addWidget(makeName(QStringLiteral("模式")));
    auto *btnManual = new QPushButton(QStringLiteral("手动"));
    auto *btnAuto = new QPushButton(QStringLiteral("自动"));
    rowMode->addWidget(btnManual);
    rowMode->addWidget(btnAuto);
    rowMode->addStretch(1);
    lay->addLayout(rowMode);

    m_lbLastCmd = new QLabel(QStringLiteral("最近下发：--"));
    m_lbLastCmd->setWordWrap(true);
    m_lbLastCmd->setStyleSheet(QStringLiteral("color:#718096;font-size:8pt;"));
    lay->addWidget(m_lbLastCmd);

    connect(m_btnEstop, &QPushButton::clicked, this, [this] { emit estopRequested(true); });
    connect(m_btnClearEstop, &QPushButton::clicked, this, [this] { emit estopRequested(false); });
    connect(m_btnStop, &QPushButton::clicked, this, [this] { emit motorRequested(0, 0); });
    connect(m_btnFwd, &QPushButton::clicked, this, [this] { emit motorRequested(1, 0); });
    connect(m_btnRev, &QPushButton::clicked, this, [this] { emit motorRequested(2, 0); });
    connect(btnSetSpeed, &QPushButton::clicked, this, [this] { emit motorRequested(3, m_spinSpeed->value()); });
    connect(btnManual, &QPushButton::clicked, this, [this] { emit modeRequested(0); });
    connect(btnAuto, &QPushButton::clicked, this, [this] { emit modeRequested(1); });

    return card;
}

QFrame *GatewayPanel::buildLogCard()
{
    auto *card = makeCard();
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(5);

    auto *rowTitle = new QHBoxLayout;
    rowTitle->addWidget(makeCardTitle(QStringLiteral("MQTT 报文日志")));
    rowTitle->addStretch(1);
    auto *btnClear = new QPushButton(QStringLiteral("清空"));
    btnClear->setObjectName(QStringLiteral("btnClearLog"));
    rowTitle->addWidget(btnClear);
    lay->addLayout(rowTitle);

    m_logView = new QPlainTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(500);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(8);
    m_logView->setFont(mono);
    m_logView->setMinimumHeight(80);
    lay->addWidget(m_logView, 1);

    connect(btnClear, &QPushButton::clicked, this, &GatewayPanel::clearLog);
    return card;
}

// ---------- 显示 ----------

void GatewayPanel::resetRuntime()
{
    setNodeOnline(false, false);
    setCollectData(-1, -1, -1);
    setControlState(-1, -1, -1, -1);
    setGatewayStatus(-1, -1, -1);
    m_lbCollectIo->setText(QStringLiteral("--"));
    m_lbCollectIo->setStyleSheet(QString::fromLatin1(kStyleMuted));
    m_lbPubIo->setText(QStringLiteral("--"));
    m_lbPubIo->setStyleSheet(QString::fromLatin1(kStyleMuted));
    setAlert(-1, 0, QString());
    m_lbLastCmd->setText(QStringLiteral("最近下发：--"));
}

void GatewayPanel::setNodeOnline(bool nodeAOnline, bool nodeBOnline)
{
    const auto apply = [](QLabel *lb, bool on) {
        lb->setText(on ? QStringLiteral("在线") : QStringLiteral("离线"));
        lb->setStyleSheet(QString::fromLatin1(on ? kStyleOn : kStyleOff));
    };
    apply(m_lbNodeA, nodeAOnline);
    apply(m_lbNodeB, nodeBOnline);
}

void GatewayPanel::setCollectData(int ir, int ldr, int seq)
{
    m_lbIr->setText(ir < 0 ? QStringLiteral("--") : QString::number(ir));
    m_lbLdr->setText(ldr < 0 ? QStringLiteral("--") : QString::number(ldr));
    m_lbSeq->setText(seq < 0 ? QStringLiteral("--") : QString::number(seq));
}

void GatewayPanel::setControlState(int motor, int pwm, int mode, int flags)
{
    if (motor < 0) {
        m_lbMotor->setText(QStringLiteral("--"));
        m_lbMotor->setStyleSheet(QString::fromLatin1(kStyleMuted));
    } else {
        m_lbMotor->setText(motorName(motor));
        m_lbMotor->setStyleSheet(motor == 0 ? QStringLiteral("color:#718096;font-weight:bold;")
                                            : QStringLiteral("color:#38a169;font-weight:bold;"));
    }

    m_lbPwm->setText(pwm < 0 ? QStringLiteral("--") : QStringLiteral("%1 %").arg(pwm));

    if (mode < 0) {
        m_lbMode->setText(QStringLiteral("--"));
        m_lbMode->setStyleSheet(QString::fromLatin1(kStyleMuted));
    } else {
        m_lbMode->setText(mode == 0 ? QStringLiteral("手动") : QStringLiteral("自动"));
        m_lbMode->setStyleSheet(QStringLiteral("color:#2d3748;font-weight:bold;"));
    }

    if (flags < 0) {
        m_lbFlags->setText(QStringLiteral("--"));
        return;
    }
    QStringList bits;
    if (flags & kFlagCommOk) bits << QStringLiteral("COMM_OK");
    if (flags & kFlagThresh) bits << QStringLiteral("THRESH");
    if (flags & kFlagAlarm)  bits << QStringLiteral("ALARM");
    if (flags & kFlagEstop)  bits << QStringLiteral("ESTOP");
    QString text = QStringLiteral("0x%1").arg(flags, 2, 16, QLatin1Char('0'));
    if (!bits.isEmpty())
        text += QStringLiteral(" [") + bits.join(QStringLiteral(", ")) + QLatin1Char(']');
    m_lbFlags->setText(text);
    m_lbFlags->setStyleSheet((flags & kFlagEstop) ? QString::fromLatin1(kStyleAlarm)
                                                  : QStringLiteral("color:#2d3748;font-weight:bold;"));
}

void GatewayPanel::setGatewayStatus(int wifi, int mqtt, int cache)
{
    const auto apply = [](QLabel *lb, int v) {
        if (v < 0) {
            lb->setText(QStringLiteral("--"));
            lb->setStyleSheet(QString::fromLatin1(kStyleMuted));
            return;
        }
        lb->setText(v ? QStringLiteral("已连接") : QStringLiteral("未连接"));
        lb->setStyleSheet(QString::fromLatin1(v ? kStyleOn : kStyleOff));
    };
    apply(m_lbWifi, wifi);
    apply(m_lbMqtt, mqtt);
    m_lbCache->setText(cache < 0 ? QStringLiteral("--") : QString::number(cache));
}

void GatewayPanel::setGatewayStats(quint32 aRx, quint32 aLost, quint32 pubTotal, quint32 pubLost)
{
    m_lbCollectIo->setText(QStringLiteral("%1 / %2").arg(aRx).arg(aLost));
    m_lbCollectIo->setStyleSheet(QStringLiteral("color:#2d3748;font-weight:bold;"));
    m_lbPubIo->setText(QStringLiteral("%1 / %2").arg(pubTotal).arg(pubLost));
    m_lbPubIo->setStyleSheet(QStringLiteral("color:#2d3748;font-weight:bold;"));
}

void GatewayPanel::setAlert(int code, int extra, const QString &node)
{
    if (code < 0) {
        m_lbAlert->setText(QStringLiteral("--"));
        m_lbAlert->setStyleSheet(QString::fromLatin1(kStyleMuted));
        return;
    }
    m_lbAlert->setText(QStringLiteral("%1（%2）  code=%3 extra=%4")
                           .arg(node.isEmpty() ? QStringLiteral("网关") : node,
                                alarmName(code))
                           .arg(code)
                           .arg(extra));
    m_lbAlert->setStyleSheet(QString::fromLatin1(kStyleAlarm));
}

void GatewayPanel::setLastCommand(const QString &topic, const QByteArray &payload)
{
    if (topic.isEmpty()) {
        m_lbLastCmd->setText(QStringLiteral("最近下发：--"));
        return;
    }
    m_lbLastCmd->setText(QStringLiteral("最近下发：%1  →  %2")
                             .arg(topic, QString::fromUtf8(payload)));
}

QString GatewayPanel::commandNode() const
{
    if (!m_cmbNode)
        return QString::fromLatin1(contract::kNodeControl);
    const QString node = m_cmbNode->currentData().toString();
    return node.isEmpty() ? QString::fromLatin1(contract::kNodeControl) : node;
}

// ---------- 日志 ----------

void GatewayPanel::clearLog()
{
    if (m_logView)
        m_logView->clear();
}

void GatewayPanel::appendLog(const QString &line)
{
    if (!m_logView)
        return;
    m_logView->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))
                               + QLatin1Char(' ') + line);
}
