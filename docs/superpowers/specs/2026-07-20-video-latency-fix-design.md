# 视频延迟累积修复：推送改拉取

## 问题

小车和无人机 SRT 视频流接收久了，APP 上视频延迟逐渐累积到 4-5 秒。视频仍然流畅（帧间隔均匀），但画面是数秒前的。APP 重启后延迟归零。

## 根因

`SrtVideoStreamer::run()` 在独立线程中持续拉取 ffmpeg 解码的 rawvideo，**每帧都 emit 信号**。Qt 自动用 `QueuedConnection` 把每一帧（含完整 QImage 拷贝）塞进主线程事件队列。

```
30fps × ~1.2MB(640×480 RGB) × 2路(无人机+小车) ≈ 72MB/s 入队
```

主线程要处理 YOLO 画框、DB 写入、地图 JS 注入、LoRa 解析、GPS 航迹、setPixmap 缩放等，跟不上入队速度。事件队列 FIFO 逐步堆积，显示的画面越来越旧。队列中累积的 QImage 还消耗大量共享内存，增加 OOM 风险。重启清空队列即恢复。

## 方案：共享最新帧 + 定时器拉取

`SrtVideoStreamer` 不再每帧 emit 信号，改为把最新帧写到一个带互斥锁的共享缓存。主线程用 `QTimer` 以固定间隔从缓存取最新帧显示/YOLO 推理。

### 架构

```
ffmpeg 解码线程 (30fps)              Qt 主线程
  │                                   │
  ├─ 每帧写入 m_latestFrame ──→  QTimer 33ms tick
  │     (mutex 保护)                  ├─ latestFrame() → onVideoFrame()
  │                                   │
                                      └─ QTimer 333ms tick
                                           ├─ latestFrame() → YOLO::onNewFrame()
```

### 改动

| 文件 | 改动 |
|------|------|
| [srtvideostreamer.h](test/srtvideostreamer.h) | 删除 `frameReadyForYolo` 信号；新增 `QMutex m_frameMutex`、`QImage m_latestFrame`；新增 `latestFrame()` 公开方法 |
| [srtvideostreamer.cpp](test/srtvideostreamer.cpp) | `run()` 中 emit 替换为 mutex 写共享帧 |
| [teagardensystem.h](test/teagardensystem.h) | 新增 `m_videoPullTimer`、`m_roverPullTimer`、`m_yoloPullTimer` 三个 QTimer |
| [teagardensystem.cpp](test/teagardensystem.cpp) | 删除 frameReady connect；新增 Timer 驱动拉取；YOLO 侧删除 3fps 限流代码 |
| [yolodetector.h](test/yolodetector.h) | 删除 `setFpsLimit()` 和 `m_fpsLimit`（可选清理） |
| [yolodetector.cpp](test/yolodetector.cpp) | `onNewFrame()` 中删除 fps timer 检查 |

### 关键设计点

**QImage 隐式共享**：`m_latestFrame = copy` 只是引用计数 +1，不拷贝像素数据。`latestFrame()` 返回 `.copy()` 做深拷贝，调用方持有独立副本。

**YOLO 频率直接匹配**：Timer 333ms 间隔精准匹配 3fps，`onNewFrame()` 自带限流变为冗余，可删除。

**双路独立 Timer**：无人机和小车各有独立 timer，互不干扰。WebSocket MJPEG 小车不受影响（它走 `textMessageReceived` → 直接 setPixmap，不在同一链路）。

**FPS 统计保留**：`m_frameCount` 和 `m_fpsTimer` 仍在 `SrtVideoStreamer::run()` 中更新，`fpsUpdated` 信号正常 emit。

## 测试要点

- 长时间运行（>10 分钟）→ 视频延迟无明显累积
- 无人机 SRT 流 YOLO 检测正常工作
- 小车 SRT 流显示流畅
- FPS 统计正常
- 内存占用稳定（不随运行时间增长）
- 流断开重连后自动恢复
