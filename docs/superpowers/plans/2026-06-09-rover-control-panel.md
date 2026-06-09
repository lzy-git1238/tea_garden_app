# 地面小车控制面板 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增独立小车控制面板，通过 LoRa 发送 WASD 指令控制小车，实时显示 USB 图传视频。

**Architecture:** 新增 `RoverControlPanel` 类（独立 QWidget），通过现有 `LoraManager` 发送字符指令，ffmpeg V4L2 管道采集 USB 图传画面。

**Tech Stack:** Qt C++ (QWidget, QProcess, QSerialPort, QKeyEvent), ffmpeg V4L2

---

## File Structure

| 文件 | 操作 | 说明 |
|------|------|------|
| `test/rovercontrolpanel.h` | 新增 | RoverControlPanel 类声明 |
| `test/rovercontrolpanel.cpp` | 新增 | 控制面板实现 |
| `test/loramanager.h` | 修改 | 新增 `sendRaw()` 方法 |
| `test/loramanager.cpp` | 修改 | 实现 `sendRaw()` |
| `test/teagardensystem.h` | 修改 | 添加 `openRoverPanel()` 声明 |
| `test/teagardensystem.cpp` | 修改 | 添加按钮 + 打开面板 |
| `test/test.pro` | 修改 | 添加 rovercontrolpanel 源文件 |

---

### Task 1: LoraManager 新增 sendRaw 方法

**Files:**
- Modify: `test/loramanager.h:20`
- Modify: `test/loramanager.cpp:97`

现有 `sendCommand(int value)` 发送整数文本。需要新增 `sendRaw(QByteArray)` 发送原始字符（如 'w'、'T'）。

- [ ] **Step 1: 在 loramanager.h 中添加 sendRaw 声明**

在 `sendCommand(int value)` 下方添加：

```cpp
    bool sendCommand(int value);
    bool sendRaw(const QByteArray &data);
```

- [ ] **Step 2: 在 loramanager.cpp 中实现 sendRaw**

在 `sendCommand` 函数之后添加：

```cpp
bool LoraManager::sendRaw(const QByteArray &data) {
    if (!serialPort->isOpen()) {
        emit statusChanged("LoRa 未连接，无法发送命令");
        return false;
    }

    const QByteArray payload = data + "\n";
    // LoRa 无线链路存在丢包风险，重复发送以带宽换可靠性
    const int repeatCount = 50;
    for (int i = 0; i < repeatCount; ++i) {
        const qint64 written = serialPort->write(payload);
        if (written != payload.size()) {
            emit statusChanged("LoRa 命令发送失败");
            return false;
        }
    }
    serialPort->flush();
    return true;
}
```

- [ ] **Step 3: Commit**

```bash
git add test/loramanager.h test/loramanager.cpp
git commit -m "feat: LoraManager 新增 sendRaw 方法"
```

---

### Task 2: RoverControlPanel 类声明

**Files:**
- Create: `test/rovercontrolpanel.h`

- [ ] **Step 1: 创建 rovercontrolpanel.h**

```cpp
#ifndef ROVERCONTROLPANEL_H
#define ROVERCONTROLPANEL_H

#include <QWidget>
#include <QProcess>
#include <QElapsedTimer>

class QLabel;
class QPushButton;
class LoraManager;

class RoverControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit RoverControlPanel(LoraManager *lora, QWidget *parent = nullptr);
    ~RoverControlPanel();

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onForwardPressed();
    void onForwardReleased();
    void onBackwardPressed();
    void onBackwardReleased();
    void onLeftPressed();
    void onLeftReleased();
    void onRightPressed();
    void onRightReleased();
    void onSprayPressed();
    void onSprayReleased();
    void onVideoFrame();

private:
    void setupUI();
    void sendRoverCmd(char cmd);
    void startVideoCapture();
    void stopVideoCapture();

    LoraManager *m_lora;
    QProcess *m_ffmpegProc = nullptr;
    QLabel *m_videoLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_forwardBtn = nullptr;
    QPushButton *m_backwardBtn = nullptr;
    QPushButton *m_leftBtn = nullptr;
    QPushButton *m_rightBtn = nullptr;
    QPushButton *m_sprayBtn = nullptr;

    QByteArray m_videoBuf;
    int m_frameW = 640;
    int m_frameH = 480;

    bool m_keyW = false;
    bool m_keyA = false;
    bool m_keyS = false;
    bool m_keyD = false;

    QTimer *m_repeatTimer = nullptr;
    char m_lastCmd = 0;
};

#endif // ROVERCONTROLPANEL_H
```

- [ ] **Step 2: Commit**

```bash
git add test/rovercontrolpanel.h
git commit -m "feat: RoverControlPanel 类声明"
```

---

### Task 3: RoverControlPanel UI 布局

**Files:**
- Create: `test/rovercontrolpanel.cpp`

- [ ] **Step 1: 创建 rovercontrolpanel.cpp，实现 setupUI 和基础框架**

```cpp
#include "rovercontrolpanel.h"
#include "loramanager.h"
#include <QDebug>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QScreen>
#include <QApplication>

RoverControlPanel::RoverControlPanel(LoraManager *lora, QWidget *parent)
    : QWidget(parent), m_lora(lora)
{
    setWindowTitle("地面小车控制");
    setFixedSize(800, 500);
    setStyleSheet(
        "RoverControlPanel { background: #0a1628; color: #E8F4FF; }"
        "QLabel { color: #E8F4FF; }"
    );
    setFocusPolicy(Qt::StrongFocus);
    setupUI();

    // 方向按钮持续发送定时器（每 200ms 重复）
    m_repeatTimer = new QTimer(this);
    m_repeatTimer->setInterval(200);
    connect(m_repeatTimer, &QTimer::timeout, this, [this]() {
        if (m_lastCmd != 0) {
            sendRoverCmd(m_lastCmd);
        }
    });
}

RoverControlPanel::~RoverControlPanel() {
    stopVideoCapture();
}

void RoverControlPanel::setupUI() {
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // ── 左侧：视频区 ──
    auto *leftPanel = new QVBoxLayout();
    m_videoLabel = new QLabel("等待图传连接...\n(/dev/video0)");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(400, 300);
    m_videoLabel->setStyleSheet(
        "background: #000; border: 2px solid #1a3040; border-radius: 8px;"
        "color: #666; font-size: 14px;");
    leftPanel->addWidget(m_videoLabel, 1);

    m_statusLabel = new QLabel("LoRa: 初始化中...");
    m_statusLabel->setStyleSheet("color: #90A4AE; font-size: 12px; padding: 4px;");
    leftPanel->addWidget(m_statusLabel);

    mainLayout->addLayout(leftPanel, 2);

    // ── 右侧：控制区 ──
    auto *rightPanel = new QVBoxLayout();
    rightPanel->setSpacing(8);

    auto *titleLabel = new QLabel("🎮 方向控制 (WASD)");
    titleLabel->setStyleSheet("color: #00d4ff; font-size: 16px; font-weight: bold;");
    rightPanel->addWidget(titleLabel);

    // 方向按钮网格
    auto *dirGrid = new QGridLayout();
    dirGrid->setSpacing(6);

    auto *dirBtnStyle = [](const QString &text) {
        return QString(
            "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 #1a4060, stop:1 #0d2030); color: #90caf9; font-size: 20px;"
            "font-weight: bold; padding: 15px; border: 1px solid #2a5070;"
            "border-radius: 8px; min-width: 60px; min-height: 40px; }"
            "QPushButton:pressed { background: #2a6090; color: #ffffff; }"
        );
    };

    m_forwardBtn = new QPushButton("W\n⬆ 前进");
    m_forwardBtn->setStyleSheet(dirBtnStyle("W"));
    connect(m_forwardBtn, &QPushButton::pressed, this, &RoverControlPanel::onForwardPressed);
    connect(m_forwardBtn, &QPushButton::released, this, &RoverControlPanel::onForwardReleased);
    dirGrid->addWidget(m_forwardBtn, 0, 1);

    m_leftBtn = new QPushButton("A\n⬅ 左转");
    m_leftBtn->setStyleSheet(dirBtnStyle("A"));
    connect(m_leftBtn, &QPushButton::pressed, this, &RoverControlPanel::onLeftPressed);
    connect(m_leftBtn, &QPushButton::released, this, &RoverControlPanel::onLeftReleased);
    dirGrid->addWidget(m_leftBtn, 1, 0);

    m_backwardBtn = new QPushButton("S\n⬇ 后退");
    m_backwardBtn->setStyleSheet(dirBtnStyle("S"));
    connect(m_backwardBtn, &QPushButton::pressed, this, &RoverControlPanel::onBackwardPressed);
    connect(m_backwardBtn, &QPushButton::released, this, &RoverControlPanel::onBackwardReleased);
    dirGrid->addWidget(m_backwardBtn, 1, 1);

    m_rightBtn = new QPushButton("D\n➡ 右转");
    m_rightBtn->setStyleSheet(dirBtnStyle("D"));
    connect(m_rightBtn, &QPushButton::pressed, this, &RoverControlPanel::onRightPressed);
    connect(m_rightBtn, &QPushButton::released, this, &RoverControlPanel::onRightReleased);
    dirGrid->addWidget(m_rightBtn, 1, 2);

    rightPanel->addLayout(dirGrid);

    // 喷水按钮
    auto *sprayLabel = new QLabel("💧 喷水控制");
    sprayLabel->setStyleSheet("color: #00d4ff; font-size: 14px; font-weight: bold; margin-top: 8px;");
    rightPanel->addWidget(sprayLabel);

    m_sprayBtn = new QPushButton("💧 喷水");
    m_sprayBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #1565C0, stop:1 #0D47A1); color: #ffffff; font-size: 16px;"
        "font-weight: bold; padding: 12px; border: 1px solid #1E88E5;"
        "border-radius: 8px; }"
        "QPushButton:pressed { background: #0D47A1; }"
    );
    m_sprayBtn->setCheckable(true);
    connect(m_sprayBtn, &QPushButton::pressed, this, &RoverControlPanel::onSprayPressed);
    connect(m_sprayBtn, &QPushButton::released, this, &RoverControlPanel::onSprayReleased);
    rightPanel->addWidget(m_sprayBtn);

    rightPanel->addStretch();
    mainLayout->addLayout(rightPanel, 1);
}

void RoverControlPanel::sendRoverCmd(char cmd) {
    if (m_lora) {
        m_lora->sendRaw(QByteArray(1, cmd));
    }
}

// ── 方向控制 ──

void RoverControlPanel::onForwardPressed() {
    m_lastCmd = 'w';
    sendRoverCmd('w');
    m_repeatTimer->start();
}
void RoverControlPanel::onForwardReleased() {
    m_lastCmd = 0;
    m_repeatTimer->stop();
}

void RoverControlPanel::onBackwardPressed() {
    m_lastCmd = 's';
    sendRoverCmd('s');
    m_repeatTimer->start();
}
void RoverControlPanel::onBackwardReleased() {
    m_lastCmd = 0;
    m_repeatTimer->stop();
}

void RoverControlPanel::onLeftPressed() {
    m_lastCmd = 'a';
    sendRoverCmd('a');
    m_repeatTimer->start();
}
void RoverControlPanel::onLeftReleased() {
    m_lastCmd = 0;
    m_repeatTimer->stop();
}

void RoverControlPanel::onRightPressed() {
    m_lastCmd = 'd';
    sendRoverCmd('d');
    m_repeatTimer->start();
}
void RoverControlPanel::onRightReleased() {
    m_lastCmd = 0;
    m_repeatTimer->stop();
}

// ── 喷水控制 ──

void RoverControlPanel::onSprayPressed() {
    sendRoverCmd('T');
    m_sprayBtn->setText("💧 喷水中...");
}

void RoverControlPanel::onSprayReleased() {
    sendRoverCmd('F');
    m_sprayBtn->setText("💧 喷水");
}

// ── 键盘控制 ──

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
    // 视频帧处理（Task 4 实现）
}

void RoverControlPanel::startVideoCapture() {
    // 视频采集启动（Task 4 实现）
}

void RoverControlPanel::stopVideoCapture() {
    if (m_ffmpegProc && m_ffmpegProc->state() == QProcess::Running) {
        m_ffmpegProc->kill();
        m_ffmpegProc->waitForFinished(2000);
    }
    delete m_ffmpegProc;
    m_ffmpegProc = nullptr;
}
```

- [ ] **Step 2: Commit**

```bash
git add test/rovercontrolpanel.cpp
git commit -m "feat: RoverControlPanel UI + 键盘控制 + 方向/喷水按钮"
```

---

### Task 4: 视频采集（V4L2 + ffmpeg）

**Files:**
- Modify: `test/rovercontrolpanel.cpp` 中的 `startVideoCapture` 和 `onVideoFrame`

- [ ] **Step 1: 实现 startVideoCapture**

替换 `startVideoCapture` 的空实现：

```cpp
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
```

- [ ] **Step 2: 实现 onVideoFrame**

替换 `onVideoFrame` 的空实现：

```cpp
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
```

- [ ] **Step 3: 在构造函数中自动启动视频采集**

在 `setupUI()` 调用之后添加：

```cpp
    startVideoCapture();
```

- [ ] **Step 4: Commit**

```bash
git add test/rovercontrolpanel.cpp
git commit -m "feat: RoverControlPanel V4L2 图传视频采集"
```

---

### Task 5: TeaGardenSystem 集成入口

**Files:**
- Modify: `test/teagardensystem.h`
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: 在 teagardensystem.h 中添加声明**

添加前向声明和成员变量：

```cpp
class RoverControlPanel;
```

在成员变量区域添加：

```cpp
    class RoverControlPanel *roverPanel = nullptr;
```

在 private slots 区域添加：

```cpp
    void openRoverPanel();
```

- [ ] **Step 2: 在 teagardensystem.cpp 中添加 include**

在文件顶部添加：

```cpp
#include "rovercontrolpanel.h"
```

- [ ] **Step 3: 添加 openRoverPanel 实现**

在文件末尾（或其他合适位置）添加：

```cpp
void TeaGardenSystem::openRoverPanel() {
    if (!roverPanel) {
        roverPanel = new RoverControlPanel(loraManager, this);
        connect(roverPanel, &RoverControlPanel::closed, this, [this]() {
            roverPanel = nullptr;
        });
    }
    roverPanel->show();
    roverPanel->raise();
    roverPanel->activateWindow();
    roverPanel->setFocus();
}
```

- [ ] **Step 4: 在 UI 中添加按钮**

在 `setupUI()` 中合适位置（如 leftPanel 底部）添加：

```cpp
    auto *roverBtn = new QPushButton("🚗 小车控制");
    connect(roverBtn, &QPushButton::clicked, this, &TeaGardenSystem::openRoverPanel);
    leftPanel->addWidget(roverBtn);
```

- [ ] **Step 5: Commit**

```bash
git add test/teagardensystem.h test/teagardensystem.cpp
git commit -m "feat: TeaGardenSystem 添加小车控制面板入口"
```

---

### Task 6: 更新 test.pro

**Files:**
- Modify: `test/test.pro`

- [ ] **Step 1: 在 SOURCES 和 HEADERS 中添加 rovercontrolpanel**

```
SOURCES += \
    ...
    rovercontrolpanel.cpp

HEADERS += \
    ...
    rovercontrolpanel.h
```

- [ ] **Step 2: Commit**

```bash
git add test/test.pro
git commit -m "build: test.pro 添加 rovercontrolpanel"
```

---

### Task 7: 端到端编译验证

- [ ] **Step 1: 桌面端编译**

```bash
cd /home/lzy/文档/QTproject/test
make -j$(nproc) 2>&1 | grep -E "error|错误" || echo "桌面端编译成功"
```

Expected: 编译成功

- [ ] **Step 2: 同步到 ELF2 并编译**

```bash
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh sync /home/lzy/文档/QTproject/test/
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh compile
```

Expected: ARM 交叉编译成功

- [ ] **Step 3: 最终 Commit（如有修复）**

```bash
git add -A
git commit -m "feat: 地面小车控制面板完成"
```
