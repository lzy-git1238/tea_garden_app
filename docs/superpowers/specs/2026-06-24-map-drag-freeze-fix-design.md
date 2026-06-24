# 地图拖拽卡死修复设计

## 问题

QWebEngineView + Leaflet 展示星图云瓦片地图。快速或频繁拖拽地图，重复多次后程序卡死，只能 kill。

## 根因

RK3588 GPU (Mali-G610) 共享系统内存，无独立显存。快速拖拽时 Leaflet 持续请求新瓦片，Chromium 渲染进程同时处理 pan 重绘 + WebP 解码 + GPU 合成，瓦片纹理堆积导致渲染进程内存耗尽崩溃。QWebEngineView 失去渲染进程后 Qt 主线程阻塞，整个程序冻结。

## 方案

### 第一步：HTML 端降低渲染压力（降低崩溃概率）

改 `test/web/amap_view.html`：

1. Canvas 渲染器 (`preferCanvas: true`) — 瓦片归 GPU 纹理管线，替代 DOM 元素
2. `updateWhenIdle: true` — 拖拽过程中不加载新瓦片，松手后再加载
3. `maxNativeZoom` 18 → 16 — 高缩放级瓦片数量减半

### 第二步：Qt 端监控渲染进程 + 自动恢复（防止卡死）

改 `test/teagardensystem.h`：加 `bool m_mapReloading` / `int m_mapRetryMs`

改 `test/teagardensystem.cpp` `setupMapView()`：监听 `renderProcessTerminated` 信号，崩溃后指数退避自动 reload（1s → 2s → 4s → ... → 30s 上限）。

## 改动范围

| 文件 | 改动 |
|------|------|
| test/web/amap_view.html | preferCanvas + updateWhenIdle + maxNativeZoom |
| test/teagardensystem.h | 2 个成员变量 |
| test/teagardensystem.cpp | setupMapView() 中加 renderProcessTerminated 信号连接 |
