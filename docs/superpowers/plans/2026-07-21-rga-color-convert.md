# RGA NV12→RGB 硬件色彩转换 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 ffmpeg 输出从 `rgb24` (CPU) 改为 `nv12`，显示/YOLO 调用 RGA 硬件做色彩转换，降低 CPU 负载。

**Architecture:** ffmpeg 输出 NV12 原始帧 → `m_latestFrameRaw` (QByteArray) → `latestFrame()` 被调用时 RGA `imcvtcolor` 做 NV12→RGB → 返回 QImage。下游（显示/YOLO）无感知变更。

**Tech Stack:** ffmpeg, RGA (librga), RK3588

---

## 文件结构

| 文件 | 改动 |
|------|------|
| `test/srtvideostreamer.h` | 新增 `QByteArray m_latestFrameRaw`、`int m_frameRawSize`、`mutable QMutex m_frameRawMutex`；删除 `QImage m_latestFrame` + `m_frameMutex` |
| `test/srtvideostreamer.cpp` | ffmpeg `-pix_fmt nv12` + 帧大小 `W*H*3/2`；`run()` 切帧写 `m_latestFrameRaw`；`latestFrame()` 内 RGA 转 RGB；加 `#include "im2d.h"` |
| `test/rknn/yolov8/preprocess.cc` | 新增 `nv12_to_rgb_rga()` 函数 |
| `test/rknn/yolov8/preprocess.h` | 声明 `nv12_to_rgb_rga()` |

---

### Task 1: SrtVideoStreamer — NV12 原始帧存储

**Files:**
- Modify: `test/srtvideostreamer.h`
- Modify: `test/srtvideostreamer.cpp`

- [ ] **Step 1: srtvideostreamer.h — 替换帧缓存成员**

读当前 `srtvideostreamer.h`，做以下改动：

1. 删除 `QImage m_latestFrame;`（第 49 行）和 `mutable QMutex m_frameMutex;`（第 48 行）
2. 改为：
```cpp
    // 共享原始帧缓存（ffmpeg 线程写 NV12 字节，主线程通过 latestFrame() 用 RGA 转 RGB）
    mutable QMutex m_frameRawMutex;
    QByteArray m_latestFrameRaw;
    int m_frameRawSize = 0;
```

3. 加 `#include <QMutexLocker>` 或确保 `QMutex` 已可用。

- [ ] **Step 2: srtvideostreamer.cpp — ffmpeg 参数改 nv12 + 帧大小改 W*H*3/2**

找到 ffmpeg args 构建处（约 33-41 行）：

```cpp
args << "-vcodec" << (m_hwDecode ? "h264_rkmpp" : "h264")
     << "-f" << "mpegts"
     << "-fflags" << "nobuffer"
     << "-flags"  << "low_delay"
     << "-i" << m_url
     << "-an"
     << "-f" << "rawvideo"
     << "-pix_fmt" << "rgb24"       // ← 改这行
     << "pipe:1";
```

改为：

```cpp
args << "-vcodec" << (m_hwDecode ? "h264_rkmpp" : "h264")
     << "-f" << "mpegts"
     << "-fflags" << "nobuffer"
     << "-flags"  << "low_delay"
     << "-i" << m_url
     << "-an"
     << "-f" << "rawvideo"
     << "-pix_fmt" << "nv12"
     << "pipe:1";
```

帧大小计算（约 71-72 行 `frameSize = m_resWidth * m_resHeight * 3`）：

```cpp
// 旧
int frameSize = m_haveResolution ? (m_resWidth * m_resHeight * 3) : 0;
// 改为
int frameSize = m_haveResolution ? (m_resWidth * m_resHeight * 3 / 2) : 0;
```

- [ ] **Step 3: srtvideostreamer.cpp — run() 切帧后写原始数据**

找到切帧 + emit 处（约 123-131 行）：

```cpp
QImage img((const uchar*)frameData.constData(), ...);
QImage copy = img.copy();
if (!copy.isNull()) {
    {
        QMutexLocker lock(&m_frameMutex);
        m_latestFrame = copy;
    }
```

改为写 NV12 字节：

```cpp
{
    QMutexLocker lock(&m_frameRawMutex);
    m_latestFrameRaw = frameData;   // 深拷贝 NV12 字节
    m_frameRawSize = frameData.size();
}
m_frameCount++;
```

删掉 `QImage img(...)`、`QImage copy = img.copy()`、`if (!copy.isNull())` 这三行。

- [ ] **Step 4: srtvideostreamer.cpp — latestFrame() RGA 转 RGB**

当前 `latestFrame()` 返回 `m_latestFrame.copy()`。改为 RGA NV12→RGB：

```cpp
QImage SrtVideoStreamer::latestFrame() const {
    QMutexLocker lock(&m_frameRawMutex);
    if (m_latestFrameRaw.isEmpty() || m_frameRawSize <= 0)
        return QImage();
    if (m_resWidth <= 0 || m_resHeight <= 0)
        return QImage();

    // RGA 硬件 NV12 → RGB888
    rga_buffer_t src = wrapbuffer_virtualaddr(
        (void*)m_latestFrameRaw.constData(),
        m_resWidth, m_resHeight, RK_FORMAT_YCbCr_420_SP);
    int rgbSize = m_resWidth * m_resHeight * 3;
    QByteArray rgbBuf(rgbSize, Qt::Uninitialized);
    rga_buffer_t dst = wrapbuffer_virtualaddr(
        (void*)rgbBuf.data(),
        m_resWidth, m_resHeight, RK_FORMAT_RGB_888);
    im_rect src_rect = {0, 0, m_resWidth, m_resHeight};
    im_rect dst_rect = {0, 0, m_resWidth, m_resHeight};
    IM_STATUS ret = imcheck(src, dst, src_rect, dst_rect);
    if (ret != IM_STATUS_NOERROR) {
        fprintf(stderr, "[SRT] RGA check error: %s\n", imStrError(ret));
        return QImage();
    }
    ret = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_RGB_888);
    if (ret != IM_STATUS_NOERROR) {
        fprintf(stderr, "[SRT] RGA cvtcolor error: %s\n", imStrError(ret));
        return QImage();
    }

    return QImage((const uchar*)rgbBuf.constData(),
                  m_resWidth, m_resHeight, QImage::Format_RGB888).copy();
}
```

顶部加 include：
```cpp
#include "im2d.h"
```

- [ ] **Step 5: 编译验证 (x86)**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4 2>&1 | grep -E 'srtvideostreamer.o|error:'
```

NOTE: x86 上 `im2d.h` 和 `imcvtcolor` 不可用（RGA 是 ARM-only）。编译会失败。这是**预期行为**——只在 ELF2 上编译。跳过 x86 验证。

- [ ] **Step 6: Commit**

```bash
git add test/srtvideostreamer.h test/srtvideostreamer.cpp
git commit -m "feat: SrtVideoStreamer NV12 原始帧 + latestFrame RGA 转 RGB"
```

---

### Task 2: ELF2 编译 + RGA 负载验证

**Files:** 无需修改

- [ ] **Step 1: 同步 + 编译**

```bash
sshpass -p "elf" rsync -avz --delete \
  --exclude='*.o' --exclude='moc_*' --exclude='moc_predefs.h' \
  --exclude='ui_widget.h' --exclude='qrc_*.cpp' \
  --exclude='Makefile' --exclude='.qmake.stash' \
  -e "ssh -o StrictHostKeyChecking=no" \
  /home/lzy/文档/QTproject/test/ elf@10.200.0.102:~/QTproject/test/ && \
sshpass -p "elf" ssh -o StrictHostKeyChecking=no elf@10.200.0.102 \
  "cd ~/QTproject/build-arm-Debug && \
   rm -f Makefile .qmake.stash *.o moc_*.cpp moc_predefs.h ui_widget.h qrc_*.cpp test && \
   qmake ../test/test.pro && make -j\$(nproc) 2>&1 | tail -1"
```

Expected: 编译链接全部通过。

- [ ] **Step 2: 运行验证 RGA 负载**

程序启动后，另一个终端：

```bash
sshpass -p "elf" ssh elf@10.200.0.102 \
  "sudo cat /sys/kernel/debug/rkrga/load"
```

Expected: RGA scheduler 显示非零负载（约 10-30%），确认 RGA 被使用。

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "verify: RGA NV12→RGB ELF2 编译通过"
```
