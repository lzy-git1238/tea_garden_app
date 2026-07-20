# CPU 优化：ffmpeg NV12 → RGA 硬件转 RGB

## 问题

有视频流时 CPU 99%，APP 卡顿。根因：ffmpeg 用 `-f rawvideo -pix_fmt rgb24` 在 CPU 做 YUV→RGB 转换，30fps × 640×480 × 3 bytes ≈ 28MB/s 全是 CPU 负担。

## 方案

ffmpeg 改为输出 NV12（不解码到 RGB，CPU 负担极低）→ `latestFrame()` 被调用时用 RGA 硬件做 NV12→RGB + resize → 返回 QImage。

### 架构

```
ffmpeg -f rawvideo -pix_fmt nv12 pipe:1  
    (VPU 硬解 H.264，输出 NV12，CPU 只负责 pipe 搬运)
        │
        ▼
SrtVideoStreamer::run()
    ├─ stdoutBuf 切帧（与现在相同，只改每帧大小：W×H×1.5）
    ├─ m_latestFrameRaw = frameData（原始 NV12 字节）
    └─ m_latestFrameSize = W×H×1.5

latestFrame() 被调用时：
    ├─ 用 RGA imcvtcolor 做 NV12 → RGB888（硬件，与 resize_rga 同一套 API）
    ├─ 返回 QImage（Format_RGB888）
    └─ 调用方（显示/YOLO）无感知变更
```

### 为什么必须 NV12

`h264_rkmpp` 硬件解码器原生输出就是 NV12。当前 ffmpeg 内部再转一次 rgb24 纯属多余——VPU 解出 NV12 → CPU 转 rgb24 → pipe → Qt。改输出 NV12 后：VPU 解出 NV12 → pipe → Qt → RGA 转 RGB，CPU 完全退出。

## 改动

| 文件 | 改动 |
|------|------|
| [srtvideostreamer.h](test/srtvideostreamer.h) | 加 `QByteArray m_latestFrameRaw`、`int m_frameRawSize`、`mutable QMutex m_frameRawMutex`；`latestFrame()` 返回类型不变 |
| [srtvideostreamer.cpp](test/srtvideostreamer.cpp) | ffmpeg 改 `-pix_fmt nv12`，帧大小改为 `W*H*3/2`；`run()` 内 NV12 字节拷贝到 `m_latestFrameRaw`；`latestFrame()` 内 RGA NV12→RGB |
| [test/rknn/yolov8/preprocess.cc](test/rknn/yolov8/preprocess.cc) | 新增 `nv12_to_rgb_rga()` 函数（RGA 色彩转换，不缩放） |

### latestFrame() 核心实现

```cpp
QImage SrtVideoStreamer::latestFrame() const {
    QMutexLocker lock(&m_frameRawMutex);
    if (m_latestFrameRaw.isEmpty() || m_frameRawSize <= 0)
        return QImage();

    // RGA NV12 → RGB888（硬件加速）
    int rgbSize = m_resWidth * m_resHeight * 3;
    QByteArray rgbBuf(rgbSize, Qt::Uninitialized);

    rga_buffer_t src = wrapbuffer_virtualaddr(
        (void*)m_latestFrameRaw.constData(),
        m_resWidth, m_resHeight, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_virtualaddr(
        (void*)rgbBuf.data(),
        m_resWidth, m_resHeight, RK_FORMAT_RGB_888);
    // RGA 单次调用同时做 NV12→RGB，零缩放
    imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_RGB_888);

    return QImage((const uchar*)rgbBuf.constData(),
                  m_resWidth, m_resHeight, QImage::Format_RGB888).copy();
}
```

### 对 YOLO 的影响

YOLO 原来在 timer lambda 里 `cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR)` ——拿到的是 RGB，再转 BGR。现在 RGA 出来也是 RGB，BGR 转换不变。**YOLO 链路完全不变。**

### 对显示的优化

`onVideoFrame()` 里不做 `QPixmap::scale()`，改为在 RGA 里同时缩放+格式转换：

```cpp
dst = wrapbuffer_virtualaddr(rgbBuf, dstW, dstH, RK_FORMAT_RGB_888);
imresize(src, dst);  // 同时做多一步：格式转换用 imcvtcolor + imresize 两步，或一次 imresize（RGA 自动处理格式）
```

但先做单 RGA 色彩转换，QPixmap::scaled 性能问题后续迭代。本次只做 NV12→RGB。

## 测试要点

- ELF2 编译通过（依赖 libRGA，已装）
- 视频显示正常，无画面错乱
- `sudo cat /sys/kernel/debug/rkrga/load` 看到 RGA 有负载
- `htop` 看 ffmpeg CPU 占比明显下降
- YOLO 检测正常工作
