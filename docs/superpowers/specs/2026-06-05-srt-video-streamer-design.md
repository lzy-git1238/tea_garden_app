# SRT 无人机视频流 — 设计文档

> 日期：2026-06-05  
> 状态：设计完成，待评审

---

## 1. 背景

**当前方案**：WebSocket + Base64-encoded JPEG 帧（`ws://100.108.217.12:8080/ws`），通过 `QWebSocket` 在主线程接收 JSON 消息，UI 直接解码显示。无硬件加速。

**目标**：改用 SRT 协议接收无人机 H.264 视频流，RK3588 硬件解码（`h264_rkmpp`），QThread 子线程拉流解码，QImage 深拷贝到主线程渲染。预留 RKNN (NPU) YOLO 接口。

---

## 2. 设计决策

| 决策项 | 选择 | 说明 |
|--------|------|------|
| 文件结构 | **独立新文件 `srtvideostreamer.h/.cpp`** | 不污染 teagardensystem (已 2100+ 行) |
| 视频编码 | **纯 H.264** | 无人机（香橙派）通过 SRT 发送 H.264 裸流 |
| 解码方式 | **h264_rkmpp 硬解优先** | 失败回退系统软解，触发 warning |
| 像素格式 | **动态读取 `pFrame->format`** | rkmpp 吐 NV12/DRM_PRIME，不写死 |
| 旧代码 | **`#ifdef TEA_USE_WEBSOCKET_VIDEO` 保留** | 不做破坏性删除 |
| YOLO 预留 | **零拷贝信号 `frameReadyForYolo`** | RGB24 指针 + 宽高 + linesize |

---

## 3. 文件变更

### 3.1 新增文件

| 文件 | 职责 |
|------|------|
| `test/srtvideostreamer.h` | `SrtVideoStreamer` 类声明（QThread 子类，FFmpeg C API） |
| `test/srtvideostreamer.cpp` | 拉流 → h264_rkmpp 硬解 → sws_scale → QImage/QImage copy → emit |

### 3.2 修改文件

| 文件 | 改动 |
|------|------|
| `test/teagardensystem.h` | 新增 `SrtVideoStreamer *m_videoStreamer`、`onVideoFrame` 槽 |
| `test/teagardensystem.cpp` | 构造函数初始化 streamer、`#ifdef TEA_USE_WEBSOCKET_VIDEO` 旧代码、`onVideoFrame` 实现 |
| `test/test.pro` | 添加 sysroot FFmpeg include path、`LIBS += -lavformat -lavcodec -lavutil -lswscale` |

---

## 4. 编译配置

```qmake
# FFmpeg 满血版头文件（手动编译，含 rkmpp/libsrt）
INCLUDEPATH += /home/elf/qt_sdk_aarch64/aarch64-buildroot-linux-gnu/sysroot/usr/include

# FFmpeg 动态库
LIBS += -lavformat -lavcodec -lavutil -lswscale
```

---

## 5. SrtVideoStreamer 接口

```cpp
class SrtVideoStreamer : public QThread {
    Q_OBJECT
public:
    explicit SrtVideoStreamer(QObject *parent = nullptr);
    ~SrtVideoStreamer();

    void setUrl(const QString &url);         // srt://0.0.0.0:9000?mode=listener
    void setHardwareDecoding(bool enable);   // 默认 true
    void stop();

signals:
    // UI 渲染
    void frameReady(const QImage &image);

    // YOLO (RKNN) 预留：零拷贝 RGB24 指针
    void frameReadyForYolo(const uint8_t *rgb24,
                           int width, int height, int linesize);

    void statusMessage(const QString &msg);
    void streamConnected();
    void streamDisconnected();
    void fpsUpdated(double fps);

protected:
    void run() override;

private:
    struct FfmpegContext {
        AVFormatContext *fmtCtx = nullptr;
        AVCodecContext  *codecCtx = nullptr;
        AVCodec         *codec = nullptr;
        SwsContext      *swsCtx = nullptr;
        AVFrame         *frame = nullptr;
        AVFrame         *rgbFrame = nullptr;
        AVPacket        *packet = nullptr;
        int              videoStreamIdx = -1;
        AVBufferRef     *hwDeviceCtx = nullptr;   // rkmpp 硬件上下文
    };

    std::unique_ptr<FfmpegContext> m_ctx;
    QString    m_url;
    bool       m_hwDecode = true;
    std::atomic<bool> m_running{false};
};
```

---

## 6. FFmpeg 管线

### 6.1 拉流

```
avformat_alloc_context
→ AVDictionary: fflags=nobuffer, analyzeduration=0, probesize=32, max_delay=0
→ avformat_open_input(srt://0.0.0.0:9000?mode=listener)
→ avformat_find_stream_info
→ 找到第一个 AVMEDIA_TYPE_VIDEO 流
```

### 6.2 解码器选择

```
1. hwDecode=true → avcodec_find_decoder_by_name("h264_rkmpp")
2. 失败 → avcodec_find_decoder(AV_CODEC_ID_H264)  // 软解
3. 软解时 emit statusMessage("[警告] h264_rkmpp 不可用，使用 CPU 软解")
```

### 6.3 解码配置

```
AVCodecContext:
  thread_count = 0
  flags |= AV_CODEC_FLAG_LOW_DELAY
  flags2 |= AV_CODEC_FLAG2_FAST
  skip_frame = AVDISCARD_DEFAULT（丢 B 帧，有编码延迟则 AVDISCARD_NONREF）
```

### 6.4 像素格式转换（关键）

```
// 不写死源格式，动态读取硬解器实际输出
int srcFmt = pFrame->format;

// rkmpp 硬解可能吐出 DRM_PRIME
AVFrame *cpuFrame = pFrame;
if (srcFmt == AV_PIX_FMT_DRM_PRIME) {
    cpuFrame = av_frame_alloc();
    av_hwframe_transfer_data(cpuFrame, pFrame, 0);
    srcFmt = cpuFrame->format;  // 实际降级为 NV12 / YUV420P 等
}

// dst: AV_PIX_FMT_RGB24
sws_getContext(w, h, srcFmt, w, h, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, ...);
sws_scale → rgbFrame->data[0]
```

### 6.5 渲染 & YOLO

```
uint8_t *rgb = rgbFrame->data[0];
int w = rgbFrame->width, h = rgbFrame->height, ls = rgbFrame->linesize[0];

// 1. UI 信号 — 深拷贝
QImage img(rgb, w, h, ls, QImage::Format_RGB888);
emit frameReady(img.copy());

// 2. YOLO 预留 — 零拷贝指针
emit frameReadyForYolo(rgb, w, h, ls);

// 注意: rgb 内存在下一帧循环覆盖，UI 端 .copy() 后安全，YOLO 端需自行拷贝
```

---

## 7. 线程安全 & 内存管理

| 场景 | 处理 |
|------|------|
| QImage 传主线程 | `.copy()` 深拷贝，防止 FFmpeg frame buffer 覆盖 |
| YOLO 指针 | 零拷贝，调用方需在下一帧前 consume 或自拷贝 |
| stop() | `m_running=false` → `avformat_close_input` 中断阻塞 → `wait()` join 线程 |
| 析构 | `stop()` + `avcodec_free_context` + `avformat_close_input` + 释放 frame/packet |
| AVPacket/AVFrame 复用 | `av_packet_unref` / `av_frame_unref` 每轮循环 |

---

## 8. 自动重连 & 错误恢复

| 场景 | 处理 |
|------|------|
| SRT 流中断 | `av_read_frame` 返回 EOF/ERROR → 关闭解码器 → 5秒后重新 `avformat_open_input` |
| 解码器错误 | `avcodec_send_packet` 返回非 0 → 丢帧继续 |
| FFmpeg 版本兼容 | `avcodec_alloc_context3` → `avcodec_open2`，不直接用 `avcodec_alloc_context3` 被废弃的 pattern |

---

## 9. TeaGardenSystem 集成（胶水代码）

```
构造函数:
  m_videoStreamer = new SrtVideoStreamer(this);
  m_videoStreamer->setUrl(srt://0.0.0.0:9000?mode=listener);
  connect(m_videoStreamer, &SrtVideoStreamer::frameReady,
          this, &TeaGardenSystem::onVideoFrame);
  connect(m_videoStreamer, &SrtVideoStreamer::statusMessage,
          this, &TeaGardenSystem::onVideoStatus);
  m_videoStreamer->start();

旧 setupMjpegStream() 用 #ifdef TEA_USE_WEBSOCKET_VIDEO 包裹

onVideoFrame(const QImage &img):
  pixmap = QPixmap::fromImage(img).scaled(1920x1080 等比)
  videoLabel->setPixmap(pixmap);
```

---

## 10. 测试要点

- [ ] SRT 流接入 → `streamConnected` 信号触发
- [ ] h264_rkmpp 硬解 → 日志显示 "h264_rkmpp" decoder
- [ ] 硬解不可用 → fallback 软解 → statusMessage 输出 warning
- [ ] 视频帧显示在 videoLabel → 流畅无绿块/花屏
- [ ] FPS 稳定在 25+（1920×1080）
- [ ] 断开 SRT 源 → `streamDisconnected` → 5秒自动重连
- [ ] stop() 后线程正常退出，无内存泄漏（valgrind 抽查）
- [ ] 旧 WebSocket 方案在 `#ifdef` 下仍可编译
