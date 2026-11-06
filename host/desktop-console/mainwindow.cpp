#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QSvgRenderer>
#include <QPixmap>
#include <QPainter>
#include <QTime>
#include <QTimer>
#include <QDebug>
#include <QDateTime>

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>

using namespace QMQTT;

static uint m_countfasong = 0;
static uint m_countdingyue = 0;
#define MAX_ZHENLV 5000
static uint  m_fps=0;
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_currentWindowBit(0)
    , m_showBps(0)
    ,tim2(new QTimer(this))
{
    ui->setupUi(this);

    this->setFocusPolicy(Qt::StrongFocus);
    this->setFocus();

    /* 新增的"网关状态看板 / 命令下发"全部用代码创建，
     * 以 Tab 页的方式插进已有的 horizontalLayout_2（不改 mainwindow.ui，
     * 也不新增列，原有三列几何关系保持不变）。 */
    setupGatewayPanel();
    /* 右列从单个 MQTT 控制组变成三页 Tab，画布宽一点更好读；不超出屏幕 */
    if (screen() != nullptr) {
        const QRect avail = screen()->availableGeometry();
        resize(qMax(width(), qMin(1500, avail.width() - 40)),
               qMax(height(), qMin(900, avail.height() - 40)));
    }

    /* 只监听"真实车辆"这一颗：它被选中 = 进真实模式；被取消 = 回仿真模式。
     * 仿真那颗不需要单独连，避免一次点击触发两遍切换。 */
    connect(ui->radiobtn_zhenshicheliang, &QRadioButton::toggled,
            this, &MainWindow::onRunModeChanged);

    /* 看板里跟"时间"有关的指标（在线率采样、命令超时兜底）统一 1 s 刷一次 */
    m_gatewayTick = new QTimer(this);
    connect(m_gatewayTick, &QTimer::timeout, this, &MainWindow::onGatewayTick);
    m_gatewayTick->start(1000);

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::on_timeout);
    timer->start(1000);

    connect(tim2, &QTimer::timeout, this, &MainWindow::on_tim2out);

}
//can日志发出定时器
void MainWindow::on_tim2out(){
    QString str = QString("[%1 km/h | SOC %2% | MCU %3℃]")
                        .arg(m_vcucurinfo.vcuSpeed)
                      .arg(m_bmscurinfo.batterySoc)
                      .arg(m_vcucurinfo.vcuTemp);
    writeLog(0,1,str);
}

//日志工具函数
QString MainWindow::getCurrentTimeString() {
    return QTime::currentTime().toString("hh:mm:ss");
}
//isRecv：0=tx，1=rx
//isCan 1=can，0=mqtt
void MainWindow::writeLog(bool isRecv,bool isCan,const QString& logstr){
    QString writelog;
    if(isRecv){
        writelog = "[" + getCurrentTimeString() + "]RX " +logstr;
    }
    else{
        writelog = "[" + getCurrentTimeString() + "]TX " + logstr;
    }
    if(isCan){
        ui->listWidgetcan->addItem(writelog);
        ui->listWidgetcan->scrollToBottom();
    }
    else{
        ui->listWidgetmqtt->addItem(writelog);
        ui->listWidgetmqtt->scrollToBottom();
    }
}
// ---------- CAN 接收 ----------
void MainWindow::on_CanFramesReceived()
{
    while (m_candevice && m_candevice->framesAvailable()) {
        QCanBusFrame frame = m_candevice->readFrame();
        if (frame.isValid()) {
            parseCanFrame(frame);
        }
    }
}

// ---------- 界面更新 ----------
void MainWindow::updateBmsUi(const s_bmsdata &data)
{
    ui->label_dianchi->setText(QString("%1").arg(data.batterySoc));
    ui->progressBar_3->setValue(static_cast<int>(data.batterySoc));
}

void MainWindow::updateVcuUi(const s_vcudata &data)
{
    ui->label_chesu->setText(QString("%1").arg(data.vcuSpeed));
    ui->label_dianjiwendu->setText(QString("%1").arg(data.vcuTemp));
    ui->progressBar_2->setValue(static_cast<int>(data.vcuTemp));
}

void MainWindow::updateBcmUi(const s_bcmdata &data)
{
    QString text = QString("车门:%1 空调:%2 温度:%3")
                       .arg(data.door_status)
                       .arg(data.air_con_status)
                       .arg(data.air_temperature);
    if (ui->listWidgetcan->count() > 0) {
        ui->listWidgetcan->item(ui->listWidgetcan->count() - 1)->setText(text);
    } else {
        ui->listWidgetcan->addItem(text);
    }
}

void MainWindow::updateDtcUi(const s_dtcdata &data)
{
    QString faultText;
    switch (data.guzhangma) {
    case 0x00:
        faultText = "无故障活动";
        ui->label_4->setText("系统自检通过");
        break;
    case 0x01:
        faultText = "左前门锁故障";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x02:
        faultText = "右前门锁故障";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x03:
        faultText = "左后门锁故障";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x04:
        faultText = "右后门锁故障";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x05:
        faultText = "空调压缩机过流";
        ui->label_4->setText("系统自检异常");
        break;
    case 0x06:
        faultText = "空调制冷剂不足";
        ui->label_4->setText("系统自检异常");
        break;
    default:
        faultText = QString("故障码: 0x%1").arg(data.guzhangma, 2, 16, QChar('0'));
        ui->label_4->setText("系统自检异常");
        break;
    }
    ui->label_guzhanghuodong->setText(faultText);

    if (data.guzhangma == 0x00) {
        ui->label_guzhanghuodong->setStyleSheet("color: #10B981;");
    } else {
        if (data.youxianji == 1)
            ui->label_guzhanghuodong->setStyleSheet("color: #FF8C00;");
        else if (data.youxianji == 2)
            ui->label_guzhanghuodong->setStyleSheet("color: red;");
    }
}

// ---------- CAN 帧解析 ----------
void MainWindow::parseCanFrame(const QCanBusFrame &frame)
{
    QByteArray payload = frame.payload();

    m_fps=m_fps+47+8*payload.size();
    //qDebug()<<"m_fps="<<m_fps;

    if (payload.size() < 8) {
        qDebug() << "payload length error, len=" << payload.size();
        return;
    }
    switch (frame.frameId()) {
    case 0x120: // BMS电池报文
    {
        uint16_t soc = static_cast<uint16_t>((payload[0] << 8) | (payload[1] & 0xFF));
        if (m_bmscurinfo.batterySoc != soc) {
            //数据变化才改ui和发送mqtt报文
            m_bmscurinfo.batterySoc = soc;
            updateBmsUi(m_bmscurinfo);
            if(m_mqttClient != nullptr)
            {
                m_mqttClient->sendBmsInfo(m_bmscurinfo);
                ui->label_mqttfabuxiaoxi->setText(QString::number(++m_countfasong));
            }
        }
        break;
    }

    case 0x100: // VCU整车运行报文
    {
        uint16_t speed = static_cast<uint16_t>((payload[0] << 8) | (payload[1] & 0xFF));
        uint16_t vcutemp = static_cast<uint16_t>((payload[2] << 8) | (payload[3] & 0xFF));
        if (m_vcucurinfo.vcuSpeed != speed || vcutemp != m_vcucurinfo.vcuTemp) {
            m_vcucurinfo.vcuSpeed = speed;
            m_vcucurinfo.vcuTemp = vcutemp ;
            updateVcuUi(m_vcucurinfo);
            if(m_mqttClient != nullptr)
            {
                m_mqttClient->sendVcuInfo(m_vcucurinfo);
                ui->label_mqttfabuxiaoxi->setText(QString::number(++m_countfasong));
            }
        }
        break;
    }

    case 0x110: // DTC故障报文
    {
        uint8_t guzhangma = static_cast<uint8_t>(payload[0]);
        uint8_t youxianji = static_cast<uint8_t>(payload[1]);

        if (m_dtccurinfo.guzhangma != guzhangma || m_dtccurinfo.youxianji != youxianji) {
            m_dtccurinfo.guzhangma = guzhangma;
            m_dtccurinfo.youxianji = youxianji;
            updateDtcUi(m_dtccurinfo);
            if(m_mqttClient != nullptr)
            {
                m_mqttClient->sendDtcInfo(m_dtccurinfo);
                ui->label_mqttfabuxiaoxi->setText(QString::number(++m_countfasong));
            }
        }
        break;
    }

    case 0x200: // BCM车身控制报文
    {
        uint8_t door_state = static_cast<uint8_t>(payload[0]);
        uint8_t ac_state = static_cast<uint8_t>(payload[1]);
        uint8_t air_temp = static_cast<uint8_t>(payload[2]);

        if (m_bcmcurinfo.door_status != door_state ||
            m_bcmcurinfo.air_con_status != ac_state ||
            m_bcmcurinfo.air_temperature != air_temp)
        {
            m_bcmcurinfo.door_status = door_state;
            m_bcmcurinfo.air_con_status = ac_state;
            m_bcmcurinfo.air_temperature = air_temp;
            updateBcmUi(m_bcmcurinfo);
            if(m_mqttClient != nullptr)
            {
                m_mqttClient->sendBcmInfo(m_bcmcurinfo);
                ui->label_mqttfabuxiaoxi->setText(QString::number(++m_countfasong));
            }
        }
        break;
    }

    default:
        break;
    }
}

// ---------- 断开连接 ----------
void MainWindow::closeConnect()
{
    if (m_bms) {
        m_bms->stopSim();
        delete m_bms;
        m_bms = nullptr;
    }
    if (m_vcu) {
        m_vcu->stopSim();
        delete m_vcu;
        m_vcu = nullptr;
    }
    if (m_bcm) {
        m_bcm->stopSim();
        delete m_bcm;
        m_bcm = nullptr;
    }
    if (m_dtc) {
        m_dtc->stopSim();
        delete m_dtc;
        m_dtc = nullptr;
    }
    if (m_candevice) {
        if (m_candevice->state() == QCanBusDevice::ConnectedState) {
            m_candevice->disconnectDevice();
        }
        delete m_candevice;
        m_candevice = nullptr;
    }
}

MainWindow::~MainWindow()
{
    closeConnect();
    if (m_gateway != nullptr) {
        m_gateway->disconnectBroker();
        delete m_gateway;
        m_gateway = nullptr;
    }
    if(m_mqttClient != nullptr)
    {
        delete m_mqttClient;
        m_mqttClient = nullptr;
    }
    delete ui;
}

// ---------- 定时器 ----------
void MainWindow::on_timeout()
{
    ui->timeedit1->setDateTime(QDateTime::currentDateTime());

    ui->progressbar_zhenlv->setValue(m_fps);
    ui->label_zhenlv->setText(QString::number(m_fps));
    //qDebug()<<"m_fps="<<m_fps;
    m_fps = 0;
}

// ---------- 启动 CAN ----------
void MainWindow::on_btn_qidongcan_clicked()
{
    /* 真实网关模式下按"启动CAN"= 明确要回到仿真数据源：
     * 先把模式拨回仿真，再按原流程启动仿真 ECU（不破坏原仿真功能）。
     * 用 QSignalBlocker 挡住 toggled，避免这里和 onRunModeChanged 互相调用。 */
    if (ui->radiobtn_zhenshicheliang->isChecked()) {
        QSignalBlocker blocker(ui->radiobtn_zhenshicheliang);
        ui->radiobtn_zhenshicheliang->setChecked(false);
        ui->radionbtn_fangzhengmoshi->setChecked(true);
        if (m_gateway != nullptr) {
            m_gateway->disconnectBroker();
        }
        m_simSuspendedForRealMode = false;
        writeLog(false, true, QStringLiteral("启动CAN：退出真实网关模式，切回仿真数据源"));
    }

    closeConnect();
    ui->timeedit2->setTime(QTime::currentTime());

    QCanBus *canBus = QCanBus::instance();
    m_candevice = canBus->createDevice("virtualcan", "can0");

    if (m_candevice) {
        m_candevice->setConfigurationParameter(QCanBusDevice::LoopbackKey, true);
        m_candevice->setConfigurationParameter(QCanBusDevice::ReceiveOwnKey, true);
        if (m_candevice->connectDevice()) {
            connect(m_candevice, &QCanBusDevice::framesReceived,
                    this, &MainWindow::on_CanFramesReceived);
            qDebug() << "CAN device connected successfully.";
            m_candevice->setConfigurationParameter(QCanBusDevice::BitRateKey,MAX_ZHENLV);

            m_bms = new BmsECU(m_candevice, this);
            m_vcu = new VcuECU(m_candevice, this);
            m_bcm = new BcmECU(m_candevice, this);
            m_dtc = new DtcECU(m_candevice, this);

            m_bms->startSim(1300);//电池
            m_vcu->startSim(300);//整车运行
            m_bcm->startSim(2400);//车门
            m_dtc->startSim(180);//故障

            tim2->start(5000);
            ui->listWidgetcan->addItem(QString("[%1]CAN已启动 波特率1kbit/s").arg(getCurrentTimeString()));
            ui->listWidgetcan->scrollToBottom();
            ui->label_yunxingzhuangtai->setText("运行中");
            ui->label_yunxingzhuangtai->setStyleSheet("color: #10B981;");
            ui->label_zongxianzhuangtai->setText("通讯运行");
        } else {
            qWarning() << "Failed to connect CAN device:" << m_candevice->errorString();
            delete m_candevice;
            m_candevice = nullptr;
            ui->label_yunxingzhuangtai->setText("连接失败");
            ui->label_yunxingzhuangtai->setStyleSheet("color: #EF4444;");
        }
    } else {
        qWarning() << "Failed to create CAN device. Check socketcan plugin.";
        ui->label_yunxingzhuangtai->setText("设备创建失败");
        ui->label_yunxingzhuangtai->setStyleSheet("color: #EF4444;");
    }
}

// ---------- 停止 CAN ----------
void MainWindow::on_btn_tingzhican_clicked()
{
    /* 真实网关模式下没有本地 CAN 仿真，忽略这个按钮，
     * 免得把看板的"网关在线"状态改成"已停止"。 */
    if (ui->radiobtn_zhenshicheliang->isChecked()) {
        writeLog(false, true, QStringLiteral("真实网关模式：本地 CAN 仿真未启动，无需停止"));
        return;
    }

    closeConnect();

    tim2->stop();
    ui->listWidgetcan->addItem(QString("[%1]CAN已安全停止").arg(getCurrentTimeString()));
    ui->listWidgetcan->scrollToBottom();

    ui->label_yunxingzhuangtai->setText("已停止");
    ui->label_yunxingzhuangtai->setStyleSheet("color: #EF4444;");
    ui->label_chesu->setText("--");
    ui->label_dianchi->setText("--");
    ui->label_dianjiwendu->setText("--");
    ui->label_zongxianzhuangtai->setText("等待启动");
}

//---------------连接mqtt-----------------
void MainWindow::on_btn_mqttlianjie_clicked()
{
    if(m_mqttClient != nullptr)
    {
        m_mqttClient->disconnectBroker();
        delete m_mqttClient;
        m_mqttClient = nullptr;
    }
    m_mqttClient = new MqttClient(this);
    if(!m_mqttClient->mqttclientIsConnected()){
        connect(m_mqttClient, &MqttClient::sigRecvPhoneCmd,
                this, &MainWindow::onPhoneCommandArrived);
        ui->label_mqttzaixian->setText("会话稳定");
        ui->label_mqttzaixian->setStyleSheet("color: #10B981;");
        ui->label_mqttlianjiezhuangtai->setText("已连接");
        ui->label_mqttlianjiezhuangtai->setStyleSheet("color: #10B981;");
        //ui->btn_mqttlianjie->setText("断开Broker");
        ui->btn_mqttlianjie->setEnabled(false);
        ui->listWidgetmqtt->addItem(QString("[%1] CONNACK Broker连接已开启").arg(getCurrentTimeString()));
        ui->listWidgetmqtt->scrollToBottom();
        m_mqttClient->connectBroker();
    }
}

void MainWindow::onPhoneCommandArrived(const QJsonObject &cmdObj)
{
    //qDebug()<<"cmdobj["<<cmdObj<<"]";
    //cmdobj[ QJsonObject({"air_con_status":3,"air_temperature":24,"door_status":15}) ]
    uint8_t airconstatus_temp = static_cast<uint8_t>(cmdObj["air_con_status"].toInt(0));
    uint8_t airtemperature_temp = static_cast<uint8_t>(cmdObj["air_temperature"].toInt(24));
    uint8_t doorstatus_temp = static_cast<uint8_t>(cmdObj["door_status"].toInt(0));
    qint64 objtimestamp = cmdObj["timestamp"].toInteger();

    qint64 curtimetamp = QDateTime::currentMSecsSinceEpoch();
    //qDebug()<<curtimetamp-objtimestamp;
    ui->label_mqttxintiaoyanchi->setText(QString::number(curtimetamp-objtimestamp));
    bool isAirStatusChanged = false;
    bool isAirTempChanged = false;
    bool isDoorChanged = false;

    uint8_t oldDoor = m_bcmcurinfo.door_status;
    uint8_t newDoor = doorstatus_temp;

    if(m_bcmcurinfo.air_con_status != airconstatus_temp){
        m_bcmcurinfo.air_con_status = airconstatus_temp;
        isAirStatusChanged = true;
    }
    if(m_bcmcurinfo.air_temperature != airtemperature_temp){
        m_bcmcurinfo.air_temperature = airtemperature_temp;
        isAirTempChanged =true;
    }
    if(oldDoor != newDoor){
        m_bcmcurinfo.door_status = newDoor;
        isDoorChanged = true;
    }

    // ========== 空调逻辑 ==========
    if(isAirStatusChanged){
        QString air_str;
        uint8_t ac = m_bcmcurinfo.air_con_status;
        if((ac & 0x01) == 0)
        {
            air_str="空调已关闭";
        }
        else if( (ac & 0x02) !=0 ) //bit1 制冷
        {
            air_str= QString("空调已开启制冷模式,当前温度:%1").arg(m_bcmcurinfo.air_temperature);
        }
        else if( (ac & 0x04) !=0 ) //bit2 制热
        {
            air_str= QString("空调已开启制热模式,当前温度:%1").arg(m_bcmcurinfo.air_temperature);
        }
        else
        {
            air_str="空调开启，未知模式";
        }
        writeLog(1,0,air_str);
    }
    if(isAirTempChanged){
        QString airtem_str= QString("调节温度:%1℃").arg(m_bcmcurinfo.air_temperature);
        writeLog(1,0,airtem_str);
    }

    // ========== 车门状态 1=关门，0=开门==========
    if(isDoorChanged){
        QStringList changeDoorList;

        if( (oldDoor ^ newDoor) & 0x01 )
        {
            if( newDoor & 0x01 )
                changeDoorList << "左前门关门";
            else
                changeDoorList << "左前门开门";
        }
        if( (oldDoor ^ newDoor) & 0x02 )
        {
            if( newDoor & 0x02 )
                changeDoorList << "右前门关门";
            else
                changeDoorList << "右前门开门";
        }
        if( (oldDoor ^ newDoor) & 0x04 )
        {
            if( newDoor & 0x04 )
                changeDoorList << "左后门关门";
            else
                changeDoorList << "左后门开门";
        }
        if( (oldDoor ^ newDoor) & 0x08 )
        {
            if( newDoor & 0x08 )
                changeDoorList << "右后门关门";
            else
                changeDoorList << "右后门开门";
        }

        QString door_str = "车门状态变更：" + changeDoorList.join(" | ");
        writeLog(1,0,door_str);
    }

    ui->label_mqttdingyuezhuti->setText(QString::number(++m_countdingyue));

    // ========== 组装CAN 0x200报文发送给BCM ==========
    // if(m_candevice == nullptr || m_candevice->state() != QCanBusDevice::ConnectedState)
    // {
    //     qDebug()<<"CAN设备未连接，不发送BCM指令";
    //     return;
    // }
    // QCanBusFrame frame;
    // frame.setFrameId(0x200);
    // QByteArray payload(8, 0x00);
    // payload[0] = m_bcmcurinfo.door_status;
    // payload[1] = m_bcmcurinfo.air_con_status;
    // payload[2] = m_bcmcurinfo.air_temperature;
    // frame.setPayload(payload);
    // bool ok = m_candevice->writeFrame(frame);
    // qDebug()<<"下发BCM CAN0x200 "<<(ok?"成功":"失败");
}

#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDir>

void MainWindow::on_pushButton_clicked()
{
    writeLog(0,0,"发起快照请求...");
    QString dirPath = QFileDialog::getExistingDirectory(
        this,
        tr("选择日志保存文件夹"),
        QString("%1../../../car_state_log").arg(QDir::currentPath()),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );

    if(dirPath.isEmpty())
    {
        writeLog(0,0,"用户取消选择文件夹");
        return;
    }
    QDir m_dir(dirPath);
    if (!m_dir.exists()) {
        QMessageBox::warning(this, "警告", "路径不存在", QMessageBox::Yes);
        writeLog(0,0,"用户选择路径不存在");
        return;
    }
    QString fileName = QDateTime::currentDateTime().toString("'carstatus_'yyyyMMdd_hhmmss'.txt'");
    QString fullFilePath = QDir(dirPath).filePath(fileName);

    QFile tmpFile(fullFilePath);
    bool createOk = tmpFile.open(QIODevice::WriteOnly | QIODevice::Text);
    if (!createOk) {
        QMessageBox::warning(this, tr("错误"),
                             tr("创建文件失败:\n%1").arg(tmpFile.errorString()));
        writeLog(0,0,"用户创建文件失败");
        return;
    }
    tmpFile.close();

    m_logFile = new QFile(fullFilePath, this);
    if (!m_logFile->open(QIODevice::Append | QIODevice::Text)) {
        QMessageBox::warning(this, tr("错误"),
                             tr("文件创建成功，但打开失败:\n%1").arg(m_logFile->errorString()));
        delete m_logFile;
        m_logFile = nullptr;
        writeLog(0,0,"用户文件创建成功，但打开失败");
        return;
    }
    QTextStream stream(m_logFile);
    stream << "车辆状态日志记录\n";
    stream << "启动时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    stream << "batterySoc: " << m_bmscurinfo.batterySoc << "\n";
    stream << "vcuSpeed: " << m_vcucurinfo.vcuSpeed << "\n";
    stream << "air_con_status: " << m_bcmcurinfo.air_con_status << "\n";
    stream << "air_temperature: " << m_bcmcurinfo.air_temperature << "\n";
    stream << "door_status: " << m_bcmcurinfo.door_status << "\n";
    stream << "guzhangma: " << m_dtccurinfo.guzhangma << "\n";
    stream.flush();

    writeLog(1,0,QString("已保存状态快照到文件(%1)").arg(fullFilePath));
}

// =====================================================================
//                    真实网关模式：看板 + 命令面板
// =====================================================================
//
// 这一段所有控件都是"代码创建 + insertWidget"，不改 mainwindow.ui。
// 原因：.ui 由 Qt Designer/uic 维护，代码里加控件放在 .ui 里容易被下一次
// 设计器保存覆盖；而且插入位置只依赖已有的 horizontalLayout_2，
// 对原仿真界面的改动面为零（只多出一列）。
namespace {

QLabel *makeCaption(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("color: #94A3B8;"));
    return label;
}

QLabel *makeValue(const QString &text = QStringLiteral("--"))
{
    QLabel *label = new QLabel(text);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setMinimumWidth(78);
    label->setStyleSheet(QStringLiteral("color: #F8FAFC;"));
    return label;
}

QLabel *makeSection(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("color: #A0B4D8; font-weight: bold;"));
    return label;
}

QFrame *makeHLine()
{
    QFrame *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setStyleSheet(QStringLiteral("color: #23324E; background-color: #23324E; max-height: 1px;"));
    return line;
}

void setLabelState(QLabel *label, const QString &text, const char *color)
{
    if (label == nullptr) {
        return;
    }
    label->setText(text);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(color)));
}

void setValueLabel(QLabel *label, const QString &text)
{
    setLabelState(label, text, "#F8FAFC");
}

QString motorStateText(int motor)
{
    switch (motor) {
    case 0:  return QStringLiteral("停止");
    case 1:  return QStringLiteral("正转");
    case 2:  return QStringLiteral("反转");
    default: return QStringLiteral("未知(%1)").arg(motor);
    }
}

QString modeText(int mode)
{
    switch (mode) {
    case 0:  return QStringLiteral("手动");
    case 1:  return QStringLiteral("自动");
    default: return QStringLiteral("未知(%1)").arg(mode);
    }
}

/* 状态位按契约 0x102.flags：bit0 COMM_OK / bit1 THRESH / bit2 ALARM / bit3 ESTOP */
QString flagsText(int flags)
{
    QStringList parts;
    if (flags & 0x01) parts << QStringLiteral("COMM_OK");
    if (flags & 0x02) parts << QStringLiteral("THRESH");
    if (flags & 0x04) parts << QStringLiteral("ALARM");
    if (flags & 0x08) parts << QStringLiteral("ESTOP");
    if (parts.isEmpty()) {
        return QStringLiteral("0x%1").arg(flags, 2, 16, QChar('0'));
    }
    return parts.join(QLatin1Char('|'));
}

/* 报警码名称来自契约（protocol/contract.json → contract_qt.h），不再手抄一份 */
QString alarmCodeName(int code)
{
    switch (code) {
    case contract::kAlarmSensorFault:   return QString::fromLatin1(contract::kAlarmSensorFaultName);
    case contract::kAlarmCommLost:      return QString::fromLatin1(contract::kAlarmCommLostName);
    case contract::kAlarmThreshOver:    return QString::fromLatin1(contract::kAlarmThreshOverName);
    case contract::kAlarmActuatorFault: return QString::fromLatin1(contract::kAlarmActuatorFaultName);
    default: return QStringLiteral("UNKNOWN");
    }
}

} // namespace

void MainWindow::setupGatewayPanel()
{
    // ---------------- 网关状态看板 ----------------
    m_gatewayPanel = new QWidget;
    m_gatewayPanel->setObjectName(QStringLiteral("gatewayPanel"));
    QVBoxLayout *panelLayout = new QVBoxLayout(m_gatewayPanel);
    panelLayout->setContentsMargins(10, 8, 10, 8);
    panelLayout->setSpacing(6);

    QLabel *linkHint = new QLabel(
        QStringLiteral("真实网关模式 · %1:%2 · 前缀 %3 · clientId qt_desktop_console")
            .arg(QString::fromLatin1(contract::kBrokerHost))
            .arg(contract::kBrokerPort)
            .arg(QString::fromLatin1(contract::kTopicPrefix)),
        m_gatewayPanel);
    linkHint->setWordWrap(true);
    linkHint->setStyleSheet(QStringLiteral("color: #7185A5; font-size: 11px;"));
    panelLayout->addWidget(linkHint);

    QGridLayout *connGrid = new QGridLayout;
    connGrid->setHorizontalSpacing(8);
    connGrid->setVerticalSpacing(4);
    connGrid->addWidget(makeCaption(QStringLiteral("Broker")), 0, 0);
    m_gwConnLabel = makeValue(QStringLiteral("未启动"));
    m_gwConnLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_gwConnLabel->setStyleSheet(QStringLiteral("color: #94A3B8;"));
    connGrid->addWidget(m_gwConnLabel, 0, 1, 1, 3);
    panelLayout->addLayout(connGrid);

    panelLayout->addWidget(makeSection(QStringLiteral("上行遥测（节点A / 节点B）")));

    QGridLayout *tele = new QGridLayout;
    tele->setHorizontalSpacing(8);
    tele->setVerticalSpacing(4);

    m_gwNodeALabel = makeValue(QStringLiteral("离线"));
    m_gwNodeALabel->setStyleSheet(QStringLiteral("color: #94A3B8;"));
    m_gwNodeBLabel = makeValue(QStringLiteral("离线"));
    m_gwNodeBLabel->setStyleSheet(QStringLiteral("color: #94A3B8;"));
    m_gwIrLabel = makeValue();
    m_gwLdrLabel = makeValue();
    m_gwSeqLabel = makeValue();
    m_gwMotorLabel = makeValue();
    m_gwModeLabel = makeValue();
    m_gwFlagsLabel = makeValue();

    int row = 0;
    tele->addWidget(makeCaption(QStringLiteral("节点A")), row, 0);
    tele->addWidget(m_gwNodeALabel, row, 1);
    tele->addWidget(makeCaption(QStringLiteral("节点B")), row, 2);
    tele->addWidget(m_gwNodeBLabel, row, 3);
    ++row;
    tele->addWidget(makeCaption(QStringLiteral("红外 ir")), row, 0);
    tele->addWidget(m_gwIrLabel, row, 1);
    tele->addWidget(makeCaption(QStringLiteral("光敏 ldr")), row, 2);
    tele->addWidget(m_gwLdrLabel, row, 3);
    ++row;
    tele->addWidget(makeCaption(QStringLiteral("电机 / PWM")), row, 0);
    tele->addWidget(m_gwMotorLabel, row, 1);
    tele->addWidget(makeCaption(QStringLiteral("模式")), row, 2);
    tele->addWidget(m_gwModeLabel, row, 3);
    ++row;
    tele->addWidget(makeCaption(QStringLiteral("状态位")), row, 0);
    tele->addWidget(m_gwFlagsLabel, row, 1);
    tele->addWidget(makeCaption(QStringLiteral("帧序号 seq")), row, 2);
    tele->addWidget(m_gwSeqLabel, row, 3);
    panelLayout->addLayout(tele);

    panelLayout->addWidget(makeHLine());
    panelLayout->addWidget(makeSection(QStringLiteral("链路质量与统计")));

    QGridLayout *diag = new QGridLayout;
    diag->setHorizontalSpacing(8);
    diag->setVerticalSpacing(4);

    m_gwWifiLabel = makeValue();
    m_gwMqttLabel = makeValue();
    m_gwCacheLabel = makeValue();
    m_gwAlertLabel = makeValue();
    m_gwLostRateLabel = makeValue();
    m_gwPubStatsLabel = makeValue();
    m_gwLatencyLabel = makeValue();
    m_gwOnlineRateLabel = makeValue();

    row = 0;
    diag->addWidget(makeCaption(QStringLiteral("网关 WiFi")), row, 0);
    diag->addWidget(m_gwWifiLabel, row, 1);
    diag->addWidget(makeCaption(QStringLiteral("网关 MQTT")), row, 2);
    diag->addWidget(m_gwMqttLabel, row, 3);
    ++row;
    diag->addWidget(makeCaption(QStringLiteral("缓存条数")), row, 0);
    diag->addWidget(m_gwCacheLabel, row, 1);
    diag->addWidget(makeCaption(QStringLiteral("最近报警")), row, 2);
    diag->addWidget(m_gwAlertLabel, row, 3);
    ++row;
    diag->addWidget(makeCaption(QStringLiteral("丢帧率")), row, 0);
    diag->addWidget(m_gwLostRateLabel, row, 1);
    diag->addWidget(makeCaption(QStringLiteral("发布统计")), row, 2);
    diag->addWidget(m_gwPubStatsLabel, row, 3);
    ++row;
    diag->addWidget(makeCaption(QStringLiteral("通信时延")), row, 0);
    m_gwLatencyLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    diag->addWidget(m_gwLatencyLabel, row, 1, 1, 3);
    ++row;
    diag->addWidget(makeCaption(QStringLiteral("在线率")), row, 0);
    m_gwOnlineRateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    diag->addWidget(m_gwOnlineRateLabel, row, 1, 1, 3);
    panelLayout->addLayout(diag);

    QLabel *footNote = new QLabel(
        QStringLiteral("在线率 = 最近 60 s 采样中该节点 telemetry 未超时"
                       "（kHbTimeoutMs=%1）的占比；通信时延 = 命令下发到收到下一帧 0x102 的本地时间差")
            .arg(contract::kHbTimeoutMs),
        m_gatewayPanel);
    footNote->setWordWrap(true);
    footNote->setStyleSheet(QStringLiteral("color: #7185A5; font-size: 10px;"));
    panelLayout->addWidget(footNote);

    // ---------------- 命令面板 ----------------
    m_commandPanel = new QWidget;
    m_commandPanel->setObjectName(QStringLiteral("commandPanel"));
    QGridLayout *cmd = new QGridLayout(m_commandPanel);
    cmd->setContentsMargins(10, 8, 10, 8);
    cmd->setHorizontalSpacing(8);
    cmd->setVerticalSpacing(6);

    cmd->addWidget(makeCaption(QStringLiteral("命令目标")), 0, 0);
    m_cmdNodeCombo = new QComboBox(m_commandPanel);
    m_cmdNodeCombo->addItem(QStringLiteral("nodeB 执行节点"), GatewayClient::nodeNameControl());
    m_cmdNodeCombo->addItem(QStringLiteral("nodeA 采集节点"), GatewayClient::nodeNameCollect());
    m_cmdNodeCombo->setToolTip(
        QStringLiteral("电机 / 目标值下发到该节点；急停固定广播 all；模式固定发 nodeB"));
    cmd->addWidget(m_cmdNodeCombo, 0, 1, 1, 3);

    auto addCmdButton = [&](const QString &text, int r, int c, int span = 1) -> QPushButton * {
        QPushButton *btn = new QPushButton(text, m_commandPanel);
        m_cmdButtons.append(btn);
        cmd->addWidget(btn, r, c, 1, span);
        return btn;
    };

    QPushButton *btnEstop = addCmdButton(QStringLiteral("急停（广播）"), 1, 0, 2);
    btnEstop->setObjectName(QStringLiteral("btn_gateway_estop"));
    btnEstop->setToolTip(QStringLiteral("estop=1 → 网关转 CAN 0x000 广播急停"));
    connect(btnEstop, &QPushButton::clicked, this, &MainWindow::onCmdEmergencyStop);

    QPushButton *btnClear = addCmdButton(QStringLiteral("解除急停"), 1, 2, 2);
    btnClear->setToolTip(QStringLiteral("estop=2 → 解除；其余任何值网关都按急停处理"));
    connect(btnClear, &QPushButton::clicked, this, &MainWindow::onCmdEmergencyClear);

    cmd->addWidget(makeCaption(QStringLiteral("电机")), 2, 0);
    QPushButton *btnStop = addCmdButton(QStringLiteral("停止"), 2, 1);
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::onCmdMotorStop);
    QPushButton *btnFwd = addCmdButton(QStringLiteral("正转"), 2, 2);
    connect(btnFwd, &QPushButton::clicked, this, &MainWindow::onCmdMotorForward);
    QPushButton *btnRev = addCmdButton(QStringLiteral("反转"), 2, 3);
    connect(btnRev, &QPushButton::clicked, this, &MainWindow::onCmdMotorReverse);

    cmd->addWidget(makeCaption(QStringLiteral("设速 0~100")), 3, 0);
    m_cmdSpeedSpin = new QSpinBox(m_commandPanel);
    m_cmdSpeedSpin->setRange(0, 100);
    m_cmdSpeedSpin->setValue(50);
    m_cmdSpeedSpin->setSuffix(QStringLiteral(" %"));
    cmd->addWidget(m_cmdSpeedSpin, 3, 1);
    QPushButton *btnSpeed = addCmdButton(QStringLiteral("设速下发"), 3, 2, 2);
    connect(btnSpeed, &QPushButton::clicked, this, &MainWindow::onCmdMotorSetSpeed);

    cmd->addWidget(makeCaption(QStringLiteral("目标值")), 4, 0);
    m_cmdTargetEdit = new QLineEdit(m_commandPanel);
    m_cmdTargetEdit->setPlaceholderText(QStringLiteral("0 ~ 65535"));
    m_cmdTargetEdit->setValidator(new QIntValidator(0, 65535, m_cmdTargetEdit));
    cmd->addWidget(m_cmdTargetEdit, 4, 1);
    QPushButton *btnTarget = addCmdButton(QStringLiteral("下发目标"), 4, 2, 2);
    connect(btnTarget, &QPushButton::clicked, this, &MainWindow::onCmdTargetSend);

    cmd->addWidget(makeCaption(QStringLiteral("模式")), 5, 0);
    QPushButton *btnManual = addCmdButton(QStringLiteral("手动"), 5, 1);
    connect(btnManual, &QPushButton::clicked, this, &MainWindow::onCmdModeManual);
    QPushButton *btnAuto = addCmdButton(QStringLiteral("自动"), 5, 2, 2);
    connect(btnAuto, &QPushButton::clicked, this, &MainWindow::onCmdModeAuto);

    QLabel *cmdHint = new QLabel(
        QStringLiteral("命令主题 %1/&lt;node&gt;/cmd/&lt;kind&gt;，QoS1、retain=false；"
                       "急停 topic 用 all，模式只支持 nodeB")
            .arg(QString::fromLatin1(contract::kTopicPrefix)),
        m_commandPanel);
    cmdHint->setWordWrap(true);
    cmdHint->setStyleSheet(QStringLiteral("color: #7185A5; font-size: 10px;"));
    cmd->addWidget(cmdHint, 6, 0, 1, 4);

    /* 看板 + 命令面板放进右侧原有的一列，列内用 Tab 切换：
     *  - "MQTT 控制" 是原来的 groupBox_3，原样搬进来当第 0 页并默认选中，
     *    所以仿真模式下的手机 MQTT 控制面板与原来完全一致；
     *  - 另两页是真实网关的看板与命令，切到"真实车辆"时自动选到看板页。
     * 这样不增加列数：布局仍是 [CAN控制, 车辆状态, 右列] 三列，
     * 只是右列从"单个 MQTT 控制组"变成"三个 Tab 页"。 */
    m_gatewayTabs = new QTabWidget(this);
    m_gatewayTabs->setObjectName(QStringLiteral("gatewayTabs"));
    m_gatewayTabs->setMinimumWidth(300);

    ui->horizontalLayout_2->removeWidget(ui->groupBox_3);
    m_gatewayTabs->addTab(ui->groupBox_3, QStringLiteral("MQTT 控制"));
    m_gatewayTabs->addTab(m_gatewayPanel, QStringLiteral("网关看板"));
    m_gatewayTabs->addTab(m_commandPanel, QStringLiteral("命令下发"));
    m_gatewayTabs->setCurrentIndex(0);

    ui->horizontalLayout_2->insertWidget(2, m_gatewayTabs, 4);
    ui->horizontalLayout_2->setStretch(0, 3);
    ui->horizontalLayout_2->setStretch(1, 5);
    ui->horizontalLayout_2->setStretch(2, 4);

    setGatewayCommandsEnabled(false);
}

void MainWindow::connectGatewaySignals()
{
    connect(m_gateway, &GatewayClient::connectionStateChanged,
            this, &MainWindow::onGatewayConnectionChanged);
    connect(m_gateway, &GatewayClient::collectTelemetryReceived,
            this, &MainWindow::onGatewayCollectTelemetry);
    connect(m_gateway, &GatewayClient::controlTelemetryReceived,
            this, &MainWindow::onGatewayControlTelemetry);
    connect(m_gateway, &GatewayClient::alertReceived,
            this, &MainWindow::onGatewayAlert);
    connect(m_gateway, &GatewayClient::gatewayStatusReceived,
            this, &MainWindow::onGatewayStatus);
    connect(m_gateway, &GatewayClient::gatewayStatsReceived,
            this, &MainWindow::onGatewayStats);
    connect(m_gateway, &GatewayClient::nodeOnlineChanged,
            this, &MainWindow::onGatewayNodeOnline);
    connect(m_gateway, &GatewayClient::traffic,
            this, &MainWindow::onGatewayTraffic);
}

void MainWindow::onRunModeChanged(bool realModeChecked)
{
    if (realModeChecked) {
        enterRealMode();
    } else {
        enterSimMode();
    }
}

void MainWindow::stopSimulation()
{
    closeConnect();
    if (tim2 != nullptr) {
        tim2->stop();
    }
    ui->label_chesu->setText(QStringLiteral("--"));
    ui->label_dianchi->setText(QStringLiteral("--"));
    ui->label_dianjiwendu->setText(QStringLiteral("--"));
    writeLog(false, true, QStringLiteral("仿真数据源已停止（切到真实网关模式）"));
}

void MainWindow::enterRealMode()
{
    if (m_gateway == nullptr) {
        m_gateway = new GatewayClient(this);
        connectGatewaySignals();
    }

    /* 停掉本机 ECU 仿真，但记住它原本在跑，切回仿真模式时自动恢复 */
    if (m_candevice != nullptr) {
        m_simSuspendedForRealMode = true;
        stopSimulation();
    }

    m_pendingCommandMs = -1;
    m_onlineHistoryA.clear();
    m_onlineHistoryB.clear();

    /* 清掉上一轮真实模式留下的数值，避免"切过去先看到旧数据" */
    m_nodeACur = CollectNodeTelemetry();
    m_nodeBCur = ControlNodeTelemetry();
    m_gwStatusCur = GatewayStatusData();
    m_gwStatsCur = GatewayStatsData();
    setValueLabel(m_gwIrLabel, QStringLiteral("--"));
    setValueLabel(m_gwLdrLabel, QStringLiteral("--"));
    setValueLabel(m_gwSeqLabel, QStringLiteral("--"));
    setValueLabel(m_gwMotorLabel, QStringLiteral("--"));
    setValueLabel(m_gwModeLabel, QStringLiteral("--"));
    setValueLabel(m_gwFlagsLabel, QStringLiteral("--"));
    setValueLabel(m_gwWifiLabel, QStringLiteral("--"));
    setValueLabel(m_gwMqttLabel, QStringLiteral("--"));
    setValueLabel(m_gwCacheLabel, QStringLiteral("--"));
    setValueLabel(m_gwAlertLabel, QStringLiteral("--"));
    setValueLabel(m_gwLostRateLabel, QStringLiteral("--"));
    setValueLabel(m_gwPubStatsLabel, QStringLiteral("--"));

    setLabelState(m_gwLatencyLabel, QStringLiteral("--"), "#94A3B8");
    updateOnlineRate();
    setGatewayCommandsEnabled(false);
    refreshNodeLabels();
    if (m_gatewayTabs != nullptr) {
        m_gatewayTabs->setCurrentIndex(1);  // 自动翻到"网关看板"
    }

    ui->label_yunxingzhuangtai->setText(QStringLiteral("网关连接中"));
    ui->label_yunxingzhuangtai->setStyleSheet(QStringLiteral("color: #F59E0B;"));
    ui->label_zongxianzhuangtai->setText(QStringLiteral("等待 MQTT"));
    ui->timeedit2->setTime(QTime::currentTime());

    writeLog(false, false, QStringLiteral("运行模式 → 真实车辆（ESP32 网关 MQTT 链路）"));
    m_gateway->connectBroker();
}

void MainWindow::enterSimMode()
{
    if (m_gateway != nullptr) {
        m_gateway->disconnectBroker();
    }

    m_pendingCommandMs = -1;
    m_onlineHistoryA.clear();
    m_onlineHistoryB.clear();
    setLabelState(m_gwOnlineRateLabel, QStringLiteral("--"), "#94A3B8");
    setLabelState(m_gwLatencyLabel, QStringLiteral("--"), "#94A3B8");
    setLabelState(m_gwConnLabel, QStringLiteral("仿真模式 · 网关未启用"), "#94A3B8");
    setLabelState(m_gwNodeALabel, QStringLiteral("--"), "#94A3B8");
    setLabelState(m_gwNodeBLabel, QStringLiteral("--"), "#94A3B8");
    setGatewayCommandsEnabled(false);
    refreshNodeLabels();
    if (m_gatewayTabs != nullptr) {
        m_gatewayTabs->setCurrentIndex(0);  // 回到原来的"MQTT 控制"
    }

    writeLog(false, false, QStringLiteral("运行模式 → 仿真模式（本机 4 个 ECU 仿真）"));

    if (m_simSuspendedForRealMode) {
        m_simSuspendedForRealMode = false;
        /* 原仿真在切走之前是跑着的，切回来就按原流程恢复 */
        on_btn_qidongcan_clicked();
    } else {
        ui->label_yunxingzhuangtai->setText(QStringLiteral("仿真待启动"));
        ui->label_yunxingzhuangtai->setStyleSheet(QStringLiteral("color: #94A3B8;"));
        ui->label_zongxianzhuangtai->setText(QStringLiteral("等待启动"));
    }
}

void MainWindow::updateGatewayConnectionUi(bool connected, const QString &detail)
{
    if (connected) {
        setLabelState(m_gwConnLabel, QStringLiteral("已连接"), "#10B981");
        setLabelState(ui->label_yunxingzhuangtai, QStringLiteral("真实网关"), "#10B981");
        setLabelState(ui->label_zongxianzhuangtai, QStringLiteral("MQTT 链路在线"), "#10B981");
        setGatewayCommandsEnabled(true);
        return;
    }

    const bool connecting = (detail == QStringLiteral("连接中…"));
    setLabelState(m_gwConnLabel, detail, connecting ? "#F59E0B" : "#EF4444");
    setLabelState(ui->label_yunxingzhuangtai,
                  connecting ? QStringLiteral("网关连接中") : QStringLiteral("网关离线"),
                  connecting ? "#F59E0B" : "#EF4444");
    setLabelState(ui->label_zongxianzhuangtai,
                  connecting ? QStringLiteral("等待 MQTT") : QStringLiteral("网关离线"),
                  connecting ? "#F59E0B" : "#EF4444");
    setGatewayCommandsEnabled(false);
    setLabelState(m_gwNodeALabel, QStringLiteral("离线"), "#EF4444");
    setLabelState(m_gwNodeBLabel, QStringLiteral("离线"), "#EF4444");
}

void MainWindow::refreshNodeLabels()
{
    const bool realMode = ui->radiobtn_zhenshicheliang->isChecked();
    if (!realMode || m_gateway == nullptr) {
        setLabelState(m_gwNodeALabel, QStringLiteral("--"), "#94A3B8");
        setLabelState(m_gwNodeBLabel, QStringLiteral("--"), "#94A3B8");
        return;
    }
    const bool aOnline = m_gateway->isNodeOnline(GatewayClient::nodeNameCollect());
    const bool bOnline = m_gateway->isNodeOnline(GatewayClient::nodeNameControl());
    setLabelState(m_gwNodeALabel, aOnline ? QStringLiteral("在线") : QStringLiteral("离线"),
                  aOnline ? "#10B981" : "#EF4444");
    setLabelState(m_gwNodeBLabel, bOnline ? QStringLiteral("在线") : QStringLiteral("离线"),
                  bOnline ? "#10B981" : "#EF4444");
}

void MainWindow::setGatewayCommandsEnabled(bool enabled)
{
    for (QPushButton *btn : m_cmdButtons) {
        btn->setEnabled(enabled);
    }
    if (m_cmdNodeCombo != nullptr) {
        m_cmdNodeCombo->setEnabled(enabled);
    }
    if (m_cmdSpeedSpin != nullptr) {
        m_cmdSpeedSpin->setEnabled(enabled);
    }
    if (m_cmdTargetEdit != nullptr) {
        m_cmdTargetEdit->setEnabled(enabled);
    }
}

void MainWindow::updateOnlineRate()
{
    auto percentText = [](const QVector<int> &history) -> QString {
        if (history.isEmpty()) {
            return QStringLiteral("--");
        }
        int online = 0;
        for (int v : history) {
            online += v;
        }
        return QStringLiteral("%1%").arg(100.0 * online / history.size(), 0, 'f', 1);
    };

    if (m_onlineHistoryA.isEmpty() && m_onlineHistoryB.isEmpty()) {
        setLabelState(m_gwOnlineRateLabel, QStringLiteral("--"), "#94A3B8");
        return;
    }

    setValueLabel(m_gwOnlineRateLabel,
                  QStringLiteral("A %1 / B %2（%3 s 窗口）")
                      .arg(percentText(m_onlineHistoryA),
                           percentText(m_onlineHistoryB))
                      .arg(qMax(m_onlineHistoryA.size(), m_onlineHistoryB.size())));
}

// ---------------- 网关信号槽 ----------------

void MainWindow::onGatewayConnectionChanged(bool connected, const QString &detail)
{
    /* 已经切回仿真模式时，忽略迟到或自动重连的网关回调：
     * 不然看板状态和命令按钮会被一个"其实不该存在"的会话改掉。 */
    if (!ui->radiobtn_zhenshicheliang->isChecked()) {
        if (connected && m_gateway != nullptr) {
            m_gateway->disconnectBroker();   // 迟到的重连：立刻断掉
        }
        writeLog(true, false, QStringLiteral("网关链路：%1").arg(detail));
        return;
    }

    updateGatewayConnectionUi(connected, detail);

    if (connected) {
        writeLog(true, false,
                 QStringLiteral("CONNACK %1:%2 已连接，已订阅 6 条上行主题")
                     .arg(QString::fromLatin1(contract::kBrokerHost))
                     .arg(contract::kBrokerPort));
    } else if (detail != QStringLiteral("连接中…")) {
        writeLog(true, false, QStringLiteral("网关链路：%1").arg(detail));
    }
}

void MainWindow::onGatewayCollectTelemetry(const CollectNodeTelemetry &data)
{
    m_nodeACur = data;
    setValueLabel(m_gwIrLabel, QString::number(data.ir));
    setValueLabel(m_gwLdrLabel, QString::number(data.ldr));
    setValueLabel(m_gwSeqLabel, QString::number(data.seq));
    refreshNodeLabels();
}

void MainWindow::onGatewayControlTelemetry(const ControlNodeTelemetry &data)
{
    m_nodeBCur = data;
    setValueLabel(m_gwMotorLabel,
                  QStringLiteral("%1 · PWM %2%").arg(motorStateText(data.motor)).arg(data.pwm));
    setValueLabel(m_gwModeLabel, modeText(data.mode));
    setValueLabel(m_gwFlagsLabel, flagsText(data.flags));
    refreshNodeLabels();

    /* 通信时延：命令下发 → 下一帧 0x102（nodeB telemetry）到达的本地时间差。
     * 0x102 在节点侧是 1 Hz 周期上报，所以正常值应在 0~1000 ms 量级。 */
    if (m_pendingCommandMs > 0) {
        const qint64 delta = QDateTime::currentMSecsSinceEpoch() - m_pendingCommandMs;
        if (delta >= 0) {
            setValueLabel(m_gwLatencyLabel, QStringLiteral("%1 ms").arg(delta));
        }
        m_pendingCommandMs = -1;
    }
}

void MainWindow::onGatewayAlert(const GatewayAlert &alert)
{
    const QString nodeText = (alert.node == GatewayClient::nodeNameCollect())
                                 ? QStringLiteral("A")
                                 : QStringLiteral("B");
    const QString text = QStringLiteral("%1 · %2").arg(nodeText, alarmCodeName(alert.code));
    setLabelState(m_gwAlertLabel, text, "#F59E0B");
    if (m_gwAlertLabel != nullptr) {
        m_gwAlertLabel->setToolTip(
            QStringLiteral("node%1 code=%2(%3) extra=%4")
                .arg(nodeText)
                .arg(alert.code)
                .arg(alarmCodeName(alert.code))
                .arg(alert.extra));
    }
}

void MainWindow::onGatewayStatus(const GatewayStatusData &status)
{
    m_gwStatusCur = status;
    setLabelState(m_gwWifiLabel,
                  status.wifi ? QStringLiteral("已连接") : QStringLiteral("断开"),
                  status.wifi ? "#10B981" : "#EF4444");
    setLabelState(m_gwMqttLabel,
                  status.mqtt ? QStringLiteral("已连接") : QStringLiteral("断开"),
                  status.mqtt ? "#10B981" : "#EF4444");
    setValueLabel(m_gwCacheLabel, QString::number(status.cache));
    if (m_gwCacheLabel != nullptr) {
        m_gwCacheLabel->setToolTip(
            QStringLiteral("网关本地待发缓存条数（0x103.cache），越大说明下行/上行越拥塞"));
    }
}

void MainWindow::onGatewayStats(const GatewayStatsData &stats)
{
    m_gwStatsCur = stats;

    /* 丢帧率口径：a_lost / (a_rx + a_lost)，与网关 can.h 注释一致 */
    const double frameTotal = static_cast<double>(stats.aRx) + static_cast<double>(stats.aLost);
    const double lostRate = (frameTotal > 0.0)
                                ? 100.0 * static_cast<double>(stats.aLost) / frameTotal
                                : 0.0;
    setLabelState(m_gwLostRateLabel,
                  QStringLiteral("%1%（%2/%3）")
                      .arg(lostRate, 0, 'f', 2)
                      .arg(stats.aLost)
                      .arg(static_cast<quint32>(stats.aRx + stats.aLost)),
                  lostRate > 5.0 ? "#EF4444" : (lostRate > 1.0 ? "#F59E0B" : "#10B981"));

    /* 发布统计：pub_lost / pub_total */
    const double pubRate = (stats.pubTotal > 0)
                               ? 100.0 * static_cast<double>(stats.pubLost) / stats.pubTotal
                               : 0.0;
    setValueLabel(m_gwPubStatsLabel,
                  QStringLiteral("总 %1 · 丢 %2（%3%）")
                      .arg(stats.pubTotal)
                      .arg(stats.pubLost)
                      .arg(pubRate, 0, 'f', 2));
    if (m_gwPubStatsLabel != nullptr) {
        m_gwPubStatsLabel->setToolTip(
            QStringLiteral("a_rx=%1  a_lost=%2  pub_total=%3  pub_lost=%4")
                .arg(stats.aRx)
                .arg(stats.aLost)
                .arg(stats.pubTotal)
                .arg(stats.pubLost));
    }
}

void MainWindow::onGatewayNodeOnline(const QString &node, bool online)
{
    const bool isCollect = (node == GatewayClient::nodeNameCollect());
    QLabel *label = isCollect ? m_gwNodeALabel : m_gwNodeBLabel;
    setLabelState(label,
                  online ? QStringLiteral("在线") : QStringLiteral("离线"),
                  online ? "#10B981" : "#EF4444");
    writeLog(true, false,
             QStringLiteral("节点 %1 %2").arg(node, online ? QStringLiteral("上线") : QStringLiteral("离线")));
}

void MainWindow::onGatewayTraffic(bool isRx, const QString &topic, const QByteArray &payload)
{
    writeLog(isRx, false, QStringLiteral("%1  %2").arg(topic, QString::fromUtf8(payload)));

    /* nodeA 遥测是 10 Hz，日志列表必须设上限，否则跑一夜内存和滚动都受不了。
     * 只留最近 500 条，已经够联调回溯。 */
    const int kMaxLogItems = 500;
    while (ui->listWidgetmqtt->count() > kMaxLogItems) {
        delete ui->listWidgetmqtt->takeItem(0);
    }
}

void MainWindow::onGatewayTick()
{
    if (!ui->radiobtn_zhenshicheliang->isChecked() || m_gateway == nullptr) {
        return;
    }

    /* 在线率是"窗口内采样占比"：每秒采一次，最长 60 个采样点 */
    const bool aOnline = m_gateway->isNodeOnline(GatewayClient::nodeNameCollect());
    const bool bOnline = m_gateway->isNodeOnline(GatewayClient::nodeNameControl());
    m_onlineHistoryA.append(aOnline ? 1 : 0);
    m_onlineHistoryB.append(bOnline ? 1 : 0);
    while (m_onlineHistoryA.size() > 60) {
        m_onlineHistoryA.removeFirst();
    }
    while (m_onlineHistoryB.size() > 60) {
        m_onlineHistoryB.removeFirst();
    }
    updateOnlineRate();
    refreshNodeLabels();

    /* 命令发出去 5 s 还没等到下一帧 0x102 → 标超时，不要一直挂着"等待" */
    if (m_pendingCommandMs > 0) {
        const qint64 waited = QDateTime::currentMSecsSinceEpoch() - m_pendingCommandMs;
        if (waited > 5000) {
            m_pendingCommandMs = -1;
            setLabelState(m_gwLatencyLabel, QStringLiteral("超时（无 0x102）"), "#EF4444");
        }
    }

    /* 断线事件没到时兜底：连接断了就禁用命令 */
    if (!m_gateway->isConnected()) {
        setGatewayCommandsEnabled(false);
    }
}

// ---------------- 命令面板 ----------------

QString MainWindow::selectedCommandNode() const
{
    if (m_cmdNodeCombo == nullptr) {
        return GatewayClient::nodeNameControl();
    }
    const QString node = m_cmdNodeCombo->currentData().toString();
    return node.isEmpty() ? GatewayClient::nodeNameControl() : node;
}

void MainWindow::sendGatewayCommand(const QString &node, const QString &kind,
                                    const QByteArray &payload, const QString &desc)
{
    if (m_gateway == nullptr || !m_gateway->isConnected()) {
        writeLog(false, false, QStringLiteral("命令未发送（网关未连接）：%1").arg(desc));
        return;
    }
    if (!m_gateway->publishCommand(node, kind, payload)) {
        writeLog(false, false, QStringLiteral("命令发布失败：%1").arg(desc));
        return;
    }

    /* 记下发时刻，等 nodeB 的下一帧 0x102 来算时延 */
    m_pendingCommandMs = QDateTime::currentMSecsSinceEpoch();
    setLabelState(m_gwLatencyLabel, QStringLiteral("等待下一帧 0x102…"), "#F59E0B");
    /* TX 报文由 GatewayClient::traffic 统一写进 listWidgetmqtt */
}

void MainWindow::onCmdEmergencyStop()
{
    sendGatewayCommand(GatewayClient::nodeNameAll(),
                       QString::fromLatin1(contract::kCmdEstop),
                       QByteArray("1"),
                       QStringLiteral("急停（广播）"));
}

void MainWindow::onCmdEmergencyClear()
{
    sendGatewayCommand(GatewayClient::nodeNameAll(),
                       QString::fromLatin1(contract::kCmdEstop),
                       QByteArray("2"),
                       QStringLiteral("解除急停"));
}

void MainWindow::onCmdMotorStop()
{
    sendGatewayCommand(selectedCommandNode(),
                       QString::fromLatin1(contract::kCmdMotor),
                       QByteArray("0"),
                       QStringLiteral("电机停止"));
}

void MainWindow::onCmdMotorForward()
{
    sendGatewayCommand(selectedCommandNode(),
                       QString::fromLatin1(contract::kCmdMotor),
                       QByteArray("1"),
                       QStringLiteral("电机正转"));
}

void MainWindow::onCmdMotorReverse()
{
    sendGatewayCommand(selectedCommandNode(),
                       QString::fromLatin1(contract::kCmdMotor),
                       QByteArray("2"),
                       QStringLiteral("电机反转"));
}

void MainWindow::onCmdMotorSetSpeed()
{
    const int speed = (m_cmdSpeedSpin != nullptr) ? m_cmdSpeedSpin->value() : 0;
    /* 契约：motor 负载 = "cmd,arg"，cmd=3 设速，arg=0~100 */
    const QByteArray payload = QStringLiteral("3,%1").arg(speed).toLatin1();
    sendGatewayCommand(selectedCommandNode(),
                       QString::fromLatin1(contract::kCmdMotor),
                       payload,
                       QStringLiteral("电机设速 %1%").arg(speed));
}

void MainWindow::onCmdTargetSend()
{
    if (m_cmdTargetEdit == nullptr) {
        return;
    }
    bool ok = false;
    const int value = m_cmdTargetEdit->text().trimmed().toInt(&ok);
    if (!ok || value < 0 || value > 65535) {
        writeLog(false, false,
                 QStringLiteral("目标值无效（需要 0~65535 的整数）：%1")
                     .arg(m_cmdTargetEdit->text()));
        return;
    }
    sendGatewayCommand(selectedCommandNode(),
                       QString::fromLatin1(contract::kCmdTarget),
                       QByteArray::number(value),
                       QStringLiteral("目标值 %1").arg(value));
}

void MainWindow::onCmdModeManual()
{
    /* 0x332 只有 nodeB 支持（契约 commands.mode → CTRL_MODE_CONTROL） */
    sendGatewayCommand(GatewayClient::nodeNameControl(),
                       QString::fromLatin1(contract::kCmdMode),
                       QByteArray("0"),
                       QStringLiteral("手动模式"));
}

void MainWindow::onCmdModeAuto()
{
    sendGatewayCommand(GatewayClient::nodeNameControl(),
                       QString::fromLatin1(contract::kCmdMode),
                       QByteArray("1"),
                       QStringLiteral("自动模式"));
}



