# 数据折线看板 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为主界面新增数据折线看板功能——8 张可折叠传感器折线图、SQLite 持久化、阈值告警弹泡通知、Excel 导出。

**Architecture:** 新建 4 个文件（DataStore 单例、ThresholdDialog 弹窗、ChartDashboard 看板窗口、thresholds.ini 配置），修改 2 个文件（TeaGardenSystem 加按钮/托盘/写入、test.pro 加 charts sql 依赖）。DataStore 与 UI 完全解耦，通过 SensorSnapshot 结构体通信。

**Tech Stack:** Qt5 C++17, Qt Charts, Qt SQL (SQLite), QSystemTrayIcon, Python3 openpyxl

---

## File Structure

```
test/
├── datastore.h            ← NEW: SensorSnapshot struct + DataStore 单例声明
├── datastore.cpp          ← NEW: SQLite 建表/插入/降采样查询实现
├── thresholddialog.h      ← NEW: 阈值编辑弹窗声明
├── thresholddialog.cpp    ← NEW: 16 个 QDoubleSpinBox + QSettings 读写
├── chartdashboard.h       ← NEW: 折线看板窗口声明
├── chartdashboard.cpp     ← NEW: 8 张图 + 时间栏 + 导出 + 折叠
├── thresholds.ini         ← NEW: 默认阈值配置文件
├── teagardensystem.h      ← MODIFY: +3 成员 +1 slot
├── teagardensystem.cpp    ← MODIFY: +数据存储/托盘/阈值检查逻辑
└── test.pro               ← MODIFY: QT += charts sql, 新源文件
```

---

### Task 1: DataStore — 数据存储层

**Files:**
- Create: `test/datastore.h`
- Create: `test/datastore.cpp`

- [ ] **Step 1: 写 datastore.h**

```cpp
#ifndef DATASTORE_H
#define DATASTORE_H

#include <QDateTime>
#include <QVector>
#include <QString>
#include <cmath>

struct SensorSnapshot {
    QDateTime timestamp;
    double windSpeed       = NAN;
    double airTemp         = NAN;
    double airHumidity     = NAN;
    double lightLux        = NAN;
    double soilHumidity    = NAN;
    double soilTemp        = NAN;
    double soilConductivity = NAN;
    double soilPh          = NAN;
};

class DataStore {
public:
    static DataStore* instance();

    void init(const QString &dbPath);
    void append(const SensorSnapshot &snap);
    QVector<SensorSnapshot> query(QDateTime from, QDateTime to);

private:
    DataStore() = default;
    QString dbPath;
};

#endif // DATASTORE_H
```

- [ ] **Step 2: 写 datastore.cpp**

```cpp
#include "datastore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

DataStore* DataStore::instance() {
    static DataStore store;
    return &store;
}

void DataStore::init(const QString &path) {
    dbPath = path;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "sensor_conn");
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "DataStore: cannot open" << dbPath << db.lastError().text();
        return;
    }

    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS sensor_log ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "ts REAL NOT NULL,"
           "wind_speed REAL,"
           "air_temp REAL,"
           "air_humidity REAL,"
           "light_lux REAL,"
           "soil_humidity REAL,"
           "soil_temp REAL,"
           "soil_conductivity REAL,"
           "soil_ph REAL)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_ts ON sensor_log(ts)");
}

void DataStore::append(const SensorSnapshot &snap) {
    QSqlDatabase db = QSqlDatabase::database("sensor_conn");
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare("INSERT INTO sensor_log (ts, wind_speed, air_temp, air_humidity, "
              "light_lux, soil_humidity, soil_temp, soil_conductivity, soil_ph) "
              "VALUES (:ts, :ws, :at, :ah, :ll, :sh, :st, :sc, :sp)");
    q.bindValue(":ts", snap.timestamp.toMSecsSinceEpoch() / 1000.0);

    auto bindD = [&](const char *name, double val) {
        q.bindValue(name, std::isfinite(val) ? val : QVariant());
    };
    bindD(":ws", snap.windSpeed);
    bindD(":at", snap.airTemp);
    bindD(":ah", snap.airHumidity);
    bindD(":ll", snap.lightLux);
    bindD(":sh", snap.soilHumidity);
    bindD(":st", snap.soilTemp);
    bindD(":sc", snap.soilConductivity);
    bindD(":sp", snap.soilPh);

    if (!q.exec()) {
        qWarning() << "DataStore::append failed:" << q.lastError().text();
    }
}

QVector<SensorSnapshot> DataStore::query(QDateTime from, QDateTime to) {
    QVector<SensorSnapshot> result;
    QSqlDatabase db = QSqlDatabase::database("sensor_conn");
    if (!db.isOpen()) return result;

    double fromSec = from.toMSecsSinceEpoch() / 1000.0;
    double toSec   = to.toMSecsSinceEpoch() / 1000.0;
    double rangeSec = toSec - fromSec;
    if (rangeSec <= 0) return result;

    // 估算数据行数 (每 3 秒一条) → 决定是否降采样
    double estimatedRows = rangeSec / 3.0;
    if (estimatedRows <= 1000.0) {
        // 全量查询
        QSqlQuery q(db);
        q.prepare("SELECT ts, wind_speed, air_temp, air_humidity, light_lux, "
                  "soil_humidity, soil_temp, soil_conductivity, soil_ph "
                  "FROM sensor_log WHERE ts BETWEEN :from AND :to ORDER BY ts");
        q.bindValue(":from", fromSec);
        q.bindValue(":to", toSec);
        q.exec();
        while (q.next()) {
            SensorSnapshot s;
            s.timestamp = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(q.value(0).toDouble() * 1000.0));
            auto readD = [&](int col) -> double {
                return q.value(col).isNull() ? NAN : q.value(col).toDouble();
            };
            s.windSpeed = readD(1);
            s.airTemp = readD(2);
            s.airHumidity = readD(3);
            s.lightLux = readD(4);
            s.soilHumidity = readD(5);
            s.soilTemp = readD(6);
            s.soilConductivity = readD(7);
            s.soilPh = readD(8);
            result.append(s);
        }
    } else {
        // 降采样：目标 ~500 个桶
        double bucket = rangeSec / 500.0;
        QSqlQuery q(db);
        q.prepare("SELECT AVG(ts), AVG(wind_speed), AVG(air_temp), "
                  "AVG(air_humidity), AVG(light_lux), AVG(soil_humidity), "
                  "AVG(soil_temp), AVG(soil_conductivity), AVG(soil_ph) "
                  "FROM sensor_log WHERE ts BETWEEN :from AND :to "
                  "GROUP BY CAST((ts - :from) / :bucket AS INTEGER) "
                  "ORDER BY AVG(ts)");
        q.bindValue(":from", fromSec);
        q.bindValue(":to", toSec);
        q.bindValue(":bucket", bucket);
        q.exec();
        while (q.next()) {
            SensorSnapshot s;
            s.timestamp = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(q.value(0).toDouble() * 1000.0));
            auto readD = [&](int col) -> double {
                return q.value(col).isNull() ? NAN : q.value(col).toDouble();
            };
            s.windSpeed = readD(1);
            s.airTemp = readD(2);
            s.airHumidity = readD(3);
            s.lightLux = readD(4);
            s.soilHumidity = readD(5);
            s.soilTemp = readD(6);
            s.soilConductivity = readD(7);
            s.soilPh = readD(8);
            result.append(s);
        }
    }
    return result;
}
```

- [ ] **Step 3: 编译验证**

```bash
cd /home/lzy/文档/QTproject/build-desktop-Debug
# 先不加入 .pro 编译，检查语法
g++ -std=c++17 -fsyntax-only -I/usr/include/x86_64-linux-gnu/qt5 \
  -I/usr/include/x86_64-linux-gnu/qt5/QtCore \
  -I/usr/include/x86_64-linux-gnu/qt5/QtSql \
  -fPIC ../test/datastore.cpp 2>&1
```

Expected: 无错误输出（语法检查通过）

- [ ] **Step 4: 提交**

```bash
git add test/datastore.h test/datastore.cpp && git commit -m "feat: DataStore SQLite 数据存储层" && git push
```

---

### Task 2: ThresholdDialog — 阈值设置弹窗

**Files:**
- Create: `test/thresholddialog.h`
- Create: `test/thresholddialog.cpp`

- [ ] **Step 1: 写 thresholddialog.h**

```cpp
#ifndef THRESHOLDDIALOG_H
#define THRESHOLDDIALOG_H

#include <QDialog>
#include <QMap>

class QDoubleSpinBox;

class ThresholdDialog : public QDialog {
    Q_OBJECT
public:
    struct Thresholds {
        double upper = 9999;
        double lower = -9999;
    };

    explicit ThresholdDialog(QWidget *parent = nullptr);

    static QMap<int, Thresholds> load();          // sensorIndex → Thresholds
    static void save(const QMap<int, Thresholds> &t);

signals:
    void thresholdsChanged();

private:
    QMap<int, QDoubleSpinBox*> upperSpins;
    QMap<int, QDoubleSpinBox*> lowerSpins;
};

#endif // THRESHOLDDIALOG_H
```

- [ ] **Step 2: 写 thresholddialog.cpp**

```cpp
#include "thresholddialog.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include <QCoreApplication>

ThresholdDialog::ThresholdDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("阈值设置");
    setMinimumWidth(400);
    setStyleSheet("background: #0d1f2d; color: #E0E0E0;");

    auto *mainLayout = new QVBoxLayout(this);

    QLabel *title = new QLabel("传感器阈值上下限");
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #00E5FF;");
    mainLayout->addWidget(title);

    QStringList names = {
        "空气温度 (℃)", "空气湿度 (%)", "光照强度 (Lux)", "土壤 pH",
        "风速 (m/s)", "土壤温度 (℃)", "土壤湿度 (%)", "土壤电导率 (mS/cm)"
    };

    QMap<int, Thresholds> current = load();

    auto *form = new QFormLayout();
    for (int i = 0; i < names.size(); ++i) {
        auto *upper = new QDoubleSpinBox();
        upper->setRange(-9999, 9999);
        upper->setDecimals(1);
        upper->setValue(current[i].upper);
        upper->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455;");
        upperSpins[i] = upper;

        auto *lower = new QDoubleSpinBox();
        lower->setRange(-9999, 9999);
        lower->setDecimals(1);
        lower->setValue(current[i].lower);
        lower->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455;");
        lowerSpins[i] = lower;

        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(names[i]));
        row->addWidget(new QLabel("上限"));
        row->addWidget(upper);
        row->addWidget(new QLabel("下限"));
        row->addWidget(lower);
        form->addRow(row);
    }
    mainLayout->addLayout(form);

    auto *btnRow = new QHBoxLayout();
    auto *resetBtn = new QPushButton("恢复默认");
    resetBtn->setStyleSheet("background: #455A64; color: white; padding: 6px 16px;");
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        // 默认值
        double defaults[8][2] = {
            {35, 0}, {90, 20}, {80000, 0}, {7.5, 4.5},
            {20, 0}, {40, 0}, {85, 15}, {5, 0}
        };
        for (int i = 0; i < 8; ++i) {
            upperSpins[i]->setValue(defaults[i][0]);
            lowerSpins[i]->setValue(defaults[i][1]);
        }
    });

    auto *saveBtn = new QPushButton("保存");
    saveBtn->setStyleSheet("background: #00897B; color: white; padding: 6px 16px; font-weight: bold;");
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        QMap<int, Thresholds> t;
        for (int i = 0; i < 8; ++i) {
            Thresholds th;
            th.upper = upperSpins[i]->value();
            th.lower = lowerSpins[i]->value();
            t[i] = th;
        }
        save(t);
        emit thresholdsChanged();
        accept();
    });

    auto *cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet("background: #607D8B; color: white; padding: 6px 16px;");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    mainLayout->addLayout(btnRow);
}

QMap<int, ThresholdDialog::Thresholds> ThresholdDialog::load() {
    QMap<int, Thresholds> t;
    const QString iniPath = QCoreApplication::applicationDirPath() + "/thresholds.ini";
    QSettings settings(iniPath, QSettings::IniFormat);

    QStringList keys = {"air_temp", "air_humidity", "light_lux", "soil_ph",
                        "wind_speed", "soil_temp", "soil_humidity", "soil_conductivity"};
    double defaults[8][2] = {
        {35, 0}, {90, 20}, {80000, 0}, {7.5, 4.5},
        {20, 0}, {40, 0}, {85, 15}, {5, 0}
    };
    for (int i = 0; i < keys.size(); ++i) {
        Thresholds th;
        th.upper = settings.value("thresholds/" + keys[i] + "_upper", defaults[i][0]).toDouble();
        th.lower = settings.value("thresholds/" + keys[i] + "_lower", defaults[i][1]).toDouble();
        t[i] = th;
    }
    return t;
}

void ThresholdDialog::save(const QMap<int, Thresholds> &t) {
    const QString iniPath = QCoreApplication::applicationDirPath() + "/thresholds.ini";
    QSettings settings(iniPath, QSettings::IniFormat);

    QStringList keys = {"air_temp", "air_humidity", "light_lux", "soil_ph",
                        "wind_speed", "soil_temp", "soil_humidity", "soil_conductivity"};
    for (int i = 0; i < keys.size(); ++i) {
        settings.setValue("thresholds/" + keys[i] + "_upper", t[i].upper);
        settings.setValue("thresholds/" + keys[i] + "_lower", t[i].lower);
    }
}
```

- [ ] **Step 3: 语法检查**

```bash
cd /home/lzy/文档/QTproject/build-desktop-Debug
g++ -std=c++17 -fsyntax-only -I/usr/include/x86_64-linux-gnu/qt5 \
  -I/usr/include/x86_64-linux-gnu/qt5/QtCore \
  -I/usr/include/x86_64-linux-gnu/qt5/QtWidgets \
  -fPIC ../test/thresholddialog.cpp 2>&1
```

Expected: 无错误

- [ ] **Step 4: 提交**

```bash
git add test/thresholddialog.h test/thresholddialog.cpp && git commit -m "feat: ThresholdDialog 阈值设置弹窗" && git push
```

---

### Task 3: 阈值默认配置文件

**Files:**
- Create: `test/thresholds.ini`

- [ ] **Step 1: 写 thresholds.ini**

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

- [ ] **Step 2: 修正 .gitignore**

`.gitignore` 中当前 `*.ini` 的规则改为只排除 `amap.ini`：

把 `.gitignore` 第 25 行 `amap.ini` 保持不变（已经是精确匹配，不用改），确认 `thresholds.ini` 不会被排除：

```bash
cd /home/lzy/文档/QTproject && git check-ignore -v test/thresholds.ini 2>&1
```

Expected: 空输出（未被忽略）

- [ ] **Step 3: 提交**

```bash
git add test/thresholds.ini && git commit -m "config: 添加阈值默认配置文件 thresholds.ini" && git push
```

---

### Task 4: ChartDashboard — 折线看板窗口

**Files:**
- Create: `test/chartdashboard.h`
- Create: `test/chartdashboard.cpp`

- [ ] **Step 1: 写 chartdashboard.h**

```cpp
#ifndef CHARTDASHBOARD_H
#define CHARTDASHBOARD_H

#include <QWidget>
#include <QMap>
#include <QDateTime>

class QScrollArea;
class QDateTimeEdit;
class QPushButton;
class QChartView;
class ThresholdDialog;

class ChartDashboard : public QWidget {
    Q_OBJECT
public:
    explicit ChartDashboard(QWidget *parent = nullptr);
    void reapplyThresholds();

private slots:
    void onPreset1h();
    void onPreset6h();
    void onPreset24h();
    void onPreset7d();
    void onCustomQuery();
    void onOpenThresholds();
    void onExportExcel();

private:
    void setupUI();
    void loadData(QDateTime from, QDateTime to);
    QWidget* createChartCard(const QString &title, const QString &unit, int sensorIndex);

    QScrollArea *scrollArea;
    QDateTimeEdit *fromTime, *toTime;
    QPushButton *thresholdBtn, *exportBtn;
    QMap<int, QChartView*> charts;    // sensorIndex → chart
    QMap<int, QWidget*> chartCards;   // sensorIndex → 卡片容器
};

#endif // CHARTDASHBOARD_H
```

- [ ] **Step 2: 写 chartdashboard.cpp（上：构造 + UI）**

```cpp
#include "chartdashboard.h"
#include "datastore.h"
#include "thresholddialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDateTimeEdit>
#include <QScrollArea>
#include <QChartView>
#include <QChart>
#include <QLineSeries>
#include <QScatterSeries>
#include <QDateTimeAxis>
#include <QValueAxis>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QtMath>

using namespace QtCharts;

ChartDashboard::ChartDashboard(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("数据折线看板");
    resize(800, 700);
    setStyleSheet("background-color: #051623; color: #E0E0E0; font-family: 'Microsoft YaHei';");
    setupUI();
    loadData(QDateTime::currentDateTime().addSecs(-3600), QDateTime::currentDateTime());
}

void ChartDashboard::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);

    // ===== 顶部工具栏 =====
    auto *toolbar = new QHBoxLayout();

    auto *btn1h  = new QPushButton("1小时");
    auto *btn6h  = new QPushButton("6小时");
    auto *btn24h = new QPushButton("24小时");
    auto *btn7d  = new QPushButton("7天");
    for (auto *b : {btn1h, btn6h, btn24h, btn7d}) {
        b->setStyleSheet("background: #1a2a3a; color: #E0E0E0; padding: 6px 12px; "
                         "border: 1px solid #334455; border-radius: 4px;");
    }
    connect(btn1h,  &QPushButton::clicked, this, &ChartDashboard::onPreset1h);
    connect(btn6h,  &QPushButton::clicked, this, &ChartDashboard::onPreset6h);
    connect(btn24h, &QPushButton::clicked, this, &ChartDashboard::onPreset24h);
    connect(btn7d,  &QPushButton::clicked, this, &ChartDashboard::onPreset7d);
    toolbar->addWidget(btn1h);
    toolbar->addWidget(btn6h);
    toolbar->addWidget(btn24h);
    toolbar->addWidget(btn7d);
    toolbar->addSpacing(12);

    toolbar->addWidget(new QLabel("自"));
    fromTime = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(-3600));
    fromTime->setDisplayFormat("yyyy-MM-dd hh:mm");
    fromTime->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455;");
    toolbar->addWidget(fromTime);

    toolbar->addWidget(new QLabel("至"));
    toTime = new QDateTimeEdit(QDateTime::currentDateTime());
    toTime->setDisplayFormat("yyyy-MM-dd hh:mm");
    toTime->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455;");
    toolbar->addWidget(toTime);

    auto *queryBtn = new QPushButton("查询");
    queryBtn->setStyleSheet("background: #00897B; color: white; padding: 6px 12px; "
                            "border-radius: 4px; font-weight: bold;");
    connect(queryBtn, &QPushButton::clicked, this, &ChartDashboard::onCustomQuery);
    toolbar->addWidget(queryBtn);

    toolbar->addStretch();

    thresholdBtn = new QPushButton("⚙ 阈值设置");
    thresholdBtn->setStyleSheet("background: #455A64; color: white; padding: 6px 12px; "
                                "border: 1px solid #334455; border-radius: 4px;");
    connect(thresholdBtn, &QPushButton::clicked, this, &ChartDashboard::onOpenThresholds);
    toolbar->addWidget(thresholdBtn);

    exportBtn = new QPushButton("📤 导出Excel");
    exportBtn->setStyleSheet("background: #2E7D32; color: white; padding: 6px 12px; "
                             "border-radius: 4px; font-weight: bold;");
    connect(exportBtn, &QPushButton::clicked, this, &ChartDashboard::onExportExcel);
    toolbar->addWidget(exportBtn);

    mainLayout->addLayout(toolbar);

    // ===== 图表滚动区 =====
    scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");
    auto *scrollContent = new QWidget();
    auto *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setSpacing(6);

    QStringList titles = {"空气温度", "空气湿度", "光照强度", "土壤 pH",
                          "风速", "土壤温度", "土壤湿度", "土壤电导率"};
    QStringList units  = {"℃", "%", "Lux", "", "m/s", "℃", "%", "mS/cm"};
    for (int i = 0; i < titles.size(); ++i) {
        QWidget *card = createChartCard(titles[i], units[i], i);
        scrollLayout->addWidget(card);
        chartCards[i] = card;
    }
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);
}
```

- [ ] **Step 3: 写 chartdashboard.cpp（中：createChartCard + loadData）**

```cpp
QWidget* ChartDashboard::createChartCard(const QString &title, const QString &unit, int sensorIndex) {
    auto *card = new QWidget();
    card->setStyleSheet("background: rgba(0,0,0,0.3); border: 1px solid #1a2a3a; border-radius: 6px;");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(8, 4, 8, 4);

    // 标题栏（可点击折叠）
    auto *header = new QPushButton("📈 " + title + "  ▼");
    header->setStyleSheet("QPushButton { background: transparent; color: #00E5FF; "
                          "font-size: 13px; font-weight: bold; text-align: left; "
                          "border: none; padding: 4px 0; }");
    header->setCursor(Qt::PointingHandCursor);

    // 图表
    auto *chart = new QChart();
    chart->setTitle("");
    chart->setBackgroundBrush(QBrush(QColor("#051623")));
    chart->legend()->hide();

    auto *axisX = new QDateTimeAxis();
    axisX->setFormat("HH:mm");
    axisX->setLabelsColor(QColor("#90A4AE"));
    axisX->setGridLineColor(QColor("#1a2a3a"));
    chart->addAxis(axisX, Qt::AlignBottom);

    auto *axisY = new QValueAxis();
    axisY->setLabelsColor(QColor("#90A4AE"));
    axisY->setGridLineColor(QColor("#1a2a3a"));
    chart->addAxis(axisY, Qt::AlignLeft);

    auto *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(200);
    chartView->setStyleSheet("background: transparent; border: none;");
    charts[sensorIndex] = chartView;

    // 折叠/展开
    connect(header, &QPushButton::clicked, this, [chartView, header]() {
        bool vis = !chartView->isVisible();
        chartView->setVisible(vis);
        header->setText(header->text().replace(vis ? "▶" : "▼", vis ? "▼" : "▶"));
    });

    cardLayout->addWidget(header);
    cardLayout->addWidget(chartView);
    return card;
}

void ChartDashboard::loadData(QDateTime from, QDateTime to) {
    if (!from.isValid() || !to.isValid() || from >= to) return;

    QVector<SensorSnapshot> data = DataStore::instance()->query(from, to);

    QMap<int, ThresholdDialog::Thresholds> thresholds = ThresholdDialog::load();

    // 传感器名 → 数据列访问器
    struct SensorDef {
        QString unit;
        double (SensorSnapshot::*field);
    };
    QVector<SensorDef> sensors = {
        {"℃",  &SensorSnapshot::airTemp},
        {"%",  &SensorSnapshot::airHumidity},
        {"Lux",&SensorSnapshot::lightLux},
        {"",   &SensorSnapshot::soilPh},
        {"m/s",&SensorSnapshot::windSpeed},
        {"℃",  &SensorSnapshot::soilTemp},
        {"%",  &SensorSnapshot::soilHumidity},
        {"mS/cm",&SensorSnapshot::soilConductivity},
    };

    for (int i = 0; i < sensors.size(); ++i) {
        auto *chartView = charts.value(i);
        if (!chartView) continue;
        auto *chart = chartView->chart();

        // 先清除旧 series
        chart->removeAllSeries();
        // 移除旧轴（addAxis 会自动处理 ownership）
        for (auto *ax : chart->axes()) chart->removeAxis(ax);

        auto *axisX = new QDateTimeAxis();
        axisX->setFormat("HH:mm");
        axisX->setLabelsColor(QColor("#90A4AE"));
        axisX->setGridLineColor(QColor("#1a2a3a"));
        axisX->setRange(from, to);
        chart->addAxis(axisX, Qt::AlignBottom);

        auto *axisY = new QValueAxis();
        axisY->setLabelsColor(QColor("#90A4AE"));
        axisY->setGridLineColor(QColor("#1a2a3a"));
        chart->addAxis(axisY, Qt::AlignLeft);

        double fieldVal;
        auto *lineSeries = new QLineSeries();
        lineSeries->setPen(QPen(QColor("#00E5FF"), 1.5));
        lineSeries->setName("数据");

        auto *alertScatter = new QScatterSeries();
        alertScatter->setMarkerSize(10);
        alertScatter->setColor(QColor("#FF5252"));
        alertScatter->setBorderColor(QColor("#FF5252"));
        alertScatter->setName("超限");

        auto *upperLine = new QLineSeries();
        upperLine->setPen(QPen(QColor("#FF5252"), 1, Qt::DashLine));
        upperLine->setName("上限");

        auto *lowerLine = new QLineSeries();
        lowerLine->setPen(QPen(QColor("#FF9800"), 1, Qt::DashLine));
        lowerLine->setName("下限");

        double minY = 1e18, maxY = -1e18;
        int alertCount = 0;

        ThresholdDialog::Thresholds th = thresholds.value(i);

        for (const auto &snap : data) {
            fieldVal = snap.*(sensors[i].field);
            if (!std::isfinite(fieldVal)) continue;

            qreal epoch = snap.timestamp.toMSecsSinceEpoch();
            lineSeries->append(epoch, fieldVal);

            if (fieldVal < minY) minY = fieldVal;
            if (fieldVal > maxY) maxY = fieldVal;

            if (fieldVal > th.upper || fieldVal < th.lower) {
                alertScatter->append(epoch, fieldVal);
                alertCount++;
            }
        }

        // 阈值虚线
        qreal fromEpoch = from.toMSecsSinceEpoch();
        qreal toEpoch   = to.toMSecsSinceEpoch();
        upperLine->append(fromEpoch, th.upper);
        upperLine->append(toEpoch,   th.upper);
        lowerLine->append(fromEpoch, th.lower);
        lowerLine->append(toEpoch,   th.lower);

        // Y 轴范围留 10% 边距
        double range = maxY - minY;
        if (range < 0.01) range = 10;
        axisY->setRange(minY - range * 0.1, maxY + range * 0.1);

        chart->addSeries(lineSeries);
        chart->addSeries(alertScatter);
        chart->addSeries(upperLine);
        chart->addSeries(lowerLine);

        lineSeries->attachAxis(axisX);
        lineSeries->attachAxis(axisY);
        alertScatter->attachAxis(axisX);
        alertScatter->attachAxis(axisY);
        upperLine->attachAxis(axisX);
        upperLine->attachAxis(axisY);
        lowerLine->attachAxis(axisX);
        lowerLine->attachAxis(axisY);

        // 更新标题：追加超限计数
        auto *card = chartCards.value(i);
        if (card) {
            auto *header = card->findChild<QPushButton*>();
            if (header) {
                QString base = header->text().left(header->text().indexOf("  "));
                if (alertCount > 0) {
                    header->setText(base + QString("  ⚠ 超限 %1 处").arg(alertCount));
                } else {
                    header->setText(base + "  ✓ 正常");
                }
            }
        }
    }
}
```

- [ ] **Step 4: 写 chartdashboard.cpp（下：槽函数 + 导出）**

```cpp
void ChartDashboard::onPreset1h() {
    QDateTime to = QDateTime::currentDateTime();
    QDateTime from = to.addSecs(-3600);
    fromTime->setDateTime(from); toTime->setDateTime(to);
    loadData(from, to);
}
void ChartDashboard::onPreset6h() {
    QDateTime to = QDateTime::currentDateTime();
    QDateTime from = to.addSecs(-21600);
    fromTime->setDateTime(from); toTime->setDateTime(to);
    loadData(from, to);
}
void ChartDashboard::onPreset24h() {
    QDateTime to = QDateTime::currentDateTime();
    QDateTime from = to.addSecs(-86400);
    fromTime->setDateTime(from); toTime->setDateTime(to);
    loadData(from, to);
}
void ChartDashboard::onPreset7d() {
    QDateTime to = QDateTime::currentDateTime();
    QDateTime from = to.addDays(-7);
    fromTime->setDateTime(from); toTime->setDateTime(to);
    loadData(from, to);
}
void ChartDashboard::onCustomQuery() {
    loadData(fromTime->dateTime(), toTime->dateTime());
}
void ChartDashboard::onOpenThresholds() {
    auto *dlg = new ThresholdDialog(this);
    connect(dlg, &ThresholdDialog::thresholdsChanged, this, &ChartDashboard::reapplyThresholds);
    dlg->exec();
    dlg->deleteLater();
}
void ChartDashboard::reapplyThresholds() {
    loadData(fromTime->dateTime(), toTime->dateTime());
}

void ChartDashboard::onExportExcel() {
    QDateTime from = fromTime->dateTime();
    QDateTime to   = toTime->dateTime();
    QVector<SensorSnapshot> data = DataStore::instance()->query(from, to);
    if (data.isEmpty()) {
        QMessageBox::information(this, "导出", "当前时间范围内无数据，无法导出。");
        return;
    }

    QString path = QFileDialog::getSaveFileName(this, "导出 Excel", "sensor_data.xlsx",
                                                 "Excel (*.xlsx)");
    if (path.isEmpty()) return;

    // 生成 Python 脚本写入临时文件
    QTemporaryFile scriptFile;
    scriptFile.setFileTemplate(QDir::tempPath() + "/tea_export_XXXXXX.py");
    scriptFile.open();

    QTextStream py(&scriptFile);
    py << "import sys, json, os\n"
          "try:\n"
          "    from openpyxl import Workbook\n"
          "    from openpyxl.styles import Font, Alignment\n"
          "except ImportError:\n"
          "    os.system(f'{sys.executable} -m pip install openpyxl')\n"
          "    from openpyxl import Workbook\n"
          "    from openpyxl.styles import Font, Alignment\n"
          "\n"
          "wb = Workbook()\n"
          "ws = wb.active\n"
          "ws.title = '传感器数据'\n"
          "headers = ['时间','风速(m/s)','空气温度(℃)','空气湿度(%)','光照(Lux)',"
          "'土壤湿度(%)','土壤温度(℃)','土壤电导率(mS/cm)','土壤pH']\n"
          "for c, h in enumerate(headers, 1):\n"
          "    cell = ws.cell(row=1, column=c, value=h)\n"
          "    cell.font = Font(bold=True, color='FFFFFF')\n"
          "    cell.fill = openpyxl.styles.PatternFill('solid', fgColor='00897B')\n"
          "    cell.alignment = Alignment(horizontal='center')\n"
          "\n"
          "data = json.loads(sys.stdin.read())\n"
          "for r, row in enumerate(data, 2):\n"
          "    for c, val in enumerate(row, 1):\n"
          "        ws.cell(row=r, column=c, value=val if val is not None else '')\n"
          "\n"
          "ws.freeze_panes = 'A2'\n"
          "for col in ws.columns:\n"
          "    max_len = max((len(str(c.value or '')) for c in col), default=8)\n"
          "    ws.column_dimensions[col[0].column_letter].width = min(max_len + 2, 30)\n"
          "\n"
          "wb.save(sys.argv[1])\n"
          "print('OK')\n";
    py.flush();
    scriptFile.close();

    // 构造 JSON 数据
    QJsonArray arr;
    for (const auto &s : data) {
        QJsonArray row;
        row.append(s.timestamp.toString("yyyy-MM-dd hh:mm:ss"));
        auto add = [&](double v) { row.append(std::isfinite(v) ? QJsonValue(v) : QJsonValue()); };
        add(s.windSpeed); add(s.airTemp); add(s.airHumidity); add(s.lightLux);
        add(s.soilHumidity); add(s.soilTemp); add(s.soilConductivity); add(s.soilPh);
        arr.append(row);
    }
    QJsonDocument doc(arr);

    auto *proc = new QProcess(this);
    proc->start("python3", {scriptFile.fileName(), path});
    proc->write(doc.toJson(QJsonDocument::Compact));
    proc->closeWriteChannel();

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, path](int code, QProcess::ExitStatus) {
        if (code == 0) {
            QMessageBox::information(this, "导出完成", "已导出到:\n" + path);
        } else {
            QString err = proc->readAllStandardError();
            QMessageBox::warning(this, "导出失败", "Python 错误:\n" + err.left(500));
        }
        proc->deleteLater();
    });
}
```

- [ ] **Step 5: 编译验证**

先只做语法检查（此时 .pro 还没加新文件）：

```bash
cd /home/lzy/文档/QTproject/build-desktop-Debug
g++ -std=c++17 -fsyntax-only -I/usr/include/x86_64-linux-gnu/qt5 \
  -I/usr/include/x86_64-linux-gnu/qt5/QtCore \
  -I/usr/include/x86_64-linux-gnu/qt5/QtWidgets \
  -I/usr/include/x86_64-linux-gnu/qt5/QtCharts \
  -I/usr/include/x86_64-linux-gnu/qt5/QtSql \
  -I. -I../test \
  -fPIC ../test/chartdashboard.cpp 2>&1
```

Expected: 无错误

- [ ] **Step 6: 提交**

```bash
git add test/chartdashboard.h test/chartdashboard.cpp && git commit -m "feat: ChartDashboard 折线看板窗口" && git push
```

---

### Task 5: 修改 test.pro

**Files:**
- Modify: `test/test.pro`

- [ ] **Step 1: 加 Qt 模块和新源文件**

把 `test.pro` 第 1 行的：
```
QT       += core gui serialport network
```
改为：
```
QT       += core gui serialport network charts sql
```

把 SOURCES 和 HEADERS 追加新文件：

在 `SOURCES +=` 块末尾 `widget.cpp` 之前加：
```
SOURCES += \
    chartdashboard.cpp \
    datastore.cpp \
    loramanager.cpp \
    main.cpp \
    teagardensystem.cpp \
    thresholddialog.cpp \
    widget.cpp
```

在 `HEADERS +=` 块中加：
```
HEADERS += \
    chartdashboard.h \
    datastore.h \
    loramanager.h \
    teagardensystem.h \
    thresholddialog.h \
    widget.h
```

- [ ] **Step 2: 重新 qmake + 编译**

```bash
cd /home/lzy/文档/QTproject/build-desktop-Debug
rm -rf * && /usr/lib/qt5/bin/qmake ../test/test.pro 2>&1 && make -j$(nproc) 2>&1
```

Expected: 编译成功，无错误

- [ ] **Step 3: 提交**

```bash
git add test/test.pro && git commit -m "build: test.pro 新增 charts sql 模块和看板源文件" && git push
```

---

### Task 6: 修改 TeaGardenSystem — 按钮、托盘、数据写入

**Files:**
- Modify: `test/teagardensystem.h`
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: 修改 teagardensystem.h**

在 `teagardensystem.h` 顶部新增 include：
```cpp
#include <QSystemTrayIcon>
#include <QMenu>
```

在 `class TeaGardenSystem` 的 private slots 块末尾（`onVoiceRecorded` 之后）新增：
```cpp
    void openChartDashboard();
```

在 private 成员变量区域末尾新增：
```cpp
    // 数据看板
    class ChartDashboard *chartDashboard = nullptr;
    QSystemTrayIcon *trayIcon = nullptr;
    QMap<int, QDateTime> lastAlertTime;  // sensorIndex → 上次告警时间
```

在前向声明区域（`#include "loramanager.h"` 附近）新增：
```cpp
class ChartDashboard;
```

- [ ] **Step 2: 修改 teagardensystem.cpp 构造函数**

在构造函数末尾（`appendChatMessage("管家", ...)`  那行之前）新增 DataStore 初始化和托盘：

```cpp
    // 初始化 SQLite 数据存储
    DataStore::instance()->init(QCoreApplication::applicationDirPath() + "/sensor_data.db");

    // 初始化系统托盘
    trayIcon = new QSystemTrayIcon(this);
    QIcon trayIconFile(":/icons/tray.png");
    if (trayIconFile.isNull()) {
        trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    } else {
        trayIcon->setIcon(trayIconFile);
    }
    trayIcon->setToolTip("智慧茶园管理系统");
    auto *trayMenu = new QMenu(this);
    trayMenu->setStyleSheet("background: #0d1f2d; color: #E0E0E0;");
    trayMenu->addAction("显示主窗口", this, [this]() { show(); raise(); });
    trayMenu->addAction("数据看板", this, &TeaGardenSystem::openChartDashboard);
    trayMenu->addSeparator();
    trayMenu->addAction("退出", qApp, &QApplication::quit);
    trayIcon->setContextMenu(trayMenu);
    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::DoubleClick) { show(); raise(); }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon->show();
    }
```

- [ ] **Step 3: 修改 onLoraData() — 写数据库 + 阈值检查**

在 `onLoraData()` 末尾（`lastLoraFrameTime = ...` 之后，更新 UI 卡片之前）新增代码：

```cpp
    // 写入 SQLite
    SensorSnapshot snap;
    snap.timestamp = QDateTime::currentDateTime();
    snap.windSpeed = curWindSpeed;
    snap.airTemp = curTemp;
    snap.airHumidity = curHumi;
    snap.lightLux = curLight;
    snap.soilPh = curPh;
    snap.soilTemp = curSoilTemp;
    snap.soilHumidity = curSoilHumi;
    snap.soilConductivity = curSoilEc;
    DataStore::instance()->append(snap);

    // 阈值超限通知
    QMap<int, ThresholdDialog::Thresholds> thresholds = ThresholdDialog::load();
    auto checkThreshold = [&](int index, double value, const QString &name, const QString &unit) {
        if (!std::isfinite(value)) return;
        ThresholdDialog::Thresholds th = thresholds.value(index);
        if (value > th.upper || value < th.lower) {
            QDateTime now = QDateTime::currentDateTime();
            if (!lastAlertTime.contains(index) || lastAlertTime[index].secsTo(now) > 60) {
                lastAlertTime[index] = now;
                QString msg = QString("%1 %2%3").arg(name)
                    .arg(value, 0, 'f', 1).arg(unit);
                QString detail = QString("阈值范围: %1 ~ %2").arg(th.lower).arg(th.upper);
                trayIcon->showMessage("⚠ 阈值告警", msg + "\n" + detail,
                                      QSystemTrayIcon::Warning, 5000);
            }
        }
    };
    checkThreshold(0, curTemp,      "空气温度",   "℃");
    checkThreshold(1, curHumi,      "空气湿度",   "%");
    checkThreshold(2, curLight,     "光照强度",   "Lux");
    checkThreshold(3, curPh,        "土壤pH",     "");
    checkThreshold(4, curWindSpeed, "风速",       "m/s");
    checkThreshold(5, curSoilTemp,  "土壤温度",   "℃");
    checkThreshold(6, curSoilHumi,  "土壤湿度",   "%");
    checkThreshold(7, curSoilEc,    "土壤电导率", "mS/cm");
```

- [ ] **Step 4: 修改 refreshData() — 模拟数据也写 SQLite**

在 `refreshData()` 末尾（更新完 valueLabels 之后）新增：

```cpp
    // 模拟数据也写入 SQLite
    SensorSnapshot snap;
    snap.timestamp = QDateTime::currentDateTime();
    snap.airTemp = curTemp;
    snap.airHumidity = curHumi;
    DataStore::instance()->append(snap);
```

- [ ] **Step 5: 修改 setupUI() — 左栏加按钮**

在左栏 `leftPanel->addStretch()` 之前加：

```cpp
    QPushButton *chartBtn = new QPushButton("📊 数据看板");
    chartBtn->setStyleSheet("background: #00897B; color: white; padding: 8px; "
                            "font-weight: bold; border-radius: 6px;");
    connect(chartBtn, &QPushButton::clicked, this, &TeaGardenSystem::openChartDashboard);
    leftPanel->addWidget(chartBtn);
```

- [ ] **Step 6: 新增 openChartDashboard() 槽函数**

在 `teagardensystem.cpp` 末尾新增：

```cpp
void TeaGardenSystem::openChartDashboard() {
    if (!chartDashboard) {
        chartDashboard = new ChartDashboard();
    }
    chartDashboard->show();
    chartDashboard->raise();
}
```

- [ ] **Step 7: 修改析构函数**

```cpp
TeaGardenSystem::~TeaGardenSystem() {
    if (chartDashboard) {
        chartDashboard->close();
        delete chartDashboard;
    }
}
```

- [ ] **Step 8: 头文件 include**

在 `teagardensystem.cpp` 顶部新增：
```cpp
#include "chartdashboard.h"
#include "thresholddialog.h"
#include "datastore.h"
#include <QStyle>
```

- [ ] **Step 9: 编译**

```bash
cd /home/lzy/文档/QTproject/build-desktop-Debug
make -j$(nproc) 2>&1
```

Expected: 编译成功

- [ ] **Step 10: 提交**

```bash
git add test/teagardensystem.h test/teagardensystem.cpp && git commit -m "feat: 主界面集成数据看板按钮、托盘通知、SQLite写入" && git push
```

---

### Task 7: 处理托盘图标资源

**Files:**
- Modify: `test/resources.qrc`
- Note: 无图标文件则用 QStyle 标准图标回退

- [ ] **Step 1: 检查 resource.qrc 中的图标**

`resources.qrc` 当前只有 amap_view.html。我们没有准备 tray 图标文件，因此在 teagardensystem.cpp 的托盘初始化中用 `QIcon::fromTheme` 或 `style()->standardIcon` 回退。

修改构造函数中的托盘图标部分：
```cpp
    // 尝试加载图标，回退到系统标准图标
    QIcon trayIconFile(":/icons/tray.png");
    if (trayIconFile.isNull()) {
        trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    } else {
        trayIcon->setIcon(trayIconFile);
    }
```

- [ ] **Step 2: 编译 + 提交**

```bash
cd /home/lzy/文档/QTproject/build-desktop-Debug
make -j$(nproc) 2>&1 && git add -A && git commit -m "fix: 托盘图标回退到系统标准图标" && git push
```

---

### Task 8: 端到端验证

- [ ] **Step 1: 编译全量**

```bash
cd /home/lzy/文档/QTproject/build-desktop-Debug
rm -rf * && /usr/lib/qt5/bin/qmake ../test/test.pro 2>&1 && make -j$(nproc) 2>&1
```

Expected: 所有文件编译通过，链接成功

- [ ] **Step 2: 启动测试**

```bash
timeout 8 ./test 2>&1; echo "EXIT:$?"
```

Expected: EXIT:124（timeout 正常杀掉进程），无段错误

- [ ] **Step 3: 检查 SQLite 文件生成**

```bash
ls -la sensor_data.db 2>/dev/null && echo "DB created OK" || echo "DB not found (may need longer run)"
```

- [ ] **Step 4: 提交最终验证结果**

```bash
git add -A && git commit -m "验证: 全量编译通过，启动无段错误" && git push
```

---

## 验证总结

```bash
# 完整编译
cd build-desktop-Debug && rm -rf * && qmake ../test/test.pro && make -j$(nproc)

# 运行测试
./test
# 手动验证:
# 1. 系统托盘出现图标
# 2. 点 📊 打开看板 → 8 张折线图显示
# 3. 切换 1h/6h/24h/7d → 图表刷新
# 4. 折叠某张图
# 5. 打开阈值弹窗 → 改温度上限为 20 → 确认通知弹出
# 6. 导出 Excel → 确认 .xlsx 内容
# 7. 关程序重启 → 确认历史数据还在
```
