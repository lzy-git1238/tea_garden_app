# 网页远程监控设计

> RK3588 作为服务器，通过 WebSocket 将 Qt APP 所有数据推送到网页端，实现远程监控。

## 背景

当前只有本地 Qt APP，需要在局域网内通过浏览器远程查看茶园数据。Qt APP 继续运行，同时内嵌 HTTP + WebSocket 服务。

## 架构

```
RK3588
┌────────────────────────────────────────┐
│  Qt APP (现有)                          │
│  ├── LoraManager (传感器数据)            │
│  ├── SrtVideoStreamer (无人机视频)        │
│  ├── YoloDetector (病虫害检测)            │
│  ├── 设备控制 (灌溉/驱虫/小车)             │
│  └── RoverControlPanel (小车图传)        │
│          │                               │
│  ┌───────┴────────┐                     │
│  │ WebServer      │  ← 新增              │
│  │ ├── HTTP :8080 │  (静态HTML)          │
│  │ └── WS   :8080 │  (实时数据)           │
│  └────────────────┘                     │
└────────────────────────────────────────┘
                  │
         局域网 WiFi / 以太网
                  │
    ┌─────────────┴─────────────┐
    │ 手机浏览器    │  电脑浏览器  │
    │ http://板子IP:8080          │
    └───────────────────────────┘
```

## 数据流

| 类型 | 方向 | 格式 | 频率 |
|------|------|------|------|
| 环境数据 | Qt→Web | JSON `{temp, humi, ...}` | 3s |
| 无人机视频 | Qt→Web | WebSocket JPEG binary | 实时 MJPEG |
| 小车图传 | Qt→Web | WebSocket JPEG binary | 实时 MJPEG |
| 病虫害检测 | Qt→Web | WebSocket JSON（已在帧上画框） | 检测到时 |
| YOLO 框 | Qt→Web | **在 Qt 端画好框再编码为 JPEG 推送** | 实时 |
| 设备控制 | Web→Qt | WebSocket `{cmd: "pump_on"}` | 按需 |
| 小车控制 | Web→Qt | WebSocket `{cmd: "rover", key: "w"}` | 按需 |
| 天气数据 | Qt→Web | WebSocket JSON | 首次+每30min |

## 组件设计

### 1. WebServer 类（新增）

文件：`webserver.h` / `webserver.cpp`

```cpp
class WebServer : public QObject {
    Q_OBJECT
public:
    explicit WebServer(QObject *parent = nullptr);
    bool start(int port = 8080);
    void stop();

    // 推送传感器数据
    void pushSensorData(const SensorData &data);
    // 推送无人机视频帧（JPEG）
    void pushDroneFrame(const QByteArray &jpeg);
    // 推送小车图传帧（JPEG）
    void pushRoverFrame(const QByteArray &jpeg);
    // 推送病虫害检测结果
    void pushPestDetection(const QJsonObject &detection);
    // 推送天气数据
    void pushWeather(const QJsonObject &weather);
    // 推送 YOLO 结果（已画框的帧）
    void pushYoloFrame(const QByteArray &jpeg);

signals:
    void controlCommand(const QString &cmd, const QJsonObject &params);

private:
    QTcpServer *m_httpServer;
    QWebSocketServer *m_wsServer;
    QList<QWebSocket*> m_clients;
};
```

### 2. Web 前端页面

文件：`web/assets/index.html`（qrc 资源）

- 响应式三栏布局，适配手机和电脑
- 无人机视频区 + 小车图传区
- 环境数据卡片
- 病虫害统计饼图（Chart.js）
- 天气预警
- 设备控制按钮（灌溉泵/驱虫灯/小车WASD）

### 3. Qt 端改动

在 `TeaGardenSystem` 构造函数中初始化 WebServer，在数据回调处推送：

```cpp
// 环境数据 → WebSocket
connect(loraManager, &LoraManager::sensorDataUpdated, 
        webServer, &WebServer::pushSensorData);

// 无人机帧 → WebSocket（YOLO 框已在 onYoloResults 中画好）
// 在 onYoloResults 之后将 overlay 编码为 JPEG 推送
```

### 4. 视频帧推送

Qt 端帧处理流水线：
```
原始帧 → YOLO 检测 → onYoloResults 画框 → QImage → JPEG → WebSocket → 网页 <img>
```

使用 `QImage::save()` 编码为 JPEG 后推送。

## 文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `test/webserver.h` | 新增 | WebServer 类声明 |
| `test/webserver.cpp` | 新增 | HTTP + WebSocket 实现 |
| `test/web/index.html` | 新增 | 前端页面 |
| `test/teagardensystem.cpp` | 修改 | 初始化 WebServer，数据推送 |
| `test/test.pro` | 修改 | 添加 webserver 源文件 |
| `test/resources.qrc` | 修改 | 添加 web/index.html 资源 |

## 视频帧编码

无人机帧：1920×1080 → 缩放到 640×360 → JPEG base64 → WebSocket
小车帧：320×240 → JPEG base64 → WebSocket

## 前端依赖

- Chart.js（CDN）：病虫害饼图
- 原生 WebSocket API：实时数据
- 无其他框架，纯 HTML/CSS/JS

## 错误处理

| 场景 | 处理 |
|------|------|
| WebSocket 断连 | 前端自动重连（3s），显示"连接断开" |
| 无视频帧 | 显示占位图 |
| 设备控制失败 | 返回错误 JSON |
