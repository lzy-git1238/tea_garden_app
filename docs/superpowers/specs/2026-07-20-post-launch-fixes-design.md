# 上线后三个问题修复

## 问题与根因

| # | 问题 | 根因 |
|---|------|------|
| 1 | 检测框闪烁太快，看不清字 | 33ms 显示 timer 每 33ms 用原始帧覆盖 `videoLabel`，YOLO 有框帧只存活 ~33ms |
| 2 | 检测到了但没入库、没地图标注 | `onYoloResults` 里 `if (m_dronePath.isEmpty()) continue;` 无 GPS 时直接丢弃所有检测 |
| 3 | 地图缩放慢、易卡死 | `injectPestMarkers()` 调用太频繁 + 每次 `runJavaScript()` 注入大段 JS 开销大 |

## 方案

### 修复 1：YOLO 帧锁定 2 秒

在显示 timer（33ms）中加判断：最近 2 秒内如果 `onYoloResults` 贴过有框帧，则跳过原始帧覆盖。

新增成员变量：
```cpp
QElapsedTimer m_lastYoloFrameTime;  // 上次 YOLO 帧贴屏时间
```

显示 timer lambda ([teagardensystem.cpp:138-141](test/teagardensystem.cpp#L138-L141)) 改为：
```cpp
connect(m_videoPullTimer, &QTimer::timeout, this, [this]() {
    // YOLO 有框帧锁定 2 秒，期间不覆盖原始帧
    if (m_lastYoloFrameTime.isValid() && m_lastYoloFrameTime.elapsed() < 2000)
        return;
    QImage frame = m_videoStreamer->latestFrame();
    if (!frame.isNull()) onVideoFrame(frame);
});
```

`onYoloResults` 贴帧时记录时间戳：
```cpp
m_lastYoloFrameTime.start();
```

### 修复 2：无 GPS 退回到地图初始点

`onYoloResults` 中 ([teagardensystem.cpp:2345-2346](test/teagardensystem.cpp#L2345-L2346))：

```cpp
// 改前
if (m_dronePath.isEmpty()) continue;

// 改后
double lat, lng;
if (m_dronePath.isEmpty()) {
    lat = qEnvironmentVariable("TEA_MAP_LAT", "25.08039").toDouble();
    lng = qEnvironmentVariable("TEA_MAP_LNG", "118.232478").toDouble();
} else {
    lat = m_dronePath.last().lat;
    lng = m_dronePath.last().lon;
}
```

后续 `rec.lat/lng` 用这两个变量。

### 修复 3：injectPestMarkers 限流 2 秒

新增成员变量：
```cpp
QElapsedTimer m_lastMarkerInject;
```

`injectPestMarkers()` 开头加防抖：
```cpp
void TeaGardenSystem::injectPestMarkers() {
#ifdef TEA_ENABLE_MAP_WEBVIEW
    if (!mapView) return;
    // 限流：2 秒内最多刷一次
    if (m_lastMarkerInject.isValid() && m_lastMarkerInject.elapsed() < 2000)
        return;
    m_lastMarkerInject.start();
    ...
```

## 改动范围

| 文件 | 改动 |
|------|------|
| [teagardensystem.h](test/teagardensystem.h) | 新增 `QElapsedTimer m_lastYoloFrameTime`、`QElapsedTimer m_lastMarkerInject` |
| [teagardensystem.cpp](test/teagardensystem.cpp) | 显示 timer 加锁判断、`onYoloResults` GPS 回退 + 时间戳、`injectPestMarkers` 限流 |

## 测试要点

- 有 YOLO 检测时框停留 2 秒可读
- 无 GPS 时 YOLO 检测正常入库 + 地图标注
- 地图缩放流畅度改善，不再频繁 JS 注入
