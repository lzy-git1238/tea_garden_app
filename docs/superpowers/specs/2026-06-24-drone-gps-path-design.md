# 无人机 GPS 路径实时显示

## 需求

订阅 PX4 `/fmu/out/vehicle_global_position` (px4_msgs/VehicleGlobalPosition) 的经纬度，
在地图上实时显示无人机飞行轨迹（折线 + 当前位置箭头）。

## 防卡死约束

- GPS 5-10Hz，不能每次回调都调 runJavaScript，必须限流
- 地图拖拽时 WebEngine 渲染线程繁忙，不可强行注入 JS
- JS payload 尽量小，避免大量字符串拼接

## 架构

新建 `Ros2GpsSubscriber`，复用 `Ros2ImageSubscriber` 的 PIMPL + 独立线程 spin 模式。

```
PX4 → /fmu/out/vehicle_global_position
  → Ros2GpsSubscriber (独立线程 rclcpp::spin)
    → emit gpsPosition(lat, lon, alt)  [Qt::QueuedConnection]
      → TeaGardenSystem::onDroneGps()
        → 写入环形 buffer, 裁剪到 MAX_PATH_POINTS
        → drawDronePath() [QTimer 500ms 防抖]
          → 检查 WebEngine 存活, 跳过 if not
          → runJavaScript(轻量 JS)
```

## 防抖策略

- `m_drawPathTimer` 500ms 间隔，GPS 回调只写 buffer 不画图
- 定时器到期取最新 buffer 一次性绘制
- 绘制前检查：`pH.isNull() || m_mapReloading` → 跳过
- JS 只传坐标数组，Leaflet 端纯数据处理

## 改动

| 文件 | 改动 |
|------|------|
| test/ros2gpssubscriber.h | 新建，PIMPL 封装 px4_msgs |
| test/ros2gpssubscriber.cpp | 新建，rclcpp 订阅 + spin |
| test/teagardensystem.h | Subscriber、path buffer、draw timer |
| test/teagardensystem.cpp | 连接信号、buffer 管理、JS 绘制 |
| test/test.pro | 链接 px4_msgs |
