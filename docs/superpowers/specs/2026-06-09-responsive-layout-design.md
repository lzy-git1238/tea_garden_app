# 全屏自适应布局设计

> 保留当前三栏布局，按屏幕宽度比例缩放所有 UI 元素，适配电脑屏幕和 7 寸 MIPI 屏（1024×600）。

## 背景

当前 app 已有 `showFullScreen()` + `Qt::FramelessWindowHint`，但大量 UI 元素使用硬编码像素值，只部分使用了 DPI 缩放。在 1024×600 的 7 寸 MIPI 屏上布局崩溃（元素重叠/截断）。

### 屏幕参数

| 屏幕 | 分辨率 | DPI | 当前 dpiScale |
|------|--------|-----|--------------|
| 电脑 | 1920×1080 | ~96 | 1.0 |
| 7寸 MIPI | 1024×600 | ~170 | 1.77 |

问题：`dpiScale` 放大了元素（1.77x），但屏幕宽度反而缩小了（1024 vs 1920），导致三栏放不下。

## 设计方案

**核心思路**：以 1920px 为参考宽度，按 `实际屏幕宽度 / 1920` 计算缩放系数，所有 UI 元素（间距、边距、固定尺寸、字号）统一乘以此系数。

```
缩放系数 m_scale = screenWidth / 1920

1920px 电脑 → 1.0x（原样）
1024px 7寸  → 0.53x（所有元素缩小到 53%）
1280px      → 0.67x
```

### 与 DPI 缩放的区别

| | DPI 缩放 (当前) | 宽度比例缩放 (新) |
|---|---|---|
| 1920px 电脑 | 1.0x | 1.0x |
| 1024px 7寸 | 1.77x（放大！） | 0.53x（缩小） |
| 效果 | 元素变大，屏幕放不下 | 元素等比缩小，布局保持 |

## 变更清单

### 1. main.cpp — 计算 m_scale

替换 `dpiScale` 为宽度比例缩放：

```cpp
QScreen *screen = a.primaryScreen();
double scale = 1.0;
if (screen) {
    qreal screenWidth = screen->geometry().width();
    scale = screenWidth / 1920.0;
    scale = qBound(0.4, scale, 2.0);  // 限制范围
    qDebug() << "[MAIN] screen:" << screen->geometry()
             << "scale:" << scale;
}

QFont appFont = a.font();
appFont.setPointSizeF(appFont.pointSizeF() * scale);
a.setFont(appFont);

TeaGardenSystem w;
w.setScreenDpiScale(scale);  // 复用现有接口，内部改名
w.setWindowFlags(Qt::FramelessWindowHint);
w.showFullScreen();
```

### 2. teagardensystem.cpp — 所有硬编码像素乘 m_scale

需要修改的位置（全部替换为 `qRound(原值 * m_scale)`）：

| 行号 | 当前代码 | 改后 |
|------|---------|------|
| 209 | `setSpacing(10)` | `setSpacing(qRound(10 * m_scale))` |
| 210 | `setContentsMargins(10,10,10,10)` | `setContentsMargins(qRound(10*m_scale), ...)` |
| 213 | `setFixedSize(90, 28)` | `setFixedSize(qRound(90*m_scale), qRound(28*m_scale))` |
| 220 | `setFixedHeight(qRound(52 * m_dpiScale))` | `setFixedHeight(qRound(52 * m_scale))` |
| 222 | `setContentsMargins(16, 4, 16, 4)` | `setContentsMargins(qRound(16*m_scale), qRound(4*m_scale), ...)` |
| 234 | `addSpacing(12)` | `addSpacing(qRound(12 * m_scale))` |
| 239 | `setSpacing(12)` | `setSpacing(qRound(12 * m_scale))` |
| 245 | `setContentsMargins(10, 8, 10, 8)` | 同上模式 |
| 246 | `setSpacing(6)` | `setSpacing(qRound(6 * m_scale))` |
| 272 | `setMinimumHeight(qRound(240 * m_dpiScale))` | `setMinimumHeight(qRound(240 * m_scale))` |
| 296 | `setContentsMargins(8, 8, 8, 8)` | 同上模式 |
| 297 | `setSpacing(4)` | `setSpacing(qRound(4 * m_scale))` |
| 310 | `setSpacing(8)` | `setSpacing(qRound(8 * m_scale))` |
| 352 | `setFixedHeight(qRound(5 * m_dpiScale))` | `setFixedHeight(qRound(5 * m_scale))` |
| 357 | `setFixedHeight(qRound(5 * m_dpiScale))` | 同上 |
| 401 | `setSpacing(4)` | `setSpacing(qRound(4 * m_scale))` |
| 407 | `setFixedSize(30, 30)` | `setFixedSize(qRound(30*m_scale), qRound(30*m_scale))` |
| 418 | `setFixedSize(30, 30)` | 同上 |
| 432 | `setContentsMargins(10, 8, 10, 8)` | 同上模式 |
| 433 | `setSpacing(6)` | 同上模式 |
| 443 | `setFixedHeight(qRound(205 * m_dpiScale))` | `setFixedHeight(qRound(205 * m_scale))` |
| 446 | `setContentsMargins(10, 6, 10, 6)` | 同上模式 |
| 447 | `setSpacing(3)` | `setSpacing(qRound(3 * m_scale))` |
| 454 | `setFixedSize(26, 26)` | `setFixedSize(qRound(26*m_scale), qRound(26*m_scale))` |
| 475 | `setContentsMargins(10, 8, 10, 8)` | 同上模式 |
| 476 | `setSpacing(6)` | 同上模式 |
| 484 | `setSpacing(8)` | 同上模式 |
| 506 | `setSpacing(6)` | 同上模式 |
| 586 | `setMinimumHeight(qRound(350 * m_dpiScale))` | `setMinimumHeight(qRound(350 * m_scale))` |
| 608 | `setMinimumHeight(qRound(520 * m_dpiScale))` | `setMinimumHeight(qRound(520 * m_scale))` |
| 615 | `setMinimumHeight(qRound(520 * m_dpiScale))` | 同上 |
| 625 | `setMinimumHeight(qRound(78 * m_dpiScale))` | `setMinimumHeight(qRound(78 * m_scale))` |
| 1541 | `dlg->resize(750, 520)` | `dlg->resize(qRound(750*m_scale), qRound(520*m_scale))` |

### 3. teagardensystem.h — 重命名成员变量

```cpp
// 改前
double m_dpiScale = 1.0;
void setScreenDpiScale(double scale) { m_dpiScale = scale; }

// 改后
double m_scale = 1.0;
void setScreenDpiScale(double scale) { m_scale = scale; }
```

全局替换 `m_dpiScale` → `m_scale`。

### 4. 视频区宽高比

`AspectRatioLabel` 已经按宽度计算高度，随面板宽度自适应，无需额外改动。

### 5. 字体缩放

`main.cpp` 中全局字体已按 `scale` 缩放，无需额外改动。

## 错误处理

无特殊错误处理需求。缩放系数通过 `qBound(0.4, scale, 2.0)` 限制范围，防止极端值。

## 验证

1. 电脑端（1920×1080）：布局应与当前一致
2. 7寸 MIPI（1024×600）：三栏布局保持，所有元素缩小到 ~53%
3. 中间分辨率（1280×720）：元素缩小到 ~67%
