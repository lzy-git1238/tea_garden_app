# 病虫害标记三项改进

## 需求

1. **实时 GPS 坐标** — YOLO 检测到的病虫害使用无人机实时位置，替代硬编码坐标
2. **同种类 + 同位置去重** — 同一植株（0.5m 内 + 相同病虫害）被多帧拍到只记一次
3. **地图 Popup 交互** — 点击标记查看详情并支持硬删除

## 去重规则

| 种类 | 位置 | 行为 |
|------|------|------|
| 相同 | 0.5m 内 | ❌ 跳过 |
| 相同 | 0.5m+ | ✅ 记录 |
| 不同 | 0.5m 内 | ✅ 记录 |
| 不同 | 0.5m+ | ✅ 记录 |

## 架构

```
YOLO 检测结果 onYoloResults()
  ├─ 取 m_dronePath.last() 作为虫害坐标（替代硬编码）
  ├─ PestStore::hasNearby(type, lat, lng) → 同种+0.5m 内已有？跳过
  ├─ PestStore::append() → 写入 SQLite（PestRecord 新增 id 字段）
  ├─ injectPestMarkers() → JS 注入 Leaflet circleMarker + Popup
  │    └─ Popup 内「删除」按钮 → console.log("PEST_DELETE:<id>")
  └─ refreshPestPie() → 更新饼图
```

### 删除链路

```
用户点击 Popup「删除」
  → JS 端: console.log("PEST_DELETE:<id>")
  → Qt 端: QWebEnginePage::javaScriptConsoleMessage 拦截
    → 解析 PEST_DELETE 前缀
    → PestStore::deleteById(id) → SQLite DELETE
    → injectPestMarkers() → 地图移除所有标记再重建
    → refreshPestPie() → 饼图刷新
```

## 数据库变更

### pest_log 表加自增 id（已有）

SQLite 的 `INTEGER PRIMARY KEY AUTOINCREMENT` 当前未在查询中暴露给应用层。`PestRecord` 新增 `int id` 字段，`queryAll()` 返回时填充。

### 新增查询方法

**`hasNearby(type, lat, lng, distanceM=0.5)`**
```sql
SELECT COUNT(*) FROM pest_log
WHERE pest_type = :type
  AND (lat - :lat)*(lat - :lat) + (lng - :lng)*(lng - :lng) < :threshold
```
`threshold = (distanceM / 111320.0)^2`（1 纬度 ≈ 111320m）

**`deleteById(id)`**
```sql
DELETE FROM pest_log WHERE id = :id
```

## 改动范围

| 文件 | 改动 |
|------|------|
| [pestdata.h](test/pestdata.h) | `PestRecord` 加 `int id`；`PestStore` 新增 `hasNearby()`、`deleteById()` |
| [pestdata.cpp](test/pestdata.cpp) | 实现两个新方法；`queryAll()` 返回 id 列 |
| [teagardensystem.h](test/teagardensystem.h) | 新增 `deletePestRecord(int id)` 私有槽、`m_lastPestInjectTimer` 防抖 |
| [teagardensystem.cpp](test/teagardensystem.cpp) | `onYoloResults()` 使用 `m_dronePath.last()`；写入前调用 `hasNearby()` 去重；`injectPestMarkers()` 改用 `PestStore::queryAll()` 含 id 并注入 Popup；`javaScriptConsoleMessage` 监听 PEST_DELETE |
| [web/amap_view.html](test/web/amap_view.html) | 无需改动（Popup 和 deletePest 函数完全由 C++ 端 runJavaScript 注入） |

## 关键设计决策

### Popup 全由 C++ 端构建

不在 amap_view.html 中预设 `deletePest()` 函数，而是在 `injectPestMarkers()` 的 JS 字符串中内联定义 `window.deletePest = function(id) { console.log('PEST_DELETE:' + id); }`，每次注入时覆盖。简单且避免 HTML 文件修改。

### 删除时全量重建标记

删除单条后执行 `injectPestMarkers()` → 清除所有标记 → 从 `PestStore::queryAll()` 重新遍历注入。虽然 O(N) 重建，但 N 通常很小（百级别），远轻于 WebSocket 通信或双向 Qt↔JS 交互。保持了现有注入模式不变。

### 同 sku 去重不加时间窗口

空间去重已经足够：如果无人机飞走后再度飞到同一株 0.5m 内，说明是二次巡检，不应去重——我们只过滤同一帧序列中的重复。但实际上不加时间限制更保守：如果之前某次飞行在相同 0.5m 已记录过同类虫害，再次飞过同一株时直接跳过更合理（病害不会短期消失），也防止数据库膨胀。

## 测试要点

- 启动 YOLO 检测 → 确认坐标来自 `m_dronePath.last()` 而非 25.08/118.232
- 同一目标连续多帧 → 只有第一帧写入数据库
- 点击地图标记 → Popup 显示详情（种类、严重度、时间、坐标）
- 点击 Popup 内删除 → 标记消失、数据库记录删除、饼图刷新
- 无 GPS 定位时（`m_dronePath.isEmpty()`）→ 跳过写入，不崩溃
