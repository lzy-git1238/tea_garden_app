#!/usr/bin/env python3
"""
YOLO RKNN 推理服务
通过 Socket 接收 Qt 端发送的 RGB 帧，执行 RKNN YOLO 推理，返回检测结果。

协议：
  Qt → Python: [4B total_len][4B width][4B height][RGB24 pixel data]
  Python → Qt: [4B json_len][JSON UTF-8]

心跳：Qt 发送 total_len=0 的包，Python 回复 pong

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
    """[x, y, w, h] → [x1, y1, x2, y2]"""
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

        print("[INFO] 初始化 RKNN 运行时 (3xNPU 核心)...")
        ret = self.rknn.init_runtime(core_mask=3)  # NPU_CORE_0_1_2 = 0b111 = 3
        if ret != 0:
            print("[ERROR] 初始化运行时失败!")
            sys.exit(1)
        print("[INFO] 模型加载完成，准备就绪")

    def infer(self, img_rgb):
        """
        对 RGB 图像执行 YOLO 推理。
        img_rgb: numpy array, shape (H, W, 3), dtype uint8, RGB 顺序
        返回: (detections_list, infer_ms)
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

                # 心跳包: total_len=0, width=0, height=0
                if total_len == 0:
                    conn.sendall(struct.pack('<I', 0))  # pong
                    continue

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
