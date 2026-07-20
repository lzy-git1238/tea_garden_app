# YOLOv8 推理引擎替换：集成 rknnPool 多线程推理

## 现状

当前 `YoloEngine` + `YoloDetector` 的 RKNN 推理在 ELF2 上 `init()` 失败（Cannot open model），YOLO 检测不工作。

EL2 上有已验证的程序 `~/yolov8_test/yolov8-helmet-rk3588-multithread-main/`，使用 `rknnPool<rkYolov5s>` 多线程推理架构，命令行 `./rknn_yolov5_demo ./model/best.rknn teavideo.mp4 output.mp4` 能正确检测并画框。

## 方案：用 rknnPool 替换 YoloEngine/YoloDetector

### 架构

```
ffmpeg 解码线程 → m_latestFrame (共享缓存)
                      │
    ┌─────────────────┴──────────────────┐
    │                                    │
    ▼                                    ▼
QTimer 33ms                            QTimer 333ms
latestFrame()                          latestFrame() → cv::Mat
    │                                      │
    ▼                                      ▼
onVideoFrame()                         rknnPool.put() → 12线程推理
直接显示原始画面                            │
(流畅 30fps，无框)                         ▼
                                      rknnPool.get() → cv::Mat（已画框）
                                          │
                                          ▼
                                     onYoloResults()
                                     ├─ cv::Mat → QPixmap → 显示（有框）
                                     └─ 提取检测结果 → PestStore + 地图标记
```

### 推理链路

1. Qt 333ms Timer 从 `m_latestFrame` 取一帧
2. `QImage` 转 `cv::Mat`（BGR）
3. `rknnPool.put(mat)` 提交到线程池
4. `rknnPool.get(mat)` 取回推理结果——**cv::Mat 已由推理线程 OpenCV 画好框**
5. 同时通过出参获取 `std::vector<Detection>` 用于写库
6. `cv::Mat` → `QImage` → `QPixmap` → `setPixmap` 更新显示
7. 提取虫害信息写入 `PestStore`

### 性能

- 推理在独立线程池（默认 12 线程），不阻塞主线程也不阻塞 ffmpeg 解码
- 显示走 33ms Timer 原始帧，大部分时间无框但流畅
- 每隔 ~333ms 刷新一帧带检测框的画面
- NPU 多核并行（12 实例共享权重），推理吞吐高

## 改动

### rkYolov5s 接口微调

`rkYolov5s::infer()` 当前返回画好框的 `cv::Mat`，但内部创建的 `detect_result_group_t` 被丢弃。需要加一个可选出参：

```cpp
// 新增重载：同时导出检测结果
cv::Mat infer(cv::Mat &ori_img, detect_result_group_t *out_results = nullptr);
```

### 替换清单

| 文件 | 动作 |
|------|------|
| `test/yolo_engine.hpp` | 删除 |
| `test/yolo_engine.cpp` | 删除 |
| `test/yolodetector.h` | 删除（Qt 层推理不再需要独立类） |
| `test/yolodetector.cpp` | 删除 |
| `test/postprocess.h` | 删除（使用 yolov8_test 的版本） |
| `test/postprocess.cpp` | 删除 |
| `test/rknn_api.h` | 替换为 yolov8_test 版本 |
| `include/rkYolov5s.hpp` | 新建（从 yolov8_test 复制） |
| `include/rknnPool.hpp` | 新建（从 yolov8_test 复制） |
| `include/ThreadPool.hpp` | 新建（从 yolov8_test 复制） |
| `include/postprocess.h` | 新建（从 yolov8_test 复制，OBJ_CLASS_NUM 和 LABEL_NALE_TXT_PATH 需调整） |
| `include/preprocess.h` | 新建（从 yolov8_test 复制） |
| `include/coreNum.hpp` | 新建（从 yolov8_test 复制） |
| `include/rga_func.h` | 新建（从 yolov8_test 复制） |
| `include/drm_func.h` | 新建（从 yolov8_test 复制） |
| `include/rknn_matmul_api.h` | 新建（从 yolov8_test 复制） |
| `include/librknnrt.so` | 新建（从 yolov8_test 复制） |
| `include/librknn_api.so` | 新建（从 yolov8_test 复制） |
| `src/rkYolov5s.cc` | 新建（从 yolov8_test 复制，加出参） |
| `src/postprocess.cc` | 新建（从 yolov8_test 复制） |
| `src/preprocess.cc` | 新建（从 yolov8_test 复制） |
| `test/teagardensystem.h` | 修改：删除 YoloDetector 成员和引用，加 rknnPool 成员 |
| `test/teagardensystem.cpp` | 修改：替换 YOLO 初始化 + 推理逻辑 |
| `test/test.pro` | 修改：更新源文件列表和 include 路径 |
| `test/model/best.rknn` | 新建（从 yolov8_test 复制或软链接） |
| `test/model/labels_list.txt` | 新建（从 yolov8_test 复制） |

### teagardensystem 端集成核心代码

```cpp
// 成员变量（teagardensystem.h）
#include "rknnPool.hpp"
#include "rkYolov5s.hpp"
std::unique_ptr<rknnPool<rkYolov5s, cv::Mat, cv::Mat>> m_yoloPool;

// 初始化（替换现有的 m_yoloDetector 相关代码）
m_yoloPool = std::make_unique<rknnPool<rkYolov5s, cv::Mat, cv::Mat>>(
    "/home/elf/QTproject/test/model/best.rknn", 12);
m_yoloPool->init();

// YOLO Timer 333ms 拉帧推理
m_yoloPullTimer = new QTimer(this);
m_yoloPullTimer->setInterval(333);
connect(m_yoloPullTimer, &QTimer::timeout, this, [this]() {
    QImage frame = m_videoStreamer->latestFrame();
    if (frame.isNull()) return;
    cv::Mat mat(frame.height(), frame.width(), CV_8UC3,
                const_cast<uchar*>(frame.bits()), frame.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
    m_yoloPool->put(bgr);
    cv::Mat result;
    if (m_yoloPool->get(result) != 0) return;
    // result 已画好框，转为 QImage 显示
    cv::cvtColor(result, result, cv::COLOR_BGR2RGB);
    QImage qimg(result.data, result.cols, result.rows, result.step,
                QImage::Format_RGB888);
    onVideoFrame(qimg.copy());  // 显示带框画面
    // 提取检测结果写 PestStore ...（通过 rkYolov5s 的出参）
});
```

## 测试要点

- EL2 编译通过（新增的 .cc 文件需 ARM64 编译）
- 无人机 SRT 流检测正常出框
- 虫害识别结果写入 PestStore + 地图标记
- 显示流畅 30fps，YOLO 结果 ~3fps 叠加
- 视频延迟不再累积（拉取模式不变）
