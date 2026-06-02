#include "teagardensystem.h"
#include "greenhomeclient.h"
#include <QRandomGenerator>
#include <QDateTime>
#include <QtGlobal>
#include <QtMath>
#include <QSerialPort>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QAbstractSocket>
#include <QLocale>
#include <QRegularExpression>
#include <QGridLayout>
#include <QDebug>
#include <QShortcut>

TeaGardenSystem::TeaGardenSystem(QWidget *parent) : QWidget(parent) {
    setupUI();

    // 1. 设置定时器：每3秒更新一次数据
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

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &TeaGardenSystem::onNetworkReply);

    bool okMinInterval = false;
    const int envMinInterval = qEnvironmentVariableIntValue("TEA_DEVICE_CMD_MIN_MS", &okMinInterval);
    if (okMinInterval && envMinInterval >= 0) {
        commandMinIntervalMs = envMinInterval;
    }

    const QString serverIp = qEnvironmentVariable("TEA_SERVER_IP", "127.0.0.1");
    greenClient = new GreenHomeClient(serverIp, this);
        connect(greenClient, &GreenHomeClient::deviceStateChanged,
            this, &TeaGardenSystem::onRemoteDeviceCommand);
        greenClient->invalidateSensors();
    greenClient->start();

    // 2. 初始化发送欢迎语
    appendChatMessage("管家", "您好！我是家庭绿植管家。监测系统已就绪，当前各项指标稳定。");
}

// 【修复报错】明确定义析构函数
TeaGardenSystem::~TeaGardenSystem() {
    // Qt 会自动处理对象树中子控件的内存释放
}

void TeaGardenSystem::setupUI() {
    this->setWindowTitle("智能家庭绿植管理系统 v2.0");
    this->resize(1200, 800);
    this->setStyleSheet("background-color: #000000; color: #ffffff; font-family: 'Microsoft YaHei';");

    QShortcut *exitShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    exitShortcut->setContext(Qt::ApplicationShortcut);
    connect(exitShortcut, &QShortcut::activated, this, &QWidget::close);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(20);

    // --- A. 左侧：传感器看板 ---
    QVBoxLayout *leftPanel = new QVBoxLayout();
    leftPanel->addWidget(new QLabel("实时环境指标"));
    loraStateLabel = new QLabel("LoRa: 初始化中...");
    loraStateLabel->setStyleSheet("color: #80CBC4; font-size: 12px;");
    leftPanel->addWidget(loraStateLabel);

    QStringList names = {"空气温度", "空气湿度", "光照强度", "风速", "土壤温度", "土壤湿度", "土壤电导率"};
    QStringList units = {"℃", "%", "Lux", "m/s", "℃", "%", "mS/cm"};
    QGridLayout *dataGrid = new QGridLayout();
    dataGrid->setHorizontalSpacing(12);
    dataGrid->setVerticalSpacing(12);
    for (int i = 0; i < names.size(); ++i) {
        const int row = i / 2;
        const int col = i % 2;
        dataGrid->addWidget(createDataCard(names[i], i, units[i]), row, col);
    }
    leftPanel->addLayout(dataGrid);

    QLabel *controlTitle = new QLabel("设备控制");
    controlTitle->setStyleSheet("font-weight: bold; color: #00E5FF; margin-top: 10px;");
    leftPanel->addWidget(controlTitle);

    QHBoxLayout *controlRow = new QHBoxLayout();
    pumpBtn = new QPushButton("水泵: 关");
    pumpBtn->setCheckable(true);
    pumpBtn->setStyleSheet("background: #37474F; padding: 8px 12px; font-weight: bold; color: #E0E0E0;");
    connect(pumpBtn, &QPushButton::toggled, this, &TeaGardenSystem::onPumpToggled);

    lampBtn = new QPushButton("灭蚊灯: 关");
    lampBtn->setCheckable(true);
    lampBtn->setStyleSheet("background: #37474F; padding: 8px 12px; font-weight: bold; color: #E0E0E0;");
    connect(lampBtn, &QPushButton::toggled, this, &TeaGardenSystem::onLampToggled);

    controlRow->addWidget(pumpBtn);
    controlRow->addWidget(lampBtn);
    leftPanel->addLayout(controlRow);

    leftPanel->addStretch();
    mainLayout->addLayout(leftPanel, 5);

    // --- C. 右侧：AI 管家聊天 ---
    QVBoxLayout *rightPanel = new QVBoxLayout();
    QLabel *aiTitle = new QLabel("AI 智慧管家分析");
    aiTitle->setStyleSheet("font-size: 16px; color: #00E5FF; font-weight: bold;");

    chatDisplay = new QTextEdit();
    chatDisplay->setReadOnly(true);
    chatDisplay->setMinimumHeight(220);
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
    mainLayout->addLayout(rightPanel, 4);
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
    valueLabels[3]->setText("-- m/s");
    valueLabels[4]->setText("-- ℃");
    valueLabels[5]->setText("-- %");
    valueLabels[6]->setText("-- mS/cm");

    if (greenClient) {
        greenClient->invalidateSensors();
    }

    if (loraStateLabel && loraManager && loraManager->isRunning()) {
        loraStateLabel->setText("LoRa: 数据超时，暂用模拟数据");
    }
}

void TeaGardenSystem::handleChat() {
    QString msg = chatInput->text().trimmed();
    if(msg.isEmpty()) return;

    appendChatMessage("用户", msg);
    chatInput->clear();

    requestAIAnalysis(msg);
}

bool TeaGardenSystem::sendLoraCommand(const QString &deviceKey, int value) {
    if (!loraManager || !loraManager->isRunning()) {
        appendChatMessage("系统", "LoRa 未连接，无法发送控制命令。");
        return false;
    }

    if (commandMinIntervalMs > 0 && !deviceKey.isEmpty()) {
        const QDateTime now = QDateTime::currentDateTime();
        const QDateTime last = lastCommandTime.value(deviceKey);
        if (last.isValid()) {
            const qint64 deltaMs = last.msecsTo(now);
            if (deltaMs >= 0 && deltaMs < commandMinIntervalMs) {
                const int delayMs = commandMinIntervalMs - static_cast<int>(deltaMs);
                scheduleLoraCommand(deviceKey, value, delayMs);
                return true;
            }
        }
    }

    return sendLoraCommandNow(deviceKey, value);
}

bool TeaGardenSystem::sendLoraCommandNow(const QString &deviceKey, int value) {
    if (!loraManager || !loraManager->isRunning()) {
        appendChatMessage("系统", "LoRa 未连接，无法发送控制命令。");
        return false;
    }

    if (!loraManager->sendCommand(value)) {
        appendChatMessage("系统", "LoRa 命令发送失败，请检查串口状态。");
        return false;
    }

    if (!deviceKey.isEmpty()) {
        lastCommandTime[deviceKey] = QDateTime::currentDateTime();
    }
    return true;
}

void TeaGardenSystem::scheduleLoraCommand(const QString &deviceKey, int value, int delayMs) {
    pendingCommands[deviceKey] = value;

    QTimer *timer = commandTimers.value(deviceKey, nullptr);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        commandTimers.insert(deviceKey, timer);
        connect(timer, &QTimer::timeout, this, [this, deviceKey]() {
            flushPendingCommand(deviceKey);
        });
    }

    if (!timer->isActive() || timer->remainingTime() > delayMs) {
        timer->start(delayMs);
    }
    qDebug() << "[LoRa] debounce" << deviceKey << "delay" << delayMs << "ms";
}

void TeaGardenSystem::flushPendingCommand(const QString &deviceKey) {
    if (!pendingCommands.contains(deviceKey)) {
        return;
    }

    const int value = pendingCommands.take(deviceKey);
    sendLoraCommandNow(deviceKey, value);
}

void TeaGardenSystem::onPumpToggled(bool checked) {
    const int command = checked ? 3 : 4;
    sendLoraCommand("pump", command);
    updatePumpButtonUi(checked);
}

void TeaGardenSystem::onLampToggled(bool checked) {
    const int command = checked ? 1 : 2;
    sendLoraCommand("lamp", command);
    lampBtn->setText(checked ? "灭蚊灯: 开" : "灭蚊灯: 关");
    lampBtn->setStyleSheet(checked
        ? "background: #F9A825; padding: 8px 12px; font-weight: bold; color: #1B1B1B;"
        : "background: #37474F; padding: 8px 12px; font-weight: bold; color: #E0E0E0;");
}

void TeaGardenSystem::onRemoteDeviceCommand(const QString &deviceName, bool isOn) {
    const QString normalized = deviceName.trimmed().toLower();
    if (normalized == "pump" || normalized == "water_pump" || normalized == "water-pump") {
        if (pumpBtn && pumpBtn->isChecked() != isOn) {
            pumpBtn->setChecked(isOn);
        }
        return;
    }

    if (normalized == "lamp" || normalized == "mosquito_lamp" || normalized == "mosquito-lamp") {
        if (lampBtn && lampBtn->isChecked() != isOn) {
            lampBtn->setChecked(isOn);
        }
        return;
    }

    qDebug() << "[GreenHomeClient] unknown device command:" << deviceName << "is_on:" << isOn;
}

bool TeaGardenSystem::handleVoiceLampCommand(const QString &text) {
    const QRegularExpression onRe("(打开|开启|启动)\\s*灭蚊灯|灭蚊灯\\s*(打开|开启|启动)");
    const QRegularExpression offRe("(关(闭|掉)?|停(止|用)?)\\s*灭蚊灯|灭蚊灯\\s*(关(闭|掉)?|停(止|用)?)");

    const bool wantOn = onRe.match(text).hasMatch();
    const bool wantOff = offRe.match(text).hasMatch();
    if (!wantOn && !wantOff) {
        return false;
    }

    if (wantOn && wantOff) {
        appendChatMessage("系统", "语音指令包含开启与关闭，未执行灭蚊灯控制，请重试。");
        return true;
    }

    const bool checked = wantOn;
    const int command = checked ? 1 : 2;
    if (!sendLoraCommand("lamp", command)) {
        return true;
    }

    if (lampBtn) {
        lampBtn->blockSignals(true);
        lampBtn->setChecked(checked);
        lampBtn->blockSignals(false);
        lampBtn->setText(checked ? "灭蚊灯: 开" : "灭蚊灯: 关");
        lampBtn->setStyleSheet(checked
            ? "background: #F9A825; padding: 8px 12px; font-weight: bold; color: #1B1B1B;"
            : "background: #37474F; padding: 8px 12px; font-weight: bold; color: #E0E0E0;");
    }

    appendChatMessage("系统", QString("语音控制: 灭蚊灯已%1").arg(checked ? "开启" : "关闭"));
    return true;
}

void TeaGardenSystem::updatePumpButtonUi(bool checked) {
    if (!pumpBtn) {
        return;
    }

    pumpBtn->setText(checked ? "水泵: 开" : "水泵: 关");
    pumpBtn->setStyleSheet(checked
        ? "background: #2E7D32; padding: 8px 12px; font-weight: bold; color: white;"
        : "background: #37474F; padding: 8px 12px; font-weight: bold; color: #E0E0E0;");
}

bool TeaGardenSystem::handleVoicePumpCommand(const QString &text) {
    const QRegularExpression onRe("(打开|开启|启动)\\s*水泵|水泵\\s*(打开|开启|启动)");
    const QRegularExpression offRe("(关(闭|掉)?|停(止|用)?)\\s*水泵|水泵\\s*(关(闭|掉)?|停(止|用)?)");

    const bool wantOn = onRe.match(text).hasMatch();
    const bool wantOff = offRe.match(text).hasMatch();
    if (!wantOn && !wantOff) {
        return false;
    }

    if (wantOn && wantOff) {
        appendChatMessage("系统", "语音指令包含开启与关闭，未执行控制，请重试。");
        return true;
    }

    const bool checked = wantOn;
    const int command = checked ? 3 : 4;
    if (!sendLoraCommand("pump", command)) {
        return true;
    }

    if (pumpBtn) {
        pumpBtn->blockSignals(true);
        pumpBtn->setChecked(checked);
        pumpBtn->blockSignals(false);
        updatePumpButtonUi(checked);
    }

    appendChatMessage("系统", QString("语音控制: 水泵已%1").arg(checked ? "开启" : "关闭"));
    return true;
}

void TeaGardenSystem::startVoiceRecording() {
    if (recordProcess && recordProcess->state() != QProcess::NotRunning) {
        appendChatMessage("系统", "停止录音，开始识别...");
        recordProcess->terminate();
        QTimer::singleShot(500, this, [this]() {
            if (recordProcess && recordProcess->state() != QProcess::NotRunning) {
                recordProcess->kill();
            }
        });
        voiceBtn->setEnabled(false);
        voiceBtn->setText("识别中...");
        voiceBtn->setStyleSheet("background: #2196F3; padding: 8px 15px; font-weight: bold; color: white;");
        return;
    }

    if (xfStreaming) {
        appendChatMessage("系统", "识别中，请稍候...");
        return;
    }

    pendingPcmPath = QDir::homePath() + "/Desktop/current_voice_cmd.pcm";
    recordProcess = new QProcess(this);
    recordProcess->setProcessChannelMode(QProcess::SeparateChannels);
    recordStderr.clear();
    connect(recordProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = QString::fromUtf8(recordProcess->readAllStandardError()).trimmed();
        if (err.isEmpty()) {
            return;
        }
        recordStderr += err + "\n";
        qDebug() << "[arecord stderr]" << err;
    });

    connect(recordProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus);

        if (recordProcess) {
            recordProcess->deleteLater();
            recordProcess = nullptr;
        }

        if (exitCode != 0 && exitCode != 1) {
            QString errDetail = recordStderr.trimmed();
            if (!errDetail.isEmpty()) {
                errDetail.prepend("\n--- 录音错误信息 ---\n");
            }
            appendChatMessage("系统",
                QString("录音失败！arecord 退出码: %1，请检查麦克风是否连接。%2")
                    .arg(exitCode).arg(errDetail));
            voiceBtn->setEnabled(true);
            voiceBtn->setText("🎤 语音");
            voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
            return;
        }

        QFileInfo pcmInfo(pendingPcmPath);
        const qint64 pcmSize = pcmInfo.exists() ? pcmInfo.size() : 0;
        const qint64 minBytes = 3200; // 约 0.1s 的 16k/16bit/mono PCM
        if (!pcmInfo.exists() || pcmSize < minBytes) {
            QString errDetail = recordStderr.trimmed();
            if (!errDetail.isEmpty()) {
                errDetail.prepend("\n--- 录音错误信息 ---\n");
            }
            appendChatMessage("系统",
                QString("录音无有效内容（%1 字节），请检查麦克风或说话时长。%2")
                    .arg(pcmSize).arg(errDetail));
            voiceBtn->setEnabled(true);
            voiceBtn->setText("🎤 语音");
            voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
            return;
        }

        startXfUploadFromFile(pendingPcmPath);
    });

    const QString audioDevice = qEnvironmentVariable("TEA_AUDIO_DEVICE", "plughw:rockchipnau8822,0");
    QString program = "arecord";
    QStringList arguments;
    arguments << "-D" << audioDevice
              << "-f" << "S16_LE"
              << "-c" << "1"
              << "-r" << "16000"
              << "-t" << "raw"
              << pendingPcmPath;

    voiceBtn->setText("⏹ 停止");
    voiceBtn->setStyleSheet("background: #F44336; padding: 8px 15px; font-weight: bold; color: white;");
    voiceBtn->setEnabled(true);
    appendChatMessage("系统", "请说话 (再次点击停止)...");

    recordProcess->start(program, arguments);
    if (!recordProcess->waitForStarted(1000)) {
        appendChatMessage("系统", "录音启动失败，请检查麦克风设备。" );
        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        return;
    }

    QTimer::singleShot(10000, this, [this]() {
        if (recordProcess && recordProcess->state() != QProcess::NotRunning) {
            recordProcess->terminate();
            QTimer::singleShot(500, this, [this]() {
                if (recordProcess && recordProcess->state() != QProcess::NotRunning) {
                    recordProcess->kill();
                }
            });
        }
    });
}

void TeaGardenSystem::startXfUploadFromFile(const QString &pcmPath) {
    xfAppId = qEnvironmentVariable("XF_APP_ID");
    const QString apiKey = qEnvironmentVariable("XF_API_KEY");
    const QString apiSecret = qEnvironmentVariable("XF_API_SECRET");

    if (xfAppId.isEmpty() || apiKey.isEmpty() || apiSecret.isEmpty()) {
        appendChatMessage("系统", " API 未配置，请设置 XF_APP_ID/XF_API_KEY/XF_API_SECRET。");
        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        return;
    }

    QFile pcmFile(pcmPath);
    if (!pcmFile.open(QIODevice::ReadOnly)) {
        appendChatMessage("系统", "无法读取录音文件，请重试。");
        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        return;
    }
    xfAudioBuffer = pcmFile.readAll();
    pcmFile.close();

    if (xfAudioBuffer.isEmpty()) {
        appendChatMessage("系统", "录音无有效内容，请检查麦克风或说话时长。");
        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        return;
    }

    const QUrl url = buildXfIatUrl();
    if (!url.isValid()) {
        appendChatMessage("系统", "鉴权 URL 无效，请检查配置。");
        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        return;
    }

    if (!xfSocket) {
        xfSocket = new QWebSocket();
        connect(xfSocket, &QWebSocket::connected, this, [this]() {
            if (!xfAudioTimer) {
                xfAudioTimer = new QTimer(this);
                xfAudioTimer->setInterval(40);
                connect(xfAudioTimer, &QTimer::timeout, this, &TeaGardenSystem::sendXfAudioFrame);
            }
            xfAudioTimer->start();
        });

        connect(xfSocket, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
            handleXfMessage(message);
        });

        connect(xfSocket, &QWebSocket::disconnected, this, [this]() {
            if (xfStreaming && !xfFinalFrameSent) {
                appendChatMessage("系统", "连接已断开。");
            }
            xfStreaming = false;
            voiceBtn->setEnabled(true);
            voiceBtn->setText("🎤 语音");
            voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        });

        connect(xfSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
                [this](QAbstractSocket::SocketError) {
            appendChatMessage("系统", "连接失败: " + xfSocket->errorString());
            stopXfStreaming();
        });
    }

    xfTextBuffer.clear();
    xfResultPieces.clear();
    xfFirstFrame = true;
    xfStopRequested = true;
    xfFinalFrameSent = false;
    xfSeq = 1;
    xfStreaming = true;

    voiceBtn->setEnabled(false);
    voiceBtn->setText("识别中...");
    voiceBtn->setStyleSheet("background: #2196F3; padding: 8px 15px; font-weight: bold; color: white;");

    appendChatMessage("系统", "正在使用Sensevoice本地识别...");
    if (xfSocket->state() == QAbstractSocket::ConnectedState) {
        xfSocket->close();
    }
    xfSocket->open(url);
}

void TeaGardenSystem::stopXfStreaming() {
    xfStopRequested = true;
    xfStreaming = false;

    if (xfAudioTimer && xfAudioTimer->isActive()) {
        xfAudioTimer->stop();
    }

    if (xfSocket && xfSocket->state() == QAbstractSocket::ConnectedState) {
        xfSocket->close();
    }

    voiceBtn->setEnabled(true);
    voiceBtn->setText("🎤 语音");
    voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
}

QUrl TeaGardenSystem::buildXfIatUrl() const {
    const QString apiKey = qEnvironmentVariable("XF_API_KEY");
    const QString apiSecret = qEnvironmentVariable("XF_API_SECRET");
    const QString host = "iat.xf-yun.com";
    const QString path = "/v1";

    QLocale enLocale(QLocale::English);
    const QString date = enLocale.toString(QDateTime::currentDateTimeUtc(), "ddd, dd MMM yyyy HH:mm:ss 'GMT'");
    const QString signatureOrigin = QString("host: %1\ndate: %2\nGET %3 HTTP/1.1")
                                    .arg(host, date, path);
    const QByteArray signatureSha = QMessageAuthenticationCode::hash(
        signatureOrigin.toUtf8(), apiSecret.toUtf8(), QCryptographicHash::Sha256);
    const QString signature = QString::fromUtf8(signatureSha.toBase64());

    const QString authorizationOrigin = QString(
        "api_key=\"%1\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%2\"")
        .arg(apiKey, signature);
    const QString authorization = QString::fromUtf8(authorizationOrigin.toUtf8().toBase64());

    QUrl url(QString("wss://%1%2").arg(host, path));
    QUrlQuery query;
    query.addQueryItem("authorization", authorization);
    query.addQueryItem("date", date);
    query.addQueryItem("host", host);
    url.setQuery(query);
    return url;
}

void TeaGardenSystem::sendXfAudioFrame() {
    if (!xfSocket || xfSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    if (xfFirstFrame) {
        if (xfAudioBuffer.isEmpty()) {
            if (xfStopRequested) {
                appendChatMessage("系统", "录音过短，未发送语音。");
                xfStreaming = false;
                voiceBtn->setEnabled(true);
                voiceBtn->setText("🎤 语音");
                voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
            }
            return;
        }

        const int size = qMin(xfFrameSize, xfAudioBuffer.size());
        const QByteArray chunk = xfAudioBuffer.left(size);
        xfAudioBuffer.remove(0, size);
        sendXfFrame(chunk, 0);
        xfFirstFrame = false;
        return;
    }

    if (xfAudioBuffer.size() >= xfFrameSize) {
        const QByteArray chunk = xfAudioBuffer.left(xfFrameSize);
        xfAudioBuffer.remove(0, xfFrameSize);
        sendXfFrame(chunk, 1);
        return;
    }

    if (xfStopRequested) {
        const QByteArray chunk = xfAudioBuffer;
        xfAudioBuffer.clear();
        sendXfFrame(chunk, 2);
        xfFinalFrameSent = true;
        if (xfAudioTimer) {
            xfAudioTimer->stop();
        }
    }
}

void TeaGardenSystem::sendXfFrame(const QByteArray &pcm, int status) {
    if (!xfSocket || xfSocket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QJsonObject frame;

    QJsonObject header;
    header["app_id"] = xfAppId;
    header["status"] = status;
    frame["header"] = header;

    if (status == 0) {
        QJsonObject parameter;
        QJsonObject iat;
        iat["domain"] = "slm";
        iat["language"] = "zh_cn";
        iat["accent"] = "mandarin";
        iat["eos"] = 6000;
        iat["dwa"] = "wpgs";

        QJsonObject result;
        result["encoding"] = "utf8";
        result["compress"] = "raw";
        result["format"] = "json";
        iat["result"] = result;

        parameter["iat"] = iat;
        frame["parameter"] = parameter;
    }

    QJsonObject payloadObj;
    QJsonObject audio;
    audio["encoding"] = "raw";
    audio["sample_rate"] = 16000;
    audio["channels"] = 1;
    audio["bit_depth"] = 16;
    audio["seq"] = xfSeq++;
    audio["status"] = status;
    audio["audio"] = QString::fromLatin1(pcm.toBase64());
    payloadObj["audio"] = audio;
    frame["payload"] = payloadObj;

    const QByteArray jsonBytes = QJsonDocument(frame).toJson(QJsonDocument::Compact);
    xfSocket->sendTextMessage(QString::fromUtf8(jsonBytes));
}

void TeaGardenSystem::handleXfMessage(const QString &message) {
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        return;
    }

    const QJsonObject obj = doc.object();
    const QJsonObject header = obj.value("header").toObject();
    const int code = header.value("code").toInt(-1);
    if (code != 0) {
        appendChatMessage("系统", "识别失败: " + header.value("message").toString());
        xfStreaming = false;
        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        return;
    }

    const QJsonObject payload = obj.value("payload").toObject();
    const QJsonObject result = payload.value("result").toObject();

    QString textChunk;
    if (result.contains("text")) {
        const QByteArray rawText = QByteArray::fromBase64(result.value("text").toString().toUtf8());
        const QJsonDocument textDoc = QJsonDocument::fromJson(rawText);
        if (textDoc.isObject()) {
            const QJsonObject textObj = textDoc.object();
            const QJsonArray wsList = textObj.value("ws").toArray();
            for (const QJsonValue &wsVal : wsList) {
                const QJsonObject wsObj = wsVal.toObject();
                const QJsonArray cwList = wsObj.value("cw").toArray();
                if (!cwList.isEmpty()) {
                    textChunk += cwList.first().toObject().value("w").toString();
                }
            }

            const int sn = textObj.value("sn").toInt(-1);
            const QString pgs = textObj.value("pgs").toString();
            const QJsonArray rg = textObj.value("rg").toArray();
            if (pgs == "rpl" && rg.size() == 2) {
                const int start = rg.at(0).toInt();
                const int end = rg.at(1).toInt();
                for (int i = start; i <= end; ++i) {
                    xfResultPieces.remove(i);
                }
            }

            if (sn >= 0 && !textChunk.isEmpty()) {
                xfResultPieces[sn] = textChunk;
            }
        }
    }

    if (!xfResultPieces.isEmpty()) {
        QString merged;
        for (auto it = xfResultPieces.cbegin(); it != xfResultPieces.cend(); ++it) {
            merged += it.value();
        }
        xfTextBuffer = merged;
    }

    const int status = header.value("status").toInt(-1);
    const bool isLast = (status == 2) || result.value("ls").toBool();
    if (isLast) {
        if (!xfTextBuffer.trimmed().isEmpty()) {
            appendChatMessage("系统", QString("识别文本: %1").arg(xfTextBuffer));
            appendChatMessage("用户(语音)", xfTextBuffer);
            handleVoicePumpCommand(xfTextBuffer);
            handleVoiceLampCommand(xfTextBuffer);
            requestAIAnalysis(xfTextBuffer);
        } else {
            appendChatMessage("系统", "未能识别到有效语音，请重试。");
        }

        xfStreaming = false;
        if (xfAudioTimer && xfAudioTimer->isActive()) {
            xfAudioTimer->stop();
        }
        voiceBtn->setEnabled(true);
        voiceBtn->setText("🎤 语音");
        voiceBtn->setStyleSheet("background: #FF9800; padding: 8px 15px; font-weight: bold; color: white;");
        if (xfSocket) {
            xfSocket->close();
        }
    }
}

void TeaGardenSystem::requestAIAnalysis(const QString &userMessage) {
    // 构造请求数据
    QJsonObject requestData;
    requestData["model"] = "deepseek-chat"; // 或者您使用的其他模型名称
    
    QJsonArray messages;
    
    // 系统提示词，包含当前环境数据
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    QString systemPrompt = QString("你是一个专业家庭绿植AI管家。当前家中环境数据如下：\n"
                                   "空气温度: %1 ℃\n"
                                   "空气湿度: %2 %\n"
                                   "光照强度: %3 Lux\n"
                                   "风速: %4 m/s\n"
                                   "土壤温度: %5 ℃\n"
                                   "土壤湿度: %6 %\n"
                                   "土壤电导率: %7 mS/cm\n"
                                   "系统具备设备控制能力（如水泵、灭蚊灯）。"
                                   "当用户提出控制需求时，直接确认已执行或将执行，并给出简短的安全/节水建议。"
                                   "不要说无法控制设备或需要用户手动操作。"
                                   "请根据这些数据和用户的提问，给出专业的家庭绿植管理建议。回答要简明扼要。")
                               .arg(curTemp).arg(curHumi).arg(curLight)
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

    // 替换为您实际的 API 地址和 Key
    QString apiUrl = qEnvironmentVariable("TEA_AI_API_URL", "https://api.deepseek.com/v1/chat/completions");
    QString apiKey = qEnvironmentVariable("TEA_AI_API_KEY", "sk-d1b7d18a36a243bb8a9abeb5f8a0e898");

    QNetworkRequest request((QUrl(apiUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    networkManager->post(request, postData);
    appendChatMessage("管家", "正在思考中...");
}

void TeaGardenSystem::onNetworkReply(QNetworkReply *reply) {
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

    valueLabels[0]->setText(QString::number(curTemp, 'f', 2) + " ℃");
    valueLabels[1]->setText(QString::number(curHumi, 'f', 2) + " %");
    valueLabels[2]->setText(QString::number(curLight) + " Lux");
    valueLabels[3]->setText(QString::number(curWindSpeed, 'f', 2) + " m/s");
    valueLabels[4]->setText(QString::number(curSoilTemp, 'f', 2) + " ℃");
    valueLabels[5]->setText(QString::number(curSoilHumi, 'f', 2) + " %");
    valueLabels[6]->setText(QString::number(curSoilEc, 'f', 2) + " mS/cm");

    if (loraStateLabel) {
        loraStateLabel->setText("LoRa: 已接收实时数据");
    }

    if (greenClient) {
        greenClient->updateSensors(curTemp, curHumi, curSoilTemp, curSoilHumi,
                                   static_cast<double>(curLight), curWindSpeed, curSoilEc);
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
