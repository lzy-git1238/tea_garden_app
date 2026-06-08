# YOLO RKNN 病虫害检测集成设计

> 将 RKNN YOLO 推理集成到 SRT 视频流管线，实现茶树病虫害实时检测。

## 背景

当前 SRT 视频流已能稳定显示无人机画面，病虫害检测通过 `injectMockPest()` 模拟。需要接入真实的 YOLO 推理能力。

### 现有资产

- **SrtVideoStreamer**：已声明 `frameReadyForYolo` 信号（raw RGB24），但从未 emit
- **PestDashboard + PestStore**：已就绪，支持记录/展示/图表
- **RKNN 模型**：YOLOv5s，640×640 输入，6 类茶树病害，已转换为 `.rknn` 格式
- **ELF2 环境**：`~/sdcard/rknn_env` 虚拟环境，模型文件在 `~/sdcard/models/`
- **参考模式**：SenseVoice RKNN 集成（QProcess 调 Python），但不适合高频流式场景

### 模型参数

| 参数 | 值 |
|------|-----|
| 架构 | YOLOv5s |
| 输入尺寸 | 640×640 RGB |
| 平台 | RK3588（3×NPU 核心） |
| 量化 | INT8 |
| 类别 | 6 类（见下方映射表） |
| 置信度阈值 | 0.25 |
| NMS 阈值 | 0.45 |

## 架构设计

### 整体数据流

```
SRT 视频流 → SrtVideoStreamer (Qt 线程)
                │
                ├── emit frameReady(QImage)  ──→ videoLabel 直接显示
                │
                └── emit frameReadyForYolo ──→ YoloDetector (Qt 线程)
                                                    │
                                              降采样到 640×640
                                              帧率限制 2-5 fps
                                                    │
                                              TcpSocket 发送
                                                    │
                                              ┌─────┴─────┐
                                              │  Python   │
                                              │  推理服务  │
                                              │           │
                                              │ RKNN 加载  │
                                              │ 常驻进程   │
                                              └─────┬─────┘
                                                    │
                                              JSON 检测结果
                                                    │
                                              Qt 画框叠加
                                              写入 PestStore
```

### 三种方案对比（选定方案 A）

| 方案 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| **A: Unix Socket（选定）** | Python 常驻进程 + Socket 通信 | 模型只加载一次，帧级延迟低 | 需维护进程生命周期 |
| B: Socket + 共享内存 | 用 shm 传帧 | 性能极限最优 | 640×640 才 1.2MB，过度优化 |
| C: QProcess 逐帧调用 | 每帧启 Python 进程 | 最简单 | 每次启动 2-3 秒，无法实时 |

## 组件设计

### 1. Qt 端：YoloDetector 类

新增文件：`yolodetector.h` / `yolodetector.cpp`

```cpp
struct DetectionResult {
    QString className;   // "Brown Blight" 等
    double score;        // 0.0~1.0
    QRect  bbox;         // 原始帧坐标系
};

class YoloDetector : public QObject {
    Q_OBJECT
public:
    explicit YoloDetector(QObject *parent = nullptr);
    ~YoloDetector();

    void start(const QString &host = "127.0.0.1", int port = 9800);
    void stop();
    void setFpsLimit(int fps);  // 默认 3

signals:
    void detectionReady(const QImage &frame, const QVector<DetectionResult> &results);
    void statusMessage(const QString &msg);
    void fpsUpdated(double inferFps);

public slots:
    void onNewFrame(const QImage &frame);

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError err);

private:
    QTcpSocket  *m_socket;
    QTimer      *m_heartbeat;
    QImage       m_lastFrame;
    int          m_fpsLimit = 3;
    QElapsedTimer m_fpsTimer;
    int          m_frameCount = 0;
    QByteArray   m_recvBuffer;
};
```

**关键行为**：

1. 收到 `frameReadyForYolo` → 检查帧率限制 → `QImage::scaled(640, 640)` 降采样
2. 发送前保存原始帧引用，等结果回来后在原始帧上画框
3. 收到 JSON → 解析 → 映射 bbox 到原始帧坐标 → `emit detectionReady()`
4. 断连自动重试（3 秒间隔）

### 2. Python 端：rknn_yolo_server.py

部署位置：ELF2 `~/sdcard/rknn_yolo_server.py`

```python
class YoloServer:
    def __init__(self, model_path, port=9800):
        self.rknn = RKNN()
        self.rknn.config(
            mean_values=[[0, 0, 0]],
            std_values=[[255, 255, 255]],
            target_platform='rk3588'
        )
        self.rknn.load_rknn(model_path)
        self.rknn.init_runtime(core_mask=RKNN.NPU_CORE_0_1_2)  # 三核 NPU
        self.port = port

    def handle_client(self, conn):
        while True:
            # 1. 读帧: [4B总长度][4B宽][4B高][RGB数据]
            header = recv_exact(conn, 8)
            total, w, h = parse_header(header)
            rgb_data = recv_exact(conn, w * h * 3)

            # 2. 预处理 → 推理 → 后处理
            img = np.frombuffer(rgb_data, dtype=np.uint8).reshape(h, w, 3)
            detections = self.infer(img)

            # 3. 返回 JSON
            send_json(conn, {"detections": detections, "infer_ms": ms})
```

**后处理**：复用现有转换脚本中的 `yolov5_post_process`、`filter_boxes`、`nms_boxes` 函数。

**启动方式**：通过 Shell 包装脚本解决虚拟环境问题

新增文件：`start_yolo_server.sh`（部署到 ELF2 `~/sdcard/`）

```bash
#!/bin/bash
# YOLO RKNN 推理服务启动脚本
# 解决 rknn_env 虚拟环境依赖问题

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/rknn_env"
MODEL_DIR="${SCRIPT_DIR}/models"
PORT="${YOLO_PORT:-9800}"

# 检查虚拟环境
if [ ! -d "$VENV_DIR" ]; then
    echo "[ERROR] 虚拟环境不存在: $VENV_DIR"
    exit 1
fi

# 激活虚拟环境
source "${VENV_DIR}/bin/activate"

# 检查模型文件
RKNN_MODEL="${MODEL_DIR}/yolov5s_last.rknn"
if [ ! -f "$RKNN_MODEL" ]; then
    echo "[ERROR] 模型文件不存在: $RKNN_MODEL"
    exit 1
fi

echo "[INFO] 启动 YOLO RKNN 推理服务..."
echo "[INFO] 虚拟环境: $VENV_DIR"
echo "[INFO] 模型: $RKNN_MODEL"
echo "[INFO] 端口: $PORT"

# 启动推理服务（在虚拟环境下运行）
exec python "${SCRIPT_DIR}/rknn_yolo_server.py" \
    --port "$PORT" \
    --model "$RKNN_MODEL"
```

**使用方式**：

```bash
# 手动启动（SSH 到 ELF2 后执行）
cd ~/sdcard
chmod +x start_yolo_server.sh
./start_yolo_server.sh

# 或指定端口
YOLO_PORT=9801 ./start_yolo_server.sh
```

Qt 端不需要管虚拟环境——它只通过 TCP 连接到 `localhost:9800`，Python 服务已在 Shell 脚本中处理好环境。

### 3. Socket 通信协议

#### Qt → Python（发送帧）

```
[4B uint32 总长度][4B 宽][4B 高][RGB24 像素数据]
```

- 总长度 = 8 + width × height × 3
- 固定发送 640×640 降采样帧（1,228,800 字节）

#### Python → Qt（返回结果）

```json
{
  "detections": [
    {"class": "Brown Blight", "score": 0.87, "bbox": [120, 80, 350, 290]}
  ],
  "infer_ms": 65
}
```

- bbox 格式：`[x1, y1, x2, y2]`，坐标基于 640×640 输入
- Qt 端映射到原始帧尺寸

#### 连接管理

- 默认端口：`localhost:9800`，可通过 `TEA_YOLO_PORT` 环境变量覆盖
- 断连自动重试（3 秒间隔）
- 心跳：Qt 每 5 秒发空包，Python 回 pong

### 4. TeaGardenSystem 集成

在构造函数中（`srtvideostreamer` 初始化之后）：

```cpp
m_yoloDetector = new YoloDetector(this);
connect(m_videoStreamer, &SrtVideoStreamer::frameReadyForYolo,
        m_yoloDetector, &YoloDetector::onNewFrame);
connect(m_yoloDetector, &YoloDetector::detectionReady,
        this, &TeaGardenSystem::onYoloResults);
m_yoloDetector->start();
```

### 5. 画框叠加

收到检测结果后，在当前视频帧上画框：

```cpp
void TeaGardenSystem::onYoloResults(const QImage &frame,
                                     const QVector<DetectionResult> &results) {
    QImage overlay = frame.copy();
    QPainter painter(&overlay);
    for (auto &det : results) {
        painter.setPen(QPen(colorForClass(det.className), 2));
        painter.drawRect(det.bbox);
        // 标签背景 + 文字
        painter.drawText(det.bbox.topLeft() - QPoint(0, 5),
                         QString("%1 %2%").arg(det.className).arg(det.score * 100, 0, 'f', 0));
    }
    painter.end();
    videoLabel->setPixmap(QPixmap::fromImage(overlay).scaled(...));
}
```

### 6. 类名映射

| 模型输出 | 中文名 | PestStore 类型 | 颜色 |
|---------|--------|---------------|------|
| Algal Leaf Spot | 茶藻斑病 | 茶藻斑病 | #2196F3 |
| Brown Blight | 茶炭疽病 | 茶炭疽病 | #F44336 |
| Gray Blight | 茶灰斑病 | 茶灰斑病 | #9E9E9E |
| Healthy | 健康 | （不记录） | #4CAF50 |
| Helopeltis | 茶小绿叶蝉 | 茶小绿叶蝉 | #FF9800 |
| Red Leaf Spot | 茶红叶斑 | 茶红叶斑 | #E91E63 |

### 7. PestStore 写入

检测到非 Healthy 类别时记录：

```cpp
if (!det.className.contains("Healthy")) {
    PestRecord rec;
    rec.timestamp = QDateTime::currentDateTime();
    rec.pestType = mapClassName(det.className);
    rec.severity = scoreToSeverity(det.score);  // <0.5 轻微, <0.75 中等, >=0.75 严重
    PestStore::instance()->append(rec);
}
```

## 文件变更清单

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `yolodetector.h` | 新增 | YoloDetector 类声明 |
| `yolodetector.cpp` | 新增 | Socket 通信 + 降采样 + 结果解析 |
| `srtvideostreamer.cpp` | 修改 | 补 emit `frameReadyForYolo` |
| `teagardensystem.h` | 修改 | 添加 `YoloDetector*` 成员 + `onYoloResults` 声明 |
| `teagardensystem.cpp` | 修改 | 构造函数初始化 + 画框叠加 + 写入 PestStore |
| `test.pro` | 修改 | 添加 `yolodetector.cpp/h` |
| `rknn_yolo_server.py` | 新增 | Python 推理服务（ELF2 部署） |
| `start_yolo_server.sh` | 新增 | Shell 包装脚本，解决 rknn_env 虚拟环境依赖 |

## 错误处理

| 场景 | 处理方式 |
|------|---------|
| Python 服务未启动 | YoloDetector 连接失败 → 3 秒重试，状态栏提示 |
| Socket 断连 | 自动重连，状态栏显示"YOLO 推理断开" |
| 推理超时 | Qt 端 3 秒超时，丢弃该帧，不阻塞视频显示 |
| 检测结果为空 | 不画框，不写入 PestStore |
| 模型加载失败 | Python 服务打印错误并退出，Qt 端重连时提示 |

## 性能预期

| 指标 | 预期值 |
|------|--------|
| 单帧推理延迟 | 50-80ms（3×NPU 核心） |
| 帧发送耗时 | ~5ms（640×640 RGB，localhost） |
| 端到端延迟 | <100ms（帧到画框） |
| 检测帧率 | 3 fps（可配置 2-5） |
| 视频显示帧率 | 不受影响（独立管线） |
