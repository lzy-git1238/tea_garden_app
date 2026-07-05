# YOLOv5 → YOLOv8 迁移设计

## 目标

将茶园病虫害检测系统从 YOLOv5 + Python RKNN TCP 服务 替换为 YOLOv8 C++ 直连引擎，消除图像变形和网络回环开销，提升检测精度和可靠性。

## 当前问题

1. **图像变形**：1280×720 视频被 `Qt::IgnoreAspectRatio` 强制拉伸到 640×640，不保持宽高比，导致目标失真、漏检
2. **不必要的 TCP 回环**：Qt 和推理服务都在 ELF2 同一块板子上，却通过 localhost TCP 通信，增加延迟和序列化开销
3. **Python 开销**：Python 推理服务增加进程间通信和解释器延迟
4. **YOLOv5 后处理不兼容**：新的 `tealeaf.rknn` 是 YOLOv8 anchor-free + DFL 格式，Python 端的 YOLOv5 anchor-based 解码无法使用
5. **无 NMS**：原 Python 端移除了 NMS，导致同一目标重复检出

## 架构变更

```
之前:  Qt UI → TCP localhost:9800 → Python YOLOv5 服务
之后:  Qt UI → YoloEngine::infer() (同进程直接调用)
```

### 删除
- `rknn_yolo_server.py` Python 推理服务
- `YoloDetector` 中所有 TCP socket、心跳、重连、JSON 序列化代码
- `Qt::IgnoreAspectRatio` 强制拉伸

### 新增
- `YoloEngine` 实例嵌入 YoloDetector
- 推理工作线程（拉模式，参照 yolo_deploy/src/yolo_node.cpp）
- CV 互斥锁 + 条件变量保护最新帧

## 线程模型

```
Qt 主线程                    推理工作线程
────────                    ──────────
onNewFrame(frame)
  ├─ 帧率检查 (3 FPS)
  ├─ lock → m_latestFrame = frame
  └─ notify ──────────────→ wait_for(100ms)
                                   ├─ frame = m_latestFrame.clone()
                                   ├─ QImage → cv::Mat (RGB→BGR)
                                   ├─ engine.infer(frame, dets)
                                   │    ├─ letterbox 640×640 (保持宽高比)
                                   │    ├─ RKNN NPU 推理
                                   │    └─ DFL + NMS 后处理
                                   ├─ dets → QVector<DetectionResult>
                                   └─ emit detectionReady(frame, results)
```

拉模式：推理跟不上帧率时自动取最新帧，丢旧帧，延迟不累积。

## 前处理：Letterbox

YoloEngine 已有正确实现（`yolo_engine.cpp:155-183`），无需修改：

- 计算 scale = min(640/1280, 640/720) = 0.5，保持宽高比
- 缩放后有效区域 640×360，上下各填充 140px 灰边(114,114,114)
- 后处理自动做 pad 逆映射 + scale 逆映射，坐标还原回原图 1280×720

## 文件改动清单

### 修改（5 个文件）

| 文件 | 改动 |
|------|------|
| `test/test.pro` | 添加 yolo_engine.cpp、postprocess.cpp 到 SOURCES；链接 librknnrt.so、OpenCV |
| `yolodetector.h` | 删除 TCP 成员(m_socket, m_reconnectTimer, m_heartbeatTimer, m_recvBuffer 等)；新增 YoloEngine、std::thread、mutex、condition_variable、帧缓存 |
| `yolodetector.cpp` | 删除 onConnected/onReadyRead/onSocketError/sendFrame/parseDetectionJson/connectToServer (~150 行)；新实现 initEngine/startInferThread/inferLoop (~60 行) |
| `postprocess.h` | 确认 OBJ_CLASS_NUM=6 与模型类别数一致 |
| `model/labels_list.txt` | 确认 6 个类别名与训练顺序一致 |

### 不修改

- `yolo_engine.hpp/cpp` — 引擎层，无需改动
- `postprocess.cpp` — 后处理，无需改动
- `teagardensystem.cpp` — 显示/入库，走 detectionReady 信号，接口不变

### 新增到项目

- `include/slam_car_yolo/yolo_engine.hpp`
- `include/slam_car_yolo/postprocess.h`
- `include/slam_car_yolo/rknn_api.h`
- `include/slam_car_yolo/rknn_matmul_api.h`
- `src/yolo_engine.cpp`
- `src/postprocess.cpp`
- `lib/librknnrt.so`

## 模型兼容性

用户的 `tealeaf.rknn` 按 [yolov8-helmet-rk3588-multithread](https://github.com/JA-cmd-wq/yolov8-helmet-rk3588-multithread) 教程训练，关键特征：

- ReLU 激活（替代 SiLU，确保 NPU 全算子运行）
- Anchor-free + DFL（DFL_LEN=16）
- 9 个 INT8 输出 tensor：3 stride × {box[1,64,H,W], score[1,C,H,W], score_sum[1,1,H,W]}
- strides = {8, 16, 32}

yolo_deploy 的 `postprocess.cpp` 完全匹配此输出格式。

## 验证检查清单

- [ ] Qt 项目编译通过，链接 librknnrt.so 无符号缺失
- [ ] OBJ_CLASS_NUM=6 与 `tealeaf.rknn` 输出类别数一致
- [ ] labels_list.txt 类别顺序与训练时一致（Algal Leaf Spot, Brown Blight, Gray Blight, Healthy, Helopeltis, Red Leaf Spot）
- [ ] 1280×720 SRT 视频流推理无变形，检出框位置正确
- [ ] detectionReady 信号正常触发，TeaGardenSystem 画框、入库、饼图刷新正常工作
- [ ] 3 FPS 限制有效，不丢关键帧
- [ ] 推理延迟 < 100ms（单核 NPU）
