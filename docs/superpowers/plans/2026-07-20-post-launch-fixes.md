# 上线后三问题修复 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复框闪烁（锁2秒）、无GPS不入库（回退地图初始点）、地图卡顿（injectPestMarkers限流2秒）

**Architecture:** 三个修复均在 `teagardensystem.h` 加 timer + `teagardensystem.cpp` 改三处：显示Timer、onYoloResults、injectPestMarkers

**Tech Stack:** Qt 5

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `test/teagardensystem.h` | 新增 `QElapsedTimer m_lastYoloFrameTime`、`QElapsedTimer m_lastMarkerInject` |
| `test/teagardensystem.cpp` | 三处改动 |

---

### Task 1: 三个修复一次性实施

**Files:**
- Modify: `test/teagardensystem.h`
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: teagardensystem.h — 加两个 QElapsedTimer 成员**

在 `m_videoPullTimer` 附近（约 192 行）加：

```cpp
    QElapsedTimer m_lastYoloFrameTime;    // 上次 YOLO 帧贴屏时刻，用于锁定2秒
    QElapsedTimer m_lastMarkerInject;     // 上次地图标注刷新时刻，限流2秒
```

- [ ] **Step 2: teagardensystem.cpp — 修复 1：33ms 显示Timer加锁定**

找到 33ms 显示 timer（约 138-141 行），当前代码：

```cpp
    connect(m_videoPullTimer, &QTimer::timeout, this, [this]() {
        QImage frame = m_videoStreamer->latestFrame();
        if (!frame.isNull()) onVideoFrame(frame);
    });
```

改为：

```cpp
    connect(m_videoPullTimer, &QTimer::timeout, this, [this]() {
        // YOLO 有框帧锁定 2 秒，期间跳过原始帧覆盖
        if (m_lastYoloFrameTime.isValid() && m_lastYoloFrameTime.elapsed() < 2000)
            return;
        QImage frame = m_videoStreamer->latestFrame();
        if (!frame.isNull()) onVideoFrame(frame);
    });
```

- [ ] **Step 3: teagardensystem.cpp — 修复 2：onYoloResults 无GPS退回到环境变量**

找到 `onYoloResults()` 中（约 2345-2346 行）：

```cpp
        if (m_dronePath.isEmpty()) continue;

        double lat = m_dronePath.last().lat;
        double lng = m_dronePath.last().lon;
```

改为：

```cpp
        double lat, lng;
        if (m_dronePath.isEmpty()) {
            lat = qEnvironmentVariable("TEA_MAP_LAT", "25.08039").toDouble();
            lng = qEnvironmentVariable("TEA_MAP_LNG", "118.232478").toDouble();
        } else {
            lat = m_dronePath.last().lat;
            lng = m_dronePath.last().lon;
        }
```

并在方法开头（`if (!videoLabel || frame.isNull()) return;` 之后）加时间戳：

```cpp
void TeaGardenSystem::onYoloResults(const QImage &frame,
                                     const detect_result_group_t &results) {
    if (!videoLabel || frame.isNull()) return;

    m_lastYoloFrameTime.start();  // 记录贴帧时间，锁定2秒显示

    // 1. 直接显示（框已由推理线程 OpenCV 绘制）
    ...
```

- [ ] **Step 4: teagardensystem.cpp — 修复 3：injectPestMarkers 限流**

找到 `injectPestMarkers()` 开头（约 2013 行）：

```cpp
void TeaGardenSystem::injectPestMarkers() {
#ifdef TEA_ENABLE_MAP_WEBVIEW
    if (!mapView) return;
```

改为：

```cpp
void TeaGardenSystem::injectPestMarkers() {
#ifdef TEA_ENABLE_MAP_WEBVIEW
    if (!mapView) return;
    // 限流：2 秒内最多刷新一次地图标注
    if (m_lastMarkerInject.isValid() && m_lastMarkerInject.elapsed() < 2000)
        return;
    m_lastMarkerInject.start();
```

- [ ] **Step 5: 编译验证 (x86)**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4 2>&1 | grep -E 'teagardensystem.o|error'
```

Expected: `teagardensystem.o` 编译成功，无错误。

- [ ] **Step 6: Commit**

```bash
git add test/teagardensystem.h test/teagardensystem.cpp
git commit -m "fix: 框闪烁锁2秒 + 无GPS回退坐标 + injectPestMarkers限流"
```

---

### Task 2: ELF2 编译部署

**Files:** 无需修改

- [ ] **Step 1: 同步 + 编译**

```bash
sshpass -p "elf" rsync -avz --delete \
  --exclude='*.o' --exclude='moc_*' --exclude='moc_predefs.h' \
  --exclude='ui_widget.h' --exclude='qrc_*.cpp' \
  --exclude='Makefile' --exclude='.qmake.stash' \
  -e "ssh -o StrictHostKeyChecking=no" \
  /home/lzy/文档/QTproject/test/ elf@10.200.0.102:~/QTproject/test/ && \
sshpass -p "elf" ssh -o StrictHostKeyChecking=no elf@10.200.0.102 \
  "cd ~/QTproject/build-arm-Debug && \
   rm -f Makefile .qmake.stash *.o moc_*.cpp moc_predefs.h ui_widget.h qrc_*.cpp test && \
   qmake ../test/test.pro && make -j\$(nproc) 2>&1 | tail -1"
```

Expected: 编译链接全部通过。

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "verify: 三问题修复 ELF2 编译通过"
```
