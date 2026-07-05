# YOLOv5 → YOLOv8 迁移实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将茶园病虫害检测从 YOLOv5+Python+TCP 替换为 YOLOv8 C++ YoloEngine 直连，消除图像变形和网络回环。

**Architecture:** 从 yolo_deploy 复制 YoloEngine（letterbox 前处理 + DFL/NMS 后处理）到 QTproject，嵌入 YoloDetector 替代 TCP socket 逻辑。推理跑在独立工作线程，拉模式取帧。Qt 端 teagardensystem.cpp 仅改一行初始化参数。

**Tech Stack:** Qt5/6 (qmake), C++17, OpenCV 4.x, RKNN Runtime (librknnrt.so), RK3588 NPU

---

## 文件结构

```
QTproject/test/
├── test.pro                    ← 修改：加源文件 + lib 链接
├── yolodetector.h              ← 重写：删 TCP，加 YoloEngine + 线程
├── yolodetector.cpp            ← 重写：删 TCP/JSON 逻辑 (~150行)，新推理循环 (~70行)
├── yolo_engine.hpp             ← 新增，从 yolo_deploy 复制，改 include 路径
├── yolo_engine.cpp             ← 新增，从 yolo_deploy 复制，改 include 路径
├── postprocess.h               ← 新增，从 yolo_deploy 复制，验证 OBJ_CLASS_NUM
├── postprocess.cpp             ← 新增，从 yolo_deploy 复制，改 include 路径
├── rknn_api.h                  ← 新增，从 yolo_deploy 复制
├── rknn_matmul_api.h           ← 新增，从 yolo_deploy 复制
├── labels_list.txt             ← 新增/验证，与模型类别顺序一致
├── teagardensystem.cpp         ← 修改：start(host,port) → init(model,labels)
└── ../lib/librknnrt.so         ← 新增，从 yolo_deploy 复制
```

---

### Task 1: 复制引擎文件到 QTproject 并修复 include 路径

**Files:**
- Create: `test/yolo_engine.hpp` (copy from `yolo_deploy/include/slam_car_yolo/yolo_engine.hpp`)
- Create: `test/yolo_engine.cpp` (copy from `yolo_deploy/src/yolo_engine.cpp`)
- Create: `test/postprocess.h` (copy from `yolo_deploy/include/slam_car_yolo/postprocess.h`)
- Create: `test/postprocess.cpp` (copy from `yolo_deploy/src/postprocess.cpp`)
- Create: `test/rknn_api.h` (copy from `yolo_deploy/include/slam_car_yolo/rknn_api.h`)
- Create: `test/rknn_matmul_api.h` (copy from `yolo_deploy/include/slam_car_yolo/rknn_matmul_api.h`)
- Create: `lib/librknnrt.so` (copy from `yolo_deploy/lib/librknnrt.so`)

- [ ] **Step 1: 复制头文件和源文件**

```bash
# 从 yolo_deploy 复制头文件到 QTproject/test/
cp /home/lzy/文档/yolo_deploy/include/slam_car_yolo/rknn_api.h \
   /home/lzy/文档/yolo_deploy/include/slam_car_yolo/rknn_matmul_api.h \
   /home/lzy/文档/yolo_deploy/include/slam_car_yolo/postprocess.h \
   /home/lzy/文档/yolo_deploy/include/slam_car_yolo/yolo_engine.hpp \
   /home/lzy/文档/QTproject/test/

# 从 yolo_deploy 复制源文件到 QTproject/test/
cp /home/lzy/文档/yolo_deploy/src/postprocess.cpp \
   /home/lzy/文档/yolo_deploy/src/yolo_engine.cpp \
   /home/lzy/文档/QTproject/test/

# 复制 librknnrt.so 到 QTproject/lib/
mkdir -p /home/lzy/文档/QTproject/lib
cp /home/lzy/文档/yolo_deploy/lib/librknnrt.so /home/lzy/文档/QTproject/lib/
```

- [ ] **Step 2: 修复 `test/yolo_engine.hpp` 的 include 路径**

文件头部两行 include 从 yolo_deploy 的嵌套路径改为本地平铺路径：

```cpp
// 改前:
#include "slam_car_yolo/rknn_api.h"
#include "slam_car_yolo/postprocess.h"

// 改后:
#include "rknn_api.h"
#include "postprocess.h"
```

- [ ] **Step 3: 修复 `test/yolo_engine.cpp` 的 include 路径**

```cpp
// 改前:
#include "slam_car_yolo/yolo_engine.hpp"

// 改后:
#include "yolo_engine.hpp"
```

- [ ] **Step 4: 修复 `test/postprocess.cpp` 的 include 路径**

```cpp
// 改前:
#include "slam_car_yolo/postprocess.h"

// 改后:
#include "postprocess.h"
```

- [ ] **Step 5: 验证 `test/postprocess.h` 中 OBJ_CLASS_NUM=6**

```bash
grep "OBJ_CLASS_NUM" /home/lzy/文档/QTproject/test/postprocess.h
```

Expected output: `#define OBJ_CLASS_NUM         6`

如果值不为 6，修改为 6（用户模型有 6 个病虫害类别）。

- [ ] **Step 6: 创建 `test/labels_list.txt`**

内容与训练时类别顺序一致：

```
Algal Leaf Spot
Brown Blight
Gray Blight
Healthy
Helopeltis
Red Leaf Spot
```

---

### Task 2: 修改 test.pro 编译配置

**Files:**
- Modify: `test/test.pro`

- [ ] **Step 1: 在 SOURCES 中添加 yolo_engine.cpp 和 postprocess.cpp**

在 `test/test.pro` 的 SOURCES 列表末尾添加：

```qmake
SOURCES += \
    ...
    yolodetector.cpp \
    yolo_engine.cpp \
    postprocess.cpp
```

- [ ] **Step 2: 在 HEADERS 中添加新头文件**

```qmake
HEADERS += \
    ...
    yolodetector.h \
    yolo_engine.hpp \
    postprocess.h \
    rknn_api.h \
    rknn_matmul_api.h
```

- [ ] **Step 3: 添加 OpenCV 和 librknnrt.so 链接**

在 `test/test.pro` 文件末尾追加：

```qmake
# OpenCV (板端已通过 apt 安装 libopencv-dev)
unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += opencv4

# RKNN 运行时库
LIBS += -L$$PWD/../lib -lrknnrt
INCLUDEPATH += $$PWD
```

---

### Task 3: 重写 yolodetector.h

**Files:**
- Modify: `test/yolodetector.h`

- [ ] **Step 1: 用以下完整内容替换 `test/yolodetector.h`**

```cpp
#ifndef YOLODETECTOR_H
#define YOLODETECTOR_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <QElapsedTimer>

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "yolo_engine.hpp"

struct DetectionResult {
    QString className;   // 英文类名
    double score;        // 0.0~1.0
    QRect  bbox;         // 原图坐标
};

class YoloDetector : public QObject {
    Q_OBJECT
public:
    explicit YoloDetector(QObject *parent = nullptr);
    ~YoloDetector();

    /// 初始化引擎 + 启动推理线程
    /// @param modelPath  tealeaf.rknn 路径
    /// @param labelsPath labels_list.txt 路径
    /// @param coreMask  NPU 核心掩码，默认单核 Core0
    bool init(const std::string &modelPath,
              const std::string &labelsPath,
              int coreMask = 1);
    void stop();
    void setFpsLimit(int fps);
    void setConfThreshold(float v);
    void setNmsThreshold(float v);

signals:
    void detectionReady(const QImage &frame,
                        const QVector<DetectionResult> &results);
    void statusMessage(const QString &msg);

public slots:
    void onNewFrame(const QImage &frame);

private:
    void inferenceLoop();

    slam_car_yolo::YoloEngine m_engine;
    bool m_initialized = false;

    // 推理工作线程
    std::thread m_inferThread;
    std::atomic<bool> m_running{false};

    // 帧缓存 (拉模式: 推理线程取最新帧，Qt 主线程写)
    std::mutex m_mutex;
    std::condition_variable m_cv;
    QImage m_latestFrame;
    bool m_hasFrame = false;

    // 帧率控制
    int m_fpsLimit = 3;
    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;
};

#endif // YOLODETECTOR_H
```

---

### Task 4: 重写 yolodetector.cpp

**Files:**
- Modify: `test/yolodetector.cpp`

- [ ] **Step 1: 用以下完整内容替换 `test/yolodetector.cpp`**

```cpp
#include "yolodetector.h"
#include <QDebug>
#include <opencv2/opencv.hpp>

YoloDetector::YoloDetector(QObject *parent)
    : QObject(parent)
{
}

YoloDetector::~YoloDetector() {
    stop();
}

bool YoloDetector::init(const std::string &modelPath,
                         const std::string &labelsPath,
                         int coreMask) {
    m_initialized = m_engine.init(modelPath, labelsPath, coreMask);
    if (!m_initialized) {
        qCritical() << "[YOLO] Engine init FAILED";
        emit statusMessage("YOLO 引擎初始化失败");
        return false;
    }

    m_engine.setConfThreshold(0.25f);
    m_engine.setNmsThreshold(0.45f);

    m_running = true;
    m_inferThread = std::thread(&YoloDetector::inferenceLoop, this);

    qDebug() << "[YOLO] Engine READY";
    emit statusMessage("YOLO 引擎就绪");
    return true;
}

void YoloDetector::stop() {
    m_running = false;
    m_cv.notify_all();
    if (m_inferThread.joinable()) {
        m_inferThread.join();
    }
}

void YoloDetector::setFpsLimit(int fps) {
    m_fpsLimit = qBound(1, fps, 30);
}

void YoloDetector::setConfThreshold(float v) {
    m_engine.setConfThreshold(v);
}

void YoloDetector::setNmsThreshold(float v) {
    m_engine.setNmsThreshold(v);
}

void YoloDetector::onNewFrame(const QImage &frame) {
    if (m_fpsTimer.isValid() &&
        m_fpsTimer.elapsed() < 1000.0 / m_fpsLimit) {
        return;
    }
    m_fpsTimer.restart();

    if (!m_initialized) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestFrame = frame;
        m_hasFrame = true;
    }
    m_cv.notify_one();
}

void YoloDetector::inferenceLoop() {
    while (m_running) {
        QImage qtFrame;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(100),
                          [this] { return m_hasFrame; });
            if (!m_hasFrame) continue;
            qtFrame = m_latestFrame;
            m_hasFrame = false;
        }

        // QImage (RGB888) → cv::Mat (BGR)
        QImage rgb = qtFrame.convertToFormat(QImage::Format_RGB888);
        cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3,
                    const_cast<uchar*>(rgb.bits()),
                    rgb.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);

        // 推理 (draw=false，画框由 Qt QPainter 完成)
        std::vector<slam_car_yolo::Detection> detections;
        if (!m_engine.infer(bgr, detections, false)) {
            continue;
        }

        // 转换结果
        QVector<DetectionResult> results;
        for (auto &d : detections) {
            DetectionResult r;
            r.className = QString::fromUtf8(d.name);
            r.score = d.confidence;
            r.bbox = QRect(d.left, d.top,
                           d.right - d.left,
                           d.bottom - d.top);
            results.append(r);
        }

        m_frameCount++;
        if (m_frameCount % 30 == 0) {
            qDebug() << "[YOLO] Frame" << m_frameCount
                     << "Detections:" << results.size();
        }

        emit detectionReady(qtFrame, results);
    }
}
```

---

### Task 5: 修改 teagardensystem.cpp 初始化调用

**Files:**
- Modify: `test/teagardensystem.cpp:112-124`

- [ ] **Step 1: 替换 YoloDetector 初始化代码**

找到 `teagardensystem.cpp` 中 `m_yoloDetector` 的初始化块（约 112-124 行）：

```cpp
// 改前:
    // YOLO 病虫害检测（Socket 连接 Python 推理服务）
    m_yoloDetector = new YoloDetector(this);
    m_yoloDetector->setFpsLimit(3);
    connect(m_videoStreamer, &SrtVideoStreamer::frameReadyForYolo,
            m_yoloDetector, &YoloDetector::onNewFrame);
    connect(m_yoloDetector, &YoloDetector::detectionReady,
            this, &TeaGardenSystem::onYoloResults);
    connect(m_yoloDetector, &YoloDetector::statusMessage,
            this, &TeaGardenSystem::onVideoStatus);
    bool yoloOk = false;
    int yoloPort = qEnvironmentVariableIntValue("TEA_YOLO_PORT", &yoloOk);
    if (!yoloOk) yoloPort = 9800;
    m_yoloDetector->start("127.0.0.1", yoloPort);
```

替换为：

```cpp
    // YOLOv8 病虫害检测（C++ 引擎直连，同进程推理）
    m_yoloDetector = new YoloDetector(this);
    m_yoloDetector->setFpsLimit(3);
    connect(m_videoStreamer, &SrtVideoStreamer::frameReadyForYolo,
            m_yoloDetector, &YoloDetector::onNewFrame);
    connect(m_yoloDetector, &YoloDetector::detectionReady,
            this, &TeaGardenSystem::onYoloResults);
    connect(m_yoloDetector, &YoloDetector::statusMessage,
            this, &TeaGardenSystem::onVideoStatus);

    // 初始化引擎：模型和标签路径可通过环境变量覆盖
    QString modelPath = qEnvironmentVariable("TEA_YOLO_MODEL",
                                             "/home/elf/tealeaf.rknn");
    QString labelsPath = qEnvironmentVariable("TEA_YOLO_LABELS",
                                              "/home/elf/labels_list.txt");
    int coreMask = qEnvironmentVariableIntValue("TEA_YOLO_CORE_MASK");
    if (coreMask == 0) coreMask = 1;
    m_yoloDetector->init(modelPath.toStdString(),
                         labelsPath.toStdString(),
                         coreMask);
```

- [ ] **Step 2: 若 teagardensystem.h 中有 YoloDetector 的前向声明或 include，保持不变**

```bash
grep -n "YoloDetector\|yolodetector" /home/lzy/文档/QTproject/test/teagardensystem.h
```

已有 `#include "yolodetector.h"` 或前向声明则无需修改。如果没有 include，在文件头部添加：

```cpp
#include "yolodetector.h"
```

---

### Task 6: 同步 labels_list.txt 到板端路径

**Files:**
- Verify: `test/labels_list.txt`

- [ ] **Step 1: 确认本地 labels_list.txt 内容正确**

```bash
cat /home/lzy/文档/QTproject/test/labels_list.txt
```

预期输出 6 行：

```
Algal Leaf Spot
Brown Blight
Gray Blight
Healthy
Helopeltis
Red Leaf Spot
```

---

### Task 7: 编译与板端验证

- [ ] **Step 1: 清理旧构建产物（若存在）**

```bash
cd /home/lzy/文档/QTproject/test
make clean 2>/dev/null; true
```

- [ ] **Step 2: 运行 qmake 生成 Makefile**

```bash
cd /home/lzy/文档/QTproject/test
qmake test.pro
```

- [ ] **Step 3: 编译**

```bash
make -j$(nproc)
```

Expected: 编译成功，无链接错误（`librknnrt.so` 符号正常解析）。

- [ ] **Step 4: 部署到 ELF2 板端**

```bash
# 将编译产物和相关文件复制到板端
scp /home/lzy/文档/QTproject/test/test elf@10.31.222.22:/home/elf/
scp /home/lzy/文档/yolov8/tealeaf.rknn elf@10.31.222.22:/home/elf/
scp /home/lzy/文档/QTproject/test/labels_list.txt elf@10.31.222.22:/home/elf/
scp /home/lzy/文档/QTproject/lib/librknnrt.so elf@10.31.222.22:/home/elf/
```

- [ ] **Step 5: 板端运行验证**

```bash
# SSH 到板子
ssh elf@10.31.222.22
export LD_LIBRARY_PATH=/home/elf:$LD_LIBRARY_PATH
# 确保 NPU 设备存在
ls /dev/dri/
# 运行程序 (确保 SRT 流已推送)
./test
```

Expected:
- 程序启动后日志显示 `[YoloEngine] Init done. Input: 640x640x3`
- 日志显示 `[YOLO] Engine READY`
- `/yolo/annotated` 相关日志不再出现（不再使用 ROS2）
- 检测框位置与目标对齐，无偏移
- 推理延迟 < 100ms
- 帧率稳定在 3 FPS

- [ ] **Step 6: 检查推理结果**

```bash
# 观察日志中的检测输出
# 每 30 帧打印: [YOLO] Frame 30 Detections: N
# 确认 N > 0（有检出）
```

---

### 验证检查清单

- [ ] 编译通过，无 librknnrt.so 符号缺失
- [ ] OBJ_CLASS_NUM=6 与 tealeaf.rknn 一致
- [ ] labels_list.txt 类别顺序与训练一致
- [ ] 1280×720 SRT 视频推理无变形，letterbox 生效
- [ ] detectionReady 信号正常，TeaGardenSystem 画框/入库/饼图正常
- [ ] 3 FPS 帧率限制有效
- [ ] 推理延迟 < 100ms
