#include "teagardensystem.h"
#include "chartdashboard.h"
#include "thresholddialog.h"
#include "datastore.h"

#include <QRandomGenerator>
#include <QDateTime>
#include <QtGlobal>
#include <QtMath>
#include <QSerialPort>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QCoreApplication>
#include <QSettings>
#include <QPixmap>
#include <QDebug>
#include <QStyle>
#include <QApplication>
#include <cmath>
#ifdef TEA_ENABLE_MAP_WEBVIEW
#include <QtWebEngineWidgets/QWebEngineView>
#endif

namespace {
QString readAmapSetting(const char *envName, const char *iniKey, const QString &defaultValue = QString()) {
    const QByteArray envValue = qgetenv(envName);
    if (!envValue.trimmed().isEmpty()) {
        return QString::fromUtf8(envValue).trimmed();
    }

    const QString iniPath = QCoreApplication::applicationDirPath() + "/amap.ini";
    QSettings settings(iniPath, QSettings::IniFormat);
    return settings.value(QString::fromUtf8(iniKey), defaultValue).toString().trimmed();
}
}

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

    // --- A. 左侧：传感器看板 ---
    QVBoxLayout *leftPanel = new QVBoxLayout();
    leftPanel->addWidget(new QLabel("实时环境指标"));
    loraStateLabel = new QLabel("LoRa: 初始化中...");
    loraStateLabel->setStyleSheet("color: #80CBC4; font-size: 12px;");
    leftPanel->addWidget(loraStateLabel);

    QStringList names = {"空气温度", "空气湿度", "光照强度", "土壤pH", "风速", "土壤温度", "土壤湿度", "土壤电导率"};
    QStringList units = {"℃", "%", "Lux", "", "m/s", "℃", "%", "mS/cm"};
    for(int i=0; i<names.size(); ++i) {
        leftPanel->addWidget(createDataCard(names[i], i, units[i]));
    }
    QPushButton *chartBtn = new QPushButton("📊 数据看板");
    chartBtn->setStyleSheet("background: #00897B; color: white; padding: 8px; "
                            "font-weight: bold; border-radius: 6px;");
    connect(chartBtn, &QPushButton::clicked, this, &TeaGardenSystem::openChartDashboard);
    leftPanel->addWidget(chartBtn);
    leftPanel->addStretch();
    mainLayout->addLayout(leftPanel, 2);

    // --- B. 中间：空间可视化 ---
    QVBoxLayout *midPanel = new QVBoxLayout();
    QLabel *vTitle = new QLabel("茶园实景与空间分布");
    vTitle->setStyleSheet("font-weight: bold; color: #00E5FF;");

    videoLabel = new QLabel("【实时视频流：等待 MJPEG 连接...】");
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setStyleSheet("background: black; border: 2px solid #1a2a3a; min-height: 180px; color: #4CAF50;");

    midPanel->addWidget(vTitle);
    midPanel->addWidget(videoLabel, 2);
    setupMapView(midPanel);
    mainLayout->addLayout(midPanel, 6);

    // --- C. 右侧：AI 管家聊天 ---
    QVBoxLayout *rightPanel = new QVBoxLayout();
    QLabel *aiTitle = new QLabel("AI 智慧管家分析");
    aiTitle->setStyleSheet("font-size: 16px; color: #00E5FF; font-weight: bold;");

    chatDisplay = new QTextEdit();
    chatDisplay->setReadOnly(true);
    chatDisplay->setStyleSheet("background: rgba(0,0,0,0.4); border: 1px solid #334455; border-radius: 5px; padding: 10px;");

    QHBoxLayout *inputBox = new QHBoxLayout();
    chatInput = new QLineEdit();
    chatInput->setAttribute(Qt::WA_InputMethodEnabled, true);
    chatInput->setPlaceholderText("询问管理建议...");
    chatInput->setStyleSheet("padding: 8px; background: #1a2a3a; color: white; border: 1px solid #334455;");

    voiceBtn = new QPushButton("🎤 语音");
    voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
    connect(voiceBtn, &QPushButton::clicked, this, &TeaGardenSystem::startVoiceRecording);

    QPushButton *sendBtn = new QPushButton("发送");
    sendBtn->setStyleSheet("background: #00897B; padding: 8px 15px; font-weight: bold;");

    connect(sendBtn, &QPushButton::clicked, this, &TeaGardenSystem::handleChat);
    connect(chatInput, &QLineEdit::returnPressed, this, &TeaGardenSystem::handleChat);

    inputBox->addWidget(chatInput);
    inputBox->addWidget(voiceBtn);
    inputBox->addWidget(sendBtn);

    rightPanel->addWidget(aiTitle);
    rightPanel->addWidget(chatDisplay);
    rightPanel->addLayout(inputBox);
    mainLayout->addLayout(rightPanel, 2);
}

void TeaGardenSystem::setupMjpegStream() {
    const QString streamUrl = qEnvironmentVariable("TEA_MJPEG_URL", "http://100.108.217.12:8080/");

    QNetworkRequest request{QUrl(streamUrl)};
    mjpegReply = networkManager->get(request);

    connect(mjpegReply, &QNetworkReply::readyRead, this, [this]() {
        mjpegBuffer.append(mjpegReply->readAll());

        // 首次收到数据时从 Content-Type 响应头提取 boundary
        if (mjpegBoundary.isEmpty()) {
            QByteArray contentType = mjpegReply->rawHeader("Content-Type");
            int idx = contentType.indexOf("boundary=");
            if (idx >= 0) {
                mjpegBoundary = QByteArray("--") + contentType.mid(idx + 9).trimmed();
            }
        }

        if (mjpegBoundary.isEmpty()) return;

        // 按 boundary 分割帧
        while (true) {
            int start = mjpegBuffer.indexOf(mjpegBoundary);
            if (start < 0) break;

            int end = mjpegBuffer.indexOf(mjpegBoundary, start + mjpegBoundary.size());
            if (end < 0) break; // 帧不完整，等更多数据

            QByteArray part = mjpegBuffer.mid(start, end - start);
            mjpegBuffer.remove(0, end);

            // 跳过 HTTP 头部分，找到 JPEG 数据（以 \r\n\r\n 结束的头部）
            int headerEnd = part.indexOf("\r\n\r\n");
            if (headerEnd < 0) continue;

            QByteArray jpegData = part.mid(headerEnd + 4);
            if (jpegData.isEmpty()) continue;

            QImage frame = QImage::fromData(jpegData, "JPEG");
            if (frame.isNull()) continue;

            if (videoLabel) {
                const QPixmap pix = QPixmap::fromImage(frame);
                videoLabel->setPixmap(pix.scaled(videoLabel->size(),
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
                videoLabel->setText(QString());
            }
        }
    });

    // 流断开或出错时尝试重连
    connect(mjpegReply, &QNetworkReply::errorOccurred,
            this, [this, streamUrl](QNetworkReply::NetworkError err) {
        Q_UNUSED(err);
        qWarning() << "MJPEG stream error:" << mjpegReply->errorString();
        appendChatMessage("系统", "MJPEG 视频流断开，即将重连...");
        mjpegReply->deleteLater();
        mjpegReply = nullptr;
        mjpegBuffer.clear();
        mjpegBoundary.clear();
        // 3 秒后重连
        QTimer::singleShot(3000, this, [this]() {
            setupMjpegStream();
        });
    });

    appendChatMessage("系统", "MJPEG 视频流已连接: " + streamUrl);
}

void TeaGardenSystem::setupMapView(QVBoxLayout *midPanel) {
#ifdef TEA_ENABLE_MAP_WEBVIEW
    QFile htmlFile(":/web/amap_view.html");
    if (!htmlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QLabel *fallback = new QLabel("AMap page load failed: :/web/amap_view.html");
        fallback->setAlignment(Qt::AlignCenter);
        fallback->setMinimumHeight(350);
        fallback->setStyleSheet("background: #1a2a3a; border: 1px solid #334455; color: #ff8a80;");
        midPanel->addWidget(fallback);
        return;
    }

    QString html = QString::fromUtf8(htmlFile.readAll());
    htmlFile.close();

    const QString amapKey = readAmapSetting("TEA_AMAP_KEY", "amap/key");
    const QString centerLng = readAmapSetting("TEA_AMAP_LNG", "amap/lng", "120.135947");
    const QString centerLat = readAmapSetting("TEA_AMAP_LAT", "amap/lat", "30.274084");
    const QString zoom = readAmapSetting("TEA_AMAP_ZOOM", "amap/zoom", "14");

    html.replace("__AMAP_KEY__", amapKey.toHtmlEscaped());
    html.replace("__CENTER_LNG__", centerLng.toHtmlEscaped());
    html.replace("__CENTER_LAT__", centerLat.toHtmlEscaped());
    html.replace("__ZOOM__", zoom.toHtmlEscaped());

    mapView = new QWebEngineView(this);
    mapView->setMinimumHeight(460);
    mapView->setStyleSheet("border: 1px solid #334455; border-radius: 10px;");
    static_cast<QWebEngineView *>(mapView)->setHtml(html, QUrl("https://webapi.amap.com/"));
    midPanel->addWidget(mapView, 5);
#else
    QLabel *fallback = new QLabel("Qt WebEngine module missing. Install qtwebengine5-dev to enable AMap.");
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

    QVBoxLayout *vbox = new QVBoxLayout(card);
    QLabel *tLbl = new QLabel(title);
    tLbl->setStyleSheet("color: #909399; font-size: 12px;");

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
    // 暂时不模拟其他数据，保持为空
    // curLight = 800 + QRandomGenerator::global()->bounded(3000);
    // curPh = 4.5 + QRandomGenerator::global()->bounded(25) / 10.0;
    // curWindSpeed = QRandomGenerator::global()->bounded(100) / 10.0;
    // curSoilTemp = 10.0 + QRandomGenerator::global()->bounded(150) / 10.0;
    // curSoilHumi = 20 + QRandomGenerator::global()->bounded(60);
    // curSoilEc = 0.5 + QRandomGenerator::global()->bounded(20) / 10.0;

    valueLabels[0]->setText(QString::number(curTemp, 'f', 2) + " ℃");
    valueLabels[1]->setText(QString::number(curHumi, 'f', 2) + " %");
    valueLabels[2]->setText("-- Lux");
    valueLabels[3]->setText("--");
    valueLabels[4]->setText("-- m/s");
    valueLabels[5]->setText("-- ℃");
    valueLabels[6]->setText("-- %");
    valueLabels[7]->setText("-- mS/cm");

    if (loraStateLabel && loraManager && loraManager->isRunning()) {
        loraStateLabel->setText("LoRa: 数据超时，暂用模拟数据");
    }

    // 模拟数据也写入 SQLite
    SensorSnapshot snap2;
    snap2.timestamp = QDateTime::currentDateTime();
    snap2.airTemp = curTemp;
    snap2.airHumidity = curHumi;
    DataStore::instance()->append(snap2);
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
    // 跳过 MJPEG 流回复（由 setupMjpegStream 中的 readyRead 处理）
    if (reply == mjpegReply) {
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
