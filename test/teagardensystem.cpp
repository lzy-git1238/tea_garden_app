#include "teagardensystem.h"
#include "chartdashboard.h"
#include "thresholddialog.h"
#include "datastore.h"
#include "pestdata.h"
#include "pestdashboard.h"
#include "iotuploader.h"

#include <QRandomGenerator>
#include <QDateTime>
#include <QtGlobal>
#include <QtMath>
#include <QSerialPort>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QCoreApplication>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialog>
#include <QtCharts/QChart>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QPixmap>
#include <QWebSocket>
#include <QDebug>
#include <QStyle>
using namespace QtCharts;
#include <QApplication>
#include <cmath>
#ifdef TEA_ENABLE_MAP_WEBVIEW
#include <QtWebEngineWidgets/QWebEngineView>
#endif

TeaGardenSystem::TeaGardenSystem(QWidget *parent) : QWidget(parent) {
    setupUI();

    // 网络（MJPEG 流 + AI 对话共用）
    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &TeaGardenSystem::onNetworkReply);

    setupMjpegStream();

    // 定时器：每3秒更新一次数据
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &TeaGardenSystem::refreshData);
    updateTimer->start(3000);

    bool ok = false;
    const int envBaud = qEnvironmentVariableIntValue("TEA_LORA_BAUD", &ok);
    const qint32 baudRate = ok ? static_cast<qint32>(envBaud) : QSerialPort::Baud9600;
    const QString portName = qEnvironmentVariable("TEA_LORA_PORT", "/dev/ttyUSB0");

    loraManager = new LoraManager(this);
    loraManager->setConnection(portName, baudRate);
    connect(loraManager, &LoraManager::sensorDataUpdated, this, &TeaGardenSystem::onLoraData);
    connect(loraManager, &LoraManager::statusChanged, this, &TeaGardenSystem::onLoraStatus);
    connect(loraManager, &LoraManager::parseError, this, &TeaGardenSystem::onLoraParseError);
    loraManager->start();

    // 初始化 SQLite 数据存储
    DataStore::instance()->init(QCoreApplication::applicationDirPath() + "/sensor_data.db");
    PestStore::instance()->init(QCoreApplication::applicationDirPath() + "/pest_data.db");
    IotUploader::instance()->init(QCoreApplication::applicationDirPath() + "/../IOT_DATA");

    // 初始化系统托盘
    trayIcon = new QSystemTrayIcon(this);
    QIcon trayIconFile(":/icons/tray.png");
    if (trayIconFile.isNull()) {
        trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    } else {
        trayIcon->setIcon(trayIconFile);
    }
    trayIcon->setToolTip("智慧茶园管理系统");
    auto *trayMenu = new QMenu(this);
    trayMenu->setStyleSheet("background: #0d1f2d; color: #E0E0E0;");
    trayMenu->addAction("显示主窗口", this, [this]() { show(); raise(); });
    trayMenu->addAction("数据看板", this, &TeaGardenSystem::openChartDashboard);
    trayMenu->addSeparator();
    trayMenu->addAction("退出", qApp, &QApplication::quit);
    trayIcon->setContextMenu(trayMenu);
    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::DoubleClick) { show(); raise(); }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon->show();
    }

    // 2. 初始化发送欢迎语
    appendChatMessage("管家", "您好！我是茶园AI管家。监测系统已就绪，当前各项指标稳定。");

    // 天气预警定时器（启动时立刻查一次，之后每30分钟）
    weatherTimer = new QTimer(this);
    connect(weatherTimer, &QTimer::timeout, this, &TeaGardenSystem::refreshWeatherAlert);
    weatherTimer->start(30 * 60 * 1000);
    refreshWeatherAlert();

    // 【临时验证】启动后自动触发一次病虫害上传，验证格式后删除
    QTimer::singleShot(3000, this, [this]() { injectMockPest(); });
}

// 【修复报错】明确定义析构函数
TeaGardenSystem::~TeaGardenSystem() {
    if (chartDashboard) {
        chartDashboard->close();
        delete chartDashboard;
    }
}

void TeaGardenSystem::setupUI() {
    this->setWindowTitle("智慧茶园综合管理系统 v2.0");
    this->resize(1200, 800);
    this->setStyleSheet("background-color: #051623; color: #E0E0E0; font-family: 'Microsoft YaHei';");

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(20);

    // 主题切换按钮（放在窗口标题旁，右栏顶部）
    themeBtn = new QPushButton("☀️ 日间");
    themeBtn->setFixedSize(90, 28);
    themeBtn->setCursor(Qt::PointingHandCursor);
    connect(themeBtn, &QPushButton::clicked, this, &TeaGardenSystem::toggleTheme);

    // --- A. 左侧：传感器看板 ---
    QVBoxLayout *leftPanel = new QVBoxLayout();
    leftPanelTitle = new QLabel("实时环境指标");
    leftPanel->addWidget(leftPanelTitle);
    loraStateLabel = new QLabel("LoRa: 初始化中...");
    loraStateLabel->setStyleSheet("color: #80CBC4; font-size: 12px;");
    leftPanel->addWidget(loraStateLabel);

    QStringList names = {"空气温度", "空气湿度", "光照强度", "土壤pH", "风速", "土壤温度", "土壤湿度", "土壤电导率"};
    QStringList units = {"℃", "%", "Lux", "", "m/s", "℃", "%", "mS/cm"};
    for(int i=0; i<names.size(); ++i) {
        leftPanel->addWidget(createDataCard(names[i], i, units[i]));
    }
    chartBtn = new QPushButton("📊 数据看板");
    chartBtn->setStyleSheet("background: #00897B; color: white; padding: 8px; "
                            "font-weight: bold; border-radius: 4px;");
    connect(chartBtn, &QPushButton::clicked, this, &TeaGardenSystem::openChartDashboard);
    leftPanel->addWidget(chartBtn);

    auto *pestBtn = new QPushButton("🐛 病虫害面板");
    pestBtn->setStyleSheet("background: #5D4037; color: white; padding: 8px; "
                            "font-weight: bold; border-radius: 4px;");
    connect(pestBtn, &QPushButton::clicked, this, &TeaGardenSystem::openPestDashboard);
    leftPanel->addWidget(pestBtn);

    auto *mockPestBtn = new QPushButton("🐛 模拟检测");
    mockPestBtn->setStyleSheet("background: #3E2723; color: #FF8A65; padding: 6px; "
                               "font-size: 11px; font-weight: bold; border-radius: 4px;"
                               "border-top: 1px solid #5D4037; border-bottom: 1px solid #1a0a08;");
    connect(mockPestBtn, &QPushButton::clicked, this, &TeaGardenSystem::injectMockPest);
    leftPanel->addWidget(mockPestBtn);

    // ── 天气预警卡片（立体3D） ──
    weatherCard = new QFrame();
    weatherCard->setFixedHeight(155);
    weatherCard->setStyleSheet(
        "QFrame#weatherCard {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #2a1a18, stop:0.4 #1e1018, stop:1 #100810);"
        "  border-top: 1px solid #603030; border-left: 1px solid #502828;"
        "  border-right: 1px solid #100808; border-bottom: 2px solid #080408;"
        "  border-radius: 8px;"
        "}");
    weatherCard->setObjectName("weatherCard");
    auto *wcLayout = new QVBoxLayout(weatherCard);
    wcLayout->setContentsMargins(10, 8, 10, 8);
    wcLayout->setSpacing(4);

    // 标题行
    auto *wcTitleRow = new QHBoxLayout();
    weatherTitleLabel = new QLabel("🌤 24h预报");
    weatherTitleLabel->setStyleSheet("color: #f0c0a0; font-size: 13px; font-weight: bold; border: none; background: transparent;");
    wcTitleRow->addWidget(weatherTitleLabel);
    wcTitleRow->addStretch();
    auto *wcRefreshBtn = new QPushButton("↻");
    wcRefreshBtn->setFixedSize(26, 26);
    wcRefreshBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #503828, stop:0.5 #382018, stop:1 #201008);"
        "  color: #f0c8a0; font-size: 14px; font-weight: bold; border-radius: 4px;"
        "  border-top: 1px solid #806040; border-bottom: 1px solid #100808;"
        "}"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #705040, stop:0.5 #503828, stop:1 #302018); color: #fff; }"
        "QPushButton:pressed { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #201008, stop:0.5 #382018, stop:1 #503828); padding-top: 1px; }");
    connect(wcRefreshBtn, &QPushButton::clicked, this, &TeaGardenSystem::refreshWeatherAlert);
    wcTitleRow->addWidget(wcRefreshBtn);
    wcLayout->addLayout(wcTitleRow);

    // 级别 + 类型
    weatherLevelLabel = new QLabel("加载中...");
    weatherLevelLabel->setStyleSheet("color: #FFC107; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    wcLayout->addWidget(weatherLevelLabel);

    // 详情
    weatherDetailLabel = new QLabel("");
    weatherDetailLabel->setWordWrap(true);
    weatherDetailLabel->setStyleSheet("color: #c0a090; font-size: 13px; border: none; background: transparent; padding: 3px 0;");
    wcLayout->addWidget(weatherDetailLabel);

    // 生效时间
    weatherTimeLabel = new QLabel("");
    weatherTimeLabel->setStyleSheet("color: #806860; font-size: 11px; border: none; background: transparent;");
    wcLayout->addWidget(weatherTimeLabel);

    // 点击卡片打开24h详情弹窗
    weatherCard->setCursor(Qt::PointingHandCursor);
    weatherCard->installEventFilter(this);

    leftPanel->addWidget(weatherCard);
    leftPanel->addStretch();
    mainLayout->addLayout(leftPanel, 2);

    // --- B. 中间：空间可视化 ---
    QVBoxLayout *midPanel = new QVBoxLayout();
    videoTitleLabel = new QLabel("茶园实景与空间分布");
    videoTitleLabel->setStyleSheet("font-weight: bold; color: #00E5FF;");

    videoLabel = new QLabel("【实时视频流：等待 MJPEG 连接...】");
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setStyleSheet("background: black; border: 2px solid #1a2a3a; min-height: 180px; color: #4CAF50;");

    midPanel->addWidget(videoTitleLabel);
    midPanel->addWidget(videoLabel, 2);
    setupMapView(midPanel);
    mainLayout->addLayout(midPanel, 6);

    // --- C. 右侧：AI 管家聊天 ---
    QVBoxLayout *rightPanel = new QVBoxLayout();

    // 标题行
    auto *rightTopRow = new QHBoxLayout();
    aiTitleLabel = new QLabel("AI 智慧管家分析");
    aiTitleLabel->setStyleSheet("font-size: 16px; color: #00E5FF; font-weight: bold;");
    rightTopRow->addWidget(aiTitleLabel);
    rightTopRow->addStretch();
    rightTopRow->addWidget(themeBtn);
    rightPanel->addLayout(rightTopRow);

    // ── 病虫害饼图（嵌入式） ──
    auto *pestHeaderRow = new QHBoxLayout();
    auto *pestPieTitle = new QLabel("🐛 病虫害统计");
    pestPieTitle->setStyleSheet("font-size: 13px; color: #FF8A65; font-weight: bold;");
    pestHeaderRow->addWidget(pestPieTitle);
    pestHeaderRow->addStretch();
    auto *pestDetailBtn = new QPushButton("详情 ▶");
    pestDetailBtn->setStyleSheet(
        "QPushButton { background: #3E2723; color: #FF8A65; font-size: 11px; padding: 2px 8px; "
        "border: 1px solid #5D4037; border-radius: 4px; }"
        "QPushButton:hover { background: #4E332E; }");
    connect(pestDetailBtn, &QPushButton::clicked, this, &TeaGardenSystem::openPestDashboard);
    pestHeaderRow->addWidget(pestDetailBtn);
    rightPanel->addLayout(pestHeaderRow);

    auto *pestChart = new QChart();
    pestChart->setBackgroundBrush(QBrush(QColor("#051623")));
    pestChart->legend()->setLabelColor(QColor("#90A4AE"));
    pestChart->legend()->setAlignment(Qt::AlignBottom);
    pestChart->setMargins(QMargins(0, 0, 0, 0));
    pestPieView = new QChartView(pestChart);
    pestPieView->setRenderHint(QPainter::Antialiasing);
    pestPieView->setMinimumHeight(240);
    pestPieView->setStyleSheet("background: transparent; border: 1px solid #2a1a1a; border-radius: 4px;");
    rightPanel->addWidget(pestPieView);

    // 聊天区（缩小）
    chatDisplay = new QTextEdit();
    chatDisplay->setReadOnly(true);
    chatDisplay->setMinimumHeight(80);
    chatDisplay->setStyleSheet("background: rgba(0,0,0,0.4); border: 1px solid #334455; border-radius: 4px; padding: 10px;");

    QHBoxLayout *inputBox = new QHBoxLayout();
    chatInput = new QLineEdit();
    chatInput->setAttribute(Qt::WA_InputMethodEnabled, true);
    chatInput->setPlaceholderText("询问管理建议...");
    chatInput->setStyleSheet("padding: 8px; background: #1a2a3a; color: white; border: 1px solid #334455;");

    voiceBtn = new QPushButton("🎤 语音");
    voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
    connect(voiceBtn, &QPushButton::clicked, this, &TeaGardenSystem::startVoiceRecording);

    sendBtn = new QPushButton("发送");
    sendBtn->setStyleSheet("background: #00897B; padding: 8px 15px; font-weight: bold;");

    connect(sendBtn, &QPushButton::clicked, this, &TeaGardenSystem::handleChat);
    connect(chatInput, &QLineEdit::returnPressed, this, &TeaGardenSystem::handleChat);

    inputBox->addWidget(chatInput);
    inputBox->addWidget(voiceBtn);
    inputBox->addWidget(sendBtn);

    rightPanel->addWidget(chatDisplay, 1);
    rightPanel->addLayout(inputBox);
    mainLayout->addLayout(rightPanel, 3);
}

void TeaGardenSystem::setupMjpegStream() {
    const QString streamUrl = qEnvironmentVariable("TEA_MJPEG_URL", "ws://100.108.217.12:8080/ws");

    videoSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    videoSocket->open(QUrl(streamUrl));

    connect(videoSocket, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
        QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
        if (doc.isNull()) return;
        QJsonObject obj = doc.object();
        QString b64 = obj["image_b64"].toString();
        if (b64.isEmpty()) return;

        QByteArray jpegData = QByteArray::fromBase64(b64.toUtf8());
        QImage frame = QImage::fromData(jpegData, "JPEG");
        if (frame.isNull()) return;

        if (videoLabel) {
            QSize sz = videoLabel->size();
            if (sz.width() < 10) sz = QSize(640, 360);
            const QPixmap pix = QPixmap::fromImage(frame);
            videoLabel->setPixmap(pix.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            videoLabel->setText(QString());
        }
    });

    connect(videoSocket, &QWebSocket::disconnected, this, [this, streamUrl]() {
        qWarning() << "WebSocket video disconnected, reconnecting in 3s...";
        appendChatMessage("系统", "视频流断开，3 秒后重连...");
        QTimer::singleShot(3000, this, [this]() {
            setupMjpegStream();
        });
    });

    connect(videoSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, [this](QAbstractSocket::SocketError) {
        qWarning() << "WebSocket error:" << videoSocket->errorString();
    });

    appendChatMessage("系统", "WebSocket 视频流已连接: " + streamUrl);
}

void TeaGardenSystem::setupMapView(QVBoxLayout *midPanel) {
#ifdef TEA_ENABLE_MAP_WEBVIEW
    QFile htmlFile(":/web/amap_view.html");
    if (!htmlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QLabel *fallback = new QLabel("Map page load failed: :/web/amap_view.html");
        fallback->setAlignment(Qt::AlignCenter);
        fallback->setMinimumHeight(350);
        fallback->setStyleSheet("background: #1a2a3a; border: 1px solid #334455; color: #ff8a80;");
        midPanel->addWidget(fallback);
        return;
    }

    QString html = QString::fromUtf8(htmlFile.readAll());
    htmlFile.close();

    // Token: 优先 TEA_GEOVIS_TOKEN 环境变量，回退到天气 Token
    const QString token = qEnvironmentVariable("TEA_GEOVIS_TOKEN",
                           qEnvironmentVariable("TEA_WEATHER_TOKEN", ""));
    const QString centerLng = qEnvironmentVariable("TEA_MAP_LNG", "118.232478");
    const QString centerLat = qEnvironmentVariable("TEA_MAP_LAT", "25.08039");
    const QString zoom = qEnvironmentVariable("TEA_MAP_ZOOM", "15");

    html.replace("__GEOVIS_TOKEN__", token.toHtmlEscaped());
    html.replace("__CENTER_LNG__", centerLng.toHtmlEscaped());
    html.replace("__CENTER_LAT__", centerLat.toHtmlEscaped());
    html.replace("__ZOOM__", zoom.toHtmlEscaped());

    mapView = new QWebEngineView(this);
    mapView->setMinimumHeight(460);
    mapView->setStyleSheet("border: 1px solid #334455; border-radius: 8px;");
    static_cast<QWebEngineView *>(mapView)->setHtml(html, QUrl("https://api.open.geovisearth.com/"));
    midPanel->addWidget(mapView, 5);
#else
    QLabel *fallback = new QLabel("Qt WebEngine module missing. Install qtwebengine5-dev to enable Map.");
    fallback->setAlignment(Qt::AlignCenter);
    fallback->setMinimumHeight(460);
    fallback->setStyleSheet("background: #1a2a3a; border: 1px solid #334455; color: #ff8a80;");
    midPanel->addWidget(fallback, 5);
#endif
}

QWidget* TeaGardenSystem::createDataCard(QString title, int index, QString unit) {
    // 【修复警告】使用 Q_UNUSED 忽略暂时不用的 index 参数
    Q_UNUSED(index);

    QFrame *card = new QFrame();
    card->setStyleSheet("background: rgba(255,255,255,0.05); border: 1px solid #1a2a3a; border-radius: 8px;");
    card->setFixedHeight(80);
    dataCards.append(card);

    QVBoxLayout *vbox = new QVBoxLayout(card);
    QLabel *tLbl = new QLabel(title);
    tLbl->setStyleSheet("color: #909399; font-size: 12px;");
    dataCardTitles.append(tLbl);

    QLabel *vLbl = new QLabel("-- " + unit);
    vLbl->setStyleSheet("color: #00E5FF; font-size: 22px; font-weight: bold;");

    valueLabels.append(vLbl);
    vbox->addWidget(tLbl);
    vbox->addWidget(vLbl);
    return card;
}

void TeaGardenSystem::refreshData() {
    if(lastLoraFrameTime.isValid() && lastLoraFrameTime.msecsTo(QDateTime::currentDateTime()) < 10000) {
        return;
    }

    curTemp = 15.0 + QRandomGenerator::global()->bounded(1500)/100.0;
    curHumi = 30.0 + QRandomGenerator::global()->bounded(5000)/100.0;
    curLight = 800 + QRandomGenerator::global()->bounded(3000);
    curPh = 4.5 + QRandomGenerator::global()->bounded(25) / 10.0;
    curWindSpeed = QRandomGenerator::global()->bounded(100) / 10.0;
    curSoilTemp = 10.0 + QRandomGenerator::global()->bounded(150) / 10.0;
    curSoilHumi = 20 + QRandomGenerator::global()->bounded(60);
    curSoilEc = 0.5 + QRandomGenerator::global()->bounded(20) / 10.0;

    valueLabels[0]->setText(QString::number(curTemp, 'f', 2) + " ℃");
    valueLabels[1]->setText(QString::number(curHumi, 'f', 2) + " %");
    valueLabels[2]->setText(QString::number(curLight) + " Lux");
    valueLabels[3]->setText(QString::number(curPh, 'f', 2));
    valueLabels[4]->setText(QString::number(curWindSpeed, 'f', 2) + " m/s");
    valueLabels[5]->setText(QString::number(curSoilTemp, 'f', 2) + " ℃");
    valueLabels[6]->setText(QString::number(curSoilHumi, 'f', 2) + " %");
    valueLabels[7]->setText(QString::number(curSoilEc, 'f', 2) + " mS/cm");

    if (loraStateLabel && loraManager && loraManager->isRunning()) {
        loraStateLabel->setText("LoRa: 数据超时，暂用模拟数据");
    }

    // 模拟数据也写入 SQLite
    SensorSnapshot snap2;
    snap2.timestamp = QDateTime::currentDateTime();
    snap2.airTemp = curTemp;
    snap2.airHumidity = curHumi;
    snap2.lightLux = curLight;
    snap2.soilPh = curPh;
    snap2.windSpeed = curWindSpeed;
    snap2.soilTemp = curSoilTemp;
    snap2.soilHumidity = curSoilHumi;
    snap2.soilConductivity = curSoilEc;
    DataStore::instance()->append(snap2);

    // 模拟数据也上报 IoT
    QJsonObject iotData2;
    iotData2["air_temp"] = curTemp;
    iotData2["air_hum"]  = curHumi;
    iotData2["light_intensity"] = static_cast<double>(curLight);
    iotData2["wind_speed"] = curWindSpeed;
    iotData2["soli_temp"] = curSoilTemp;
    iotData2["soli_hum"]  = curSoilHumi;
    iotData2["soli_ec"]   = curSoilEc;
    IotUploader::instance()->upload(iotData2);
}

void TeaGardenSystem::handleChat() {
    QString msg = chatInput->text().trimmed();
    if(msg.isEmpty()) return;

    appendChatMessage("用户", msg);
    chatInput->clear();

    requestAIAnalysis(msg);
}

void TeaGardenSystem::startVoiceRecording() {
    // ---- 如果正在录音，点击则立即停止 ----
    if (recordProcess && recordProcess->state() != QProcess::NotRunning) {
        appendChatMessage("系统", "手动停止录音，开始识别...");
        recordProcess->terminate();  // 发送 SIGTERM，arecord 会正常写入 WAV 尾部
        // 给 500ms 让 arecord 优雅退出，否则强杀
        QTimer::singleShot(500, this, [this]() {
            if (recordProcess && recordProcess->state() != QProcess::NotRunning) {
                recordProcess->kill();
            }
        });
        return;
    }

    // ---- 开始新录音 ----
    // 主录音文件（覆盖写）
    pendingWavPath = QDir::homePath() + "/Desktop/current_voice_cmd.wav";
    // 带时间戳的备份文件，方便事后回放检查麦克风是否正常
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupPath = QDir::homePath() + QString("/Desktop/voice_backup_%1.wav").arg(timestamp);

    recordProcess = new QProcess(this);

    // arecord 不加 -d，改为手动停止；也可保留 -d 10 作为最长录音保护
    QString program = "arecord";
    QStringList arguments;
    arguments << "-d" << "10"   // 最长10秒保护，防止忘记停止
              << "-f" << "S16_LE" << "-c" << "1" << "-r" << "16000" << pendingWavPath;

    // 捕获 arecord 的 stderr（设备打开失败等错误会在这里）
    connect(recordProcess, &QProcess::readyReadStandardError, this, [this]() {
        QString err = QString::fromUtf8(recordProcess->readAllStandardError()).trimmed();
        if (err.isEmpty()) return;
        qDebug() << "[arecord stderr]" << err;
        // 过滤掉手动停止时 arecord 打印的正常中断信息，避免误导用户
        if (err.contains("被中断的系统调用") ||
            err.contains("Interrupted system call") ||
            err.contains("被信号已终止") ||
            err.contains("Terminated")) {
            return;
        }
        appendChatMessage("系统", "[录音设备] " + err);
    });

    connect(recordProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, backupPath](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus);

        // 恢复按钮状态
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        voiceBtn->setEnabled(true);

        recordProcess->deleteLater();
        recordProcess = nullptr;

        // exitCode=0  : arecord 正常录满时长后退出
        // exitCode=1  : arecord 被 SIGTERM/SIGINT 中断（手动停止），WAV 文件仍然有效
        // exitCode=其他: 真正的错误（设备打不开等）
        // 注意：只要文件存在且大于 WAV 头部大小(44字节)，就认为录音有效
        if (exitCode != 0 && exitCode != 1) {
            appendChatMessage("系统", QString("录音失败！arecord 退出码: %1，请检查麦克风是否连接。").arg(exitCode));
            return;
        }

        if (!QFile::exists(pendingWavPath)) {
            appendChatMessage("系统", "录音文件未生成，请检查麦克风权限。");
            return;
        }

        // 备份录音文件，方便事后用 aplay 回放验证
        QFile::copy(pendingWavPath, backupPath);
        qDebug() << "[录音] 已保存备份到" << backupPath;
        appendChatMessage("系统", QString("录音完成，备份: %1").arg(backupPath));

        voiceBtn->setEnabled(false);
        voiceBtn->setText("识别中...");
        voiceBtn->setStyleSheet("background: #2196F3; padding: 8px 15px; font-weight: bold; color: white;");
        onVoiceRecorded(pendingWavPath);
    });

    voiceBtn->setText("⏹ 停止");
    voiceBtn->setStyleSheet("background: #F44336; padding: 8px 15px; font-weight: bold; color: white;");
    voiceBtn->setEnabled(true);  // 保持可点击，用于手动停止

    appendChatMessage("系统", "请说话 (最长10秒，再次点击可提前停止)...");
    recordProcess->start(program, arguments);
}

void TeaGardenSystem::onVoiceRecorded(const QString &wavFilePath) {
    if (!QFile::exists(wavFilePath)) {
        appendChatMessage("系统", "录音失败，未找到音频文件。");
        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        return;
    }

    QProcess *process = new QProcess(this);

    // 1. 设置 Python 虚拟环境的绝对路径
    QString program = "/home/elf/SenseVoiceSmall-RKNN2/sense_env/bin/python";

    // 2. 设置脚本路径和参数
    QStringList arguments;
    arguments << "/home/elf/SenseVoiceSmall-RKNN2/sensevoice_rknn.py"
              << "-a" << wavFilePath;

    // 关键修复：将 stderr 合并到 stdout，统一在 finished 后一次性读取
    // 避免 readyReadStandardOutput 流式触发导致数据不完整
    process->setProcessChannelMode(QProcess::SeparateChannels);

    // 进程结束后一次性读取全部输出，确保数据完整
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [process, this](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus);

        // 读取 stderr（RKNN 日志）仅用于调试
        QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        if (!stderrText.isEmpty()) {
            qDebug() << "[SenseVoice stderr]" << stderrText;
        }

        // 读取 stdout（识别结果）
        QString stdoutText = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        qDebug() << "[SenseVoice stdout 原始]" << stdoutText;

        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");

        if (exitCode != 0) {
            // 识别失败时把完整的 stderr 也显示出来，方便定位是模型加载失败还是其他原因
            QString errDetail = stderrText.isEmpty() ? "(无错误输出)" : stderrText.left(300);
            appendChatMessage("系统",
                QString("语音识别失败！退出码: %1\n--- Python 错误信息 ---\n%2")
                    .arg(exitCode).arg(errDetail));
            process->deleteLater();
            return;
        }

        // 逐行过滤 RKNN 日志，提取真正的识别文本
        QStringList lines = stdoutText.split('\n', Qt::SkipEmptyParts);
        QString result;
        for (int i = lines.size() - 1; i >= 0; --i) {
            QString line = lines.at(i).trimmed();
            if (!line.startsWith("W RKNN:") &&
                !line.startsWith("I RKNN:") &&
                !line.startsWith("E RKNN:") &&
                !line.isEmpty()) {
                result = line;
                break;
            }
        }

        qDebug() << "[SenseVoice 识别结果]" << result;

        if (!result.isEmpty()) {
            appendChatMessage("用户(语音)", result);
            requestAIAnalysis(result);
        } else {
            appendChatMessage("系统", "未能识别到有效语音，请重试。");
        }

        process->deleteLater();
    });

    // 启动进程
    process->start(program, arguments);
}

void TeaGardenSystem::requestAIAnalysis(const QString &userMessage) {
    // 构造请求数据
    QJsonObject requestData;
    requestData["model"] = "deepseek-chat"; // 或者您使用的其他模型名称
    
    QJsonArray messages;
    
    // 系统提示词，包含当前环境数据
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    QString systemPrompt = QString("你是一个专业的茶园AI管家。当前茶园环境数据如下：\n"
                                   "空气温度: %1 ℃\n"
                                   "空气湿度: %2 %\n"
                                   "光照强度: %3 Lux\n"
                                   "土壤pH: %4\n"
                                   "风速: %5 m/s\n"
                                   "土壤温度: %6 ℃\n"
                                   "土壤湿度: %7 %\n"
                                   "土壤电导率: %8 mS/cm\n"
                                   "请根据这些数据和用户的提问，给出专业的茶园管理建议。回答要简明扼要。")
                               .arg(curTemp).arg(curHumi).arg(curLight).arg(curPh)
                               .arg(curWindSpeed).arg(curSoilTemp).arg(curSoilHumi).arg(curSoilEc);
    systemMsg["content"] = systemPrompt;
    messages.append(systemMsg);
    
    // 用户消息
    QJsonObject userMsgObj;
    userMsgObj["role"] = "user";
    userMsgObj["content"] = userMessage;
    messages.append(userMsgObj);
    
    requestData["messages"] = messages;
    requestData["temperature"] = 0.7;

    QJsonDocument doc(requestData);
    QByteArray postData = doc.toJson();

    QString apiUrl = qEnvironmentVariable("TEA_AI_API_URL", "https://api.deepseek.com/v1/chat/completions");
    QString apiKey = qEnvironmentVariable("TEA_AI_API_KEY");
    if (apiKey.isEmpty()) {
        appendChatMessage("系统", "未配置 TEA_AI_API_KEY 环境变量，AI 对话不可用。");
        return;
    }

    QNetworkRequest request((QUrl(apiUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    networkManager->post(request, postData);
    appendChatMessage("管家", "正在思考中...");
}

void TeaGardenSystem::onNetworkReply(QNetworkReply *reply) {

    // 24h逐时预报回复
    if (reply == weatherReply) {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj["status"].toInt() == 0 && obj.contains("result") && obj["result"].isObject()) {
                    // 保存地点名称
                    if (obj.contains("location") && obj["location"].isObject()) {
                        weatherLocationPath = obj["location"].toObject()["path"].toString();
                        // 格式化：广东省,珠海市,斗门 → 珠海·斗门
                        QStringList parts = weatherLocationPath.split(",");
                        if (parts.size() >= 3)
                            weatherTitleLabel->setText(QString("🌤 24h预报 · %1·%2").arg(parts[1], parts[2]));
                        else if (parts.size() >= 2)
                            weatherTitleLabel->setText(QString("🌤 24h预报 · %1").arg(parts[1]));
                        else if (!weatherLocationPath.isEmpty())
                            weatherTitleLabel->setText("🌤 24h预报 · " + weatherLocationPath);
                    }
                    QJsonObject result = obj["result"].toObject();
                    QJsonArray datas = result["datas"].toArray();
                    weatherForecastData = datas;  // 缓存用于弹窗
                    if (datas.isEmpty()) {
                        weatherLevelLabel->setText("暂无预报数据");
                        weatherLevelLabel->setStyleSheet("color: #FF9800; font-size: 18px; font-weight: bold; border: none; background: transparent;");
                        weatherDetailLabel->setText("");
                        weatherTimeLabel->setText("");
                    } else {
                        // 取第一条（最近一小时）
                        QJsonObject first = datas[0].toObject();
                        QString wp      = first["wp"].toString();           // 天气现象: 晴/多云/雨...
                        double tem      = first["tem"].toDouble();          // 预报温度
                        double real_tem = first["real_tem"].toDouble();     // 体感温度
                        int rh          = first["rh"].toInt();              // 湿度
                        QString wd_desc = first["wd_desc"].toString();      // 风向
                        double ws       = first["ws"].toDouble();           // 风速
                        QString ws_desc = first["ws_desc"].toString();      // 风力等级
                        double pre      = first["pre"].toDouble();          // 降水量

                        // 计算全天温度范围
                        double dayMin = 99, dayMax = -99;
                        for (const auto &d : datas) {
                            double t = d.toObject()["tem"].toDouble();
                            if (t < dayMin) dayMin = t;
                            if (t > dayMax) dayMax = t;
                        }

                        // 天气图标
                        QString icon = "☀️";
                        if (wp.contains("云"))     icon = "⛅";
                        if (wp.contains("阴"))     icon = "☁️";
                        if (wp.contains("雨"))     icon = "🌧";
                        if (wp.contains("雪"))     icon = "🌨";
                        if (wp.contains("雷"))     icon = "⛈";

                        weatherLevelLabel->setText(
                            QString("%1 %2  %3℃").arg(icon, wp).arg(tem, 0, 'f', 0));
                        weatherLevelLabel->setStyleSheet(
                            "color: #4FC3F7; font-size: 18px; font-weight: bold; border: none; background: transparent;");

                        weatherDetailLabel->setText(
                            QString("体感 %1℃ | 湿度 %2% | %3 %4 %5m/s\n"
                                    "降水量 %6mm | 气温 %7~%8℃")
                                .arg(real_tem, 0, 'f', 0).arg(rh)
                                .arg(wd_desc, ws_desc).arg(ws, 0, 'f', 1)
                                .arg(pre, 0, 'f', 1)
                                .arg(dayMin, 0, 'f', 0).arg(dayMax, 0, 'f', 0));

                        weatherTimeLabel->setText(
                            QString("更新: %1  |  %2小时逐时预报")
                                .arg(obj["date"].toObject()["time"].toString(),
                                     QString::number(datas.size())));
                    }
                }
            }
        } else {
            weatherLevelLabel->setText("⚠ 天气服务不可用");
            weatherLevelLabel->setStyleSheet("color: #FF9800; font-size: 16px; font-weight: bold; border: none; background: transparent;");
            weatherDetailLabel->setText(reply->errorString());
            weatherTimeLabel->setText("");
        }
        weatherReply->deleteLater();
        weatherReply = nullptr;
        return;
    }

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("choices") && obj["choices"].isArray()) {
                QJsonArray choices = obj["choices"].toArray();
                if (!choices.isEmpty()) {
                    QJsonObject firstChoice = choices[0].toObject();
                    if (firstChoice.contains("message") && firstChoice["message"].isObject()) {
                        QJsonObject message = firstChoice["message"].toObject();
                        if (message.contains("content")) {
                            QString aiResponse = message["content"].toString();
                            // 移除之前的“正在思考中...”消息（如果需要，这里简单追加）
                            appendChatMessage("管家", aiResponse);
                        }
                    }
                }
            }
        }
    } else {
        appendChatMessage("系统", "AI 请求失败: " + reply->errorString());
    }
    reply->deleteLater();
}

void TeaGardenSystem::onLoraData(double windSpeed,
                                 double airTemp,
                                 double airHumidity,
                                 double lightLux,
                                 double soilHumidity,
                                 double soilTemp,
                                 double soilConductivity,
                                 double soilPh,
                                 const QDateTime &timestamp) {
    Q_UNUSED(timestamp);

    if (qIsFinite(windSpeed)) {
        curWindSpeed = qMax(0.0, windSpeed);
    }
    if (qIsFinite(airTemp)) {
        curTemp = airTemp;
    }
    if (qIsFinite(airHumidity)) {
        curHumi = qBound(0.0, airHumidity, 100.0);
    }
    if (qIsFinite(lightLux)) {
        curLight = qMax(0, static_cast<int>(qRound(lightLux)));
    }
    if (qIsFinite(soilPh)) {
        curPh = qBound(0.0, soilPh, 14.0);
    }
    if (qIsFinite(soilTemp)) {
        curSoilTemp = soilTemp;
    }
    if (qIsFinite(soilHumidity)) {
        curSoilHumi = qBound(0.0, soilHumidity, 100.0);
    }
    if (qIsFinite(soilConductivity)) {
        curSoilEc = qMax(0.0, soilConductivity);
    }

    lastLoraFrameTime = QDateTime::currentDateTime();

    // 写入 SQLite
    SensorSnapshot snap;
    snap.timestamp = QDateTime::currentDateTime();
    snap.windSpeed = curWindSpeed;
    snap.airTemp = curTemp;
    snap.airHumidity = curHumi;
    snap.lightLux = curLight;
    snap.soilPh = curPh;
    snap.soilTemp = curSoilTemp;
    snap.soilHumidity = curSoilHumi;
    snap.soilConductivity = curSoilEc;
    DataStore::instance()->append(snap);

    // 上传到华为云 IoT
    QJsonObject iotData;
    auto addField = [&](const char *key, double val) {
        if (std::isfinite(val)) iotData[key] = val;
    };
    addField("air_temp", curTemp);
    addField("air_hum", curHumi);
    addField("light_intensity", static_cast<double>(curLight));
    addField("wind_speed", curWindSpeed);
    addField("soli_temp", curSoilTemp);
    addField("soli_hum", curSoilHumi);
    addField("soli_ec", curSoilEc);
    IotUploader::instance()->upload(iotData);

    // 阈值超限通知
    QMap<int, ThresholdDialog::Thresholds> thresholds = ThresholdDialog::load();
    auto checkThreshold = [&](int index, double value, const QString &name, const QString &unit) {
        if (!std::isfinite(value)) return;
        ThresholdDialog::Thresholds th = thresholds.value(index);
        if (value > th.upper || value < th.lower) {
            QDateTime now = QDateTime::currentDateTime();
            if (!lastAlertTime.contains(index) || lastAlertTime[index].secsTo(now) > 60) {
                lastAlertTime[index] = now;
                QString msg = QString("%1 %2%3").arg(name)
                    .arg(value, 0, 'f', 1).arg(unit);
                QString detail = QString("阈值范围: %1 ~ %2").arg(th.lower).arg(th.upper);
                trayIcon->showMessage("⚠ 阈值告警", msg + "\n" + detail,
                                      QSystemTrayIcon::Warning, 5000);
            }
        }
    };
    checkThreshold(0, curTemp,      "空气温度",   "℃");
    checkThreshold(1, curHumi,      "空气湿度",   "%");
    checkThreshold(2, curLight,     "光照强度",   "Lux");
    checkThreshold(3, curPh,        "土壤pH",     "");
    checkThreshold(4, curWindSpeed, "风速",       "m/s");
    checkThreshold(5, curSoilTemp,  "土壤温度",   "℃");
    checkThreshold(6, curSoilHumi,  "土壤湿度",   "%");
    checkThreshold(7, curSoilEc,    "土壤电导率", "mS/cm");

    valueLabels[0]->setText(QString::number(curTemp, 'f', 2) + " ℃");
    valueLabels[1]->setText(QString::number(curHumi, 'f', 2) + " %");
    valueLabels[2]->setText(QString::number(curLight) + " Lux");
    valueLabels[3]->setText(qIsFinite(curPh) ? QString::number(curPh, 'f', 2) : "--");
    valueLabels[4]->setText(QString::number(curWindSpeed, 'f', 2) + " m/s");
    valueLabels[5]->setText(QString::number(curSoilTemp, 'f', 2) + " ℃");
    valueLabels[6]->setText(QString::number(curSoilHumi, 'f', 2) + " %");
    valueLabels[7]->setText(QString::number(curSoilEc, 'f', 2) + " mS/cm");

    if (loraStateLabel) {
        loraStateLabel->setText("LoRa: 已接收实时数据");
    }
}

void TeaGardenSystem::onLoraStatus(const QString &status) {
    if (loraStateLabel) {
        loraStateLabel->setText(QString("LoRa: %1").arg(status));
    }
}

void TeaGardenSystem::onLoraParseError(const QString &errorText) {
    // 串口通信中偶尔会出现截断或粘包的残缺数据，频繁在聊天框告警会打扰用户。
    // 这里改为只在控制台/日志中输出，不再显示到 UI 聊天框。
    qWarning() << "LoRa Parse Error:" << errorText;
}

void TeaGardenSystem::appendChatMessage(QString role, QString message) {
    QString color = (role == "管家") ? "#00E5FF" : "#FFA000";
    QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
    chatDisplay->append(QString("<b style='color:%1;'>%2 [%3]:</b><br>%4<br>").arg(color, role, time, message));
}

void TeaGardenSystem::openChartDashboard() {
    if (!chartDashboard) {
        chartDashboard = new ChartDashboard();
    }
    chartDashboard->show();
    chartDashboard->raise();
}

void TeaGardenSystem::toggleTheme() {
    isDarkMode = !isDarkMode;
    themeBtn->setText(isDarkMode ? "☀️ 日间" : "🌙 夜间");
    applyTheme();
}

void TeaGardenSystem::applyTheme() {
    // ══════ 颜色色板 ══════
    QString bg, fg, cardBg, border, muted, accent, accent2;
    QString inputBg, chatBg, loraColor, chartBtnBg, sendBtnBg;
    QString wcBg1, wcBg2, wcBg3, wcBorder1, wcBorder2;
    QString wcTitle, wcLevel, wcDetail, wcTime, wcRefreshBg1, wcRefreshBg2, wcRefreshBorder;
    QString tbBg, tbColor, tbBorder, tbHoverBg, tbHoverColor;
    QString leftTitleClr, videoTitleClr, aiTitleClr;

    if (isDarkMode) {
        bg          = "#051623";  fg          = "#E0E0E0";
        cardBg      = "rgba(255,255,255,0.05)";  border = "#1a2a3a";
        muted       = "#90A4AE";  accent      = "#00E5FF";  accent2   = "#00897B";
        inputBg     = "#1a2a3a";  chatBg      = "rgba(0,0,0,0.4)";
        loraColor   = "#80CBC4";  chartBtnBg  = "#00897B";  sendBtnBg = "#00897B";
        // 天气卡片
        wcBg1 = "#2a1a18";  wcBg2 = "#1e1018";  wcBg3 = "#100810";
        wcBorder1 = "#603030";  wcBorder2 = "#100808";
        wcTitle = "#f0c0a0";  wcLevel = "#4FC3F7";  wcDetail = "#c0a090";  wcTime = "#806860";
        wcRefreshBg1 = "#503828";  wcRefreshBg2 = "#201008";  wcRefreshBorder = "#806040";
        // 主题按钮
        tbBg = "#3a5068";  tbColor = "#c0d8f0";  tbBorder = "#506880";
        tbHoverBg = "#4a6080";  tbHoverColor = "#fff";
        // 标题
        leftTitleClr = "#E0E0E0";  videoTitleClr = "#00E5FF";  aiTitleClr = "#00E5FF";
    } else {
        bg          = "#Faf6f0";  fg          = "#3e3028";
        cardBg      = "rgba(0,0,0,0.04)";  border = "#c8b898";
        muted       = "#8c7a6a";  accent      = "#2e7d32";  accent2   = "#43a047";
        inputBg     = "#ffffff";  chatBg      = "rgba(0,0,0,0.04)";
        loraColor   = "#558b2f";  chartBtnBg  = "#43a047";  sendBtnBg = "#43a047";
        // 天气卡片 — 暖金色纸质卡片
        wcBg1 = "#f5ecd7";  wcBg2 = "#ede0c0";  wcBg3 = "#e0d0a8";
        wcBorder1 = "#c8b878";  wcBorder2 = "#a09060";
        wcTitle = "#5a4020";  wcLevel = "#2e7d32";  wcDetail = "#6a5040";  wcTime = "#8c7a6a";
        wcRefreshBg1 = "#e0c898";  wcRefreshBg2 = "#c0a878";  wcRefreshBorder = "#a08860";
        // 主题按钮 — 深棕木质
        tbBg = "#8c7a5a";  tbColor = "#fffdf0";  tbBorder = "#6a5840";
        tbHoverBg = "#a09070";  tbHoverColor = "#ffffff";
        // 标题
        leftTitleClr = "#3e3028";  videoTitleClr = "#2e7d32";  aiTitleClr = "#2e7d32";
    }

    // ══════ 托盘菜单 ══════
    if (trayIcon && trayIcon->contextMenu()) {
        trayIcon->contextMenu()->setStyleSheet(
            QString("background: %1; color: %2;").arg(isDarkMode ? "#0d1f2d" : bg, fg));
    }

    // ══════ 主窗口背景 ══════
    this->setStyleSheet(QString(
        "QWidget { background-color: %1; color: %2; font-family: 'Microsoft YaHei'; }").arg(bg, fg));

    // ══════ 左侧面板 ══════
    if (leftPanelTitle)
        leftPanelTitle->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;").arg(leftTitleClr));
    if (loraStateLabel)
        loraStateLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(loraColor));

    for (auto *card : dataCards)
        card->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 8px;").arg(cardBg, border));
    for (auto *tlbl : dataCardTitles)
        tlbl->setStyleSheet(QString("color: %1; font-size: 12px;").arg(muted));
    for (auto *vlbl : valueLabels)
        vlbl->setStyleSheet(QString("color: %1; font-size: 22px; font-weight: bold;").arg(accent));

    // 数据看板按钮
    if (chartBtn)
        chartBtn->setStyleSheet(QString("background: %1; color: white; padding: 8px; "
            "font-weight: bold; border-radius: 4px;").arg(chartBtnBg));

    // ══════ 中间面板 ══════
    if (videoTitleLabel)
        videoTitleLabel->setStyleSheet(QString("font-weight: bold; color: %1;").arg(videoTitleClr));
    if (videoLabel)
        videoLabel->setStyleSheet(QString("background: black; border: 2px solid %1; min-height: 180px; color: #4CAF50;").arg(border));
    if (mapView)
        mapView->setStyleSheet(QString("border: 1px solid %1; border-radius: 8px;").arg(border));

    // ══════ 右侧面板 ══════
    if (aiTitleLabel)
        aiTitleLabel->setStyleSheet(QString("font-size: 16px; color: %1; font-weight: bold;").arg(aiTitleClr));
    if (chatDisplay)
        chatDisplay->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 4px; padding: 10px;").arg(chatBg, border));
    if (chatInput)
        chatInput->setStyleSheet(QString("padding: 8px; background: %1; color: %2; border: 1px solid %3;").arg(inputBg, fg, border));
    if (sendBtn)
        sendBtn->setStyleSheet(QString("background: %1; padding: 8px 15px; font-weight: bold; color: white;").arg(sendBtnBg));

    // ══════ 天气卡片 ══════
    if (weatherCard)
        weatherCard->setStyleSheet(QString(
            "QFrame#weatherCard {"
            "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %1, stop:0.4 %2, stop:1 %3);"
            "  border-top: 1px solid %4; border-left: 1px solid %5;"
            "  border-right: 1px solid %6; border-bottom: 2px solid %7;"
            "  border-radius: 8px;"
            "}").arg(wcBg1, wcBg2, wcBg3, wcBorder1, wcBorder2, wcBorder2, isDarkMode ? "#080408" : "#a09060"));
    if (weatherTitleLabel)
        weatherTitleLabel->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold; border: none; background: transparent;").arg(wcTitle));
    if (weatherDetailLabel)
        weatherDetailLabel->setStyleSheet(QString("color: %1; font-size: 13px; border: none; background: transparent; padding: 3px 0;").arg(wcDetail));
    if (weatherTimeLabel)
        weatherTimeLabel->setStyleSheet(QString("color: %1; font-size: 11px; border: none; background: transparent;").arg(wcTime));

    // ══════ 主题按钮 ══════
    if (themeBtn)
        themeBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; font-size: 12px; font-weight: bold;"
            "  border: 1px solid %3; border-radius: 4px; padding: 2px 8px; }"
            "QPushButton:hover { background: %4; color: %5; }")
            .arg(tbBg, tbColor, tbBorder, tbHoverBg, tbHoverColor));

    // ══════ 看板窗口（同步主题） ══════
    if (chartDashboard) {
        chartDashboard->setStyleSheet(QString(
            "background-color: %1; color: %2; font-family: 'Microsoft YaHei';").arg(bg, fg));
    }
}

void TeaGardenSystem::refreshWeatherAlert() {
    QString location = qEnvironmentVariable("TEA_WEATHER_LOCATION", "WTX_CH101280800");
    QString token    = qEnvironmentVariable("TEA_WEATHER_TOKEN");
    if (token.isEmpty()) {
        weatherLevelLabel->setText("⚠ 未配置Token");
        weatherLevelLabel->setStyleSheet("color: #FF9800; font-size: 16px; font-weight: bold; border: none; background: transparent;");
        weatherDetailLabel->setText("设置 TEA_WEATHER_TOKEN 环境变量");
        weatherTimeLabel->setText("");
        return;
    }

    QString url = QString("https://api.open.geovisearth.com/v2/cn/hourly/basic?location=%1&token=%2")
                      .arg(location, token);
    QNetworkRequest request{QUrl(url)};
    weatherReply = networkManager->get(request);
}

bool TeaGardenSystem::eventFilter(QObject *obj, QEvent *event) {
    if (obj == weatherCard && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            showWeatherDetail();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void TeaGardenSystem::showWeatherDetail() {
    if (weatherForecastData.isEmpty()) {
        return;
    }

    auto *dlg = new QDialog(this, Qt::Window | Qt::WindowCloseButtonHint);
    QString dialogTitle = "24小时逐时预报";
    if (!weatherLocationPath.isEmpty()) {
        QStringList parts = weatherLocationPath.split(",");
        if (parts.size() >= 3)
            dialogTitle = QString("24小时逐时预报 · %1%2").arg(parts[1], parts[2]);
        else
            dialogTitle = "24小时逐时预报 · " + weatherLocationPath;
    }
    dlg->setWindowTitle(dialogTitle);
    dlg->resize(750, 520);
    dlg->setStyleSheet("background-color: #051623;");

    auto *layout = new QVBoxLayout(dlg);

    // 标题
    QString innerTitle = "🌤 24小时逐时预报";
    if (!weatherLocationPath.isEmpty()) {
        QStringList parts = weatherLocationPath.split(",");
        if (parts.size() >= 3)
            innerTitle += QString(" · %1·%2").arg(parts[1], parts[2]);
        else if (parts.size() >= 2)
            innerTitle += " · " + parts[1];
    }
    auto *title = new QLabel(innerTitle);
    title->setStyleSheet("color: #4FC3F7; font-size: 18px; font-weight: bold; padding: 8px 0;");
    layout->addWidget(title);

    // 表格
    auto *table = new QTableWidget(dlg);
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"日期时间", "天气", "温度℃", "体感℃", "湿度%", "风向", "风力"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setRowCount(weatherForecastData.size());
    table->setStyleSheet(
        "QTableWidget { background: #0a1628; color: #E0E0E0; gridline-color: #1a2a3a; border: 1px solid #1a3040; }"
        "QHeaderView::section { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #1a3040, stop:1 #0d1f30); "
        "color: #90A4AE; font-weight: bold; padding: 5px; border: 1px solid #1a2a3a; }"
        "QTableWidget::item { padding: 4px 6px; }"
        "QTableWidget::item:selected { background: #1a4050; color: #00E5FF; }");

    for (int i = 0; i < weatherForecastData.size(); ++i) {
        QJsonObject row = weatherForecastData[i].toObject();
        QString fcTime = row["fc_time"].toString(); // "2024091113"
        // 格式化为 HH:00
        // fc_time = "2024091113" → "09-11 13:00"
        QString hour;
        if (fcTime.size() >= 10) {
            hour = fcTime.mid(4, 2) + "-" + fcTime.mid(6, 2) + " " + fcTime.right(2) + ":00";
        } else {
            hour = fcTime.right(2) + ":00";
        }

        QString wp = row["wp"].toString();
        double tem = row["tem"].toDouble();
        double real_tem = row["real_tem"].toDouble();
        int rh = row["rh"].toInt();
        QString wd = row["wd_desc"].toString();
        QString wsDesc = row["ws_desc"].toString();

        auto makeCell = [&](const QString &text, const QString &color = "#E0E0E0") {
            auto *item = new QTableWidgetItem(text);
            item->setForeground(QColor(color));
            item->setTextAlignment(Qt::AlignCenter);
            return item;
        };

        // 天气颜色
        QString wpColor = "#4FC3F7";  // 晴
        if (wp.contains("云")) wpColor = "#90A4AE";
        if (wp.contains("阴")) wpColor = "#78909C";
        if (wp.contains("雨")) wpColor = "#64B5F6";
        if (wp.contains("雷")) wpColor = "#FF9800";
        if (wp.contains("雪")) wpColor = "#E0E0E0";

        table->setItem(i, 0, makeCell(hour));
        table->setItem(i, 1, makeCell(wp, wpColor));
        table->setItem(i, 2, makeCell(QString::number(tem, 'f', 0), "#FFC107"));
        table->setItem(i, 3, makeCell(QString::number(real_tem, 'f', 0), "#FF9800"));
        table->setItem(i, 4, makeCell(QString::number(rh)));
        table->setItem(i, 5, makeCell(wd));
        table->setItem(i, 6, makeCell(wsDesc));
    }

    layout->addWidget(table);

    // 关闭按钮
    auto *closeBtn = new QPushButton("关闭");
    closeBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #1a3040, stop:1 #0d1f30);"
        "  color: #90A4AE; padding: 6px 24px; font-size: 13px; font-weight: bold; border-radius: 4px;"
        "  border-top: 1px solid #2a4050; border-bottom: 1px solid #081018;"
        "}"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #284868, stop:1 #183848); color: #fff; }");
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    dlg->setModal(false);
    dlg->show();
}

void TeaGardenSystem::openPestDashboard() {
    if (!pestDashboard) {
        pestDashboard = new PestDashboard();
    }
    pestDashboard->show();
    pestDashboard->raise();
    pestDashboard->refresh();
}

void TeaGardenSystem::injectMockPest() {
    QStringList types = {"茶尺蠖", "茶小绿叶蝉", "茶炭疽病", "茶尺蠖", "茶小绿叶蝉"};
    PestRecord rec;
    rec.timestamp = QDateTime::currentDateTime();
    rec.lat = 25.08 + (QRandomGenerator::global()->bounded(200) - 100) / 10000.0;
    rec.lng = 118.232 + (QRandomGenerator::global()->bounded(200) - 100) / 10000.0;
    rec.pestType = types[QRandomGenerator::global()->bounded(types.size())];
    rec.severity = 1 + QRandomGenerator::global()->bounded(3);
    PestStore::instance()->append(rec);

    // 上传病虫害到华为云 IoT
    QJsonObject pestData;
    pestData["pest_type"]   = rec.pestType;
    pestData["pest_severity"] = rec.severity;
    pestData["pest_lat"]    = rec.lat;
    pestData["pest_lng"]    = rec.lng;
    pestData["pest_total_count"] = PestStore::instance()->totalCount();
    IotUploader::instance()->uploadPest(pestData);

    appendChatMessage("无人机", QString("检测到 %1 @ (%2, %3)")
        .arg(rec.pestType)
        .arg(rec.lat, 0, 'f', 6)
        .arg(rec.lng, 0, 'f', 6));

    injectPestMarkers();
    refreshPestPie();
    if (pestDashboard && pestDashboard->isVisible()) pestDashboard->refresh();
}

void TeaGardenSystem::injectPestMarkers() {
#ifdef TEA_ENABLE_MAP_WEBVIEW
    if (!mapView) return;
    QVector<PestRecord> records = PestStore::instance()->queryAll();
    if (records.isEmpty()) return;

    // 构建 JS 注入: 清除旧标注并添加新标注
    QMap<QString, QString> colors;
    colors["茶尺蠖"]   = "#4CAF50";
    colors["茶小绿叶蝉"] = "#FF9800";
    colors["茶炭疽病"]  = "#F44336";

    QString js = "try{"
                 "if(typeof pestMarkers !== 'undefined'){pestMarkers.forEach(function(m){map.removeLayer(m);});}"
                 "pestMarkers=[];"
                 "if(typeof map==='undefined'||typeof L==='undefined'){console.log('Map or Leaflet not ready');}"
                 "else{";
    for (const auto& r : records) {
        QString clr = colors.value(r.pestType, "#2196F3");
        QString label = r.pestType;
        label.replace("'", "\\'");
        js += QString(
            // 圆点
            "var cm=L.circleMarker([%1,%2],{radius:8,color:'%3',fillColor:'%3',fillOpacity:0.7,weight:2}).addTo(map);"
            // 箭头 + 文字标签 (用 divIcon)
            "var icon=L.divIcon({"
            "  html:'<div style=\"position:relative;left:12px;top:-12px;white-space:nowrap;\">"
            "    <span style=\"font-size:11px;background:rgba(0,0,0,0.75);color:%3;padding:2px 6px;border-radius:3px;"
            "      border-left:2px solid %3;\">%4</span></div>',"
            "  iconSize:[0,0],iconAnchor:[-10,8]});"
            "var lbl=L.marker([%1,%2],{icon:icon}).addTo(map);"
            "pestMarkers.push(cm);pestMarkers.push(lbl);"
        ).arg(r.lat, 0, 'f', 6).arg(r.lng, 0, 'f', 6)
         .arg(clr, label);
    }
    js += "}}catch(e){console.log('Pest marker error:'+e.message);}";
    static_cast<QWebEngineView*>(mapView)->page()->runJavaScript(js);
#endif
}


void TeaGardenSystem::refreshPestPie() {
    if (!pestPieView) return;
    auto *chart = pestPieView->chart();
    chart->removeAllSeries();

    QMap<QString, int> counts = PestStore::instance()->typeCounts();
    auto *series = new QPieSeries();
    series->setPieSize(0.75);
    series->setHorizontalPosition(0.5);
    series->setVerticalPosition(0.55);

    if (counts.isEmpty()) {
        auto *sl = series->append("暂无数据", 1);
        sl->setColor(QColor("#455A64"));
        sl->setLabelVisible(false);
    } else {
        QMap<QString, QColor> colors;
        colors["茶尺蠖"]   = QColor("#4CAF50");
        colors["茶小绿叶蝉"] = QColor("#FF9800");
        colors["茶炭疽病"]  = QColor("#F44336");

        int total = 0;
        for (int v : counts.values()) total += v;

        for (auto it = counts.begin(); it != counts.end(); ++it) {
            double pct = total > 0 ? it.value() * 100.0 / total : 0;
            auto *sl = series->append(
                QString("%1 %2(%3%)").arg(it.key()).arg(it.value()).arg(pct, 0, 'f', 0),
                it.value());

            sl->setColor(colors.value(it.key(), QColor("#2196F3")));
            sl->setBorderColor(QColor("#051623"));
            sl->setBorderWidth(2);
            sl->setLabelVisible(true);
            sl->setLabelColor(QColor("#E0E0E0"));
        }
    }

    chart->addSeries(series);
}
