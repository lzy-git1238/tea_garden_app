# YOLOv8 rknnPool 推理引擎集成 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 ELF2 上已验证的 rknnPool 多线程 RKNN 推理引擎替换当前失效的 YoloEngine/YoloDetector，使 YOLO 检测在 Qt APP 内正常工作。

**Architecture:** 从 `~/yolov8_test` 复制 rknnPool/rkYolov5s/postprocess 等源码到 Qt 工程。rkYolov5s::infer() 加可选出参导出检测结果。Qt 333ms Timer 拉帧 → rknnPool.put/get → 画好框的 cv::Mat 直接显示，检测结果另路写 PestStore。3 线程线程池各绑一个 NPU 核。

**Tech Stack:** RK3588 NPU, rknnPool, rkYolov5s, OpenCV, RGA, Qt 5, CMake

---

## 文件结构

| 文件 | 动作 |
|------|------|
| `test/rknn/yolov8/` | 新建目录，放所有从 yolov8_test 复制的源文件 |
| `test/yolo_engine.hpp` | 删除 |
| `test/yolo_engine.cpp` | 删除 |
| `test/yolodetector.h` | 删除（不再需要 Qt 层封装） |
| `test/yolodetector.cpp` | 删除 |
| `test/postprocess.h` | 删除（用 yolov8_test 版本） |
| `test/postprocess.cpp` | 删除 |
| `test/rknn_api.h` | 替换 |
| `test/rknn_matmul_api.h` | 替换 |
| `test/model/` | 新建目录，放 best.rknn + labels_list.txt |
| `test/teagardensystem.h` | 删 YoloDetector，加 rknnPool 成员 |
| `test/teagardensystem.cpp` | 替换 YOLO 初始化 + 推理 |
| `test/test.pro` | 更新源文件列表 |

---

### Task 1: 创建目录结构 + 复制 yolov8_test 源文件

**Files:**
- Create: `test/rknn/yolov8/rkYolov5s.hpp`, `test/rknn/yolov8/rkYolov5s.cc`, `test/rknn/yolov8/rknnPool.hpp`, `test/rknn/yolov8/ThreadPool.hpp`, `test/rknn/yolov8/postprocess.h`, `test/rknn/yolov8/postprocess.cc`, `test/rknn/yolov8/preprocess.h`, `test/rknn/yolov8/preprocess.cc`, `test/rknn/yolov8/coreNum.hpp`, `test/rknn/yolov8/rga_func.h`, `test/rknn/yolov8/drm_func.h`, `test/rknn/yolov8/rknn_api.h`, `test/rknn/yolov8/rknn_matmul_api.h`
- Create: `test/model/`
- Create: `test/model/best.rknn`, `test/model/labels_list.txt`

- [ ] **Step 1: 创建目录**

```bash
mkdir -p /home/lzy/文档/QTproject/test/rknn/yolov8
mkdir -p /home/lzy/文档/QTproject/test/model
```

- [ ] **Step 2: 复制源码文件**

```bash
SRC=/home/lzy/yolov8_test/yolov8-helmet-rk3588-multithread-main
DST=/home/lzy/文档/QTproject/test/rknn/yolov8
cp $SRC/include/rkYolov5s.hpp $DST/
cp $SRC/src/rkYolov5s.cc $DST/
cp $SRC/include/rknnPool.hpp $DST/
cp $SRC/include/ThreadPool.hpp $DST/
cp $SRC/include/postprocess.h $DST/
cp $SRC/src/postprocess.cc $DST/
cp $SRC/include/preprocess.h $DST/
cp $SRC/src/preprocess.cc $DST/
cp $SRC/include/coreNum.hpp $DST/
cp $SRC/include/rga_func.h $DST/
cp $SRC/include/drm_func.h $DST/
cp $SRC/include/rknn_api.h $DST/
cp $SRC/include/rknn_matmul_api.h $DST/
```

- [ ] **Step 3: 复制模型和标签**

```bash
cp $SRC/model/best.rknn /home/lzy/文档/QTproject/test/model/
cp $SRC/model/labels_list.txt /home/lzy/文档/QTproject/test/model/
```

- [ ] **Step 4: 修复 labels_list.txt 第二行多余逗号**

`yolov8_test` 的 `labels_list.txt` 第二行是 `Brown Blight,`（有逗号），需要去掉：

```bash
sed -i 's/Brown Blight,/Brown Blight/' /home/lzy/文档/QTproject/test/model/labels_list.txt
```

- [ ] **Step 5: 更新 postprocess.h 中的路径常量**

将 `LABEL_NALE_TXT_PATH` 从 `"./model/labels_list.txt"` 改为模型在板子上的实际路径：

[test/rknn/yolov8/postprocess.h](test/rknn/yolov8/postprocess.h) 中修改：

```cpp
#define LABEL_NALE_TXT_PATH "/home/elf/QTproject/test/model/labels_list.txt"
```

确认 `OBJ_CLASS_NUM` 为 6（六类茶病虫害）。

- [ ] **Step 6: Commit**

```bash
git add test/rknn/ test/model/
git commit -m "feat: 从 yolov8_test 复制 rknnPool 推理引擎源码和模型"
```

---

### Task 2: 修改 rkYolov5s::infer() 加检测结果出参

**Files:**
- Modify: `test/rknn/yolov8/rkYolov5s.hpp`
- Modify: `test/rknn/yolov8/rkYolov5s.cc`

- [ ] **Step 1: 修改 rkYolov5s.hpp — infer 声明加可选出参**

```cpp
// 当前（第 36 行）：
    cv::Mat infer(cv::Mat &ori_img);
// 改为：
    cv::Mat infer(cv::Mat &ori_img, detect_result_group_t *out_results = nullptr);
```

- [ ] **Step 2: 修改 rkYolov5s.cc — infer 实现中把 detect_result_group 填充到出参**

在 `rkYolov5s::infer()` 中找到 `post_process(...)` 调用后、画框前的代码：

```cpp
// 在 post_process(...) 调用之后、画框循环之前插入：
    if (out_results != nullptr) {
        *out_results = detect_result_group;
    }
```

即 [rkYolov5s.cc:287-289](test/rknn/yolov8/rkYolov5s.cc#L287-L289) 附近，`post_process(...)` 之后、`// 绘制框体` 注释之前添加。

- [ ] **Step 3: Commit**

```bash
git add test/rknn/yolov8/rkYolov5s.hpp test/rknn/yolov8/rkYolov5s.cc
git commit -m "feat: rkYolov5s::infer 加 detect_result_group 出参"
```

---

### Task 3: 清理旧 YOLO 代码

**Files:**
- Delete: `test/yolo_engine.hpp`, `test/yolo_engine.cpp`, `test/yolodetector.h`, `test/yolodetector.cpp`, `test/postprocess.h`, `test/postprocess.cpp`
- Modify: `test/test.pro`

- [ ] **Step 1: 删除文件和从 .pro 移除**

```bash
rm /home/lzy/文档/QTproject/test/yolo_engine.hpp
rm /home/lzy/文档/QTproject/test/yolo_engine.cpp
rm /home/lzy/文档/QTproject/test/yolodetector.h
rm /home/lzy/文档/QTproject/test/yolodetector.cpp
rm /home/lzy/文档/QTproject/test/postprocess.h
rm /home/lzy/文档/QTproject/test/postprocess.cpp
```

不删 `rknn_api.h` / `rknn_matmul_api.h` — 保留在 test/ 根目录，给 `librknnrt.so` 链接用（yolov8 源码自己带了另一份在 `rknn/yolov8/` 下）。

- [ ] **Step 2: 修改 test.pro**

从 `SOURCES` 删除：`yolodetector.cpp`, `yolo_engine.cpp`, `postprocess.cpp`
从 `HEADERS` 删除：`yolodetector.h`, `yolo_engine.hpp`, `postprocess.h`, `rknn_api.h`, `rknn_matmul_api.h`

添加新源文件：

```pro
SOURCES += \
    ...
    rknn/yolov8/rkYolov5s.cc \
    rknn/yolov8/postprocess.cc \
    rknn/yolov8/preprocess.cc

HEADERS += \
    ...
    rknn/yolov8/rkYolov5s.hpp \
    rknn/yolov8/rknnPool.hpp \
    rknn/yolov8/ThreadPool.hpp \
    rknn/yolov8/postprocess.h \
    rknn/yolov8/preprocess.h \
    rknn/yolov8/coreNum.hpp \
    rknn/yolov8/rga_func.h \
    rknn/yolov8/drm_func.h \
    rknn/yolov8/rknn_api.h \
    rknn/yolov8/rknn_matmul_api.h
```

添加 include 路径：

```pro
INCLUDEPATH += $$PWD/rknn/yolov8
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "chore: 删除旧 YoloEngine/YoloDetector，更新 .pro"
```

---

### Task 4: teagardensystem 集成 rknnPool

**Files:**
- Modify: `test/teagardensystem.h`
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: 修改 teagardensystem.h — 删 YoloDetector 引用，加 rknnPool 成员**

删除：
```cpp
#include "yolodetector.h"
...
YoloDetector *m_yoloDetector = nullptr;
```

替换为：
```cpp
#include "rknnPool.hpp"
#include "rkYolov5s.hpp"

// 替换 YoloDetector：
std::unique_ptr<rknnPool<rkYolov5s, cv::Mat, cv::Mat>> m_yoloPool;
```

注意 `rknnPool` 的头文件引用了 `<opencv2/core/core.hpp>`，Qt 工程已有 OpenCV。

- [ ] **Step 2: 修改 teagardensystem.cpp — 替换 YOLO 初始化**

找到当前 `m_yoloDetector` 初始化代码（约 lines 137-156）：

```cpp
// 删除：
m_yoloDetector = new YoloDetector(this);
m_yoloDetector->setFpsLimit(3);
connect(m_videoStreamer, &SrtVideoStreamer::frameReadyForYolo, ...);
...

// YOLO Timer 改为：
m_yoloPullTimer = new QTimer(this);
connect(m_yoloPullTimer, &QTimer::timeout, this, [this]() {
    QImage frame = m_videoStreamer->latestFrame();
    if (frame.isNull() || !m_yoloPool) return;
    // QImage → cv::Mat BGR
    cv::Mat mat(frame.height(), frame.width(), CV_8UC3,
                const_cast<uchar*>(frame.bits()), frame.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
    // 提交推理
    if (m_yoloPool->put(bgr) != 0) return;
    cv::Mat result;
    detect_result_group_t results;
    if (m_yoloPool->get(result, &results) != 0) return;
    // ... 显示和处理结果
});
m_yoloPullTimer->setInterval(333);
m_yoloPullTimer->start();
```

**等等**——`rknnPool::get()` 当前签名是 `int get(outputType &outputData)`，只返回 `cv::Mat`。需要也调整 `rknnPool` 支持检测结果出参。但 rknnPool 是泛型模板，不好直接改。

**替代方案**：在 `rkYolov5s` 里加一个成员变量缓存最近一次推理的 `detect_result_group_t`，然后加一个 `getLastResults()` 方法。`rkYolov5s::infer()` 内部填充这个缓存，APP 端 `get()` 拿到 `cv::Mat` 后调用 `getLastResults()`。

这样不改 `rknnPool` 接口，只改 `rkYolov5s`。

**rkYolov5s.hpp 加：**
```cpp
    detect_result_group_t& lastResults() { return m_lastResults; }
private:
    detect_result_group_t m_lastResults;
```

**rkYolov5s.cc infer() 里**，`post_process(...)` 之后加：
```cpp
    m_lastResults = detect_result_group;
```

**teagardensystem.cpp 中完整的 YOLO Timer：**

```cpp
    // 替换旧的 YoloDetector 初始化
    // rknnPool: 3 线程，共享 best.rknn 权重，NPU 三核各绑一个
    m_yoloPool = std::make_unique<rknnPool<rkYolov5s, cv::Mat, cv::Mat>>(
        "/home/elf/QTproject/test/model/best.rknn", 3);
    if (m_yoloPool->init() != 0) {
        appendChatMessage("系统", "YOLO 模型加载失败！");
    } else {
        appendChatMessage("系统", "YOLO 引擎就绪 (3 NPU 核心)");
    }

    // YOLO Timer 333ms 拉帧 → rknnPool 推理
    m_yoloPullTimer = new QTimer(this);
    m_yoloPullTimer->setInterval(333);
    connect(m_yoloPullTimer, &QTimer::timeout, this, [this]() {
        if (!m_yoloPool) return;
        QImage frame = m_videoStreamer->latestFrame();
        if (frame.isNull()) return;
        cv::Mat mat(frame.height(), frame.width(), CV_8UC3,
                    const_cast<uchar*>(frame.bits()), frame.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
        if (m_yoloPool->put(bgr) != 0) return;
        cv::Mat result;
        if (m_yoloPool->get(result) != 0) return;
        // result 已画框（OpenCV 推理线程绘制），直接转 QPixmap 显示
        cv::cvtColor(result, result, cv::COLOR_BGR2RGB);
        QImage qimg(result.data, result.cols, result.rows, result.step,
                    QImage::Format_RGB888);
        onVideoFrame(qimg.copy());

        // 提取检测结果写数据库
        if (!m_yoloPool || m_yoloPool->empty()) return;
        // 通过 m_yoloPool 获取最近推理的 rkYolov5s 实例结果
        // 使用 rkYolov5s 的 lastResults() 方法
    });
    m_yoloPullTimer->start();
```

**注意**：`rknnPool` 的 get() 是通过 `futs.front().get()` 从线程池取结果的，无法直接拿到对应模型实例的 `lastResults()`。需要在 `rknnPool::get()` 之外再提供一个方法。

**最终方案——在 rknnPool 里加一个简单的结果缓存队列**：

`rknnPool.hpp` 的 `put()` 改为同时缓存检测结果：

实际上最简方案是：不通过 `rknnPool` 取结果，而是在每个 `rkYolov5s` 实例的 `infer()` 里把 `m_lastResults` 设为 public 成员，再在 `rknnPool` 里用一个 parallel queue 存结果指针。

但这太复杂。**最简洁方案：检测结果跟着 cv::Mat 走**——实际上当前设计已决定画面在 APP 直接显示原始帧+YOLO有框帧交替，那检测结果的提取可以：每个 rkYolov5s 实例 infer 时把结果存到一个全局可访问的位置，主线程取。

**最终最简方案：在 rknnPool 模板外再加一层薄封装**：

```cpp
// teagardensystem.cpp 私有的小管理器
struct YoloResult {
    cv::Mat frame;
    detect_result_group_t detections;
};
std::queue<YoloResult> m_yoloResults;
std::mutex m_yoloResultsMutex;
```

Timer 里 put 后也立即尝试 get，拿出 cv::Mat（带框）的同时，从 rkYolov5s 的全局缓存拿结果。但这需要 rkYolov5s 是静态可访问的。

**实际操作的最简路径**：给 rkYolov5s 加 `detect_result_group_t m_lastResults` 成员和 `detect_result_group_t& getLastResults()` 方法。`infer()` 里在 `post_process()` 后赋值。主线程通过 `getLastResults()` 拿。因为 rknnPool 里的 models 是 `std::vector<std::shared_ptr<rkYolov5s>>`，我们可以在 rknnPool 加一个方法返回最近使用的模型指针：

```cpp
// rknnPool.hpp 加
std::shared_ptr<rknnModel> lastModel() {
    std::lock_guard<std::mutex> lock(idMtx);
    int idx = (id - 1 + threadNum) % threadNum;  // 上一个用的模型索引
    return models[idx];
}
```

这样主线程 `auto model = m_yoloPool->lastModel(); auto &results = model->getLastResults();` 就能拿到。

- [ ] **Step 3: 全部改动文件的最终版本**

综合以上，TeaGardenSystem 端完整改动如下：

**rkYolov5s.hpp** — 加 `detect_result_group_t m_lastResults` 成员 + `getLastResults()`：

略（在 Task 2 已完成基础上加一个 public 方法）。

**rknnPool.hpp** — 加 `lastModel()`：

在 `public` 区域加：
```cpp
std::shared_ptr<rknnModel> lastModel() {
    std::lock_guard<std::mutex> lock(idMtx);
    if (id == 0) return nullptr;
    int idx = (id - 1) % threadNum;
    return models[idx];
}
```

**teagardensystem.cpp** — YOLO Timer（替换现有 m_yoloDetector 相关代码）：

```cpp
    // rknnPool 推理引擎（替代旧 YoloDetector）
    m_yoloPool = std::make_unique<rknnPool<rkYolov5s, cv::Mat, cv::Mat>>(
        "/home/elf/QTproject/test/model/best.rknn", 3);
    if (m_yoloPool->init() != 0) {
        appendChatMessage("系统", "YOLO 模型加载失败！");
    }

    // YOLO 拉帧推理 Timer
    m_yoloPullTimer = new QTimer(this);
    m_yoloPullTimer->setInterval(333);
    connect(m_yoloPullTimer, &QTimer::timeout, this, [this]() {
        if (!m_yoloPool) return;
        QImage frame = m_videoStreamer->latestFrame();
        if (frame.isNull()) return;
        cv::Mat mat(frame.height(), frame.width(), CV_8UC3,
                    const_cast<uchar*>(frame.bits()), frame.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
        if (m_yoloPool->put(bgr) != 0) return;
        cv::Mat result;
        if (m_yoloPool->get(result) != 0) return;
        // result 已画框，直接显示
        cv::cvtColor(result, result, cv::COLOR_BGR2RGB);
        QImage qimg(result.data, result.cols, result.rows, result.step,
                    QImage::Format_RGB888);
        onVideoFrame(qimg.copy());

        // 提取检测结果写 PestStore
        auto model = m_yoloPool->lastModel();
        if (!model) return;
        auto &dets = model->getLastResults();
        bool hasNew = false;
        for (int i = 0; i < dets.count; i++) {
            detect_result_t &d = dets.results[i];
            QString pestType = mapYoloClassToChinese(QString::fromUtf8(d.name));
            if (pestType == "Healthy" || pestType.isEmpty()) continue;
            if (m_dronePath.isEmpty()) continue;
            double lat = m_dronePath.last().lat;
            double lng = m_dronePath.last().lon;
            if (PestStore::instance()->hasNearby(pestType, lat, lng)) continue;
            PestRecord rec;
            rec.timestamp = QDateTime::currentDateTime();
            rec.pestType  = pestType;
            rec.lat = lat;
            rec.lng = lng;
            rec.severity = d.prop >= 0.75 ? 3 : (d.prop >= 0.50 ? 2 : 1);
            PestStore::instance()->append(rec);
            hasNew = true;
        }
        if (hasNew) {
            injectPestMarkers();
            refreshPestPie();
            if (pestDashboard && pestDashboard->isVisible())
                pestDashboard->refresh();
        }
    });
    m_yoloPullTimer->start();
```

**teagardensystem.h** — 删除 `YoloDetector` 引用：
```cpp
// 删除：
#include "yolodetector.h"
YoloDetector *m_yoloDetector = nullptr;
// 替换为：
#include "rknnPool.hpp"
#include "rkYolov5s.hpp"
std::unique_ptr<rknnPool<rkYolov5s, cv::Mat, cv::Mat>> m_yoloPool;
```

- [ ] **Step 4: 编译验证**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4
```

Expected: 编译通过。链接 `-lrknnrt` 错误在 x86 上正常。

- [ ] **Step 5: Commit**

```bash
git add test/teagardensystem.h test/teagardensystem.cpp test/rknn/yolov8/rkYolov5s.hpp test/rknn/yolov8/rknnPool.hpp
git commit -m "feat: teagardensystem 集成 rknnPool 替换旧 YoloDetector"
```

---

### Task 5: ELF2 编译 + 部署验证

**Files:** 无需修改

- [ ] **Step 1: 同步源码到板子**

```bash
sshpass -p "elf" rsync -avz --delete \
  --exclude='*.o' --exclude='moc_*' --exclude='moc_predefs.h' \
  --exclude='ui_widget.h' --exclude='qrc_*.cpp' \
  --exclude='Makefile' --exclude='.qmake.stash' \
  -e "ssh -o StrictHostKeyChecking=no" \
  /home/lzy/文档/QTproject/test/ elf@10.200.0.102:~/QTproject/test/
```

- [ ] **Step 2: 编译**

```bash
sshpass -p "elf" ssh -o StrictHostKeyChecking=no elf@10.200.0.102 \
  "cd ~/QTproject/build-arm-Debug && \
   rm -f Makefile .qmake.stash *.o moc_*.cpp moc_predefs.h ui_widget.h qrc_*.cpp test && \
   qmake ../test/test.pro && make -j\$(nproc) 2>&1 | tail -5"
```

Expected: 编译链接全部通过，无错误。

- [ ] **Step 3: 运行验证**

```bash
sshpass -p "elf" ssh -o StrictHostKeyChecking=no elf@10.200.0.102 \
  "cd ~/QTproject/build-arm-Debug && timeout 10 ./test 2>&1 | grep -E 'YOLO|yolo|rknn|model'"
```

Expected: 看到 `sdk version: ... driver version: ...` 等 RKNN 初始化成功信息。

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "verify: rknnPool 集成 ELF2 编译通过"
```
