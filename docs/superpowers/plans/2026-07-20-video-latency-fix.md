# 视频延迟修复：推送改拉取 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除 SRT 视频长时间运行后累积的 4-5 秒延迟，将推送模式改为拉取模式，延迟恒定 1 帧。

**Architecture:** `SrtVideoStreamer::run()` 不再每帧 emit 信号，改为写入带 mutex 的共享帧缓存。主线程用 QTimer 定频从缓存拉取最新帧显示和 YOLO 推理，自然丢弃堆积的旧帧。

**Tech Stack:** Qt 5, QThread, QMutex, QTimer, ffmpeg (pipe)

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `test/srtvideostreamer.h` | 删除 `frameReadyForYolo` 信号；新增 `QMutex`、`QImage m_latestFrame`、`latestFrame()` 方法 |
| `test/srtvideostreamer.cpp` | `run()` 中 emit 替换为 mutex 写共享帧 |
| `test/teagardensystem.h` | 新增 3 个 `QTimer*` 成员 |
| `test/teagardensystem.cpp` | 删除 frameReady/frameReadyForYolo connect；新增 Timer 拉取驱动显示和 YOLO |
| `test/yolodetector.cpp` | `onNewFrame()` 中删除 FPS 限流检查 |

---

### Task 1: SrtVideoStreamer — 共享帧缓存 + latestFrame()

**Files:**
- Modify: `test/srtvideostreamer.h`
- Modify: `test/srtvideostreamer.cpp`

- [ ] **Step 1: 修改 srtvideostreamer.h — 删除信号、新增成员和方法**

当前文件见 `test/srtvideostreamer.h`。完整替换为：

```cpp
#ifndef SRTVIDEOSTREAMER_H
#define SRTVIDEOSTREAMER_H

#include <QThread>
#include <QImage>
#include <QString>
#include <QElapsedTimer>
#include <QProcess>
#include <QMutex>
#include <atomic>

class SrtVideoStreamer : public QThread {
    Q_OBJECT
public:
    explicit SrtVideoStreamer(QObject *parent = nullptr);
    ~SrtVideoStreamer();

    void setUrl(const QString &url);
    void setResolution(int w, int h);
    void setHardwareDecoding(bool enable);
    void stop();

    /// 取最新解码帧（线程安全，返回深拷贝）
    QImage latestFrame() const;

signals:
    void frameReady(const QImage &image);       // 保留：timer 拉取后仍用此信号通知
    void statusMessage(const QString &msg);
    void streamConnected();
    void streamDisconnected();
    void fpsUpdated(double fps);

protected:
    void run() override;

private:
    QString m_url;
    bool    m_hwDecode = true;
    std::atomic<bool> m_running{false};

    int     m_frameCount = 0;
    QElapsedTimer m_fpsTimer;
    int     m_resWidth  = 640;
    int     m_resHeight = 480;
    bool    m_haveResolution = false;

    // 共享帧缓存（ffmpeg 线程写，主线程读）
    mutable QMutex m_frameMutex;
    QImage m_latestFrame;
};

#endif // SRTVIDEOSTREAMER_H
```

关键变化：
- 新增 `#include <QMutex>`
- 新增 `mutable QMutex m_frameMutex;` 和 `QImage m_latestFrame;`
- 新增 `QImage latestFrame() const;` 公开方法
- **删除 `frameReadyForYolo` 信号**（两路 emit 合并为单路 `latestFrame()` + timer）
- **保留 `frameReady` 信号**（timer 拉取后仍然通过此信号通知下游，保持兼容）

- [ ] **Step 2: 修改 srtvideostreamer.cpp — run() 中 emit 替换为 mutex 写 + 新增 latestFrame()**

修改 `run()` 中第 127-129 行：

```cpp
// 旧代码（删除）：
// emit frameReady(copy);
// emit frameReadyForYolo(copy);

// 新代码：
{
    QMutexLocker lock(&m_frameMutex);
    m_latestFrame = copy;   // QImage 隐式共享，仅 refcount++
}
```

即把 [srtvideostreamer.cpp:127-129](test/srtvideostreamer.cpp#L127-L129) 改为：

```cpp
                QImage copy = img.copy();
                if (!copy.isNull()) {
                    {
                        QMutexLocker lock(&m_frameMutex);
                        m_latestFrame = copy;
                    }
                    m_frameCount++;
                    if (m_fpsTimer.elapsed() >= 2000) {
                        double fps = m_frameCount * 1000.0 / m_fpsTimer.elapsed();
                        emit fpsUpdated(fps);
                        m_frameCount = 0;
                        m_fpsTimer.restart();
                    }
                }
```

在文件末尾（第 150 行 `}` 之前）新增 `latestFrame()` 实现：

```cpp
QImage SrtVideoStreamer::latestFrame() const {
    QMutexLocker lock(&m_frameMutex);
    return m_latestFrame.isNull() ? QImage() : m_latestFrame.copy();
}
```

- [ ] **Step 3: 编译验证**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4
```

`srtvideostreamer.o` 编译通过即可（链接 -lrknnrt 错误是预现有问题）。

- [ ] **Step 4: Commit**

```bash
git add test/srtvideostreamer.h test/srtvideostreamer.cpp
git commit -m "feat: SrtVideoStreamer 共享帧缓存 + latestFrame()，删除 frameReadyForYolo"
```

---

### Task 2: TeaGardenSystem — Timer 拉取代替信号推送

**Files:**
- Modify: `test/teagardensystem.h`
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: 在 teagardensystem.h 新增 3 个 QTimer 成员**

在 `teagardensystem.h` 中，找到 `SrtVideoStreamer *m_videoStreamer = nullptr;` 附近（约 187 行），在附近加：

```cpp
    // 视频拉取 timer（替代 frameReady 信号推送模式）
    QTimer *m_videoPullTimer = nullptr;   // 无人机显示 ~30fps
    QTimer *m_roverPullTimer = nullptr;   // 小车显示 ~30fps
    QTimer *m_yoloPullTimer  = nullptr;   // YOLO 推理 ~3fps
```

- [ ] **Step 2: 修改 teagardensystem.cpp — 无人机视频：删除 frameReady/frameReadyForYolo connect，替换为 Timer 拉取**

找到 [teagardensystem.cpp:126-141](test/teagardensystem.cpp#L126-L141)，删除这两行 connect：

```cpp
// 删除这两行：
    connect(m_videoStreamer, &SrtVideoStreamer::frameReady,
            this, &TeaGardenSystem::onVideoFrame);
    // ...中间的 statusMessage 和 fpsUpdated 保留...
    connect(m_videoStreamer, &SrtVideoStreamer::frameReadyForYolo,
            m_yoloDetector, &YoloDetector::onNewFrame);
```

替换为（在 `m_videoStreamer->start();` 之前插入）：

```cpp
    // 无人机显示：33ms timer 取最新帧刷新
    m_videoPullTimer = new QTimer(this);
    m_videoPullTimer->setInterval(33);
    connect(m_videoPullTimer, &QTimer::timeout, this, [this]() {
        QImage frame = m_videoStreamer->latestFrame();
        if (!frame.isNull()) onVideoFrame(frame);
    });
    m_videoPullTimer->start();

    // YOLO：333ms timer 取最新帧推理（匹配 3fps NPU 速率）
    m_yoloPullTimer = new QTimer(this);
    m_yoloPullTimer->setInterval(333);
    connect(m_yoloPullTimer, &QTimer::timeout, this, [this]() {
        QImage frame = m_videoStreamer->latestFrame();
        if (!frame.isNull()) m_yoloDetector->onNewFrame(frame);
    });
    m_yoloPullTimer->start();
```

注意：`statusMessage` 和 `fpsUpdated` 的 connect 保持不变——这两个信号不受影响。

- [ ] **Step 3: 修改 teagardensystem.cpp — 小车视频：删除 frameReady connect，替换为 Timer 拉取**

找到 [teagardensystem.cpp:236-245](test/teagardensystem.cpp#L236-L245)，删除整个 connect 块：

```cpp
// 删除这段：
    connect(m_roverStreamer, &SrtVideoStreamer::frameReady,
            this, [this](const QImage &frame) {
        if (!roverVideoLabel || frame.isNull()) return;
        QPixmap pix = QPixmap::fromImage(frame);
        int w = roverVideoLabel->width() - 4;
        int h = w * frame.height() / frame.width();
        roverVideoLabel->setPixmap(pix.scaled(QSize(w, h),
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
        roverVideoLabel->setText(QString());
    });
```

替换为（在 `m_roverStreamer->start();` 之前插入）：

```cpp
    // 小车显示：33ms timer 取最新帧刷新
    m_roverPullTimer = new QTimer(this);
    m_roverPullTimer->setInterval(33);
    connect(m_roverPullTimer, &QTimer::timeout, this, [this]() {
        QImage frame = m_roverStreamer->latestFrame();
        if (!frame.isNull() && roverVideoLabel) {
            QPixmap pix = QPixmap::fromImage(frame);
            int w = roverVideoLabel->width() - 4;
            int h = w * frame.height() / frame.width();
            roverVideoLabel->setPixmap(pix.scaled(QSize(w, h),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
            roverVideoLabel->setText(QString());
        }
    });
    m_roverPullTimer->start();
```

- [ ] **Step 4: 编译验证**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4
```

`teagardensystem.o` 编译通过即可。

- [ ] **Step 5: Commit**

```bash
git add test/teagardensystem.h test/teagardensystem.cpp
git commit -m "feat: 视频推送改拉取 — Timer 定频取最新帧，消除延迟累积"
```

---

### Task 3: YoloDetector — 删除 onNewFrame 中的 FPS 限流

**Files:**
- Modify: `test/yolodetector.cpp`（仅 `onNewFrame()` 方法）

- [ ] **Step 1: 删除 YoloDetector::onNewFrame 中的 FPS 限流代码**

Timer 已经以 333ms 间隔精准控制调用频率，`onNewFrame` 内不需要再限流。删除 [yolodetector.cpp:57-61](test/yolodetector.cpp#L57-L61)：

```cpp
// 删除前（第 56-76 行完整上下文）：
void YoloDetector::onNewFrame(const QImage &frame) {
    if (m_fpsTimer.isValid() &&              // ← 删除
        m_fpsTimer.elapsed() < 1000.0 / m_fpsLimit) {  // ← 删除
        return;                               // ← 删除
    }                                         // ← 删除
    m_fpsTimer.restart();                     // ← 删除

    if (!m_initialized) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestFrame = frame;
        m_hasFrame = true;
    }
    m_cv.notify_one();
    static int frame_cnt = 0;
    if (++frame_cnt % 60 == 0) {
        printf("[YOLO] Frames received: %d\n", frame_cnt);
        fflush(stdout);
    }
}
```

改为：

```cpp
void YoloDetector::onNewFrame(const QImage &frame) {
    if (!m_initialized) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestFrame = frame;
        m_hasFrame = true;
    }
    m_cv.notify_one();
}
```

保留 `m_fpsTimer` 和 `m_fpsLimit` 成员变量不删（头文件不改），避免连锁改动干扰其他编译单元。只删 `.cpp` 中的逻辑。

- [ ] **Step 2: 编译验证**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4
```

`yolodetector.o` 编译通过即可。

- [ ] **Step 3: Commit**

```bash
git add test/yolodetector.cpp
git commit -m "feat: YOLO onNewFrame 删除 FPS 限流，由 Timer 控制频率"
```

---

### Task 4: ELF2 板子编译 + 运行验证

**Files:** 无需修改

- [ ] **Step 1: 同步源码到板子并编译**

```bash
sshpass -p "elf" rsync -avz --delete \
  --exclude='*.o' --exclude='moc_*' --exclude='moc_predefs.h' \
  --exclude='ui_widget.h' --exclude='qrc_*.cpp' \
  --exclude='Makefile' --exclude='.qmake.stash' \
  -e "ssh -o StrictHostKeyChecking=no" \
  /home/lzy/文档/QTproject/test/ elf@10.200.0.102:~/QTproject/test/

sshpass -p "elf" ssh -o StrictHostKeyChecking=no elf@10.200.0.102 \
  "mkdir -p ~/QTproject/build-arm-Debug && cd ~/QTproject/build-arm-Debug && \
   rm -f Makefile .qmake.stash *.o moc_*.cpp moc_predefs.h ui_widget.h qrc_*.cpp test && \
   qmake ../test/test.pro && make -j\$(nproc)"
```

Expected: 编译链接全部通过，无错误。

- [ ] **Step 2: 运行验证（如有 SRT 流源可测）**

```bash
sshpass -p "elf" ssh -o StrictHostKeyChecking=no elf@10.200.0.102 \
  "cd ~/QTproject/build-arm-Debug && timeout 30 ./test 2>&1 | head -30"
```

Expected: 程序正常启动，无崩溃。

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "verify: 视频拉取模式 ELF2 编译通过"
```
