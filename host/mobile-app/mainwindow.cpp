#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "contract_qt.h"
#include "gatewaypanel.h"
#include <QPixmap>
#include <QGuiApplication>
#include <QScreen>
#include <QRect>
#include <QBitArray>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#define DOOR_LEFT_FRONT   0x01
#define DOOR_RIGHT_FRONT  0x02
#define DOOR_LEFT_REAR    0x04
#define DOOR_RIGHT_REAR   0x08
#define ALL_DOORS         0x0F

// 空调状态位定义
#define AIR_SWITCH   0x01  // bit0
#define AIR_COOL     0x02  // bit1
#define AIR_HEAT     0x04  // bit2
using namespace QMQTT;

namespace {
constexpr int kTopBarHeight = 52;          // 顶部模式栏高度：原有控件整体下移这么多
constexpr qint64 kOnlineTimeoutMs = 5000;  // 5 s 内收到过遥测才算“在线”
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 顶部模式切换栏必须在其它控件定位前创建：它会把原有控件整体下移
    buildTopBar();

    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenRect = screen->availableGeometry();
    QRect winRect = this->frameGeometry();
    winRect.moveCenter(screenRect.center());
    this->move(winRect.topLeft());
    this->setFixedSize(this->size());

    QPixmap pix(":/car.jpg");
    ui->label_img->setScaledContents(true);
    ui->label_img->setPixmap(pix);
    QPixmap pix2(":/logo1.png");
    ui->label_logo->setScaledContents(true);
    ui->label_logo->setPixmap(pix2);

    // 网关模式面板：与仿真界面共用同一块中央区域，二者互斥显示
    m_gatewayPanel = new GatewayPanel(ui->centralwidget);
    m_gatewayPanel->setGeometry(11, kTopBarHeight, 401, height() - kTopBarHeight - 8);
    m_gatewayPanel->hide();
    connect(m_gatewayPanel, &GatewayPanel::estopRequested, this, &MainWindow::onEstopRequested);
    connect(m_gatewayPanel, &GatewayPanel::motorRequested, this, &MainWindow::onMotorRequested);
    connect(m_gatewayPanel, &GatewayPanel::modeRequested, this, &MainWindow::onGatewayModeRequested);

    connect(ui->ckbox_zqdoor,  &QCheckBox::stateChanged, this, &MainWindow::onSingleDoorStateChanged);
    connect(ui->ckbox_yqdoor,  &QCheckBox::stateChanged, this, &MainWindow::onSingleDoorStateChanged);
    connect(ui->ckbox_zhdoor,  &QCheckBox::stateChanged, this, &MainWindow::onSingleDoorStateChanged);
    connect(ui->ckbox_yhdoor, &QCheckBox::stateChanged, this, &MainWindow::onSingleDoorStateChanged);

    m_freshTimer = new QTimer(this);
    m_freshTimer->setInterval(1000);
    connect(m_freshTimer, &QTimer::timeout, this, &MainWindow::onRefreshOnlineState);
    m_freshTimer->start();

    // 默认进入仿真模式，客户端行为与改造前保持一致
    enterMode(ModeSim);
}

MainWindow::~MainWindow()
{
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        if (m_client->connectionState() != STATE_DISCONNECTED)
            m_client->disconnectFromHost();
        delete m_client;
        m_client = nullptr;
    }
    delete ui;
}

// ---------- 顶部模式切换栏（代码创建，未改 .ui） ----------
void MainWindow::buildTopBar()
{
    // 先把窗口加高，给模式栏腾出位置，再把原有控件整体下移
    resize(width(), height() + kTopBarHeight);

    const QList<QWidget *> children =
        ui->centralwidget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : children) {
        m_simWidgets.append(w);
        w->move(w->x(), w->y() + kTopBarHeight);
    }

    auto *bar = new QWidget(ui->centralwidget);
    bar->setGeometry(11, 8, 401, 40);
    bar->setStyleSheet(QStringLiteral(
        "QComboBox{background:#ffffff;border:1px solid #e2e8f0;border-radius:8px;padding:4px 8px;color:#2d3748;}"
        "QComboBox:hover{border-color:#94bfff;}"
        "QComboBox QAbstractItemView{background:#ffffff;selection-background-color:#5a9eff;}"));

    QFont barFont(QStringLiteral("Microsoft YaHei UI"));
    barFont.setPointSize(10);
    barFont.setBold(false);
    bar->setFont(barFont);

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    auto *lbMode = new QLabel(QStringLiteral("模式"));
    m_cmbMode = new QComboBox;
    m_cmbMode->addItem(QStringLiteral("仿真模式（ECU 模拟）"));
    m_cmbMode->addItem(QStringLiteral("网关模式（真实链路）"));
    m_cmbMode->setMinimumWidth(168);

    m_labelConn = new QLabel;
    m_labelConn->setStyleSheet(QStringLiteral("color:#a0aec0;font-size:8pt;"));

    lay->addWidget(lbMode);
    lay->addWidget(m_cmbMode);
    lay->addStretch(1);
    lay->addWidget(m_labelConn);

    connect(m_cmbMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);
}

// ---------- 模式切换 ----------
void MainWindow::onModeChanged(int index)
{
    const Mode target = (index == static_cast<int>(ModeGateway)) ? ModeGateway : ModeSim;
    if (target == m_mode)
        return;
    enterMode(target);
}

void MainWindow::enterMode(Mode mode)
{
    // 1) 先断开旧连接、退订旧主题，保证两种模式绝不并行
    teardownClient();

    // 2) 切换界面：同一时刻只显示一套
    m_mode = mode;
    const bool gateway = (mode == ModeGateway);
    for (QWidget *w : m_simWidgets)
        w->setVisible(!gateway);
    m_gatewayPanel->setVisible(gateway);

    // 3) 清空上一轮运行值，避免把旧数据当成新数据
    m_lastNodeA = 0;
    m_lastNodeB = 0;
    m_gatewayPanel->resetRuntime();
    m_gatewayPanel->clearLog();

    // 4) 按新模式建链
    startClient();
    updateConnLabel();
    logGateway(QStringLiteral("[模式] 切换到%1模式 → %2")
                   .arg(gateway ? QStringLiteral("网关") : QStringLiteral("仿真"), brokerText()));
}

QString MainWindow::brokerText() const
{
    if (m_mode == ModeGateway) {
        return QStringLiteral("%1:%2")
            .arg(QString::fromLatin1(contract::kBrokerHost))
            .arg(contract::kBrokerPort);
    }
    return QStringLiteral("8.138.233.135:1883");
}

QStringList MainWindow::subscriptionsForMode(Mode mode) const
{
    if (mode == ModeGateway) {
        return QStringList{
            QString::fromLatin1(contract::kTelemetryCollect),
            QString::fromLatin1(contract::kTelemetryControl),
            QString::fromLatin1(contract::kAlertCollect),
            QString::fromLatin1(contract::kAlertControl),
            QString::fromLatin1(contract::kGatewayStatus),
            QString::fromLatin1(contract::kGatewayStats),
        };
    }
    return QStringList{m_topic_car_bms, m_topic_car_vcu, m_topic_car_bcm, m_topic_car_dtc};
}

void MainWindow::startClient()
{
    m_client = new Client();

    if (m_mode == ModeGateway) {
        // 主题/端口/主机全部来自 contract.json 生成的常量
        m_client->setHostName(QString::fromLatin1(contract::kBrokerHost));
        m_client->setPort(static_cast<quint16>(contract::kBrokerPort));
        // client_ids.mobile（protocol/contract.json）
        m_client->setClientId(QStringLiteral("qt_mobile_app"));
    } else {
        m_client->setHostName(QStringLiteral("8.138.233.135"));
        m_client->setPort(1883);
        m_client->setClientId(QStringLiteral("myphone"));
    }
    m_client->setUsername(QString());
    m_client->setPassword(QByteArray());

    connect(m_client, &Client::connected, this, &MainWindow::onConnected);
    connect(m_client, &Client::disconnected, this, &MainWindow::onDisconnected);
    connect(m_client, &Client::received, this, &MainWindow::onReceived);
    connect(m_client, &Client::error, this, [this](const QMQTT::ClientError error) {
        logGateway(QStringLiteral("[MQTT] 错误码 %1").arg(static_cast<int>(error)));
        updateConnLabel();
    });

    logGateway(QStringLiteral("[MQTT] 正在连接 %1 clientId=%2")
                   .arg(brokerText(), m_client->clientId()));
    m_client->connectToHost();
}

void MainWindow::teardownClient()
{
    if (!m_client)
        return;

    // 先退订本模式订阅过的所有主题，再断开；旧 client 随后整体销毁
    if (m_client->connectionState() == STATE_CONNECTED) {
        const QStringList topics = subscriptionsForMode(m_mode);
        for (const QString &topic : topics) {
            m_client->unsubscribe(topic);
            logGateway(QStringLiteral("[MQTT] 取消订阅 %1").arg(topic));
        }
        m_client->disconnectFromHost();
    }

    // 断开信号：切换瞬间旧 client 的迟到回调不允许再改界面
    disconnect(m_client, nullptr, this, nullptr);
    m_client->deleteLater();
    m_client = nullptr;
}

void MainWindow::updateConnLabel()
{
    if (!m_labelConn)
        return;

    const bool online = m_client && m_client->connectionState() == STATE_CONNECTED;
    m_labelConn->setText(QStringLiteral("%1 · %2")
                             .arg(online ? QStringLiteral("已连接") : QStringLiteral("未连接"), brokerText()));
    m_labelConn->setStyleSheet(online ? QStringLiteral("color:#38a169;font-size:8pt;")
                                      : QStringLiteral("color:#a0aec0;font-size:8pt;"));
}

void MainWindow::subscribeTopic(const QString &topic, quint8 qos)
{
    if (!m_client || m_client->connectionState() != STATE_CONNECTED)
        return;

    m_client->subscribe(topic, qos);
    logGateway(QStringLiteral("[MQTT] 订阅 %1 (QoS%2)").arg(topic).arg(qos));
}

// ---------- MQTT 回调 ----------
void MainWindow::onConnected()
{
    logGateway(QStringLiteral("[MQTT] 连接成功 %1").arg(brokerText()));
    updateConnLabel();

    if (m_mode == ModeGateway) {
        // QoS 与 protocol/contract.json 的 publish 表一致
        subscribeTopic(QString::fromLatin1(contract::kTelemetryCollect), 0);
        subscribeTopic(QString::fromLatin1(contract::kTelemetryControl), 0);
        subscribeTopic(QString::fromLatin1(contract::kAlertCollect), 1);
        subscribeTopic(QString::fromLatin1(contract::kAlertControl), 1);
        subscribeTopic(QString::fromLatin1(contract::kGatewayStatus), 0);
        subscribeTopic(QString::fromLatin1(contract::kGatewayStats), 0);
    } else {
        subscribeTopic(m_topic_car_bms, 0);
        subscribeTopic(m_topic_car_vcu, 0);
        subscribeTopic(m_topic_car_bcm, 0);
        subscribeTopic(m_topic_car_dtc, 0);
    }
}

void MainWindow::onDisconnected()
{
    logGateway(QStringLiteral("[MQTT] 断开连接 %1").arg(brokerText()));
    updateConnLabel();
}

void MainWindow::onReceived(const QMQTT::Message &msg)
{
    const QString topic = msg.topic();
    const QByteArray payload = msg.payload();

    // 收发全量日志：qDebug 一份，网关模式再上界面日志面板一份
    qDebug().noquote() << QStringLiteral("[MQTT<-] %1 qos=%2 retain=%3 %4")
                              .arg(topic)
                              .arg(msg.qos())
                              .arg(msg.retain() ? QStringLiteral("true") : QStringLiteral("false"),
                                   QString::fromUtf8(payload));
    if (m_mode == ModeGateway)
        m_gatewayPanel->appendLog(QStringLiteral("← %1  qos=%2  %3")
                                      .arg(topic)
                                      .arg(msg.qos())
                                      .arg(QString::fromUtf8(payload)));

    if (m_mode == ModeGateway) {
        handleGatewayMessage(topic, payload);
        return;
    }

    // ---------- 仿真模式（原有逻辑，未改变） ----------
    if (topic == m_topic_car_bms)
    {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject())
            return;
        QJsonObject jo = doc.object();
        int soc = jo["batterySoc"].toInt(-1);
        if(soc >=0)
        {
            ui->label_dianchi->setText(QString("%1").arg(soc));
        }
        else
        {
            ui->label_dianchi->setText("--");
        }
    }
    if(topic ==m_topic_car_vcu){
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject())
            return;
        QJsonObject jo = doc.object();
        int speed = jo["vcuSpeed"].toInt(-1);
        if(speed >=0)
        {
            ui->label_chesu->setText(QString("%1").arg(speed));
        }
        else
        {
            ui->label_chesu->setText("--");
        }
    }
}

// ---------- 网关模式：JSON 解析 ----------
void MainWindow::handleGatewayMessage(const QString &topic, const QByteArray &payload)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (!doc.isObject()) {
        logGateway(QStringLiteral("[警告] %1 载荷不是 JSON（%2）").arg(topic, parseError.errorString()));
        return;
    }
    const QJsonObject jo = doc.object();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (topic == QString::fromLatin1(contract::kTelemetryCollect)) {
        // {ir,ldr,seq}
        m_lastNodeA = now;
        m_gatewayPanel->setCollectData(jo.value(QStringLiteral("ir")).toInt(-1),
                                      jo.value(QStringLiteral("ldr")).toInt(-1),
                                      jo.value(QStringLiteral("seq")).toInt(-1));
    } else if (topic == QString::fromLatin1(contract::kTelemetryControl)) {
        // {motor,pwm,mode,flags}
        m_lastNodeB = now;
        m_gatewayPanel->setControlState(jo.value(QStringLiteral("motor")).toInt(-1),
                                       jo.value(QStringLiteral("pwm")).toInt(-1),
                                       jo.value(QStringLiteral("mode")).toInt(-1),
                                       jo.value(QStringLiteral("flags")).toInt(-1));
    } else if (topic == QString::fromLatin1(contract::kAlertCollect)
               || topic == QString::fromLatin1(contract::kAlertControl)) {
        // {code,extra}
        const bool isCollect = (topic == QString::fromLatin1(contract::kAlertCollect));
        const QString node = isCollect ? QString::fromLatin1(contract::kNodeCollect)
                                       : QString::fromLatin1(contract::kNodeControl);
        const int code = jo.value(QStringLiteral("code")).toInt(-1);
        const int extra = jo.value(QStringLiteral("extra")).toInt(0);
        m_gatewayPanel->setAlert(code, extra, node);
        m_gatewayPanel->appendLog(QStringLiteral("[告警] %1 code=%2 extra=%3").arg(node).arg(code).arg(extra));
    } else if (topic == QString::fromLatin1(contract::kGatewayStatus)) {
        // {wifi,mqtt,cache}
        m_gatewayPanel->setGatewayStatus(jo.value(QStringLiteral("wifi")).toInt(-1),
                                        jo.value(QStringLiteral("mqtt")).toInt(-1),
                                        jo.value(QStringLiteral("cache")).toInt(-1));
    } else if (topic == QString::fromLatin1(contract::kGatewayStats)) {
        // {a_rx,a_lost,pub_total,pub_lost}，uint32，用 QVariant 转换避免溢出
        const auto u32 = [&jo](const char *key) {
            return static_cast<quint32>(jo.value(QLatin1String(key)).toVariant().toUInt());
        };
        m_gatewayPanel->setGatewayStats(u32("a_rx"), u32("a_lost"), u32("pub_total"), u32("pub_lost"));
    }

    onRefreshOnlineState();
}

void MainWindow::onRefreshOnlineState()
{
    if (m_mode != ModeGateway || !m_gatewayPanel)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool onlineA = (m_lastNodeA > 0) && (now - m_lastNodeA < kOnlineTimeoutMs);
    const bool onlineB = (m_lastNodeB > 0) && (now - m_lastNodeB < kOnlineTimeoutMs);

    m_gatewayPanel->setNodeOnline(onlineA, onlineB);

    // 节点离线时把数值显示成未知，避免把上一帧数据当实时值
    if (!onlineA)
        m_gatewayPanel->setCollectData(-1, -1, -1);
    if (!onlineB)
        m_gatewayPanel->setControlState(-1, -1, -1, -1);
}

// ---------- 网关模式：命令下发 ----------
void MainWindow::publishGatewayCommand(const QString &node, const char *kind, const QByteArray &payload)
{
    if (m_mode != ModeGateway || !m_client) {
        qDebug() << "[MQTT] 非网关模式，命令已忽略";
        return;
    }
    if (m_client->connectionState() != STATE_CONNECTED) {
        logGateway(QStringLiteral("[警告] MQTT 未连接，命令未下发"));
        return;
    }

    // 契约拼法：<prefix>/<node>/cmd/<kind>（contract_qt.h 注释给的就是这种拼法）
    const QString topic = QString::fromLatin1(contract::kTopicPrefix) + QLatin1Char('/') + node
                          + QLatin1String("/cmd/") + QString::fromLatin1(kind);
    QMQTT::Message msg(0, topic, payload, 1, false);   // QoS1，retain=false

    m_client->publish(msg);
    qDebug().noquote() << QStringLiteral("[MQTT->] %1 qos=1 retain=false %2")
                              .arg(topic, QString::fromUtf8(payload));
    m_gatewayPanel->appendLog(QStringLiteral("→ %1  qos=1  %2").arg(topic, QString::fromUtf8(payload)));
    m_gatewayPanel->setLastCommand(topic, payload);
}

void MainWindow::onEstopRequested(bool engage)
{
    // 急停/解除一律广播到 all：安全动作不做单节点收敛
    publishGatewayCommand(QString::fromLatin1(contract::kNodeAll), contract::kCmdEstop,
                          QByteArray(engage ? "1" : "2"));
}

void MainWindow::onMotorRequested(int cmd, int arg)
{
    const QByteArray payload = QStringLiteral("%1,%2").arg(cmd).arg(arg).toUtf8();
    publishGatewayCommand(m_gatewayPanel->commandNode(), contract::kCmdMotor, payload);
}

void MainWindow::onGatewayModeRequested(int mode)
{
    publishGatewayCommand(m_gatewayPanel->commandNode(), contract::kCmdMode, QByteArray::number(mode));
}

void MainWindow::logGateway(const QString &line)
{
    qDebug().noquote() << line;
    if (m_gatewayPanel && m_mode == ModeGateway)
        m_gatewayPanel->appendLog(line);
}

// ---------- 车门控制 ----------
void MainWindow::on_ckbox_alldoor_stateChanged(int arg1)
{
    // 避免在批量更新时递归
    if (m_batchUpdate) return;

    m_batchUpdate = true;   // 进入批量模式

    if (arg1 == Qt::Checked) {
        // 全开
        cmd.door_status |= ALL_DOORS;
        ui->ckbox_zqdoor->setChecked(true);
        ui->ckbox_yqdoor->setChecked(true);
        ui->ckbox_zhdoor->setChecked(true);
        ui->ckbox_yhdoor->setChecked(true);
        ui->ckbox_zqdoor->setEnabled(false);
        ui->ckbox_yqdoor->setEnabled(false);
        ui->ckbox_zhdoor->setEnabled(false);
        ui->ckbox_yhdoor->setEnabled(false);
    } else {
        // 全关
        cmd.door_status &= ~ALL_DOORS;
        ui->ckbox_zqdoor->setChecked(false);
        ui->ckbox_yqdoor->setChecked(false);
        ui->ckbox_zhdoor->setChecked(false);
        ui->ckbox_yhdoor->setChecked(false);
        ui->ckbox_zqdoor->setEnabled(true);
        ui->ckbox_yqdoor->setEnabled(true);
        ui->ckbox_zhdoor->setEnabled(true);
        ui->ckbox_yhdoor->setEnabled(true);
    }

    m_batchUpdate = false;   // 退出批量模式
    sendCmd(cmd);
}
void MainWindow::onSingleDoorStateChanged(int arg1)
{
    // 批量更新时跳过，避免循环
    if (m_batchUpdate) return;

    QCheckBox *box = qobject_cast<QCheckBox*>(sender());
    if (!box) return;

    uint8_t bitMask = 0;
    if      (box == ui->ckbox_zqdoor)   bitMask = DOOR_LEFT_FRONT;
    else if (box == ui->ckbox_yqdoor)   bitMask = DOOR_RIGHT_FRONT;
    else if (box == ui->ckbox_zhdoor)   bitMask = DOOR_LEFT_REAR;
    else if (box == ui->ckbox_yhdoor)   bitMask = DOOR_RIGHT_REAR;
    else return;

    // 更新命令位
    if (arg1 == Qt::Checked)
        cmd.door_status |= bitMask;
    else
        cmd.door_status &= ~bitMask;

    // 发送命令
    sendCmd(cmd);

    // 同步更新全部门复选框
    updateAllDoorCheckBox();
}
void MainWindow::updateAllDoorCheckBox()
{
    // 判断四个门是否全部勾选
    bool allChecked = ui->ckbox_zqdoor->isChecked() &&
                      ui->ckbox_yqdoor->isChecked() &&
                      ui->ckbox_zhdoor->isChecked() &&
                      ui->ckbox_yhdoor->isChecked();


    if (ui->ckbox_alldoor->isChecked() != allChecked) {
        m_batchUpdate = true;   // 暂时屏蔽全门槽
        ui->ckbox_alldoor->setChecked(allChecked);
        m_batchUpdate = false;
    }
    // 如果全部勾选，禁用单个门（用户友好）
    bool enableSingle = !allChecked;
    ui->ckbox_zqdoor->setEnabled(enableSingle);
    ui->ckbox_yqdoor->setEnabled(enableSingle);
    ui->ckbox_zhdoor->setEnabled(enableSingle);
    ui->ckbox_yhdoor->setEnabled(enableSingle);
}

// ---------- 发送命令 ----------
void MainWindow::sendCmd(const cmddata &cmd)
{
    QJsonObject jo = cmd.toJson();
    QJsonDocument doc(jo);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);
    qDebug() << "下发消息[" << payload << "]";

    // 互斥保护：网关模式下不向 ECU 仿真 broker 发车门/空调命令
    if (m_mode != ModeSim) {
        qDebug() << "[MQTT] 非仿真模式，车门/空调命令已忽略";
        return;
    }

    if (!m_client || m_client->connectionState() == STATE_DISCONNECTED) {
        qDebug() << "MQTT未连接，放弃发送";
        return;
    }

    QMQTT::Message msg(0, m_topic_cmd, payload, 1);
    m_client->publish(msg);
}

// ---------- 空调控制 ----------

// 总开关
void MainWindow::on_ckbox_kongtiaokaiguan_stateChanged(int arg1)
{
    if (arg1 == Qt::Checked) {
        cmd.air_con_status |= AIR_SWITCH;   // 开启总开关
        // 如果尚未设置任何模式，默认制冷
        if ((cmd.air_con_status & (AIR_COOL | AIR_HEAT)) == 0) {
            cmd.air_con_status |= AIR_COOL;
        }
        ui->ckbox_kongtiaokaiguan->setText("开");
        ui->ckbox_lenrekaiguan->setEnabled(true);
    } else {
        cmd.air_con_status &= ~AIR_SWITCH;  // 关闭总开关
        cmd.air_con_status &= ~(AIR_COOL | AIR_HEAT); // 清除模式位
        ui->ckbox_kongtiaokaiguan->setText("关");
        ui->ckbox_lenrekaiguan->setEnabled(false);
        // 冷热开关取消勾选并恢复文本
        ui->ckbox_lenrekaiguan->setChecked(false);
        ui->ckbox_lenrekaiguan->setText("冷热开关");
    }

    // 同步界面（根据当前状态更新冷热开关的文本和勾选）
    updateAirConUI();
    // 温度值也需要包含在命令中
    cmd.air_temperature = ui->spinbox_wendu->value();
    sendCmd(cmd);
}

// 冷热切换开关：勾选 = 制冷，取消 = 制热
void MainWindow::on_ckbox_lenrekaiguan_stateChanged(int arg1)
{
    // 如果总开关未开，不允许切换（直接返回，并恢复原状态）
    if ((cmd.air_con_status & AIR_SWITCH) == 0) {
        ui->ckbox_lenrekaiguan->setChecked(false);
        return;
    }

    if (arg1 == Qt::Checked) {
        // 勾选 → 制冷
        cmd.air_con_status |= AIR_COOL;
        cmd.air_con_status &= ~AIR_HEAT;
        ui->ckbox_lenrekaiguan->setText("制冷模式");
    } else {
        // 取消 → 制热
        cmd.air_con_status |= AIR_HEAT;
        cmd.air_con_status &= ~AIR_COOL;
        ui->ckbox_lenrekaiguan->setText("制热模式");
    }

    cmd.air_temperature = ui->spinbox_wendu->value();
    sendCmd(cmd);
}

// 温度旋钮
void MainWindow::on_spinbox_wendu_valueChanged(int value)
{
    cmd.air_temperature = value;
    // 只有总开关开启时才发送（关着也能调温度，但命令可能无效，根据需求可选）
    if (cmd.air_con_status & AIR_SWITCH) {
        sendCmd(cmd);
    }
}
void MainWindow::updateAirConUI()
{
    bool isOn = (cmd.air_con_status & AIR_SWITCH) != 0;
    ui->ckbox_kongtiaokaiguan->setChecked(isOn);
    ui->ckbox_kongtiaokaiguan->setText(isOn ? "开" : "关");

    ui->ckbox_lenrekaiguan->setEnabled(isOn);
    if (isOn) {
        bool isCooling = (cmd.air_con_status & AIR_COOL) != 0;
        ui->ckbox_lenrekaiguan->setChecked(isCooling);
        ui->ckbox_lenrekaiguan->setText(isCooling ? "制冷模式" : "制热模式");
    } else {
        ui->ckbox_lenrekaiguan->setChecked(false);
        ui->ckbox_lenrekaiguan->setText("冷热开关");
    }
}
