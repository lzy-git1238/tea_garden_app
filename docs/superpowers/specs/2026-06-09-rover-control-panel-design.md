# 地面小车控制面板设计

> 新增独立页面，通过 LoRa 控制地面小车，实时显示 USB 图传视频。

## 背景

现有系统已有 LoRa 串口（一主多从：传感器 + 小车）。RK3588 上插有 USB 图传接收端（/dev/video*）。需要新增控制面板。

### 硬件配置

- LoRa：共用现有串口（TEA_LORA_PORT），一主多从
- 图传：USB 接收端，设备节点 `/dev/video*`
- 小车指令集：w(前进) a(左转) s(后退) d(右转) T(喷水开) F(喷水关)

## 架构

```
RoverControlPanel（独立 QWidget 窗口）
├── 视频区：V4L2 + ffmpeg 采集 USB 图传画面 → QLabel 显示
├── 控制区：
│   ├── WASD 方向（按钮 + 键盘）
│   │   ├── W → loraManager->sendCommand('w') → 前进
│   │   ├── A → sendCommand('a') → 左转
│   │   ├── S → sendCommand('s') → 后退
│   │   └── D → sendCommand('d') → 右转
│   └── 喷水按钮
│       ├── pressed() → sendCommand('T') → 开喷
│       └── released() → sendCommand('F') → 停喷
└── 状态栏：LoRa 连接状态
```

## 组件设计

### 1. RoverControlPanel 类

新增文件：`rovercontrolpanel.h` / `rovercontrolpanel.cpp`

```cpp
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
    void onVideoFrame(const QImage &frame);

private:
    void setupUI();
    void sendRoverCmd(char cmd);
    void startVideoCapture();
    void stopVideoCapture();

    LoraManager *m_lora;
    QProcess *m_ffmpegProc = nullptr;
    QLabel *m_videoLabel;
    QLabel *m_statusLabel;
    bool m_keyW = false, m_keyA = false, m_keyS = false, m_keyD = false;
};
```

### 2. LoRa 命令发送

复用现有 `LoraManager::sendCommand()`，但当前接口是 `sendCommand(int value)`。

**改动**：在 `LoraManager` 中新增 `sendRaw(const QByteArray &data)` 方法，直接发送原始字节。

```cpp
// loramanager.h 新增
bool sendRaw(const QByteArray &data);

// loramanager.cpp 实现
bool LoraManager::sendRaw(const QByteArray &data) {
    if (!serialPort || !serialPort->isOpen()) return false;
    return serialPort->write(data) == data.size();
}
```

调用方式：
```cpp
m_lora->sendRaw(QByteArray("w"));  // 前进
m_lora->sendRaw(QByteArray("T"));  // 喷水开
```

### 3. 视频采集

V4L2 + ffmpeg 管道方式，与 SrtVideoStreamer 类似：

```cpp
void RoverControlPanel::startVideoCapture() {
    m_ffmpegProc = new QProcess(this);
    QStringList args;
    args << "-f" << "v4l2"
         << "-i" << "/dev/video0"
         << "-f" << "rawvideo"
         << "-pix_fmt" << "rgb24"
         << "-vf" << "scale=640:480"
         << "pipe:1";
    m_ffmpegProc->start("ffmpeg", args);
    // 读取 stdout 帧数据，构造 QImage 显示
}
```

### 4. 键盘控制

```cpp
void RoverControlPanel::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_W: if (!m_keyW) { m_keyW = true;  sendRoverCmd('w'); } break;
    case Qt::Key_A: if (!m_keyA) { m_keyA = true;  sendRoverCmd('a'); } break;
    case Qt::Key_S: if (!m_keyS) { m_keyS = true;  sendRoverCmd('s'); } break;
    case Qt::Key_D: if (!m_keyD) { m_keyD = true;  sendRoverCmd('d'); } break;
    }
}

void RoverControlPanel::keyReleaseEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_W: m_keyW = false; break;
    case Qt::Key_A: m_keyA = false; break;
    case Qt::Key_S: m_keyS = false; break;
    case Qt::Key_D: m_keyD = false; break;
    }
}
```

键盘控制只发一次按下指令（不重复发送），松开不发停止（小车自行超时停止）。

### 5. 按钮控制

方向按钮用 `pressed()`/`released()` 信号：

```cpp
connect(m_forwardBtn, &QPushButton::pressed, this, &RoverControlPanel::onForwardPressed);
connect(m_forwardBtn, &QPushButton::released, this, &RoverControlPanel::onForwardReleased);
// ... 其他方向同理

connect(m_sprayBtn, &QPushButton::pressed, this, &RoverControlPanel::onSprayPressed);
connect(m_sprayBtn, &QPushButton::released, this, &RoverControlPanel::onSprayReleased);
```

按钮控制：按下持续发送（每 200ms 重复），松开发送停止指令。

### 6. UI 布局

```
┌─────────────────────────────────────────────┐
│              地面小车控制                     │
├──────────────────┬──────────────────────────┤
│                  │         [W]              │
│   视频画面        │    [A]  [S]  [D]        │
│   (图传实时)      │                         │
│                  │    [💧 喷水]             │
│                  │                         │
├──────────────────┴──────────────────────────┤
│ 状态: LoRa 已连接 | 图传: /dev/video0       │
└─────────────────────────────────────────────┘
```

### 7. 入口

在 `TeaGardenSystem` 主界面添加"🚗 小车控制"按钮，点击打开 `RoverControlPanel`。

## 文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `rovercontrolpanel.h` | 新增 | RoverControlPanel 类声明 |
| `rovercontrolpanel.cpp` | 新增 | 控制面板实现（UI + 键盘 + 视频 + LoRa） |
| `loramanager.h` | 修改 | 新增 `sendRaw()` 方法 |
| `loramanager.cpp` | 修改 | 实现 `sendRaw()` |
| `teagardensystem.h` | 修改 | 添加 `openRoverPanel()` 声明 |
| `teagardensystem.cpp` | 修改 | 添加"小车控制"按钮 + 打开面板 |
| `test.pro` | 修改 | 添加 rovercontrolpanel.cpp/h |

## 错误处理

| 场景 | 处理方式 |
|------|---------|
| USB 图传未连接 | 视频区显示"等待图传连接..."，按钮仍可用 |
| LoRa 未连接 | 状态栏提示，按钮置灰 |
| ffmpeg 启动失败 | 重试或提示用户检查设备 |

## 键盘控制行为

| 按键 | LoRa 指令 | 动作 |
|------|----------|------|
| W | 'w' | 前进 |
| A | 'a' | 左转 |
| S | 's' | 后退 |
| D | 'd' | 右转 |
| 松开任意键 | 不发送 | 小车自行超时停止 |
