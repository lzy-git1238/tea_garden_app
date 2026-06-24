#include "rovercontrolpanel.h"
#include "loramanager.h"
#include <QDebug>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTimer>
#include <QScreen>
#include <QApplication>

RoverControlPanel::RoverControlPanel(LoraManager *lora, QWidget *parent)
    : QWidget(parent), m_lora(lora)
{
    setWindowTitle("🚗 农业检测车控制");
    resize(350, 380);
    setMinimumSize(300, 320);
    setWindowFlags(Qt::Window);  // 有标题栏，可移动、可缩放
    setStyleSheet(
        "RoverControlPanel { background: #0a1628; color: #E8F4FF; }"
        "QLabel { color: #E8F4FF; }"
    );
    setFocusPolicy(Qt::StrongFocus);
    setupUI();

    m_repeatTimer = new QTimer(this);
    m_repeatTimer->setInterval(200);
    connect(m_repeatTimer, &QTimer::timeout, this, [this]() {
        if (m_lastCmd != 0) {
            sendRoverCmd(m_lastCmd);
        }
    });
}

RoverControlPanel::~RoverControlPanel() {
}

void RoverControlPanel::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 15, 20, 15);

    // 标题
    auto *titleLabel = new QLabel("🎮 农业检测车控制 (WASD)");
    titleLabel->setStyleSheet("color: #00d4ff; font-size: 18px; font-weight: bold;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // 方向按钮网格
    auto *dirGrid = new QGridLayout();
    dirGrid->setSpacing(8);

    auto dirBtnStyle = []() {
        return QString(
            "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 #1a4060, stop:1 #0d2030); color: #90caf9; font-size: 20px;"
            "font-weight: bold; padding: 15px; border: 1px solid #2a5070;"
            "border-radius: 8px; min-width: 70px; min-height: 45px; }"
            "QPushButton:pressed { background: #2a6090; color: #ffffff; }"
        );
    };

    m_forwardBtn = new QPushButton("W\n⬆ 前进");
    m_forwardBtn->setStyleSheet(dirBtnStyle());
    connect(m_forwardBtn, &QPushButton::pressed, this, &RoverControlPanel::onForwardPressed);
    connect(m_forwardBtn, &QPushButton::released, this, &RoverControlPanel::onForwardReleased);
    dirGrid->addWidget(m_forwardBtn, 0, 1);

    m_leftBtn = new QPushButton("A\n⬅ 左转");
    m_leftBtn->setStyleSheet(dirBtnStyle());
    connect(m_leftBtn, &QPushButton::pressed, this, &RoverControlPanel::onLeftPressed);
    connect(m_leftBtn, &QPushButton::released, this, &RoverControlPanel::onLeftReleased);
    dirGrid->addWidget(m_leftBtn, 1, 0);

    m_backwardBtn = new QPushButton("S\n⬇ 后退");
    m_backwardBtn->setStyleSheet(dirBtnStyle());
    connect(m_backwardBtn, &QPushButton::pressed, this, &RoverControlPanel::onBackwardPressed);
    connect(m_backwardBtn, &QPushButton::released, this, &RoverControlPanel::onBackwardReleased);
    dirGrid->addWidget(m_backwardBtn, 1, 1);

    m_rightBtn = new QPushButton("D\n➡ 右转");
    m_rightBtn->setStyleSheet(dirBtnStyle());
    connect(m_rightBtn, &QPushButton::pressed, this, &RoverControlPanel::onRightPressed);
    connect(m_rightBtn, &QPushButton::released, this, &RoverControlPanel::onRightReleased);
    dirGrid->addWidget(m_rightBtn, 1, 2);

    mainLayout->addLayout(dirGrid);

    // 喷水按钮
    m_sprayBtn = new QPushButton("💧 喷水");
    m_sprayBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #1565C0, stop:1 #0D47A1); color: #ffffff; font-size: 16px;"
        "font-weight: bold; padding: 12px; border: 1px solid #1E88E5;"
        "border-radius: 8px; }"
        "QPushButton:pressed { background: #0D47A1; }"
    );
    connect(m_sprayBtn, &QPushButton::pressed, this, &RoverControlPanel::onSprayPressed);
    connect(m_sprayBtn, &QPushButton::released, this, &RoverControlPanel::onSprayReleased);
    mainLayout->addWidget(m_sprayBtn);

    // 状态
    m_statusLabel = new QLabel("LoRa: 就绪 | 键盘: WASD");
    m_statusLabel->setStyleSheet("color: #90A4AE; font-size: 11px; padding: 4px;");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();
}

void RoverControlPanel::sendRoverCmd(char cmd) {
    if (m_lora) {
        m_lora->sendRaw(QByteArray(1, cmd));
    }
}

void RoverControlPanel::onForwardPressed() {
    sendRoverCmd('w');
    m_forwardBtn->setDown(true);
}
void RoverControlPanel::onForwardReleased() {
    sendRoverCmd('r');
    m_forwardBtn->setDown(false);
}

void RoverControlPanel::onBackwardPressed() {
    sendRoverCmd('s');
    m_backwardBtn->setDown(true);
}
void RoverControlPanel::onBackwardReleased() {
    sendRoverCmd('r');
    m_backwardBtn->setDown(false);
}

void RoverControlPanel::onLeftPressed() {
    sendRoverCmd('a');
    m_leftBtn->setDown(true);
}
void RoverControlPanel::onLeftReleased() {
    sendRoverCmd('r');
    m_leftBtn->setDown(false);
}

void RoverControlPanel::onRightPressed() {
    sendRoverCmd('d');
    m_rightBtn->setDown(true);
}
void RoverControlPanel::onRightReleased() {
    sendRoverCmd('r');
    m_rightBtn->setDown(false);
}

void RoverControlPanel::onSprayPressed() {
    sendRoverCmd('o');
    m_sprayBtn->setText("💧 喷水中...");
    m_sprayBtn->setDown(true);
}

void RoverControlPanel::onSprayReleased() {
    sendRoverCmd('f');
    m_sprayBtn->setText("💧 喷水");
    m_sprayBtn->setDown(false);
}

void RoverControlPanel::keyPressEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) return;
    switch (event->key()) {
    case Qt::Key_W: m_keyW = true;  onForwardPressed();  break;
    case Qt::Key_A: m_keyA = true;  onLeftPressed();     break;
    case Qt::Key_S: m_keyS = true;  onBackwardPressed(); break;
    case Qt::Key_D: m_keyD = true;  onRightPressed();    break;
    default: QWidget::keyPressEvent(event); break;
    }
}

void RoverControlPanel::keyReleaseEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) return;
    switch (event->key()) {
    case Qt::Key_W: m_keyW = false; onForwardReleased();  break;
    case Qt::Key_A: m_keyA = false; onLeftReleased();     break;
    case Qt::Key_S: m_keyS = false; onBackwardReleased(); break;
    case Qt::Key_D: m_keyD = false; onRightReleased();    break;
    default: QWidget::keyReleaseEvent(event); break;
    }
}

void RoverControlPanel::closeEvent(QCloseEvent *event) {
    stopVideoCapture();
    emit closed();
    QWidget::closeEvent(event);
}

void RoverControlPanel::onVideoFrame() {
    if (!m_ffmpegProc) return;

    m_videoBuf.append(m_ffmpegProc->readAllStandardOutput());

    const int frameSize = m_frameW * m_frameH * 3;
    while (m_videoBuf.size() >= frameSize) {
        QByteArray frameData = m_videoBuf.left(frameSize);
        m_videoBuf.remove(0, frameSize);

        QImage img(reinterpret_cast<const uchar*>(frameData.constData()),
                   m_frameW, m_frameH, QImage::Format_RGB888);
        QImage copy = img.copy();
        if (!copy.isNull()) {
            QPixmap pix = QPixmap::fromImage(copy);
            int w = qMax(m_videoLabel->width(), 200);
            int h = w * m_frameH / m_frameW;
            m_videoLabel->setPixmap(pix.scaled(QSize(w, h),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_videoLabel->setText(QString());
        }
    }
}

void RoverControlPanel::startVideoCapture() {
    if (m_ffmpegProc) return;

    m_ffmpegProc = new QProcess(this);
    m_ffmpegProc->setProcessChannelMode(QProcess::SeparateChannels);

    QStringList args;
    args << "-f" << "v4l2"
         << "-input_format" << "yuyv422"
         << "-video_size" << "640x480"
         << "-framerate" << "30"
         << "-i" << "/dev/video0"
         << "-f" << "rawvideo"
         << "-pix_fmt" << "rgb24"
         << "-vf" << "scale=640:480"
         << "pipe:1";

    qDebug() << "[ROVER] 启动图传采集: ffmpeg" << args;
    m_ffmpegProc->start("ffmpeg", args);

    if (!m_ffmpegProc->waitForStarted(5000)) {
        qDebug() << "[ROVER] ffmpeg 启动失败";
        delete m_ffmpegProc;
        m_ffmpegProc = nullptr;
        m_videoLabel->setText("图传连接失败\n请检查 /dev/video0");
        return;
    }

    m_frameW = 640;
    m_frameH = 480;

    connect(m_ffmpegProc, &QProcess::readyReadStandardOutput,
            this, &RoverControlPanel::onVideoFrame);
    connect(m_ffmpegProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        qDebug() << "[ROVER] ffmpeg 进程错误";
        m_videoLabel->setText("图传连接断开");
    });

    m_videoLabel->setText("图传连接中...");
}

void RoverControlPanel::stopVideoCapture() {
    if (m_ffmpegProc && m_ffmpegProc->state() == QProcess::Running) {
        m_ffmpegProc->kill();
        m_ffmpegProc->waitForFinished(2000);
    }
    delete m_ffmpegProc;
    m_ffmpegProc = nullptr;
}
