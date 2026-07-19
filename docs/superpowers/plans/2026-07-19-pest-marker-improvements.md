# 病虫害标记三项改进 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** YOLO 虫害坐标关联无人机实时 GPS，同种+同位置去重，地图 Popup 展示详情+硬删除。

**Architecture:** `PestStore` 新增 `hasNearby()`/`deleteById()`；`onYoloResults()` 取 `m_dronePath.last()` 并去重；`injectPestMarkers()` 注入带 Popup 的 JS；`QWebEnginePage::javaScriptConsoleMessage` 拦截删除事件。

**Tech Stack:** Qt 5, SQLite (qsdb QSQLITE), Leaflet 1.9, QWebEngineView

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `test/pestdata.h` | 数据结构声明 (`PestRecord` + `id`)、`PestStore` 接口新增 |
| `test/pestdata.cpp` | SQLite 层：`queryAll()` 返回 id、`hasNearby()`、`deleteById()` |
| `test/teagardensystem.h` | 声明 `deletePestRecord(int id)` 槽 |
| `test/teagardensystem.cpp` | GPS 坐标关联、去重调用、Popup JS 注入、console 拦截 |
| `test/web/amap_view.html` | **无需改动** |

---

### Task 1: PestStore — 暴露 id、新增 hasNearby() 和 deleteById()

**Files:**
- Modify: `test/pestdata.h`
- Modify: `test/pestdata.cpp`

- [ ] **Step 1: 在 pestdata.h 中给 PestRecord 加 id 字段，PestStore 新增两个方法声明**

在 `PestRecord` 结构体中加 `int id = 0;`，在 `PestStore` 类中加两个公有方法声明：

```cpp
#ifndef PESTDATA_H
#define PESTDATA_H

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QMap>

struct PestRecord {
    int id = 0;             // 数据库自增主键
    QDateTime timestamp;
    double lat = 0;
    double lng = 0;
    QString pestType;       // "茶尺蠖" / "茶小绿叶蝉" / "茶炭疽病" / ...
    int severity = 1;       // 1轻微 2中等 3严重
};

class PestStore {
public:
    static PestStore* instance();

    void init(const QString &dbPath);
    void append(const PestRecord &rec);
    QVector<PestRecord> queryAll();
    int totalCount();
    QMap<QString, int> typeCounts();

    // 新增：查询 0.5m 内是否已有同种类病虫害
    bool hasNearby(const QString &pestType, double lat, double lng, double distanceM = 0.5);
    // 新增：按 id 硬删除
    bool deleteById(int id);

private:
    PestStore() = default;
    QString dbPath;
};

#endif // PESTDATA_H
```

- [ ] **Step 2: 在 pestdata.cpp 中修改 queryAll() 返回 id 列**

`queryAll()` 的 SELECT 语句加上 `id` 列：

```cpp
QVector<PestRecord> PestStore::queryAll() {
    QVector<PestRecord> result;
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return result;
    QSqlQuery q(db);
    q.exec("SELECT id, ts, lat, lng, pest_type, severity FROM pest_log ORDER BY ts DESC");
    while (q.next()) {
        PestRecord r;
        r.id        = q.value(0).toInt();
        r.timestamp = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(q.value(1).toDouble() * 1000.0));
        r.lat       = q.value(2).toDouble();
        r.lng       = q.value(3).toDouble();
        r.pestType  = q.value(4).toString();
        r.severity  = q.value(5).toInt();
        result.append(r);
    }
    return result;
}
```

原来的 SQL 是 `SELECT ts, lat, lng, pest_type, severity`，列索引从 0-4。新 SQL 加 `id` 在前面，`ts` 变成 index 1，以此类推。每个 `q.value(n)` 索引 +1。

- [ ] **Step 3: 实现 hasNearby()**

```cpp
bool PestStore::hasNearby(const QString &pestType, double lat, double lng, double distanceM) {
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    // 1° ≈ 111320m，threshold = (distanceM / 111320)^2
    double threshold = (distanceM / 111320.0) * (distanceM / 111320.0);
    q.prepare("SELECT COUNT(*) FROM pest_log "
              "WHERE pest_type = :type "
              "  AND (lat - :lat)*(lat - :lat) + (lng - :lng)*(lng - :lng) < :th");
    q.bindValue(":type", pestType);
    q.bindValue(":lat", lat);
    q.bindValue(":lng", lng);
    q.bindValue(":th", threshold);
    if (!q.exec()) {
        qWarning() << "PestStore::hasNearby failed:" << q.lastError().text();
        return false;
    }
    if (q.next())
        return q.value(0).toInt() > 0;
    return false;
}
```

- [ ] **Step 4: 实现 deleteById()**

```cpp
bool PestStore::deleteById(int id) {
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("DELETE FROM pest_log WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec()) {
        qWarning() << "PestStore::deleteById failed:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}
```

- [ ] **Step 5: 编译验证**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4
```

确保 `pestdata.o` 编译通过。

- [ ] **Step 6: Commit**

```bash
git add test/pestdata.h test/pestdata.cpp
git commit -m "feat: PestStore 暴露 id + 新增 hasNearby()、deleteById()"
```

---

### Task 2: teagardensystem — GPS 坐标关联 + 去重

**Files:**
- Modify: `test/teagardensystem.cpp`（仅 `onYoloResults()` 方法）

- [ ] **Step 1: 修改 onYoloResults() 中写入 PestStore 的部分**

当前代码（约 2257-2277 行）：

```cpp
// 3. 写入 PestStore（非 Healthy 类别）
for (const DetectionResult &det : results) {
    if (det.className == "Healthy") continue;

    PestRecord rec;
    rec.timestamp = QDateTime::currentDateTime();
    rec.pestType = mapYoloClassToChinese(det.className);
    rec.lat = 25.08;
    rec.lng = 118.232;
    rec.severity = scoreToSeverity(det.score);
    PestStore::instance()->append(rec);
}

// 4. 刷新 UI
if (!results.isEmpty()) {
    injectPestMarkers();
    refreshPestPie();
    if (pestDashboard && pestDashboard->isVisible()) {
        pestDashboard->refresh();
    }
}
```

替换为：

```cpp
    // 3. 写入 PestStore（非 Healthy 类别，取实时 GPS + 去重）
    bool hasNewRecord = false;
    for (const DetectionResult &det : results) {
        if (det.className == "Healthy") continue;

        // GPS 未就绪则跳过
        if (m_dronePath.isEmpty()) continue;

        const QString pestType = mapYoloClassToChinese(det.className);
        const double lat = m_dronePath.last().lat;
        const double lng = m_dronePath.last().lon;

        // 同种类 + 0.5m 内去重
        if (PestStore::instance()->hasNearby(pestType, lat, lng))
            continue;

        PestRecord rec;
        rec.timestamp = QDateTime::currentDateTime();
        rec.pestType  = pestType;
        rec.lat       = lat;
        rec.lng       = lng;
        rec.severity  = scoreToSeverity(det.score);
        PestStore::instance()->append(rec);
        hasNewRecord = true;
    }

    // 4. 刷新 UI（仅当有新记录时）
    if (hasNewRecord) {
        injectPestMarkers();
        refreshPestPie();
        if (pestDashboard && pestDashboard->isVisible()) {
            pestDashboard->refresh();
        }
    }
```

关键变化：
- `rec.lat / rec.lng` 从硬编码改为 `m_dronePath.last().lat / .lon`
- `m_dronePath.isEmpty()` 时跳过写入（无 GPS 定位不记录）
- 调用 `hasNearby(pestType, lat, lng)` 去重
- `hasNewRecord` 标志只在新记录产生时才刷新 UI，避免无意义重绘

- [ ] **Step 2: 编译验证**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4
```

- [ ] **Step 3: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: YOLO 虫害坐标使用无人机实时 GPS + 0.5m 同种去重"
```

---

### Task 3: teagardensystem — JS 注入 Popup + console 拦截删除

**Files:**
- Modify: `test/teagardensystem.h`
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: 在 teagardensystem.h 声明 deletePestRecord 槽**

在"病虫害"区域（约 223-229 行）加一个私有槽声明：

```cpp
    // 病虫害
    void openPestDashboard();
    void injectMockPest();
    void injectPestMarkers();
    void refreshPestPie();
    void deletePestRecord(int id);   // 新增：按 id 删除虫害记录
    class PestDashboard *pestDashboard = nullptr;
    class QtCharts::QChartView *pestPieView = nullptr;
```

- [ ] **Step 2: 重写 injectPestMarkers() — 加 Popup + 删除按钮**

将当前 `injectPestMarkers()` 的 JS 注入逻辑替换为包含 Popup 和 `deletePest()` 函数的版本：

```cpp
void TeaGardenSystem::injectPestMarkers() {
#ifdef TEA_ENABLE_MAP_WEBVIEW
    if (!mapView) return;
    QVector<PestRecord> records = PestStore::instance()->queryAll();
    if (records.isEmpty()) {
        // 无记录时也要清空地图上的旧标记
        QString js = "try{"
                     "if(typeof pestMarkers !== 'undefined'){pestMarkers.forEach(function(m){map.removeLayer(m);});}"
                     "pestMarkers=[];"
                     "}catch(e){}";
        static_cast<QWebEngineView*>(mapView)->page()->runJavaScript(js);
        return;
    }

    QMap<QString, QString> colors;
    colors["茶尺蠖"]    = "#4CAF50";
    colors["茶小绿叶蝉"] = "#FF9800";
    colors["茶炭疽病"]   = "#F44336";
    colors["茶藻斑病"]   = "#2196F3";
    colors["茶灰斑病"]   = "#9E9E9E";
    colors["茶红叶斑"]   = "#E91E63";

    auto severityText = [](int sev) -> QString {
        switch (sev) {
            case 3: return "严重";
            case 2: return "中等";
            default: return "轻微";
        }
    };

    QString js = "try{"
                 // 定义 deletePest 全局函数
                 "window.deletePest=function(id){console.log('PEST_DELETE:'+id);};"
                 // 清除旧标记
                 "if(typeof pestMarkers !== 'undefined'){pestMarkers.forEach(function(m){map.removeLayer(m);});}"
                 "pestMarkers=[];"
                 "if(typeof map==='undefined'||typeof L==='undefined'){console.log('Map or Leaflet not ready');}"
                 "else{";

    for (const auto& r : records) {
        QString clr   = colors.value(r.pestType, "#2196F3");
        QString label = r.pestType;
        label.replace("'", "\\'");
        QString sev   = severityText(r.severity);
        QString time  = r.timestamp.toString("yyyy-MM-dd HH:mm:ss");
        // 用 JS 的 toFixed
        QString latStr = QString::number(r.lat, 'f', 6);
        QString lngStr = QString::number(r.lng, 'f', 6);

        js += QString(
            "var cm=L.circleMarker([%1,%2],{radius:8,color:'%3',fillColor:'%3',fillOpacity:0.7,weight:2});"
            "var popup=L.popup().setContent("
            "'<div style=\"min-width:180px;font:12px Microsoft YaHei,sans-serif;color:#b0d0f0;\">"
            "<b style=\"color:%3;font-size:15px;\">🐛 %4</b><br>"
            "严重程度：%5<br>"
            "检测时间：%6<br>"
            "坐标：%7, %8<br>"
            "<button onclick=\"deletePest(%9)\" style=\"margin-top:8px;width:100%%;padding:5px;"
            "background:#D32F2F;color:white;border:none;border-radius:3px;cursor:pointer;font-size:12px;\">"
            "🗑 删除此标记</button>"
            "</div>');"
            "cm.bindPopup(popup);cm.addTo(map);"
            "pestMarkers.push(cm);"
        ).arg(latStr, lngStr)
         .arg(clr, label, sev, time, latStr, lngStr)
         .arg(r.id);
    }

    js += "}}catch(e){console.log('Pest marker error:'+e.message);}";
    static_cast<QWebEngineView*>(mapView)->page()->runJavaScript(js);
#endif
}
```

关键变化：
- 先注入 `window.deletePest = function(id) { console.log('PEST_DELETE:' + id); }`
- 每个 `circleMarker` 绑定 `L.popup`，内容包含种类/严重度/时间/坐标/删除按钮
- 删除按钮 `onclick="deletePest(id)"` 触发 console.log
- 不再创建 `divIcon` 标签 marker（Popup 内已显示文字信息），图层数从 2N 降到 N
- 无记录时也清空旧标记

- [ ] **Step 3: 在 setupMapView() 中添加 javaScriptConsoleMessage 监听**

找到 `setupMapView()` 中已有的 `renderProcessTerminated` connect（约 766-779 行），在其后追加 console 监听。`setupMapView()` 位于约 735 行，`midPanel->addWidget(mapView, 10)` 之前（约 780 行）。在 `TEA_ENABLE_MAP_WEBVIEW` 块内、`midPanel->addWidget` 之前插入：

```cpp
    // 监听 JS console.log("PEST_DELETE:<id>") 实现地图上删除虫害标记
    connect(static_cast<QWebEngineView*>(mapView)->page(),
            &QWebEnginePage::javaScriptConsoleMessage,
            this, [this](QWebEnginePage::JavaScriptConsoleMessageLevel level,
                         const QString &message, int lineNumber, const QString &sourceID) {
        Q_UNUSED(level); Q_UNUSED(lineNumber); Q_UNUSED(sourceID);
        if (message.startsWith("PEST_DELETE:")) {
            int id = message.mid(QString("PEST_DELETE:").length()).toInt();
            deletePestRecord(id);
        }
    });
```

`QWebEnginePage::javaScriptConsoleMessage` 是 Qt 5 的信号签名。注意需要确认头文件包含——`#include <QWebEnginePage>` 或通过 `QWebEngineView` 间接可用。当前代码中 `QWebEnginePage` 已在 `renderProcessTerminated` connect 中使用，无需加头文件。

- [ ] **Step 4: 实现 deletePestRecord(int id)**

在 `teagardensystem.cpp` 末尾（约 2277 行 `onYoloResults` 之后，或找个合适位置）添加：

```cpp
void TeaGardenSystem::deletePestRecord(int id) {
    bool ok = PestStore::instance()->deleteById(id);
    if (ok) {
        // 地图移除旧标记并重建（已删除的不会出现在 queryAll 结果中）
        injectPestMarkers();
        // 刷新饼图
        refreshPestPie();
        // 刷新病虫害面板（如果开着）
        if (pestDashboard && pestDashboard->isVisible())
            pestDashboard->refresh();
        // 上传更新后的汇总到 IoT
        IotUploader::instance()->uploadPest();
    }
}
```

- [ ] **Step 5: 确认 #include 完整**

检查 `teagardensystem.cpp` 顶部已包含 `QWebEnginePage` 头文件。当前代码在 `renderProcessTerminated` 信号连接中使用了 `QWebEnginePage`，说明该类型已可见。如未显式 include，加一行：

```cpp
#include <QWebEnginePage>
```

同时确认已包含 `pestdata.h` 和 `iotuploader.h`（代码中已有）。

- [ ] **Step 6: 编译验证**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4
```

- [ ] **Step 7: Commit**

```bash
git add test/teagardensystem.h test/teagardensystem.cpp
git commit -m "feat: 病虫害地图 Popup 详情 + 硬删除 + console 拦截"
```

---

### Task 4: 端到端验证

**Files:** 无需修改

- [ ] **Step 1: 验证编译**

```bash
cd /home/lzy/文档/QTproject/test && qmake && make -j4
```
Expected: 编译通过，无错误。

- [ ] **Step 2: 快速逻辑验证（如有 ELF2 板子）**

在板上启动程序后：
1. 点击"🐛 模拟检测" → 地图出现标记，坐标在 `m_dronePath.last()` 附近
2. 点击同一位置再次模拟检测 → 不应生成同种类的新标记（去重生效）
3. 点击地图圆点 → Popup 弹出，显示种类、严重度、时间、坐标
4. 点击 Popup 中"删除" → 标记消失、饼图更新
5. 无 GPS 数据时（`m_dronePath` 为空）→ 模拟检测不写库

- [ ] **Step 3: Commit 最终验证状态**

```bash
git add -A
git commit -m "verify: 病虫害标记三项改进端到端验证通过"
```
