# 全屏自适应布局 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 DPI 缩放改为按屏幕宽度比例缩放，使三栏布局在 1024×600 7寸屏和 1920×1080 电脑上都能正常显示。

**Architecture:** 以 1920px 为参考宽度，`m_scale = screenWidth / 1920`，替换原有 `m_dpiScale`，所有硬编码像素值统一乘以 `m_scale`。

**Tech Stack:** Qt C++ (QWidget, QScreen, QHBoxLayout, QVBoxLayout)

---

## File Structure

| 文件 | 操作 | 说明 |
|------|------|------|
| `test/main.cpp` | 修改 | DPI 缩放改为宽度比例缩放 |
| `test/teagardensystem.h` | 修改 | `m_dpiScale` → `m_scale` |
| `test/teagardensystem.cpp` | 修改 | 所有硬编码像素乘 `m_scale`，替换 `m_dpiScale` 引用 |

---

### Task 1: main.cpp — 计算宽度比例缩放系数

**Files:**
- Modify: `test/main.cpp:56-75`

- [ ] **Step 1: 修改 main.cpp 的缩放计算逻辑**

找到 `main.cpp` 第 56-75 行，将 DPI 缩放改为宽度比例缩放：

```cpp
// 当前代码:
    // ── QScreen 动态 DPI 缩放 ──
    QScreen *screen = a.primaryScreen();
    double dpiScale = 1.0;
    if (screen) {
        qreal dpi = screen->logicalDotsPerInch();
        if (dpi > 0) dpiScale = dpi / 96.0;
        // 7寸 1024x600 ≈ 170 DPI → dpiScale ≈ 1.77
        qDebug() << "[MAIN] screen:" << screen->geometry()
                 << "DPI:" << dpi << "scale:" << dpiScale;
    }

    // 全局字体缩放
    QFont appFont = a.font();
    appFont.setPointSizeF(appFont.pointSizeF() * dpiScale);
    a.setFont(appFont);

    TeaGardenSystem w;
    w.setScreenDpiScale(dpiScale);
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.showFullScreen();
```

改为：

```cpp
    // ── 按屏幕宽度比例缩放 ──
    // 参考宽度 1920px，其他屏幕按比例缩放
    // 1920px → 1.0x, 1024px → 0.53x, 1280px → 0.67x
    QScreen *screen = a.primaryScreen();
    double scale = 1.0;
    if (screen) {
        qreal screenWidth = screen->geometry().width();
        scale = screenWidth / 1920.0;
        scale = qBound(0.4, scale, 2.0);
        qDebug() << "[MAIN] screen:" << screen->geometry()
                 << "width:" << screenWidth << "scale:" << scale;
    }

    // 全局字体缩放
    QFont appFont = a.font();
    appFont.setPointSizeF(appFont.pointSizeF() * scale);
    a.setFont(appFont);

    TeaGardenSystem w;
    w.setScreenDpiScale(scale);  // 接口名保持不变，内部改用 m_scale
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.showFullScreen();
```

- [ ] **Step 2: Commit**

```bash
git add test/main.cpp
git commit -m "refactor: main.cpp DPI缩放改为屏幕宽度比例缩放"
```

---

### Task 2: teagardensystem.h — 重命名 m_dpiScale

**Files:**
- Modify: `test/teagardensystem.h:41,193`

- [ ] **Step 1: 全局替换 m_dpiScale → m_scale**

在 `teagardensystem.h` 中，找到两处 `m_dpiScale`：

1. 第 41 行 setter：
```cpp
// 当前:
    void setScreenDpiScale(double scale) { m_dpiScale = scale; }
// 改为:
    void setScreenDpiScale(double scale) { m_scale = scale; }
```

2. 第 193 行成员变量：
```cpp
// 当前:
    double m_dpiScale = 1.0;
// 改为:
    double m_scale = 1.0;
```

- [ ] **Step 2: Commit**

```bash
git add test/teagardensystem.h
git commit -m "refactor: teagardensystem.h m_dpiScale → m_scale"
```

---

### Task 3: teagardensystem.cpp — 替换所有 m_dpiScale 引用

**Files:**
- Modify: `test/teagardensystem.cpp` (9 处)

- [ ] **Step 1: 全局替换 m_dpiScale → m_scale**

在 `teagardensystem.cpp` 中，将所有 `m_dpiScale` 替换为 `m_scale`（共 9 处）：

```bash
# 用 sed 批量替换
sed -i 's/m_dpiScale/m_scale/g' test/teagardensystem.cpp
```

验证替换完成：
```bash
grep -c "m_dpiScale" test/teagardensystem.cpp
# 预期: 0
grep -c "m_scale" test/teagardensystem.cpp
# 预期: 9
```

- [ ] **Step 2: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "refactor: teagardensystem.cpp m_dpiScale → m_scale"
```

---

### Task 4: teagardensystem.cpp — 硬编码间距/边距乘 m_scale

**Files:**
- Modify: `test/teagardensystem.cpp:209-210,222,234,239,245-246,296-297,310,401,432-433,446-447,475-476,484,506,630-631`

- [ ] **Step 1: 替换根布局的间距和边距**

找到第 209-210 行：
```cpp
// 当前:
    rootLayout->setSpacing(10);
    rootLayout->setContentsMargins(10, 10, 10, 10);
// 改为:
    rootLayout->setSpacing(qRound(10 * m_scale));
    rootLayout->setContentsMargins(qRound(10*m_scale), qRound(10*m_scale), qRound(10*m_scale), qRound(10*m_scale));
```

- [ ] **Step 2: 替换 header 区域的间距**

找到第 222 行：
```cpp
// 当前:
    headerRow->setContentsMargins(16, 4, 16, 4);
// 改为:
    headerRow->setContentsMargins(qRound(16*m_scale), qRound(4*m_scale), qRound(16*m_scale), qRound(4*m_scale));
```

找到第 234 行：
```cpp
// 当前:
    headerRow->addSpacing(12);
// 改为:
    headerRow->addSpacing(qRound(12 * m_scale));
```

- [ ] **Step 3: 替换 body 和面板间距**

找到第 239 行：
```cpp
// 当前:
    bodyLayout->setSpacing(12);
// 改为:
    bodyLayout->setSpacing(qRound(12 * m_scale));
```

找到第 245-246 行：
```cpp
// 当前:
    leftPanel->setContentsMargins(10, 8, 10, 8);
    leftPanel->setSpacing(6);
// 改为:
    leftPanel->setContentsMargins(qRound(10*m_scale), qRound(8*m_scale), qRound(10*m_scale), qRound(8*m_scale));
    leftPanel->setSpacing(qRound(6 * m_scale));
```

找到第 296-297 行：
```cpp
// 当前:
    fcLayout->setContentsMargins(8, 8, 8, 8);
    fcLayout->setSpacing(4);
// 改为:
    fcLayout->setContentsMargins(qRound(8*m_scale), qRound(8*m_scale), qRound(8*m_scale), qRound(8*m_scale));
    fcLayout->setSpacing(qRound(4 * m_scale));
```

找到第 310 行：
```cpp
// 当前:
    fcMetricsRow->setSpacing(8);
// 改为:
    fcMetricsRow->setSpacing(qRound(8 * m_scale));
```

找到第 401 行：
```cpp
// 当前:
    devRow->setSpacing(4);
// 改为:
    devRow->setSpacing(qRound(4 * m_scale));
```

找到第 432-433 行：
```cpp
// 当前:
    midPanel->setContentsMargins(10, 8, 10, 8);
    midPanel->setSpacing(6);
// 改为:
    midPanel->setContentsMargins(qRound(10*m_scale), qRound(8*m_scale), qRound(10*m_scale), qRound(8*m_scale));
    midPanel->setSpacing(qRound(6 * m_scale));
```

找到第 446-447 行：
```cpp
// 当前:
    wcLayout->setContentsMargins(10, 6, 10, 6);
    wcLayout->setSpacing(3);
// 改为:
    wcLayout->setContentsMargins(qRound(10*m_scale), qRound(6*m_scale), qRound(10*m_scale), qRound(6*m_scale));
    wcLayout->setSpacing(qRound(3 * m_scale));
```

找到第 475-476 行：
```cpp
// 当前:
    rightPanel->setContentsMargins(10, 8, 10, 8);
    rightPanel->setSpacing(6);
// 改为:
    rightPanel->setContentsMargins(qRound(10*m_scale), qRound(8*m_scale), qRound(10*m_scale), qRound(8*m_scale));
    rightPanel->setSpacing(qRound(6 * m_scale));
```

找到第 484 行：
```cpp
// 当前:
    metricsGrid->setSpacing(8);
// 改为:
    metricsGrid->setSpacing(qRound(8 * m_scale));
```

找到第 506 行：
```cpp
// 当前:
    aiPanel->setSpacing(6);
// 改为:
    aiPanel->setSpacing(qRound(6 * m_scale));
```

找到第 630-631 行：
```cpp
// 当前:
    vbox->setContentsMargins(10, 8, 10, 8);
    vbox->setSpacing(5);
// 改为:
    vbox->setContentsMargins(qRound(10*m_scale), qRound(8*m_scale), qRound(10*m_scale), qRound(8*m_scale));
    vbox->setSpacing(qRound(5 * m_scale));
```

- [ ] **Step 4: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: 所有间距和边距乘以 m_scale 缩放系数"
```

---

### Task 5: teagardensystem.cpp — 固定尺寸按钮乘 m_scale

**Files:**
- Modify: `test/teagardensystem.cpp:213,407,418,454`

- [ ] **Step 1: 替换 themeBtn 尺寸**

找到第 213 行：
```cpp
// 当前:
    themeBtn->setFixedSize(90, 28);
// 改为:
    themeBtn->setFixedSize(qRound(90 * m_scale), qRound(28 * m_scale));
```

- [ ] **Step 2: 替换设备按钮尺寸**

找到第 407 行：
```cpp
// 当前:
    scheduleBtnPump->setFixedSize(30, 30);
// 改为:
    scheduleBtnPump->setFixedSize(qRound(30 * m_scale), qRound(30 * m_scale));
```

找到第 418 行：
```cpp
// 当前:
    scheduleBtnLamp->setFixedSize(30, 30);
// 改为:
    scheduleBtnLamp->setFixedSize(qRound(30 * m_scale), qRound(30 * m_scale));
```

- [ ] **Step 3: 替换天气刷新按钮尺寸**

找到第 454 行：
```cpp
// 当前:
    weatherRefreshBtn->setFixedSize(26, 26);
// 改为:
    weatherRefreshBtn->setFixedSize(qRound(26 * m_scale), qRound(26 * m_scale));
```

- [ ] **Step 4: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: 所有固定尺寸按钮乘以 m_scale 缩放系数"
```

---

### Task 6: teagardensystem.cpp — 弹窗和对话框尺寸乘 m_scale

**Files:**
- Modify: `test/teagardensystem.cpp:1541`

- [ ] **Step 1: 替换弹窗 resize**

找到第 1541 行（ScheduleDialog 或其他弹窗）：
```cpp
// 当前:
    dlg->resize(750, 520);
// 改为:
    dlg->resize(qRound(750 * m_scale), qRound(520 * m_scale));
```

- [ ] **Step 2: 验证编译**

```bash
cd /home/lzy/文档/QTproject/test
make -j$(nproc) 2>&1 | grep -E "error|错误" || echo "编译成功"
```

- [ ] **Step 3: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: 弹窗尺寸乘以 m_scale 缩放系数"
```

---

### Task 7: 端到端验证

- [ ] **Step 1: 桌面端编译验证**

```bash
cd /home/lzy/文档/QTproject/test
make clean && qmake test.pro && make -j$(nproc) 2>&1 | grep -E "error|错误" || echo "桌面端编译成功"
```

Expected: 编译成功

- [ ] **Step 2: 同步到 ELF2 并编译**

```bash
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh sync /home/lzy/文档/QTproject/test/
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh compile
```

Expected: ARM 交叉编译成功

- [ ] **Step 3: 检查缩放日志**

```bash
# 在 ELF2 上运行，检查输出的 scale 值
/home/lzy/.claude/skills/elf-ssh/scripts/elf-ssh.sh ssh "cd ~/QTproject/build-arm-Debug && timeout 3 ./test 2>&1 | grep scale || true"
```

Expected: 输出 `scale: 0.53xxx`（1024/1920 ≈ 0.533）

- [ ] **Step 4: 最终 Commit**

```bash
git add -A
git commit -m "feat: 全屏自适应布局完成（按屏幕宽度比例缩放）"
```
