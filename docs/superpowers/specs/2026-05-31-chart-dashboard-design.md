# 数据折线看板 — 设计规格

**日期**: 2026-05-31  
**状态**: 待实施  
**依赖**: Qt5 Charts, Qt5 SQL (SQLite)

**注意**: 新增 `.gitignore` 例外 — `*.ini` 中有 `thresholds.ini` 需要提交到 git。修改 `.gitignore` 把 `*.ini` 限制为 `amap.ini`。

---

## 1. 背景与目标

当前系统（TeaGardenSystem）只展示最新一条传感器数值，不存储历史数据。需要新增：

- **历史数据折线图** — 8 项传感器各自一张折线图，直观展示趋势
- **时间范围选择** — 预设 (1h/6h/24h/7d) + 自定义起止时间
- **阈值警告** — 每项传感器可设独立阈值，超限数据点红色标记
- **数据持久化** — SQLite 存储，重启不丢，可回溯任意时间段

---

## 2. 新增文件

### 2.1 `test/datastore.h/.cpp` — 数据存储层

**共用数据结构** (定义在 `datastore.h`):
```cpp
struct SensorSnapshot {
    QDateTime timestamp;
    double windSpeed = NAN;
    double airTemp = NAN;
    double airHumidity = NAN;
    double lightLux = NAN;
    double soilHumidity = NAN;
    double soilTemp = NAN;
    double soilConductivity = NAN;
    double soilPh = NAN;
};
```

**职责**: SQLite 读写，单例模式，与 UI 完全解耦

```
class DataStore {
public:
    static DataStore* instance();
    void init(const QString &dbPath);        // 建表
    void append(const SensorSnapshot &snap); // 写入一条数据
    QVector<SensorSnapshot> query(QDateTime from, QDateTime to); // 按时间范围查
};
```

**表结构**:
```sql
CREATE TABLE IF NOT EXISTS sensor_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts REAL NOT NULL,        -- QDateTime::toMSecsSinceEpoch() / 1000.0
    wind_speed REAL,
    air_temp REAL,
    air_humidity REAL,
    light_lux REAL,
    soil_humidity REAL,
    soil_temp REAL,
    soil_conductivity REAL,
    soil_ph REAL
);
CREATE INDEX IF NOT EXISTS idx_ts ON sensor_log(ts);
```

**关键取舍**:
- 存 `sensor_log` 一张宽表，不是按传感器分 8 张表 — 一次写入一个时间点的 8 个传感器值，查询一次拉全部，比 JOIN 快
- `ts` 用 Unix 秒（REAL），不用 TEXT — SQLite 范围查询效率
- 不设自动清理，简单靠磁盘；如果未来数据量过大再加定期清理（>30 天的删除）
- `DataStore` 用单例不是 QObject — 避免信号槽开销，点对点直接调用

### 2.2 `test/chartdashboard.h/.cpp` — 折线看板窗口

**职责**: 独立 QWidget 窗口，8 张可折叠折线图 + 时间工具栏 + 阈值处理

```
class ChartDashboard : public QWidget {
    Q_OBJECT
public:
    explicit ChartDashboard(QWidget *parent = nullptr);
    void reapplyThresholds();  // 外部调用：阈值变更后重绘
private:
    void setupUI();
    void loadData(QDateTime from, QDateTime to);
    QWidget* createChartCard(const QString &title, const QString &unit, int sensorIndex);
    QChartView* createChart(QString title, QString unit);
    void exportExcel();       // 导出当前时间范围数据为 .xlsx（通过 Python openpyxl）

    QScrollArea *scrollArea;
    QDateTimeEdit *fromTime, *toTime;
    QPushButton *thresholdBtn, *exportBtn;
    QMap<int, QChartView*> charts;  // sensorIndex → chart
    QList<QWidget*> chartCards;     // 卡片容器，用于折叠/展开
};
```

**关键取舍**:
- 8 张 QChartView 全生成，用 `setVisible()` 控制折叠/展开；不动态创建销毁 — 避免反复 new/delete 的开销和闪烁
- 超阈值点用 `QScatterSeries` 叠在折线图上方，红色 + 放大半径 — 不用 `QLineSeries` 逐点换色（Qt Charts 不支持一条线里不同颜色的点）
- 阈值线用 `QLineSeries` 画水平虚线（`setPen(QPen(..., Qt::DashLine))`）
- 工具栏固定在顶部不随滚动 — `QVBoxLayout(topToolbar + scrollArea)`
- 每张图高度 200px，收起后只显示标题栏 32px

### 2.3 `test/thresholddialog.h/.cpp` — 阈值设置弹窗

**职责**: QDialog 弹窗，表格编辑 8 项阈值，保存到 QSettings

```
class ThresholdDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThresholdDialog(QWidget *parent = nullptr);
    struct Thresholds { double upper, lower; };
    static QMap<int, Thresholds> load();      // 从 QSettings 读
    static void save(const QMap<int, Thresholds> &t);  // 写回 QSettings
signals:
    void thresholdsChanged();
private:
    // QFormLayout + QDoubleSpinBox × 16
};
```

### 2.5 阈值超限通知

**方案**: 主窗口添加 `QSystemTrayIcon`，阈值为触发时桌面弹泡。

```
TeaGardenSystem::onLoraData() 中:
  curValue > threshold.upper || curValue < threshold.lower
    → trayIcon->showMessage("⚠ 阈值告警",
          "空气温度 38.5℃ 超过上限 35℃",
          QSystemTrayIcon::Warning, 5000)
```

**关键取舍**:
- 使用 `QSystemTrayIcon`（Qt5 Widgets 内置），不需要额外依赖
- 同一个传感器连续超限时 60 秒静默期，避免每 3 秒弹一次刷屏
- 系统托盘图标常驻，右键菜单：显示主窗口 / 打开看板 / 退出
- 用户可在主窗口设置中关闭通知（checkbox: "阈值超限通知"）

### 2.6 Excel 导出

**方案**: 生成 `.xlsx` 文件，通过 Python `openpyxl` 库。

```
ChartDashboard::exportExcel():
  1. 弹出 QFileDialog 选择保存路径
  2. DataStore::query(from, to) 拿数据
  3. 生成临时 Python 脚本（openpyxl create workbook）
  4. QProcess 调用 python3 执行
  5. 完成提示: "已导出到 xxx.xlsx"
```

**关键取舍**:
- 用 Python openpyxl 而非 C++ xlsx 库 — 项目已有 Python 依赖（语音识别），不加新 C++ 依赖
- 生成的 xlsx 包含：表头行（时间 + 8 列传感器名）、数据行、冻结首行、自动列宽
- Python 找不到 openpyxl 时自动安装：`pip3 install openpyxl`
- 同时保留 CSV 文本导出作为命令行备选（`exportCSV()` 内部方法）
```

**关键取舍**:
- 阈值存在 QSettings 而非 SQLite — 阈值是配置不是数据，跟数据库文件分开更清晰；换机/清数据不影响阈值
- 弹窗 OK 后立即生效，调用 `ChartDashboard::reapplyThresholds()`
- QSettings 用 `QSettings::IniFormat` + 当前目录，跟 `amap.ini` 模式一致

### 2.4 `test/thresholds.ini` — 默认阈值配置（示例）

```ini
[thresholds]
air_temp_upper=35
air_temp_lower=0
air_humidity_upper=90
air_humidity_lower=20
light_lux_upper=80000
light_lux_lower=0
soil_ph_upper=7.5
soil_ph_lower=4.5
wind_speed_upper=20
wind_speed_lower=0
soil_temp_upper=40
soil_temp_lower=0
soil_humidity_upper=85
soil_humidity_lower=15
soil_conductivity_upper=5
soil_conductivity_lower=0
```

---

## 3. 修改现有文件

### 3.1 `test/teagardensystem.h/.cpp`

- 成员新增:
  - `ChartDashboard *chartDashboard = nullptr;`
  - `QSystemTrayIcon *trayIcon = nullptr;`
  - `QMap<int, QDateTime> lastAlertTime;` — 60 秒静默期记录
- `setupUI()` 中左栏底部加按钮 `📊 数据看板`；初始化系统托盘图标
- 新增 slot `void openChartDashboard();` — 首次点击 new ChartDashboard + show，后续 `.show()` + `.raise()`
- `onLoraData()` 中:
  - 每次收到数据调用 `DataStore::instance()->append(...)` 写入 SQLite
  - 检查阈值 → 超限且距上次通知 >60s → `trayIcon->showMessage(...)`
- 非 LoRa 模拟数据（refreshData 中）也写入 SQLite
- 析构前关闭 ChartDashboard

### 3.2 `test/test.pro`

```diff
-QT += core gui serialport network
+QT += core gui serialport network charts sql
+
+SOURCES += chartdashboard.cpp datastore.cpp thresholddialog.cpp
+HEADERS += chartdashboard.h datastore.h thresholddialog.h
```

---

## 4. 数据流

```
LoRa 串口 → LoraManager::parseTextLine()
  → TeaGardenSystem::onLoraData()
      ├→ 更新 UI 数据卡片 (现有逻辑不变)
      ├→ DataStore::append(snapshot)  ← 新：写入 SQLite
      └→ 检查阈值 → 超限?(距上次>60s)? → trayIcon->showMessage() ← 新：桌面通知

用户点 📊 按钮
  → TeaGardenSystem::openChartDashboard()
      → new ChartDashboard (窗口)
          └→ 构造中 loadData(最近1小时)

用户切换时间范围 (点击 6h / 自定义)
  → loadData(from, to)
      → DataStore::query(from, to)
          ← QVector<SensorSnapshot>
      → 遍历 8 个 QChartView
          → series->clear() + 重建 QLineSeries + QScatterSeries
          → 超阈值点加入 QScatterSeries (红色)
          → 添加阈值虚线 QLineSeries
          → 标注 "⚠ 超限 N 处"

用户打开阈值弹窗
  → ThresholdDialog → 编辑 → 保存到 QSettings
      → emit thresholdsChanged()
          → ChartDashboard::reapplyThresholds() → 重绘
```

---

## 5. 数据采样策略

问题：每 3 秒一次 LoRa 数据（如果 LoRa 离线则是模拟数据），1小时=1200条，7天=201600条。7天的 201600 点直接画折线图会卡。

**方案**: `query()` 内做降采样
- ≤ 1000 条 → 全部返回
- > 1000 条 → SQL 查时按 N 秒间隔取平均值

```sql
-- 例如 7 天范围内取 ~500 个采样点
SELECT ts, AVG(air_temp), AVG(air_humidity), ...
FROM sensor_log
WHERE ts BETWEEN :from AND :to
GROUP BY CAST((ts - :from) / :bucket_seconds AS INTEGER)
ORDER BY ts
```

**bucket_seconds**: `(to - from) / 500`，保证无论什么时间范围都 ~500 个点。

---

## 6. 资源清理

- ChartDashboard 不随主窗口关闭而自动删除 — 需要用户手动关
- 主窗口析构时 if(chartDashboard) { chartDashboard->close(); delete chartDashboard; }
- DataStore 单例在 `QApplication::aboutToQuit()` 中销毁

---

## 7. 验证计划

```bash
# 1. 编译
cd build-desktop-Debug
qmake ../test/test.pro && make -j$(nproc)

# 2. 运行
./test
# - 点 📊 按钮 → 看板窗口弹出
# - 切换时间预设按钮 → 图表刷新
# - 折叠/展开某张图
# - 打开阈值弹窗 → 修改阈值 → 观察图中红线标记
# - 导出 Excel → 检查 .xlsx 文件内容
# - 阈值超限 → 观察桌面通知弹泡（温度 > 35℃ 时触发）

# 3. 数据持久化试验
# - 运行 30 秒 → 关闭程序
# - 重新运行 → 打开看板 → 选择前后时间段 → 确认历史数据还在
```

---

## 8. 边界情况

| 场景 | 处理 |
|------|------|
| SQLite 文件不存在（首次启动） | 自动建库建表 |
| 时间范围内无数据 | 显示空图表，"暂无数据" 文字 |
| LoRa 断连期间 refreshData 模拟数据 | 也写入 SQLite，统一处理 |
| 传感器偶尔缺失字段 (NaN) | 不写入 SQLite 该字段（NULL），图表跳过该点 |
| 窗口最小化 / 关闭态 | show()+raise() 恢复 |
| 阈值弹窗取消 | 不保存，恢复原值 |
| 同一传感器连续超限 | 60 秒通知静默期，避免刷屏 |
| Python openpyxl 未安装 | 自动 pip3 install openpyxl |
| 导出时数据为空 | 提示"当前时间范围内无数据，无法导出" |

---

## 9. 不在本次范围

- 不在这里做数据清理定时任务（>30天删除），先靠磁盘
