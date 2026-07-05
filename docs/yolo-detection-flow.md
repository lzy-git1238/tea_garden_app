# 无人机视频 YOLO 识别流程

## 整体架构

```
无人机 → SRT 推流 → Qt 桌面/ARM 端接收
                         ├── videoLabel 显示
                         └── YoloDetector → Python RKNN 推理服务
                                  ↓
                            ELF2 RK3588 NPU 推理
                                  ↓
                            videoLabel 画框叠加
```

## 详细流程

### 1. 视频流接收

- `SrtVideoStreamer` 通过 SRT 协议拉取无人机视频流
- 接收到的原始帧通过 `frameReadyForYolo` 信号发送给 `YoloDetector`
- 同时原始帧也显示在 `videoLabel` 上（供用户观看）

### 2. 帧发送给推理服务

**YoloDetector::onNewFrame()** (`yolodetector.cpp:87-107`)
- 帧率限制：默认 3 FPS（可通过 `setFpsLimit()` 调节 1-30）
- 上一帧推理未完成时跳过（`m_waitingResult` 保护）
- 未连接推理服务时跳过

**YoloDetector::sendFrame()** (`yolodetector.cpp:109-141`)
- 原始帧强制拉伸到 640×640（`Qt::IgnoreAspectRatio`）
- 转 RGB888 格式
- 构造二进制帧头：`[4B total_len][4B width][4B height]` + RGB 像素数据
- 通过 TCP Socket 发送到 Python 推理服务（默认 `127.0.0.1:9800`）

### 3. Python RKNN 推理服务

**rknn_yolo_server.py** (部署在 ELF2 `~/sdcard/` 目录)

通信协议：
```
Qt → Python: [4B total_len][4B width][4B height][RGB24 pixel data]
Python → Qt: [4B json_len][JSON UTF-8]
心跳：Qt 发送 total_len=0 的 12 字节零包头，Python 回复 {"status": "pong"}
```

推理流程 (`infer()` 方法)：
1. 接收 RGB 裸数据，reshape 为 numpy 数组
2. `cv2.resize` 到 640×640（冗余保护）
3. 送入 RKNN NPU 推理 (`rknn.inference()`，指定 `data_format=['nhwc']`)
4. YOLOv5 后处理：
   - 3 个输出头，9 个 anchor（标准 YOLOv5 配置）
   - 物体置信度阈值：0.25
   - 已移除 NMS（保留所有通过阈值的目标框）
   - `xywh2xyxy`：中心点+宽高 → 左上角+右下角
5. 返回坐标还原：
   - 根据原始帧尺寸计算缩放比例，将 640×640 空间坐标映射回原图
   - 坐标 clamp 到 `[0, orig_w-1]` / `[0, orig_h-1]`

识别类别（6 类）：
| 英文名 | 中文名 |
|--------|--------|
| Algal Leaf Spot | 茶藻斑病 |
| Brown Blight | 茶炭疽病 |
| Gray Blight | 茶灰斑病 |
| Healthy | 健康 |
| Helopeltis | 茶小绿叶蝉 |
| Red Leaf Spot | 茶红叶斑 |

### 4. 结果回传与解析

**YoloDetector::onReadyRead()** (`yolodetector.cpp:144-169`)
- 累积接收数据到 `m_recvBuffer`
- 按 4 字节长度前缀协议解析 JSON
- 心跳响应识别：JSON 含 `"status"` 字段且不含 `"detections"` → 跳过，不清除 `m_waitingResult`

**YoloDetector::parseDetectionJson()** (`yolodetector.cpp:172-211`)
- 解析 JSON：`{"detections": [...], "infer_ms": 12.3}`
- 每个检测结果坐标已在 Python 端完成原图映射
- 发出 `detectionReady(frame, results)` 信号

### 5. 显示与记录

**TeaGardenSystem::onYoloResults()** (`teagardensystem.cpp:2095-2143`)
1. 在原帧上 QPainter 画框 + 类别标签 + 置信度百分比
2. 按 1920×1080 比例缩放到 `videoLabel`
3. 写入 `PestStore`（非 Healthy 类别）：
   - 时间戳、虫害类型（中文映射）、经纬度（默认 25.08, 118.232）
   - 严重程度（根据置信度分数）
4. 刷新病虫害 UI：饼图 + 地图标记 + 仪表盘

## 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 推理服务地址 | 127.0.0.1:9800 | 环境变量 `TEA_YOLO_PORT` |
| 帧率限制 | 3 FPS | `setFpsLimit()` |
| 输入分辨率 | 640×640 | 不保持宽高比 |
| 物体置信度阈值 | 0.25 | Python 端 `OBJ_THRESH` |
| 推理核心 | NPU_CORE_0_1_2 | 3 核 |
| 心跳间隔 | 5 秒 | `kHeartbeatMs` |
| 重连间隔 | 3 秒 | `kConnectRetryMs` |

## Qt 端心跳适配

Python 端心跳返回 `{"status": "pong"}` 而非原始 4 字节零。Qt 端 `onReadyRead()` 通过检查 JSON 是否含 `"status"` 且不含 `"detections"` 来识别心跳响应并跳过，避免误清除 `m_waitingResult` 导致帧同步错乱。
