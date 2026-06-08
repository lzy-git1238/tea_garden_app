import os
import cv2
import time
import numpy as np
import subprocess
from rknnlite.api import RKNNLite

# ================= 参数配置 =================
RKNN_MODEL = '/home/elf/sdcard/models/yolov5s_last.rknn'

IMG_SIZE = 640
CONF_THRES = 0.55  # 提高置信度阈值，过滤杂波
IOU_THRES = 0.45

VIDEO_W = 640
VIDEO_H = 480

# 完美匹配你的 6 类别模型结构
CLASS_NAMES = ["person", "car", "class_2", "class_3", "class_4", "class_5"]
num_classes = len(CLASS_NAMES)

# YOLOv5 官方标准 Anchor
ANCHORS = [
    [[10, 13], [16, 30], [33, 23]],      # 80x80 层
    [[30, 61], [62, 45], [59, 119]],     # 40x40 层
    [[116, 90], [156, 198], [373, 326]]  # 20x20 层
]

def sigmoid(x):
    return 1 / (1 + np.exp(-x))

# ================= 矩阵向量化高效解码 =================
def decode_yolo_vectorized(outputs):
    """摒弃纯 Python 循环，利用 NumPy 矩阵并行解算 1.9 万个网格点"""
    all_boxes = []
    all_confs = []
    all_class_ids = []

    # 按照特征图分辨率从大到小排序 (80x80 -> 40x40 -> 20x20)
    outputs.sort(key=lambda x: x.shape[2] if len(x.shape)==4 else x.shape[1], reverse=True)

    for i, out in enumerate(outputs):
        # 确保形状是 (1, 33, H, W) 或类似，将其规范化为 (3, 11, H, W)
        if len(out.shape) == 4:
            _, channels, grid_h, grid_w = out.shape
            out = out.reshape(3, 5 + num_classes, grid_h, grid_w)
        else:
            continue

        stride = IMG_SIZE / grid_h
        anchors = np.array(ANCHORS[i], dtype=np.float32)

        # 1. 提取各个通道的数据
        box_xy = sigmoid(out[:, 0:2, :, :]) * 2 - 0.5
        box_wh = (sigmoid(out[:, 2:4, :, :]) * 2) ** 2
        box_conf = sigmoid(out[:, 4:5, :, :])
        cls_scores = sigmoid(out[:, 5:, :, :])

        # 2. 计算最终的目标置信度
        cls_max_scores = np.max(cls_scores, axis=1, keepdims=True)
        cls_ids = np.argmax(cls_scores, axis=1, keepdims=True)
        final_scores = box_conf * cls_max_scores

        # 3. 通过布尔掩码直接筛选高分网格（瞬间过滤掉 99% 的无效背景）
        mask = final_scores > CONF_THRES
        mask_indices = np.where(mask)

        if len(mask_indices[0]) == 0:
            continue

        # 4. 只针对筛选出来的有效目标进行坐标计算
        a_idx, _, y_idx, x_idx = mask_indices

        # 生成网格矩阵恢复物理中心点
        pred_x = (box_xy[a_idx, 0, y_idx, x_idx] + x_idx) * stride
        pred_y = (box_xy[a_idx, 1, y_idx, x_idx] + y_idx) * stride

        # 恢复物理宽高
        anchor_w = anchors[a_idx, 0]
        anchor_h = anchors[a_idx, 1]
        pred_w = box_wh[a_idx, 0, y_idx, x_idx] * anchor_w
        pred_h = box_wh[a_idx, 1, y_idx, x_idx] * anchor_h

        # 转换到左上角和右下角坐标
        x1 = pred_x - pred_w / 2
        y1 = pred_y - pred_h / 2
        w_box = pred_w
        h_box = pred_h

        scores = final_scores[mask].tolist()
        ids = cls_ids[mask_indices[0], 0, mask_indices[2], mask_indices[3]].tolist()

        for k in range(len(scores)):
            all_boxes.append([int(x1[k]), int(y1[k]), int(w_box[k]), int(h_box[k])])
            all_confs.append(float(scores[k]))
            all_class_ids.append(int(ids[k]))

    # 利用 OpenCV 自带的、由 C++ 编写的 NMS 抑制，效率极高
    indices = cv2.dnn.NMSBoxes(all_boxes, all_confs, CONF_THRES, IOU_THRES)
    
    clean_boxes = []
    if len(indices) > 0:
        for idx in indices.flatten():
            clean_boxes.append((all_boxes[idx], all_confs[idx], all_class_ids[idx]))
    return clean_boxes

# ================= 主程序 =================
def main():
    rknn = RKNNLite()
    print("Loading RKNN model...")
    if rknn.load_rknn(RKNN_MODEL) != 0:
        print("Model load failed!"); return
    rknn.init_runtime()

    # 完美的阻塞低延迟单线程管道
    ffmpeg_cmd = [
        'ffmpeg',
        '-threads', '1',
        '-vcodec', 'h264_rkmpp',                      # 强制使用硬解
        '-f', 'mpegts',
        '-fflags', 'nobuffer',
        '-flags', 'low_delay',
        '-analyzeduration', '0',
        '-probesize', '32',
        '-i', 'srt://0.0.0.0:9000?mode=listener',
        '-f', 'rawvideo',
        '-pix_fmt', 'bgr24',
        '-'
    ]

    pipe = subprocess.Popen(ffmpeg_cmd, stdout=subprocess.PIPE, bufsize=VIDEO_W * VIDEO_H * 3 * 2)
    frame_size = VIDEO_W * VIDEO_H * 3

    last = time.time()
    fps_count = 0
    fps = 0

    print("📡 流媒体就绪，采用【高效时间连续无丢帧算法】开始检测...")

    while True:
        t_cycle_start = time.time()
        raw = pipe.stdout.read(frame_size)
        if len(raw) != frame_size:
            continue

        # 加上 .copy() 解锁内存，允许 OpenCV 绘图
        frame = np.frombuffer(raw, np.uint8).reshape((VIDEO_H, VIDEO_W, 3)).copy()

        # ================= 高效预处理 =================
        # 移除了重复多余的 /255.0 浮点转换，直接将 BGR 矩阵调整大小送给 NPU，让 NPU 内部处理硬件转换
        img = cv2.resize(frame, (IMG_SIZE, IMG_SIZE))
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img_input = np.expand_dims(img, 0)

        # ================= NPU 推理 =================
        outputs = rknn.inference(inputs=[img_input])

        # ================= 向量化高速解码 =================
        boxes = decode_yolo_vectorized(outputs)

        # ================= 实时绘制 =================
        for b in boxes:
            (x, y, w, h), conf, cls_id = b

            # 缩放映射回原始的 640x480 画面
            x1 = max(0, int(x * VIDEO_W / IMG_SIZE))
            y1 = max(0, int(y * VIDEO_H / IMG_SIZE))
            x2 = min(VIDEO_W, int((x + w) * VIDEO_W / IMG_SIZE))
            y2 = min(VIDEO_H, int((y + h) * VIDEO_H / IMG_SIZE))

            # 仅显示你需要的 person 和 car 前两个分类，防止噪声扰动
            if cls_id < 2:
                cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 0, 255), 2)
                cv2.putText(frame, f"{CLASS_NAMES[cls_id]} {conf:.2f}", (x1, y1 - 7),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 2)

        # ================= FPS 计算与显示 =================
        fps_count += 1
        if time.time() - last >= 1:
            fps = fps_count
            fps_count = 0
            last = time.time()

        cv2.putText(frame, f"FPS: {fps} (No-Drop Smooth)", (20, 40),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)

        cv2.imshow("RK3588 YOLOv5 RKNN", frame)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    pipe.terminate()
    rknn.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()