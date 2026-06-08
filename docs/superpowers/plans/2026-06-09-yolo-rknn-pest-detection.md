# YOLO RKNN 病虫害检测集成 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 RKNN YOLO 推理集成到 SRT 视频流，实现茶树病虫害实时检测，检测结果画框叠加到视频并写入 PestStore。

**架构:** Python 常驻推理服务（Socket 端口 9800）+ Qt 端 YoloDetector 类通过 TCP 发送降采样帧、接收 JSON 检测结果。Shell 脚本包装虚拟环境启动。

**Tech Stack:** Qt C++ (QTcpSocket, QImage, QPainter), Python 3 (rknn_api, socket, numpy, opencv), RKNN RK3588 NPU

---

## File Structure

| 文件 | 操作 | 职责 |
|------|------|------|
| `test/rknn_yolo_server.py` | 新增 | Python 推理服务：加载 RKNN 模型，Socket 监听，接收帧→推理→返回 JSON |
| `test/start_yolo_server.sh` | 新增 | Shell 包装脚本：source rknn_env 虚拟环境后启动 Python 服务 |
| `test/yolodetector.h` | 新增 | YoloDetector 类声明：TCP 连接管理、帧发送、结果接收 |
| `test/yolodetector.cpp` | 新增 | YoloDetector 实现：降采样、协议解析、断连重试 |
| `test/srtvideostreamer.cpp` | 修改 | 在帧循环中补 emit `frameReadyForYolo` |
| `test/teagardensystem.h` | 修改 | 添加 `YoloDetector*` 成员 + `onYoloResults` 声明 |
| `test/teagardensystem.cpp` | 修改 | 构造函数初始化 + 画框叠加 + 类名映射 + PestStore 写入 |
| `test/test.pro` | 修改 | 添加 yolodetector.cpp/h 到 SOURCES/HEADERS |

---

### Task 1: Python 推理服务核心逻辑

**Files:**
- Create: `test/rknn_yolo_server.py`

这是整个系统的推理引擎。先写完整的 Python 服务，包含模型加载、Socket 服务、帧接收/推理/结果返回。

- [ ] **Step 1: 创建 rknn_yolo_server.py，包含完整的推理服务**

```python
#!/usr/bin/env python3
"""
YOLO RKNN 推理服务
通过 Socket 接收 Qt 端发送的 RGB 帧，执行 RKNN YOLO 推理，返回检测结果。

协议：
  Qt → Python: [4B total_len][4B width][4B height][RGB24 pixel data]
  Python → Qt: JSON {"detections": [...], "infer_ms": N}

部署位置: ELF2 ~/sdcard/rknn_yolo_server.py
虚拟环境: ~/sdcard/rknn_env (需要 rknn_api, numpy, opencv)
"""

import argparse
import json
import socket
import struct
import sys
import time
import traceback

import cv2
import numpy as np

# ── YOLOv5 后处理参数 ──
OBJ_THRESH = 0.25
NMS_THRESH = 0.45
IMG_SIZE = 640

CLASSES = (
    'Algal Leaf Spot',   # 茶藻斑病
    'Brown Blight',      # 茶炭疽病
    'Gray Blight',       # 茶灰斑病
    'Healthy',           # 健康
    'Helopeltis',        # 茶小绿叶蝉
    'Red Leaf Spot',     # 茶红叶斑
)


def xywh2xyxy(x):
    y = np.copy(x)
    y[:, 0] = x[:, 0] - x[:, 2] / 2
    y[:, 1] = x[:, 1] - x[:, 3] / 2
    y[:, 2] = x[:, 0] + x[:, 2] / 2
    y[:, 3] = x[:, 1] + x[:, 3] / 2
    return y


def process(input, mask, anchors):
    anchors = [anchors[i] for i in mask]
    grid_h, grid_w = map(int, input.shape[0:2])
    box_confidence = input[..., 4]
    box_confidence = np.expand_dims(box_confidence, axis=-1)
    box_class_probs = input[..., 5:]
    box_xy = input[..., :2] * 2 - 0.5

    col = np.tile(np.arange(0, grid_w), grid_w).reshape(-1, grid_w)
    row = np.tile(np.arange(0, grid_h).reshape(-1, 1), grid_h)
    col = col.reshape(grid_h, grid_w, 1, 1).repeat(3, axis=-2)
    row = row.reshape(grid_h, grid_w, 1, 1).repeat(3, axis=-2)
    grid = np.concatenate((col, row), axis=-1)
    box_xy += grid
    box_xy *= int(IMG_SIZE / grid_h)

    box_wh = pow(input[..., 2:4] * 2, 2)
    box_wh = box_wh * anchors
    box = np.concatenate((box_xy, box_wh), axis=-1)
    return box, box_confidence, box_class_probs


def filter_boxes(boxes, box_confidences, box_class_probs):
    boxes = boxes.reshape(-1, 4)
    box_confidences = box_confidences.reshape(-1)
    box_class_probs = box_class_probs.reshape(-1, box_class_probs.shape[-1])

    _box_pos = np.where(box_confidences >= OBJ_THRESH)
    boxes = boxes[_box_pos]
    box_confidences = box_confidences[_box_pos]
    box_class_probs = box_class_probs[_box_pos]

    class_max_score = np.max(box_class_probs, axis=-1)
    classes = np.argmax(box_class_probs, axis=-1)
    _class_pos = np.where(class_max_score >= OBJ_THRESH)

    boxes = boxes[_class_pos]
    classes = classes[_class_pos]
    scores = (class_max_score * box_confidences)[_class_pos]
    return boxes, classes, scores


def nms_boxes(boxes, scores):
    x = boxes[:, 0]
    y = boxes[:, 1]
    w = boxes[:, 2] - boxes[:, 0]
    h = boxes[:, 3] - boxes[:, 1]
    areas = w * h
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        xx1 = np.maximum(x[i], x[order[1:]])
        yy1 = np.maximum(y[i], y[order[1:]])
        xx2 = np.minimum(x[i] + w[i], x[order[1:]] + w[order[1:]])
        yy2 = np.minimum(y[i] + h[i], y[order[1:]] + h[order[1:]])
        w1 = np.maximum(0.0, xx2 - xx1 + 0.00001)
        h1 = np.maximum(0.0, yy2 - yy1 + 0.00001)
        inter = w1 * h1
        ovr = inter / (areas[i] + areas[order[1:]] - inter)
        inds = np.where(ovr <= NMS_THRESH)[0]
        order = order[inds + 1]
    keep = np.array(keep)
    return keep


def yolov5_post_process(input_data):
    masks = [[0, 1, 2], [3, 4, 5], [6, 7, 8]]
    anchors = [
        [10, 13], [16, 30], [33, 23],
        [30, 61], [62, 45], [59, 119],
        [116, 90], [156, 198], [373, 326],
    ]
    boxes, classes, scores = [], [], []
    for input, mask in zip(input_data, masks):
        b, c, s = process(input, mask, anchors)
        b, c, s = filter_boxes(b, c, s)
        boxes.append(b)
        classes.append(c)
        scores.append(s)

    boxes = np.concatenate(boxes)
    boxes = xywh2xyxy(boxes)
    classes = np.concatenate(classes)
    scores = np.concatenate(scores)

    nboxes, nclasses, nscores = [], [], []
    for c in set(classes):
        inds = np.where(classes == c)
        b = boxes[inds]
        c = classes[inds]
        s = scores[inds]
        keep = nms_boxes(b, s)
        nboxes.append(b[keep])
        nclasses.append(c[keep])
        nscores.append(s[keep])

    if not nclasses and not nscores:
        return None, None, None

    boxes = np.concatenate(nboxes)
    classes = np.concatenate(nclasses)
    scores = np.concatenate(nscores)
    return boxes, classes, scores


# ── Socket 工具函数 ──

def recv_exact(conn, n):
    """从 socket 精确读取 n 字节"""
    buf = b''
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("客户端断开连接")
        buf += chunk
    return buf


def send_json(conn, data):
    """发送 JSON 结果（4字节长度前缀 + JSON UTF-8）"""
    payload = json.dumps(data).encode('utf-8')
    conn.sendall(struct.pack('<I', len(payload)) + payload)


def recv_json(conn):
    """接收 JSON 结果（4字节长度前缀 + JSON UTF-8）"""
    header = recv_exact(conn, 4)
    length = struct.unpack('<I', header)[0]
    payload = recv_exact(conn, length)
    return json.loads(payload.decode('utf-8'))


# ── 推理服务 ──

class YoloServer:
    def __init__(self, model_path, port=9800):
        from rknn.api import RKNN

        self.port = port
        self.rknn = RKNN(verbose=False)

        print(f"[INFO] 加载 RKNN 模型: {model_path}")
        self.rknn.config(
            mean_values=[[0, 0, 0]],
            std_values=[[255, 255, 255]],
            target_platform='rk3588'
        )
        ret = self.rknn.load_rknn(model_path)
        if ret != 0:
            print("[ERROR] 加载模型失败!")
            sys.exit(1)

        print("[INFO] 初始化 RKNN 运行时 (3×NPU 核心)...")
        ret = self.rknn.init_runtime(core_mask=3)  # NPU_CORE_0_1_2 = 0b111 = 3
        if ret != 0:
            print("[ERROR] 初始化运行时失败!")
            sys.exit(1)
        print("[INFO] 模型加载完成，准备就绪")

    def infer(self, img_rgb):
        """
        对 RGB 图像执行 YOLO 推理。
        img_rgb: numpy array, shape (H, W, 3), dtype uint8, RGB 顺序
        返回: list of dict, 每个 dict 含 class, score, bbox [x1,y1,x2,y2]
        """
        t0 = time.time()

        # 预处理: resize 到 640x640
        img = cv2.resize(img_rgb, (IMG_SIZE, IMG_SIZE))
        img = np.expand_dims(img, 0)

        # 推理
        outputs = self.rknn.inference(inputs=[img], data_format=['nhwc'])

        # 后处理: reshape + transpose
        input_data = []
        for out in outputs:
            reshaped = out.reshape([3, -1] + list(out.shape[-2:]))
            input_data.append(np.transpose(reshaped, (2, 3, 0, 1)))

        boxes, classes, scores = yolov5_post_process(input_data)

        infer_ms = (time.time() - t0) * 1000

        detections = []
        if boxes is not None:
            for box, score, cl in zip(boxes, scores, classes):
                x1, y1, x2, y2 = box
                detections.append({
                    "class": CLASSES[int(cl)],
                    "score": round(float(score), 4),
                    "bbox": [int(x1), int(y1), int(x2), int(y2)],
                })

        return detections, round(infer_ms, 1)

    def handle_client(self, conn, addr):
        """处理单个客户端连接（持续接收帧）"""
        print(f"[INFO] 客户端已连接: {addr}")
        try:
            while True:
                # 1. 读取帧头: [4B total_len][4B width][4B height]
                header = recv_exact(conn, 12)
                total_len, width, height = struct.unpack('<III', header)

                # 2. 读取 RGB 像素数据
                expected = width * height * 3
                if total_len != expected:
                    print(f"[WARN] 帧长度不匹配: header={total_len}, expected={expected}")
                    continue
                rgb_data = recv_exact(conn, expected)

                # 3. 转为 numpy 数组并推理
                img = np.frombuffer(rgb_data, dtype=np.uint8).reshape(height, width, 3)
                detections, infer_ms = self.infer(img)

                # 4. 返回结果
                send_json(conn, {
                    "detections": detections,
                    "infer_ms": infer_ms,
                })

        except ConnectionError:
            print(f"[INFO] 客户端断开: {addr}")
        except Exception as e:
            print(f"[ERROR] 处理客户端异常: {e}")
            traceback.print_exc()

    def run(self):
        """启动 Socket 服务"""
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('0.0.0.0', self.port))
        server.listen(1)
        print(f"[INFO] YOLO 推理服务已启动，监听端口 {self.port}")

        try:
            while True:
                conn, addr = server.accept()
                # 单客户端模式：新连接覆盖旧连接
                self.handle_client(conn, addr)
        except KeyboardInterrupt:
            print("\n[INFO] 服务已停止")
        finally:
            server.close()
            self.rknn.release()


def main():
    parser = argparse.ArgumentParser(description='YOLO RKNN 推理服务')
    parser.add_argument('--port', type=int, default=9800, help='监听端口 (默认 9800)')
    parser.add_argument('--model', type=str, required=True, help='RKNN 模型文件路径')
    args = parser.parse_args()

    server = YoloServer(model_path=args.model, port=args.port)
    server.run()


if __name__ == '__main__':
    main()
```

- [ ] **Step 2: 验证 Python 脚本语法**

```bash
cd /home/lzy/文档/QTproject/test
python3 -c "import ast; ast.parse(open('rknn_yolo_server.py').read()); print('语法检查通过')"
```

Expected: 输出 `语法检查通过`

- [ ] **Step 3: Commit**

```bash
git add test/rknn_yolo_server.py
git commit -m "feat: 添加 YOLO RKNN Python 推理服务"
```

---

### Task 2: Shell 包装脚本

**Files:**
- Create: `test/start_yolo_server.sh`

解决 `~/sdcard/rknn_env` 虚拟环境依赖问题。

- [ ] **Step 1: 创建 start_yolo_server.sh**

```bash
#!/bin/bash
# YOLO RKNN 推理服务启动脚本
# 解决 rknn_env 虚拟环境依赖问题
#
# 用法:
#   ./start_yolo_server.sh                    # 使用默认配置
#   YOLO_PORT=9801 ./start_yolo_server.sh     # 指定端口
#
# 部署到 ELF2: ~/sdcard/start_yolo_server.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/rknn_env"
MODEL_DIR="${SCRIPT_DIR}/models"
PORT="${YOLO_PORT:-9800}"

# 检查虚拟环境
if [ ! -d "$VENV_DIR" ]; then
    echo "[ERROR] 虚拟环境不存在: $VENV_DIR"
    echo "[INFO] 请先创建: python3 -m venv $VENV_DIR"
    echo "[INFO] 然后安装: source $VENV_DIR/bin/activate && pip install rknn-api numpy opencv-python"
    exit 1
fi

# 激活虚拟环境
source "${VENV_DIR}/bin/activate"

# 检查模型文件
RKNN_MODEL="${MODEL_DIR}/yolov5s_last.rknn"
if [ ! -f "$RKNN_MODEL" ]; then
    echo "[ERROR] 模型文件不存在: $RKNN_MODEL"
    echo "[INFO] 请将 yolov5s_last.rknn 放到 $MODEL_DIR/ 目录下"
    exit 1
fi

echo "========================================="
echo "  YOLO RKNN 推理服务"
echo "========================================="
echo "[INFO] 虚拟环境: $VENV_DIR"
echo "[INFO] 模型文件: $RKNN_MODEL"
echo "[INFO] 监听端口: $PORT"
echo "[INFO] 按 Ctrl+C 停止服务"
echo "========================================="

# 启动推理服务（在虚拟环境下运行）
exec python "${SCRIPT_DIR}/rknn_yolo_server.py" \
    --port "$PORT" \
    --model "$RKNN_MODEL"
```

- [ ] **Step 2: 设置可执行权限**

```bash
chmod +x /home/lzy/文档/QTproject/test/start_yolo_server.sh
```

- [ ] **Step 3: 验证 Shell 脚本语法**

```bash
bash -n /home/lzy/文档/QTproject/test/start_yolo_server.sh && echo "语法检查通过"
```

Expected: 输出 `语法检查通过`

- [ ] **Step 4: Commit**

```bash
git add test/start_yolo_server.sh
git commit -m "feat: 添加 YOLO 服务启动脚本（解决虚拟环境依赖）"
```

---

### Task 3: Qt 端 YoloDetector 类

**Files:**
- Create: `test/yolodetector.h`
- Create: `test/yolodetector.cpp`

Qt 端的 Socket 客户端，负责连接 Python 服务、发送降采样帧、接收检测结果。

- [ ] **Step 1: 创建 yolodetector.h**

```cpp
#ifndef YOLODETECTOR_H
#define YOLODETECTOR_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QByteArray>

struct DetectionResult {
    QString className;   // "Brown Blight" 等英文类名
    double score;        // 0.0~1.0
    QRect  bbox;         // 基于原始帧坐标系
};

class YoloDetector : public QObject {
    Q_OBJECT
public:
    explicit YoloDetector(QObject *parent = nullptr);
    ~YoloDetector();

    void start(const QString &host = "127.0.0.1", int port = 9800);
    void stop();
    void setFpsLimit(int fps);

signals:
    void detectionReady(const QImage &frame, const QVector<DetectionResult> &results);
    void statusMessage(const QString &msg);
    void fpsUpdated(double inferFps);

public slots:
    void onNewFrame(const QImage &frame);

private slots:
    void onConnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);

private:
    void connectToServer();
    void sendFrame(const QImage &frame);
    void parseDetectionJson(const QByteArray &json);

    QTcpSocket  *m_socket = nullptr;
    QTimer      *m_reconnectTimer = nullptr;
    QTimer      *m_heartbeatTimer = nullptr;
    QString      m_host;
    int          m_port = 9800;

    QImage       m_lastFrame;         // 用于画框的原始帧
    QImage       m_lastResizedFrame;  // 降采样后的帧（发送用）
    int          m_fpsLimit = 3;
    QElapsedTimer m_fpsTimer;
    int          m_frameCount = 0;
    double       m_currentFps = 0;

    QByteArray   m_recvBuffer;        // 接收缓冲区
    bool         m_waitingResult = false;  // 是否正在等待推理结果

    static constexpr int kTargetWidth  = 640;
    static constexpr int kTargetHeight = 640;
    static constexpr int kConnectRetryMs = 3000;
    static constexpr int kHeartbeatMs    = 5000;
    static constexpr int kInferTimeoutMs = 3000;
};

#endif // YOLODETECTOR_H
```

- [ ] **Step 2: 创建 yolodetector.cpp**

```cpp
#include "yolodetector.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>

YoloDetector::YoloDetector(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
    , m_heartbeatTimer(new QTimer(this))
{
    // Socket 信号
    connect(m_socket, &QTcpSocket::connected,
            this, &YoloDetector::onConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &YoloDetector::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &YoloDetector::onSocketError);

    // 断连自动重连
    connect(m_reconnectTimer, &QTimer::timeout, this, &YoloDetector::connectToServer);

    // 心跳
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            // 发送空心跳包（4字节总长度=0 + 宽=0 + 高=0）
            QByteArray header;
            header.append(static_cast<char>(0), 4);  // total_len = 0 (心跳标识)
            header.append(static_cast<char>(0), 4);  // width = 0
            header.append(static_cast<char>(0), 4);  // height = 0
            m_socket->write(header);
            m_socket->flush();
        }
    });
}

YoloDetector::~YoloDetector() {
    stop();
}

void YoloDetector::start(const QString &host, int port) {
    m_host = host;
    m_port = port;
    m_fpsTimer.start();
    m_frameCount = 0;
    connectToServer();
}

void YoloDetector::stop() {
    m_reconnectTimer->stop();
    m_heartbeatTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

void YoloDetector::setFpsLimit(int fps) {
    m_fpsLimit = qBound(1, fps, 30);
}

void YoloDetector::connectToServer() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    qDebug() << "[YOLO] 连接到推理服务:" << m_host << ":" << m_port;
    emit statusMessage("正在连接 YOLO 推理服务...");
    m_socket->connectToHost(m_host, m_port);
}

void YoloDetector::onConnected() {
    qDebug() << "[YOLO] 已连接到推理服务";
    emit statusMessage("YOLO 推理服务已连接");
    m_reconnectTimer->stop();
    m_heartbeatTimer->start(kHeartbeatMs);
}

void YoloDetector::onSocketError(QAbstractSocket::SocketError err) {
    Q_UNUSED(err)
    m_heartbeatTimer->stop();
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        emit statusMessage("YOLO 推理断开，3 秒后重连...");
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(kConnectRetryMs);
        }
    }
}

void YoloDetector::onNewFrame(const QImage &frame) {
    // 帧率限制
    if (m_fpsTimer.elapsed() < 1000.0 / m_fpsLimit) {
        return;
    }
    m_fpsTimer.restart();

    // 如果正在等待上一帧结果，跳过
    if (m_waitingResult) {
        return;
    }

    // 未连接时跳过
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    // 保存原始帧，降采样后发送
    m_lastFrame = frame;
    sendFrame(frame);
}

void YoloDetector::sendFrame(const QImage &frame) {
    // 降采样到 640x640
    QImage resized = frame.scaled(kTargetWidth, kTargetHeight,
                                  Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (resized.isNull()) {
        return;
    }

    // 转为 RGB888（确保格式正确）
    QImage rgb888 = resized.convertToFormat(QImage::Format_RGB888);
    if (rgb888.isNull()) {
        return;
    }

    m_waitingResult = true;
    m_lastResizedFrame = rgb888;

    // 构造二进制帧: [4B total_len][4B width][4B height][RGB data]
    int w = rgb888.width();
    int h = rgb888.height();
    int totalLen = w * h * 3;

    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(totalLen);
    stream << static_cast<quint32>(w);
    stream << static_cast<quint32>(h);

    // 发送 header + 像素数据
    m_socket->write(header);
    m_socket->write(reinterpret_cast<const char*>(rgb888.constBits()), totalLen);
    m_socket->flush();
}

void YoloDetector::onReadyRead() {
    m_recvBuffer.append(m_socket->readAll());

    // 尝试解析: 先读 4 字节长度前缀
    while (m_recvBuffer.size() >= 4) {
        // 检查是否是心跳响应（长度=4 的 "pong"）
        quint32 jsonLen = 0;
        memcpy(&jsonLen, m_recvBuffer.constData(), 4);

        if (jsonLen == 0) {
            // 心跳响应，跳过
            m_recvBuffer.remove(0, 4);
            continue;
        }

        // 等待完整的 JSON 数据
        if (m_recvBuffer.size() < 4 + static_cast<int>(jsonLen)) {
            return;  // 数据不完整，等待更多
        }

        // 提取 JSON
        QByteArray jsonData = m_recvBuffer.mid(4, jsonLen);
        m_recvBuffer.remove(0, 4 + jsonLen);

        m_waitingResult = false;
        parseDetectionJson(jsonData);
    }
}

void YoloDetector::parseDetectionJson(const QByteArray &json) {
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "[YOLO] JSON 解析失败";
        return;
    }

    QJsonObject obj = doc.object();
    QJsonArray dets = obj["detections"].toArray();
    double inferMs = obj["infer_ms"].toDouble();

    QVector<DetectionResult> results;
    float scaleX = static_cast<float>(m_lastFrame.width()) / kTargetWidth;
    float scaleY = static_cast<float>(m_lastFrame.height()) / kTargetHeight;

    for (const QJsonValue &val : dets) {
        QJsonObject det = val.toObject();
        DetectionResult r;
        r.className = det["class"].toString();
        r.score = det["score"].toDouble();

        QJsonArray bbox = det["bbox"].toArray();
        if (bbox.size() >= 4) {
            int x1 = static_cast<int>(bbox[0].toDouble() * scaleX);
            int y1 = static_cast<int>(bbox[1].toDouble() * scaleY);
            int x2 = static_cast<int>(bbox[2].toDouble() * scaleX);
            int y2 = static_cast<int>(bbox[3].toDouble() * scaleY);
            r.bbox = QRect(x1, y1, x2 - x1, y2 - y1);
        }

        results.append(r);
    }

    // 更新 FPS 统计
    m_frameCount++;
    // inferMs 来自 Python 端，这里用它估算检测 FPS

    qDebug() << "[YOLO] 检测到" << results.size() << "个目标，推理耗时" << inferMs << "ms";
    emit detectionReady(m_lastFrame, results);
}
```

- [ ] **Step 3: 验证 C++ 编译**

```bash
cd /home/lzy/文档/QTproject/test
# 语法检查（不链接，只编译）
g++ -std=c++17 -fsyntax-only -I/usr/include/x86_64-linux-gnu/qt5 -I/usr/include/x86_64-linux-gnu/qt5/QtWidgets -I/usr/include/x86_64-linux-gnu/qt5/QtNetwork -I/usr/include/x86_64-linux-gnu/qt5/QtCore yolodetector.cpp 2>&1 || echo "（检查语法错误，链接错误可忽略）"
```

Expected: 无语法错误（链接错误可忽略，因为没链接 Qt 库）

- [ ] **Step 4: Commit**

```bash
git add test/yolodetector.h test/yolodetector.cpp
git commit -m "feat: 添加 YoloDetector Qt 类（TCP 客户端 + 降采样 + 结果解析）"
```

---

### Task 4: SrtVideoStreamer 补充 emit

**Files:**
- Modify: `test/srtvideostreamer.cpp:92-110`

在帧循环中 emit `frameReadyForYolo` 信号，让 YoloDetector 能收到帧。

- [ ] **Step 1: 修改 srtvideostreamer.cpp，在切帧完成后 emit frameReadyForYolo**

找到 `srtvideostreamer.cpp` 第 100-109 行的帧处理代码：

```cpp
// 当前代码 (line ~100):
                QImage img((const uchar*)frameData.constData(),
                           m_resWidth, m_resHeight,
                           QImage::Format_RGB888);
                QImage copy = img.copy();
                if (!copy.isNull()) {
                    emit frameReady(copy);
                    m_frameCount++;
```

改为：

```cpp
                QImage img((const uchar*)frameData.constData(),
                           m_resWidth, m_resHeight,
                           QImage::Format_RGB888);
                QImage copy = img.copy();
                if (!copy.isNull()) {
                    emit frameReady(copy);
                    emit frameReadyForYolo(copy.constBits(),
                                           copy.width(), copy.height(),
                                           copy.bytesPerLine());
                    m_frameCount++;
```

- [ ] **Step 2: Commit**

```bash
git add test/srtvideostreamer.cpp
git commit -m "feat: SrtVideoStreamer emit frameReadyForYolo 信号"
```

---

### Task 5: TeaGardenSystem 集成

**Files:**
- Modify: `test/teagardensystem.h:31,161,197-202`
- Modify: `test/teagardensystem.cpp:92-105,1876-1888`

连接 YoloDetector、画框叠加、类名映射、PestStore 写入。

- [ ] **Step 1: 修改 teagardensystem.h — 添加头文件引用和成员变量**

在 `teagardensystem.h` 中：

1. 添加前向声明（line 31 附近，`class SrtVideoStreamer;` 旁边）：

```cpp
class SrtVideoStreamer;
class YoloDetector;
```

2. 添加 `onYoloResults` slot 声明（line 69 附近，`onVideoStatus` 旁边）：

```cpp
    void onVideoFrame(const QImage &image);
    void onVideoStatus(const QString &msg);
    void onYoloResults(const QImage &frame, const QVector<DetectionResult> &results);
```

3. 添加 `#include <QVector>` 到头部 include 区域（如果还没有的话）

4. 添加成员变量（line 161 附近，`m_videoStreamer` 旁边）：

```cpp
    SrtVideoStreamer *m_videoStreamer = nullptr;
    YoloDetector *m_yoloDetector = nullptr;
```

- [ ] **Step 2: 修改 teagardensystem.cpp — 构造函数初始化 YoloDetector**

在 `teagardensystem.cpp` 构造函数中，找到 `m_videoStreamer->start();`（line 105）之后，添加：

```cpp
    m_videoStreamer->start();

    // YOLO 病虫害检测（Socket 连接 Python 推理服务）
    m_yoloDetector = new YoloDetector(this);
    m_yoloDetector->setFpsLimit(3);
    connect(m_videoStreamer, &SrtVideoStreamer::frameReadyForYolo,
            m_yoloDetector, &YoloDetector::onNewFrame);
    connect(m_yoloDetector, &YoloDetector::detectionReady,
            this, &TeaGardenSystem::onYoloResults);
    connect(m_yoloDetector, &YoloDetector::statusMessage,
            this, &TeaGardenSystem::onVideoStatus);
    int yoloPort = qEnvironmentVariableIntValue("TEA_YOLO_PORT", nullptr, 9800);
    m_yoloDetector->start("127.0.0.1", yoloPort);
```

- [ ] **Step 3: 修改 teagardensystem.cpp — 添加 onYoloResults 实现**

在 `teagardensystem.cpp` 的 `onVideoStatus` 函数之后（line ~1888），添加：

```cpp
// ── YOLO 检测结果处理 ──

static QColor colorForYoloClass(const QString &cls) {
    if (cls == "Algal Leaf Spot")  return QColor("#2196F3");
    if (cls == "Brown Blight")     return QColor("#F44336");
    if (cls == "Gray Blight")      return QColor("#9E9E9E");
    if (cls == "Healthy")          return QColor("#4CAF50");
    if (cls == "Helopeltis")       return QColor("#FF9800");
    if (cls == "Red Leaf Spot")    return QColor("#E91E63");
    return QColor("#FFFFFF");
}

static QString mapYoloClassToChinese(const QString &cls) {
    if (cls == "Algal Leaf Spot")  return "茶藻斑病";
    if (cls == "Brown Blight")     return "茶炭疽病";
    if (cls == "Gray Blight")      return "茶灰斑病";
    if (cls == "Helopeltis")       return "茶小绿叶蝉";
    if (cls == "Red Leaf Spot")    return "茶红叶斑";
    return cls;
}

static int scoreToSeverity(double score) {
    if (score >= 0.75) return 3;  // 严重
    if (score >= 0.50) return 2;  // 中等
    return 1;                      // 轻微
}

void TeaGardenSystem::onYoloResults(const QImage &frame,
                                     const QVector<DetectionResult> &results) {
    if (!videoLabel || frame.isNull()) return;

    // 1. 画框叠加
    QImage overlay = frame.copy();
    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const DetectionResult &det : results) {
        QColor color = colorForYoloClass(det.className);
        painter.setPen(QPen(color, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(det.bbox);

        // 标签背景
        QString label = QString("%1 %2%").arg(det.className).arg(det.score * 100, 0, 'f', 0);
        QFont font = painter.font();
        font.setPixelSize(12);
        painter.setFont(font);
        QFontMetrics fm(font);
        QRect textRect = fm.boundingRect(label);
        QRect bgRect(det.bbox.topLeft() - QPoint(0, textRect.height() + 4),
                     QSize(textRect.width() + 8, textRect.height() + 4));
        painter.fillRect(bgRect, color);
        painter.setPen(Qt::white);
        painter.drawText(bgRect, Qt::AlignCenter, label);
    }
    painter.end();

    // 2. 显示到 UI
    const int w = qMax(videoLabel->width(), 280);
    const int h = w * kDroneVideoHeight / kDroneVideoWidth;
    const QPixmap pix = QPixmap::fromImage(overlay);
    videoLabel->setPixmap(pix.scaled(QSize(w, h), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    videoLabel->setText(QString());

    // 3. 写入 PestStore（非 Healthy 类别）
    for (const DetectionResult &det : results) {
        if (det.className == "Healthy") continue;

        PestRecord rec;
        rec.timestamp = QDateTime::currentDateTime();
        rec.pestType = mapYoloClassToChinese(det.className);
        rec.lat = 25.08;   // TODO: 从无人机 GPS 获取
        rec.lng = 118.232;
        rec.severity = scoreToSeverity(det.score);
        PestStore::instance()->append(rec);
    }

    // 4. 刷新 UI
    if (!results.isEmpty()) {
        injectPestMarkers();
        refreshPestPie();
        if (pestDashboard && pestDashboard->isVisible()) {
            pestDashboard->refresh();
        }
    }
}
```

- [ ] **Step 4: 确保 teagardensystem.cpp 顶部包含所需头文件**

检查 `teagardensystem.cpp` 顶部，确保有以下 include（可能已有部分）：

```cpp
#include "yolodetector.h"
#include <QPainter>
#include <QPen>
#include <QFontMetrics>
```

- [ ] **Step 5: Commit**

```bash
git add test/teagardensystem.h test/teagardensystem.cpp
git commit -m "feat: TeaGardenSystem 集成 YOLO 检测（画框叠加 + PestStore 写入）"
```

---

### Task 6: 更新 test.pro

**Files:**
- Modify: `test/test.pro:20-47`

- [ ] **Step 1: 在 test.pro 的 SOURCES 和 HEADERS 中添加 yolodetector**

在 `test.pro` 中，找到 SOURCES 列表（line 20-33），添加 `yolodetector.cpp`：

```
SOURCES += \
    chartdashboard.cpp \
    datastore.cpp \
    iotuploader.cpp \
    loramanager.cpp \
    main.cpp \
    pestdashboard.cpp \
    pestdata.cpp \
    scheduledialog.cpp \
    schedulestore.cpp \
    srtvideostreamer.cpp \
    teagardensystem.cpp \
    thresholddialog.cpp \
    widget.cpp \
    yolodetector.cpp
```

找到 HEADERS 列表（line 35-47），添加 `yolodetector.h`：

```
HEADERS += \
    chartdashboard.h \
    datastore.h \
    iotuploader.h \
    loramanager.h \
    pestdashboard.h \
    pestdata.h \
    scheduledialog.h \
    schedulestore.h \
    srtvideostreamer.h \
    teagardensystem.h \
    thresholddialog.h \
    widget.h \
    yolodetector.h
```

- [ ] **Step 2: 验证 .pro 文件语法**

```bash
cd /home/lzy/文档/QTproject/test
grep -c "yolodetector" test.pro
```

Expected: 输出 `2`（SOURCES 和 HEADERS 各一次）

- [ ] **Step 3: Commit**

```bash
git add test/test.pro
git commit -m "build: test.pro 添加 yolodetector 源文件"
```

---

### Task 7: 端到端集成验证

**Files:**
- 验证所有文件已正确修改

- [ ] **Step 1: 在 ELF2 上启动 Python 推理服务**

```bash
# SSH 到 ELF2
cd ~/sdcard
chmod +x start_yolo_server.sh
./start_yolo_server.sh
```

Expected: 输出 `YOLO 推理服务已启动，监听端口 9800` 和 `模型加载完成，准备就绪`

- [ ] **Step 2: 编译 Qt 项目**

```bash
cd /home/lzy/文档/QTproject/test
qmake test.pro
make -j$(nproc)
```

Expected: 编译成功，无错误

- [ ] **Step 3: 运行程序验证**

```bash
./test
```

Expected:
1. 视频流正常显示
2. 控制台输出 `[YOLO] 已连接到推理服务`
3. 有检测结果时视频上出现彩色边框和标签
4. PestDashboard 中出现真实检测记录

- [ ] **Step 4: 最终 Commit（如有遗漏修复）**

```bash
git add -A
git commit -m "feat: YOLO RKNN 病虫害检测集成完成"
```
