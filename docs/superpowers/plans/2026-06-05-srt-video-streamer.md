# SRT 无人机视频流 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 FFmpeg (h264_rkmpp 硬解) + SRT 协议替换 WebSocket+Base64 JPEG 视频方案，在独立 QThread 中拉流解码，QImage 深拷贝到主线程渲染，预留 YOLO 零拷贝接口。

**Architecture:** 新增 `SrtVideoStreamer` 类（QThread 子类），`extern"C"` 包裹 FFmpeg C API，QThread::run() 中一帧循环完成 SRT 拉流 → h264_rkmpp 硬解 → 动态 pix_fmt → sws_scale YUV→RGB24 → emit QImage/QImage.copy()。TeaGardenSystem 只保留 ~15 行胶水代码。

**Tech Stack:** FFmpeg 6.0.1 (libavformat/libavcodec/libswscale), Qt 5, RK3588 rkmpp, SRT protocol

**Spec:** `docs/superpowers/specs/2026-06-05-srt-video-streamer-design.md`

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `test/srtvideostreamer.h` | 新建 | SrtVideoStreamer 类声明 + FFmpeg 前向声明 |
| `test/srtvideostreamer.cpp` | 新建 | FFmpeg 拉流/硬解/像素转换/QImage emit |
| `test/test.pro` | 修改 | FFmpeg sysroot include + libs |
| `test/teagardensystem.h` | 修改 | 新增 streamer 成员 + onVideoFrame 槽声明 |
| `test/teagardensystem.cpp` | 修改 | 构造函数集成新 streamer、旧代码 #ifdef 包裹 |

---

### Task 1: 更新 test.pro

**Files:**
- Modify: `test/test.pro`

- [ ] **Step 1: Add FFmpeg include path and libs**

After the `CONFIG += c++17` line (currently line 10), add:

```qmake
# FFmpeg 满血版（手动编译，含 rkmpp + libsrt + rkrga）
INCLUDEPATH += /home/elf/qt_sdk_aarch64/aarch64-buildroot-linux-gnu/sysroot/usr/include
LIBS += -lavformat -lavcodec -lavutil -lswscale
```

- [ ] **Step 2: Commit**

```bash
cd /home/lzy/文档/QTproject
git add test/test.pro
git commit -m "feat: add FFmpeg sysroot include and libs for SRT video"
```

---

### Task 2: 创建 srtvideostreamer.h

**Files:**
- Create: `test/srtvideostreamer.h`

- [ ] **Step 1: Write srtvideostreamer.h**

Create `/home/lzy/文档/QTproject/test/srtvideostreamer.h`:

```cpp
#ifndef SRTVIDEOSTREAMER_H
#define SRTVIDEOSTREAMER_H

#include <QThread>
#include <QImage>
#include <QString>
#include <memory>
#include <atomic>

// FFmpeg C 结构体前向声明
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct SwsContext;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;

class SrtVideoStreamer : public QThread {
    Q_OBJECT
public:
    explicit SrtVideoStreamer(QObject *parent = nullptr);
    ~SrtVideoStreamer();

    // 设置 SRT 流地址，如 "srt://0.0.0.0:9000?mode=listener"
    void setUrl(const QString &url);

    // 是否开启硬件解码（默认 true，优先 h264_rkmpp）
    void setHardwareDecoding(bool enable);

    // 安全停止解码线程
    void stop();

signals:
    // 解码后的 RGB 图像帧（深拷贝，可在主线程安全使用）
    void frameReady(const QImage &image);

    // YOLO (RKNN) 预留接口：零拷贝 RGB24 指针
    // 调用方需在下一帧到达前消费完毕，否则数据被覆盖
    void frameReadyForYolo(const uint8_t *rgb24,
                           int width, int height, int linesize);

    void statusMessage(const QString &msg);
    void streamConnected();
    void streamDisconnected();
    void fpsUpdated(double fps);

protected:
    void run() override;

private:
    // 初始化 FFmpeg 上下文（打开流 + 查找解码器）
    bool openStream();
    // 销毁 FFmpeg 上下文
    void closeStream();

    struct Context {
        AVFormatContext *fmtCtx  = nullptr;
        AVCodecContext  *codecCtx = nullptr;
        const AVCodec   *codec   = nullptr;
        SwsContext      *swsCtx  = nullptr;
        AVFrame         *frame   = nullptr;
        AVFrame         *rgbFrame = nullptr;
        AVPacket        *packet  = nullptr;
        int              videoStreamIdx = -1;
        AVBufferRef     *hwDeviceCtx = nullptr;
    };

    inline bool ensureSwscale(int srcW, int srcH, int srcFmt);

    QString m_url;
    bool    m_hwDecode = true;
    std::atomic<bool> m_running{false};
    Context m_ctx;

    // 统计
    int     m_frameCount = 0;
    QElapsedTimer m_fpsTimer;
};

#endif // SRTVIDEOSTREAMER_H
```

- [ ] **Step 2: Commit**

```bash
git add test/srtvideostreamer.h
git commit -m "feat: add srtvideostreamer.h — SRT video streamer interface"
```

---

### Task 3: 创建 srtvideostreamer.cpp

**Files:**
- Create: `test/srtvideostreamer.cpp`

- [ ] **Step 1: Write srtvideostreamer.cpp**

Create `/home/lzy/文档/QTproject/test/srtvideostreamer.cpp`:

```cpp
#include "srtvideostreamer.h"
#include <QDebug>
#include <QElapsedTimer>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
}

SrtVideoStreamer::SrtVideoStreamer(QObject *parent)
    : QThread(parent)
{
}

SrtVideoStreamer::~SrtVideoStreamer() {
    stop();
}

void SrtVideoStreamer::setUrl(const QString &url) {
    m_url = url;
}

void SrtVideoStreamer::setHardwareDecoding(bool enable) {
    m_hwDecode = enable;
}

void SrtVideoStreamer::stop() {
    m_running = false;
    // av_read_frame 会阻塞在 I/O 上，需要关闭 context 来中断
    if (m_ctx.fmtCtx) {
        AVFormatContext *fc = m_ctx.fmtCtx;
        // 关闭输入以中断阻塞的 av_read_frame
        if (fc->pb) fc->pb->error = 1;
    }
    if (isRunning()) {
        wait(3000);  // 最多等 3 秒
        if (isRunning()) {
            qWarning() << "[SRT] thread didn't stop, terminating";
            terminate();
            wait(1000);
        }
    }
}

void SrtVideoStreamer::run() {
    m_running = true;
    emit statusMessage("SRT 视频流初始化中...");

    while (m_running) {
        if (!openStream()) {
            if (!m_running) break;
            emit streamDisconnected();
            emit statusMessage("SRT 连接失败，5 秒后重试...");
            QThread::sleep(5);
            continue;
        }

        emit streamConnected();
        emit statusMessage(QString("SRT 视频流已连接 [%1]")
            .arg(m_ctx.codec->name));

        // 主解码循环
        int ret = 0;
        while (m_running) {
            ret = av_read_frame(m_ctx.fmtCtx, m_ctx.packet);
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    emit statusMessage("SRT 流结束，正在重连...");
                } else if (ret != AVERROR(EAGAIN)) {
                    char errBuf[256];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    emit statusMessage(QString("SRT 读取错误: %1").arg(errBuf));
                }
                break;  // 退出内层循环，重新 openStream
            }

            // 跳过非视频流
            if (m_ctx.packet->stream_index != m_ctx.videoStreamIdx) {
                av_packet_unref(m_ctx.packet);
                continue;
            }

            // 发送数据包到解码器
            ret = avcodec_send_packet(m_ctx.codecCtx, m_ctx.packet);
            av_packet_unref(m_ctx.packet);

            if (ret < 0) {
                // 解码器错误，丢弃当前帧继续
                continue;
            }

            // 接收解码后的帧
            while (m_running) {
                ret = avcodec_receive_frame(m_ctx.codecCtx, m_ctx.frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    break;
                }

                // ── 确定源像素格式 ──
                AVFrame *srcFrame = m_ctx.frame;
                int srcFmt = srcFrame->format;

                // rkmpp 硬解可能吐出 DRM_PRIME
                AVFrame *tmpFrame = nullptr;
                if (srcFmt == AV_PIX_FMT_DRM_PRIME) {
                    tmpFrame = av_frame_alloc();
                    if (tmpFrame && av_hwframe_transfer_data(tmpFrame, srcFrame, 0) == 0) {
                        srcFrame = tmpFrame;
                        srcFmt = srcFrame->format;
                    } else {
                        av_frame_free(&tmpFrame);
                        continue;
                    }
                }

                // 跳过非标准 YUV 格式
                if (srcFmt == AV_PIX_FMT_NONE || srcFmt == AV_PIX_FMT_DRM_PRIME) {
                    av_frame_free(&tmpFrame);
                    continue;
                }

                int w = srcFrame->width;
                int h = srcFrame->height;

                // ── 确保 sws_scale 上下文 ──
                if (!ensureSwscale(w, h, srcFmt)) {
                    av_frame_free(&tmpFrame);
                    continue;
                }

                // ── YUV → RGB24 ──
                sws_scale(m_ctx.swsCtx,
                          srcFrame->data, srcFrame->linesize,
                          0, h,
                          m_ctx.rgbFrame->data, m_ctx.rgbFrame->linesize);

                // ── 发射信号 ──
                uint8_t *rgb = m_ctx.rgbFrame->data[0];
                int ls = m_ctx.rgbFrame->linesize[0];

                // 1. UI 图像 — 深拷贝
                QImage img(rgb, w, h, ls, QImage::Format_RGB888);
                emit frameReady(img.copy());

                // 2. YOLO 预留 — 零拷贝指针
                emit frameReadyForYolo(rgb, w, h, ls);

                av_frame_free(&tmpFrame);

                // FPS 统计
                m_frameCount++;
                if (!m_fpsTimer.isValid()) {
                    m_fpsTimer.start();
                } else if (m_fpsTimer.elapsed() >= 2000) {
                    double fps = m_frameCount * 1000.0 / m_fpsTimer.elapsed();
                    emit fpsUpdated(fps);
                    m_frameCount = 0;
                    m_fpsTimer.restart();
                }
            }
        }

        // 内层循环退出 → 关闭当前流 → 重连
        closeStream();
        if (m_running) {
            emit streamDisconnected();
            emit statusMessage("SRT 流中断，5 秒后重连...");
            QThread::sleep(5);
        }
    }

    closeStream();
}

inline bool SrtVideoStreamer::ensureSwscale(int srcW, int srcH, int srcFmt) {
    if (m_ctx.swsCtx && m_ctx.rgbFrame &&
        m_ctx.rgbFrame->width == srcW && m_ctx.rgbFrame->height == srcH) {
        return true;  // 尺寸未变，复用
    }

    // 释放旧上下文
    if (m_ctx.swsCtx) {
        sws_freeContext(m_ctx.swsCtx);
        m_ctx.swsCtx = nullptr;
    }
    av_frame_free(&m_ctx.rgbFrame);

    m_ctx.swsCtx = sws_getContext(
        srcW, srcH, (AVPixelFormat)srcFmt,
        srcW, srcH, AV_PIX_FMT_RGB24,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_ctx.swsCtx) {
        qWarning() << "[SRT] sws_getContext failed for format" << srcFmt;
        return false;
    }

    m_ctx.rgbFrame = av_frame_alloc();
    m_ctx.rgbFrame->width  = srcW;
    m_ctx.rgbFrame->height = srcH;
    m_ctx.rgbFrame->format = AV_PIX_FMT_RGB24;
    av_frame_get_buffer(m_ctx.rgbFrame, 0);
    return true;
}

bool SrtVideoStreamer::openStream() {
    closeStream();

    // ── 1. 分配格式上下文 ──
    m_ctx.fmtCtx = avformat_alloc_context();
    if (!m_ctx.fmtCtx) {
        emit statusMessage("avformat_alloc_context 失败");
        return false;
    }

    // 低延时选项
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "analyzeduration", "0", 0);
    av_dict_set(&opts, "probesize", "32", 0);
    av_dict_set(&opts, "max_delay", "0", 0);

    // ── 2. 打开 SRT 流 ──
    int ret = avformat_open_input(&m_ctx.fmtCtx,
        m_url.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        emit statusMessage(QString("打开 SRT 流失败: %1").arg(errBuf));
        closeStream();
        return false;
    }

    // ── 3. 查找流信息 ──
    ret = avformat_find_stream_info(m_ctx.fmtCtx, nullptr);
    if (ret < 0) {
        emit statusMessage("无法获取流信息");
        closeStream();
        return false;
    }

    // ── 4. 找到第一个视频流 ──
    m_ctx.videoStreamIdx = -1;
    for (unsigned i = 0; i < m_ctx.fmtCtx->nb_streams; ++i) {
        if (m_ctx.fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_ctx.videoStreamIdx = i;
            break;
        }
    }
    if (m_ctx.videoStreamIdx < 0) {
        emit statusMessage("SRT 流中未找到视频轨道");
        closeStream();
        return false;
    }

    AVCodecParameters *codecPar = m_ctx.fmtCtx->streams[m_ctx.videoStreamIdx]->codecpar;

    // ── 5. 选择解码器 ──
    if (m_hwDecode) {
        m_ctx.codec = avcodec_find_decoder_by_name("h264_rkmpp");
        if (m_ctx.codec) {
            emit statusMessage("解码器: h264_rkmpp (硬件加速)");
        }
    }
    if (!m_ctx.codec) {
        m_ctx.codec = avcodec_find_decoder(codecPar->codec_id);
        if (!m_ctx.codec) {
            emit statusMessage("未找到 H.264 解码器");
            closeStream();
            return false;
        }
        emit statusMessage(QString("解码器: %1 (CPU 软解)").arg(m_ctx.codec->name));
    }

    // ── 6. 分配解码器上下文 ──
    m_ctx.codecCtx = avcodec_alloc_context3(m_ctx.codec);
    if (!m_ctx.codecCtx) {
        emit statusMessage("avcodec_alloc_context3 失败");
        closeStream();
        return false;
    }

    avcodec_parameters_to_context(m_ctx.codecCtx, codecPar);

    // 低延时 + 丢帧
    m_ctx.codecCtx->thread_count = 0;
    m_ctx.codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_ctx.codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;
    m_ctx.codecCtx->skip_frame = AVDISCARD_DEFAULT;

    // ── 7. 打开解码器 ──
    ret = avcodec_open2(m_ctx.codecCtx, m_ctx.codec, nullptr);
    if (ret < 0) {
        emit statusMessage("avcodec_open2 失败");
        closeStream();
        return false;
    }

    // ── 8. 分配 frame/packet ──
    m_ctx.frame  = av_frame_alloc();
    m_ctx.packet = av_packet_alloc();
    if (!m_ctx.frame || !m_ctx.packet) {
        emit statusMessage("av_frame_alloc / av_packet_alloc 失败");
        closeStream();
        return false;
    }

    return true;
}

void SrtVideoStreamer::closeStream() {
    av_frame_free(&m_ctx.rgbFrame);
    av_frame_free(&m_ctx.frame);
    av_packet_free(&m_ctx.packet);

    if (m_ctx.swsCtx) {
        sws_freeContext(m_ctx.swsCtx);
        m_ctx.swsCtx = nullptr;
    }

    if (m_ctx.codecCtx) {
        avcodec_free_context(&m_ctx.codecCtx);
    }
    m_ctx.codec = nullptr;

    if (m_ctx.fmtCtx) {
        avformat_close_input(&m_ctx.fmtCtx);
    }

    m_ctx.videoStreamIdx = -1;
    m_ctx.hwDeviceCtx = nullptr;
}
```

- [ ] **Step 2: Commit**

```bash
git add test/srtvideostreamer.cpp
git commit -m "feat: add srtvideostreamer.cpp — FFmpeg SRT + h264_rkmpp decode pipeline"
```

---

### Task 4: 更新 teagardensystem.h

**Files:**
- Modify: `test/teagardensystem.h`

- [ ] **Step 1: Add forward declaration and member**

After the existing `class ChartDashboard;` line (line 30), add:

```cpp
class SrtVideoStreamer;
```

After the `#include <QtCharts/QChartView>` line (line 28), add:

```cpp
#include <QImage>
```

Add new slot declaration after `onIotCommand` (currently line 64):

```cpp
    void onVideoFrame(const QImage &image);
    void onVideoStatus(const QString &msg);
```

Add member after `videoSocket` member (currently line 155):

```cpp
    SrtVideoStreamer *m_videoStreamer = nullptr;
```

- [ ] **Step 2: Commit**

```bash
git add test/teagardensystem.h
git commit -m "feat: add SrtVideoStreamer members and slots to header"
```

---

### Task 5: 更新 teagardensystem.cpp — 集成新视频流

**Files:**
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: Add include**

After `#include "scheduledialog.h"`, add:

```cpp
#include "srtvideostreamer.h"
```

- [ ] **Step 2: In constructor, replace video init**

Replace lines 87-91:
```cpp
    // 网络（MJPEG 流 + AI 对话共用）
    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &TeaGardenSystem::onNetworkReply);

    setupMjpegStream();
```

with:

```cpp
    // 网络（AI 对话 + 天气 共用）
    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &TeaGardenSystem::onNetworkReply);

    // SRT 视频流（无人机 H.264 硬解）
    m_videoStreamer = new SrtVideoStreamer(this);
    m_videoStreamer->setUrl(
        qEnvironmentVariable("TEA_SRT_URL", "srt://0.0.0.0:9000?mode=listener"));
    connect(m_videoStreamer, &SrtVideoStreamer::frameReady,
            this, &TeaGardenSystem::onVideoFrame);
    connect(m_videoStreamer, &SrtVideoStreamer::statusMessage,
            this, &TeaGardenSystem::onVideoStatus);
    connect(m_videoStreamer, &SrtVideoStreamer::fpsUpdated,
            this, [this](double fps) {
        loraStateLabel->setText(QString("视频 FPS: %1").arg(fps, 0, 'f', 1));
    });
    m_videoStreamer->start();
```

- [ ] **Step 3: Wrap old setupMjpegStream with #ifdef**

Wrap lines 505-545 (`setupMjpegStream` implementation):

```cpp
#ifdef TEA_USE_WEBSOCKET_VIDEO
void TeaGardenSystem::setupMjpegStream() {
    // ... 原有实现不变 ...
}
#endif // TEA_USE_WEBSOCKET_VIDEO
```

- [ ] **Step 4: Add onVideoFrame and onVideoStatus implementations**

Add before the `// ── 定时调度 ──` comment section:

```cpp
// ── SRT 视频流回调 ──

void TeaGardenSystem::onVideoFrame(const QImage &image) {
    if (!videoLabel || image.isNull()) return;

    const int w = qMax(videoLabel->width(), 280);
    const int h = w * kDroneVideoHeight / kDroneVideoWidth;
    const QPixmap pix = QPixmap::fromImage(image);
    videoLabel->setPixmap(pix.scaled(QSize(w, h), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    videoLabel->setText(QString());
}

void TeaGardenSystem::onVideoStatus(const QString &msg) {
    appendChatMessage("视频", msg);
}
```

- [ ] **Step 5: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: integrate SrtVideoStreamer, ifdef-guard old WebSocket video"
```

---

### Task 6: 板子上编译验证

**Files:**
- (build only — on ELF2 board)

- [ ] **Step 1: Sync source and build**

```bash
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh sync /home/lzy/文档/QTproject/test/
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh compile
```

Expected: zero errors. If `-lswscale` can't resolve, verify sysroot path with:
```bash
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh ssh "ls /home/elf/qt_sdk_aarch64/aarch64-buildroot-linux-gnu/sysroot/usr/lib/libswscale*"
```

Common FFmpeg compile fixes:
- `SwsContext` → `struct SwsContext` in header. Actually `SwsContext` is typedef'd in `swscale.h`, so use `struct SwsContext*` in the class forward declaration.
- `avformat_open_input` URI `const char*` → use `m_url.toUtf8().constData()` ✓
- Linker order matters: `-lavformat -lavcodec -lswscale -lavutil` (dependency order)

- [ ] **Step 2: Smoke test binary**

```bash
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh run
```

Expected: binary starts, logs show `[SRT] SRT 视频流初始化中...`. If no SRT source, should see "SRT 连接失败，5 秒后重试...". No segfault.

- [ ] **Step 3: Commit any fixes**

If any fixes were made during build verification:
```bash
git add -A
git commit -m "fix: SRT video streamer build fixes"
```
