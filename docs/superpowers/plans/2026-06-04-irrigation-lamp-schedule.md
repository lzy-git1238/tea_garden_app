# 灌溉与灭虫灯定时调度功能 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为灌溉泵和驱虫灯新增定时计划调度、实时流量显示、历史统计图表功能。

**Architecture:** 新增 ScheduleStore 单例（SQLite 存储计划+日志）和 ScheduleDialog（QDialog 双标签页管理界面），在 TeaGardenSystem 中集成 60s 调度定时器、1s 流量刷新定时器、流量状态卡片、⏰快捷按钮。

**Tech Stack:** Qt 5/6 Widgets, QtCharts, SQLite (QSqlDatabase), QSettings

**Spec:** `docs/superpowers/specs/2026-06-04-irrigation-lamp-schedule-design.md`

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `test/schedulestore.h` | 新建 | SchedulePlan/ScheduleLogEntry 结构体 + ScheduleStore 单例接口 |
| `test/schedulestore.cpp` | 新建 | SQLite 建表、CRUD、日志查询、日汇总 |
| `test/scheduledialog.h` | 新建 | ScheduleDialog 声明（计划管理 + 历史图表的 QDialog） |
| `test/scheduledialog.cpp` | 新建 | 双标签页 UI + 新增/编辑计划子对话框 + QChart 图表 |
| `test/teagardensystem.h` | 修改 | 新增成员变量、槽函数声明 |
| `test/teagardensystem.cpp` | 修改 | ⏰按钮、流量卡片、调度逻辑、主题、回调改写 |
| `test/test.pro` | 修改 | 添加 schedulestore + scheduledialog 源文件 |

---

### Task 1: 创建 ScheduleStore 头文件

**Files:**
- Create: `test/schedulestore.h`

- [ ] **Step 1: Write schedulestore.h**

```cpp
#ifndef SCHEDULESTORE_H
#define SCHEDULESTORE_H

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QMap>
#include <QDate>

struct SchedulePlan {
    int id = 0;
    QString deviceType;   // "pump" 或 "lamp"
    QString daysOfWeek;   // "1,3,5" (1=周一)
    QString startTime;    // "HH:mm"
    int durationMin = 0;
    bool enabled = true;
    QDateTime createdAt;
};

struct ScheduleLogEntry {
    int id = 0;
    QString deviceType;    // "pump" / "lamp"
    int scheduleId = 0;    // 0 = 手动操作
    QString action;        // "on" / "off"
    QString trigger;       // "schedule" / "manual"
    double flowRate = 0;   // L/min, lamp 固定为 0
    double volumeL = 0;    // 累计灌溉量 L, lamp 固定为 0
    QDateTime ts;
};

class ScheduleStore {
public:
    static ScheduleStore* instance();

    void init(const QString &dbPath);

    // ── 计划 CRUD ──
    int  addPlan(const SchedulePlan &plan);       // 返回新 ID
    void updatePlan(const SchedulePlan &plan);
    void deletePlan(int id);
    SchedulePlan getPlan(int id);
    QVector<SchedulePlan> queryPlans(const QString &deviceType = QString());
    // 按星期几 + 当前时间查找启用的计划
    QVector<SchedulePlan> queryEnabledPlans(const QString &deviceType,
                                             int dayOfWeek,      // 1=周一
                                             const QString &timeStr); // "HH:mm"

    // ── 日志 ──
    void appendLog(const ScheduleLogEntry &entry);
    QVector<ScheduleLogEntry> queryLogs(const QString &deviceType,
                                         const QDateTime &from,
                                         const QDateTime &to);

    // ── 统计 ──
    // 返回 date → 日灌溉量(L) 或 日运行时长(分钟)
    QMap<QDate, double> dailySummary(const QString &deviceType, int days);
    double totalVolume(const QDateTime &from, const QDateTime &to);
    double totalMinutes(const QString &deviceType, const QDateTime &from, const QDateTime &to);

private:
    ScheduleStore() = default;
    QString dbPath;
};

#endif // SCHEDULESTORE_H
```

- [ ] **Step 2: Commit**

```bash
git add test/schedulestore.h
git commit -m "feat: add schedulestore.h — schedule data structures and store interface"
```

---

### Task 2: 创建 ScheduleStore 实现

**Files:**
- Create: `test/schedulestore.cpp`

- [ ] **Step 1: Write schedulestore.cpp**

```cpp
#include "schedulestore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

ScheduleStore* ScheduleStore::instance() {
    static ScheduleStore store;
    return &store;
}

void ScheduleStore::init(const QString &path) {
    dbPath = path;
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "schedule_conn");
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "ScheduleStore: cannot open" << dbPath << db.lastError().text();
        return;
    }
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS schedule_config ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "device_type TEXT NOT NULL,"
           "days_of_week TEXT NOT NULL,"
           "start_time TEXT NOT NULL,"
           "duration_min INTEGER NOT NULL,"
           "enabled INTEGER DEFAULT 1,"
           "created_at TEXT)");
    q.exec("CREATE TABLE IF NOT EXISTS schedule_log ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "device_type TEXT NOT NULL,"
           "schedule_id INTEGER DEFAULT 0,"
           "action TEXT NOT NULL,"
           "trigger_type TEXT NOT NULL,"
           "flow_rate REAL DEFAULT 0,"
           "volume_l REAL DEFAULT 0,"
           "ts TEXT NOT NULL)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_schedule_log_ts ON schedule_log(ts)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_schedule_log_dev ON schedule_log(device_type, ts)");
}

int ScheduleStore::addPlan(const SchedulePlan &plan) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return -1;
    QSqlQuery q(db);
    q.prepare("INSERT INTO schedule_config (device_type, days_of_week, start_time, duration_min, enabled, created_at) "
              "VALUES (:dt, :dow, :st, :dur, :en, :ca)");
    q.bindValue(":dt", plan.deviceType);
    q.bindValue(":dow", plan.daysOfWeek);
    q.bindValue(":st", plan.startTime);
    q.bindValue(":dur", plan.durationMin);
    q.bindValue(":en", plan.enabled ? 1 : 0);
    q.bindValue(":ca", plan.createdAt.isValid()
                        ? plan.createdAt.toString(Qt::ISODate)
                        : QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "ScheduleStore::addPlan failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

void ScheduleStore::updatePlan(const SchedulePlan &plan) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("UPDATE schedule_config SET device_type=:dt, days_of_week=:dow, start_time=:st, "
              "duration_min=:dur, enabled=:en WHERE id=:id");
    q.bindValue(":dt", plan.deviceType);
    q.bindValue(":dow", plan.daysOfWeek);
    q.bindValue(":st", plan.startTime);
    q.bindValue(":dur", plan.durationMin);
    q.bindValue(":en", plan.enabled ? 1 : 0);
    q.bindValue(":id", plan.id);
    if (!q.exec()) qWarning() << "ScheduleStore::updatePlan failed:" << q.lastError().text();
}

void ScheduleStore::deletePlan(int id) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("DELETE FROM schedule_config WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) qWarning() << "ScheduleStore::deletePlan failed:" << q.lastError().text();
}

SchedulePlan ScheduleStore::getPlan(int id) {
    SchedulePlan p;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return p;
    QSqlQuery q(db);
    q.prepare("SELECT id, device_type, days_of_week, start_time, duration_min, enabled, created_at "
              "FROM schedule_config WHERE id=:id");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) {
        p.id = q.value(0).toInt();
        p.deviceType = q.value(1).toString();
        p.daysOfWeek = q.value(2).toString();
        p.startTime = q.value(3).toString();
        p.durationMin = q.value(4).toInt();
        p.enabled = q.value(5).toInt() != 0;
        p.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
    }
    return p;
}

QVector<SchedulePlan> ScheduleStore::queryPlans(const QString &deviceType) {
    QVector<SchedulePlan> result;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return result;
    QSqlQuery q(db);
    if (deviceType.isEmpty()) {
        q.exec("SELECT id, device_type, days_of_week, start_time, duration_min, enabled, created_at "
               "FROM schedule_config ORDER BY device_type, start_time");
    } else {
        q.prepare("SELECT id, device_type, days_of_week, start_time, duration_min, enabled, created_at "
                  "FROM schedule_config WHERE device_type=:dt ORDER BY start_time");
        q.bindValue(":dt", deviceType);
        q.exec();
    }
    while (q.next()) {
        SchedulePlan p;
        p.id = q.value(0).toInt();
        p.deviceType = q.value(1).toString();
        p.daysOfWeek = q.value(2).toString();
        p.startTime = q.value(3).toString();
        p.durationMin = q.value(4).toInt();
        p.enabled = q.value(5).toInt() != 0;
        p.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        result.append(p);
    }
    return result;
}

QVector<SchedulePlan> ScheduleStore::queryEnabledPlans(const QString &deviceType,
                                                         int dayOfWeek,
                                                         const QString &timeStr) {
    QVector<SchedulePlan> result;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return result;
    QSqlQuery q(db);
    q.prepare("SELECT id, device_type, days_of_week, start_time, duration_min, enabled, created_at "
              "FROM schedule_config WHERE device_type=:dt AND enabled=1 AND start_time=:st");
    q.bindValue(":dt", deviceType);
    q.bindValue(":st", timeStr);
    if (!q.exec()) return result;
    while (q.next()) {
        // 检查 days_of_week 是否包含当前星期几
        QStringList parts = q.value(2).toString().split(',', Qt::SkipEmptyParts);
        bool match = false;
        for (const QString &part : parts) {
            if (part.trimmed().toInt() == dayOfWeek) { match = true; break; }
        }
        if (!match) continue;
        SchedulePlan p;
        p.id = q.value(0).toInt();
        p.deviceType = q.value(1).toString();
        p.daysOfWeek = q.value(2).toString();
        p.startTime = q.value(3).toString();
        p.durationMin = q.value(4).toInt();
        p.enabled = true;
        p.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        result.append(p);
    }
    return result;
}

void ScheduleStore::appendLog(const ScheduleLogEntry &entry) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("INSERT INTO schedule_log (device_type, schedule_id, action, trigger_type, flow_rate, volume_l, ts) "
              "VALUES (:dt, :sid, :act, :trg, :fr, :vl, :ts)");
    q.bindValue(":dt", entry.deviceType);
    q.bindValue(":sid", entry.scheduleId);
    q.bindValue(":act", entry.action);
    q.bindValue(":trg", entry.trigger);
    q.bindValue(":fr", entry.flowRate);
    q.bindValue(":vl", entry.volumeL);
    q.bindValue(":ts", entry.ts.isValid()
                        ? entry.ts.toString(Qt::ISODate)
                        : QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) qWarning() << "ScheduleStore::appendLog failed:" << q.lastError().text();
}

QVector<ScheduleLogEntry> ScheduleStore::queryLogs(const QString &deviceType,
                                                     const QDateTime &from,
                                                     const QDateTime &to) {
    QVector<ScheduleLogEntry> result;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return result;
    QSqlQuery q(db);
    q.prepare("SELECT id, device_type, schedule_id, action, trigger_type, flow_rate, volume_l, ts "
              "FROM schedule_log WHERE device_type=:dt AND ts>=:f AND ts<=:t ORDER BY ts DESC LIMIT 500");
    q.bindValue(":dt", deviceType);
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));
    if (!q.exec()) return result;
    while (q.next()) {
        ScheduleLogEntry e;
        e.id = q.value(0).toInt();
        e.deviceType = q.value(1).toString();
        e.scheduleId = q.value(2).toInt();
        e.action = q.value(3).toString();
        e.trigger = q.value(4).toString();
        e.flowRate = q.value(5).toDouble();
        e.volumeL = q.value(6).toDouble();
        e.ts = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
        result.append(e);
    }
    return result;
}

QMap<QDate, double> ScheduleStore::dailySummary(const QString &deviceType, int days) {
    QMap<QDate, double> result;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return result;
    QDateTime since = QDateTime::currentDateTime().addDays(-days);
    QSqlQuery q(db);
    if (deviceType == "pump") {
        // 灌溉泵：每日灌溉量 = 所有 "off" 日志的 volume_l 之和
        q.prepare("SELECT date(ts), SUM(volume_l) FROM schedule_log "
                  "WHERE device_type='pump' AND action='off' AND ts>=:s "
                  "GROUP BY date(ts) ORDER BY date(ts)");
    } else {
        // 驱虫灯：每日运行时长 = 配对 (on, off) 计算分钟差
        // 简化方案 — 统计 "off" action 的数量 × 假设平均值
        // 更精确的做法：对每个 "on" 找下一个 "off"
        q.prepare("SELECT date(ts), COUNT(*) FROM schedule_log "
                  "WHERE device_type='lamp' AND action='on' AND ts>=:s "
                  "GROUP BY date(ts) ORDER BY date(ts)");
    }
    q.bindValue(":s", since.toString(Qt::ISODate));
    if (!q.exec()) return result;
    while (q.next()) {
        QDate d = QDate::fromString(q.value(0).toString(), "yyyy-MM-dd");
        result[d] = q.value(1).toDouble();
    }
    return result;
}

double ScheduleStore::totalVolume(const QDateTime &from, const QDateTime &to) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return 0;
    QSqlQuery q(db);
    q.prepare("SELECT COALESCE(SUM(volume_l), 0) FROM schedule_log "
              "WHERE device_type='pump' AND action='off' AND ts>=:f AND ts<=:t");
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));
    if (q.exec() && q.next()) return q.value(0).toDouble();
    return 0;
}

double ScheduleStore::totalMinutes(const QString &deviceType, const QDateTime &from, const QDateTime &to) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return 0;
    // 简化：统计 on 事件次数 × 从对应 plan 获取 duration
    // 精确做法从 schedule_log 中配对 on/off 时间差
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM schedule_log "
              "WHERE device_type=:dt AND action='on' AND ts>=:f AND ts<=:t");
    q.bindValue(":dt", deviceType);
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));
    if (q.exec() && q.next()) return q.value(0).toDouble();
    return 0;
}
```

- [ ] **Step 2: Commit**

```bash
git add test/schedulestore.cpp
git commit -m "feat: add schedulestore.cpp — SQLite CRUD and log queries"
```

---

### Task 3: 更新 test.pro

**Files:**
- Modify: `test/test.pro`

- [ ] **Step 1: Add new source and header files to test.pro**

In `test/test.pro`, add the new files to SOURCES and HEADERS. Replace:

```
SOURCES += \
    chartdashboard.cpp \
    ...
    widget.cpp
```

with:

```
SOURCES += \
    chartdashboard.cpp \
    datastore.cpp \
    iotuploader.cpp \
    loramanager.cpp \
    main.cpp \
    pestdashboard.cpp \
    pestdata.cpp \
    scheduledialog.cpp \
    schedulestore.cpp \
    teagardensystem.cpp \
    thresholddialog.cpp \
    widget.cpp
```

And in HEADERS, replace:

```
HEADERS += \
    chartdashboard.h \
    ...
    widget.h
```

with:

```
HEADERS += \
    chartdashboard.h \
    datastore.h \
    iotuploader.h \
    loramanager.h \
    pestdashboard.h \
    pestdata.h \
    scheduledialog.h \
    schedulestore.h \
    teagardensystem.h \
    thresholddialog.h \
    widget.h
```

- [ ] **Step 2: Commit**

```bash
git add test/test.pro
git commit -m "feat: add schedulestore and scheduledialog to test.pro"
```

---

### Task 4: 创建 ScheduleDialog 头文件

**Files:**
- Create: `test/scheduledialog.h`

- [ ] **Step 1: Write scheduledialog.h**

```cpp
#ifndef SCHEDULEDIALOG_H
#define SCHEDULEDIALOG_H

#include <QDialog>
#include "schedulestore.h"

class QTabWidget;
class QComboBox;
class QTableWidget;
class QPushButton;
class QLabel;
class QChartView;
class QTextEdit;

// ── 新增/编辑计划的子对话框 ──
class PlanEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit PlanEditDialog(QWidget *parent = nullptr);
    void setPlan(const SchedulePlan &plan);
    SchedulePlan plan() const;

private:
    class QComboBox *deviceCombo;
    class QCheckBox *dayChecks[7];
    class QTimeEdit *timeEdit;
    class QSpinBox *durationSpin;
    class QCheckBox *enabledCheck;
    class QLabel *flowPreviewLabel;
    SchedulePlan mOriginal;
};

// ── 定时计划管理主对话框 ──
class ScheduleDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScheduleDialog(QWidget *parent = nullptr);
    void setDeviceFilter(const QString &deviceType);
    void refresh();

private slots:
    void onAddPlan();
    void onEditPlan(int row);
    void onDeletePlan(int row);
    void onDeviceFilterChanged(const QString &deviceType);
    void refreshHistoryChart();
    void refreshPlanTable();

private:
    void setupPlanTab(QTabWidget *tabs);
    void setupHistoryTab(QTabWidget *tabs);
    int currentRowPlanId() const;

    QComboBox *deviceFilter;
    QComboBox *historyDeviceFilter;
    QComboBox *historyRangeCombo;
    QTableWidget *planTable;
    QChartView *chartView;
    QTextEdit *logText;
    QLabel *summaryLabel;
};

#endif // SCHEDULEDIALOG_H
```

- [ ] **Step 2: Commit**

```bash
git add test/scheduledialog.h
git commit -m "feat: add scheduledialog.h — schedule management dialog interface"
```

---

### Task 5: 创建 ScheduleDialog 实现

**Files:**
- Create: `test/scheduledialog.cpp`

- [ ] **Step 1: Write scheduledialog.cpp**

```cpp
#include "scheduledialog.h"
#include "schedulestore.h"

#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QTimeEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QGroupBox>
#include <QSettings>
#include <QChart>
#include <QChartView>
#include <QBarSeries>
#include <QBarSet>
#include <QBarCategoryAxis>
#include <QValueAxis>
#include <QLineSeries>
#include <QDateTimeAxis>
#include <QDate>
#include <QtCharts>
using namespace QtCharts;

// ────────────────────────────────────────
// PlanEditDialog 实现
// ────────────────────────────────────────

PlanEditDialog::PlanEditDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("编辑定时计划");
    setMinimumWidth(420);
    setStyleSheet("background: #0d1f2d; color: #E0E0E0;");

    auto *layout = new QVBoxLayout(this);

    // 设备类型
    auto *formLayout = new QFormLayout();
    deviceCombo = new QComboBox();
    deviceCombo->addItem("💧 灌溉泵", "pump");
    deviceCombo->addItem("🪲 驱虫灯", "lamp");
    deviceCombo->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455; padding: 4px;");
    formLayout->addRow("设备类型：", deviceCombo);

    // 星期选择
    auto *dayGroup = new QGroupBox("执行日期");
    dayGroup->setStyleSheet("QGroupBox { color: #90caf9; border: 1px solid #334455; padding-top: 10px; margin-top: 8px; }");
    auto *dayLayout = new QHBoxLayout(dayGroup);
    QStringList dayNames = {"一", "二", "三", "四", "五", "六", "日"};
    auto *everydayBtn = new QPushButton("每天");
    everydayBtn->setStyleSheet("background: #1a3a5c; color: #4fc3f7; padding: 3px 8px; font-size: 11px; border-radius: 3px;");
    connect(everydayBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < 7; ++i) dayChecks[i]->setChecked(true);
    });
    dayLayout->addWidget(everydayBtn);
    for (int i = 0; i < 7; ++i) {
        dayChecks[i] = new QCheckBox(dayNames[i]);
        dayChecks[i]->setStyleSheet("color: #ccc;");
        dayLayout->addWidget(dayChecks[i]);
    }
    formLayout->addRow(dayGroup);

    // 时间
    timeEdit = new QTimeEdit(QTime(7, 0));
    timeEdit->setDisplayFormat("HH:mm");
    timeEdit->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455; padding: 4px;");
    formLayout->addRow("开启时间：", timeEdit);

    // 持续时长
    durationSpin = new QSpinBox();
    durationSpin->setRange(1, 1440);
    durationSpin->setValue(30);
    durationSpin->setSuffix(" 分钟");
    durationSpin->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455; padding: 4px;");
    formLayout->addRow("持续时长：", durationSpin);

    // 预计流量（只读）
    flowPreviewLabel = new QLabel("预计流量：-- L");
    flowPreviewLabel->setStyleSheet("color: #4fc3f7; font-size: 12px; margin-left: 4px;");
    formLayout->addRow("", flowPreviewLabel);

    // 连接设备类型变化以更新流量预览
    connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        bool isPump = deviceCombo->currentData().toString() == "pump";
        flowPreviewLabel->setVisible(isPump);
        if (isPump) {
            double rate = QSettings().value("pump/flow_rate", 2.5).toDouble();
            flowPreviewLabel->setText(QString("预计流量：%1 L").arg(rate * durationSpin->value(), 0, 'f', 1));
        }
    });
    connect(durationSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        bool isPump = deviceCombo->currentData().toString() == "pump";
        if (isPump) {
            double rate = QSettings().value("pump/flow_rate", 2.5).toDouble();
            flowPreviewLabel->setText(QString("预计流量：%1 L").arg(rate * durationSpin->value(), 0, 'f', 1));
        }
    });

    // 启用/停用
    enabledCheck = new QCheckBox("启用此计划");
    enabledCheck->setChecked(true);
    enabledCheck->setStyleSheet("color: #81c784;");
    formLayout->addRow("", enabledCheck);

    layout->addLayout(formLayout);

    // 按钮
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto *cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet("background: #607D8B; color: white; padding: 6px 16px;");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto *saveBtn = new QPushButton("保存");
    saveBtn->setStyleSheet("background: #00897B; color: white; padding: 6px 16px; font-weight: bold;");
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        // 校验
        bool anyDay = false;
        for (int i = 0; i < 7; ++i) if (dayChecks[i]->isChecked()) { anyDay = true; break; }
        if (!anyDay) {
            QMessageBox::warning(this, "提示", "请至少选择一个执行日期。");
            return;
        }
        accept();
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    layout->addLayout(btnRow);

    // 初始化流量标签可见性
    flowPreviewLabel->setVisible(deviceCombo->currentData().toString() == "pump");
}

void PlanEditDialog::setPlan(const SchedulePlan &plan) {
    mOriginal = plan;
    int idx = deviceCombo->findData(plan.deviceType);
    if (idx >= 0) deviceCombo->setCurrentIndex(idx);
    QStringList days = plan.daysOfWeek.split(',', Qt::SkipEmptyParts);
    for (const QString &d : days) {
        int di = d.trimmed().toInt() - 1; // 1=周一 → index 0
        if (di >= 0 && di < 7) dayChecks[di]->setChecked(true);
    }
    timeEdit->setTime(QTime::fromString(plan.startTime, "HH:mm"));
    durationSpin->setValue(plan.durationMin);
    enabledCheck->setChecked(plan.enabled);
}

SchedulePlan PlanEditDialog::plan() const {
    SchedulePlan p = mOriginal;
    p.deviceType = deviceCombo->currentData().toString();
    QStringList days;
    for (int i = 0; i < 7; ++i) {
        if (dayChecks[i]->isChecked()) days.append(QString::number(i + 1));
    }
    p.daysOfWeek = days.join(',');
    p.startTime = timeEdit->time().toString("HH:mm");
    p.durationMin = durationSpin->value();
    p.enabled = enabledCheck->isChecked();
    return p;
}

// ────────────────────────────────────────
// ScheduleDialog 实现
// ────────────────────────────────────────

ScheduleDialog::ScheduleDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("定时计划管理");
    setMinimumSize(680, 520);
    setStyleSheet("background: #0d1f2d; color: #E0E0E0;");

    auto *mainLayout = new QVBoxLayout(this);

    auto *tabs = new QTabWidget();
    tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #1a4a78; background: #0a1a30; }"
        "QTabBar::tab { background: #0f2840; color: #90caf9; padding: 6px 16px; border: 1px solid #1a4a78; }"
        "QTabBar::tab:selected { background: #1a3a5c; color: #00d4ff; }");
    setupPlanTab(tabs);
    setupHistoryTab(tabs);
    mainLayout->addWidget(tabs);
}

void ScheduleDialog::setupPlanTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    // 顶部栏
    auto *topRow = new QHBoxLayout();
    deviceFilter = new QComboBox();
    deviceFilter->addItem("💧 灌溉泵", "pump");
    deviceFilter->addItem("🪲 驱虫灯", "lamp");
    deviceFilter->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455; padding: 4px;");
    connect(deviceFilter, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
            this, &ScheduleDialog::onDeviceFilterChanged);
    topRow->addWidget(new QLabel("设备："));
    topRow->addWidget(deviceFilter);
    topRow->addStretch();
    auto *addBtn = new QPushButton("+ 新增计划");
    addBtn->setStyleSheet("background: #00897B; color: white; padding: 6px 14px; font-weight: bold; border-radius: 3px;");
    connect(addBtn, &QPushButton::clicked, this, &ScheduleDialog::onAddPlan);
    topRow->addWidget(addBtn);
    layout->addLayout(topRow);

    // 计划表格
    planTable = new QTableWidget(0, 7);
    planTable->setHorizontalHeaderLabels({"ID", "星期", "时间", "时长", "预计流量", "状态", "操作"});
    planTable->horizontalHeader()->setStretchLastSection(true);
    planTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    planTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    planTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    planTable->verticalHeader()->hide();
    planTable->setColumnHidden(0, true); // 隐藏 ID 列
    planTable->setStyleSheet(
        "QTableWidget { background: #0a1a30; color: #ccc; gridline-color: #1a3a5c; border: 1px solid #1a4a78; }"
        "QHeaderView::section { background: #0f2840; color: #90caf9; padding: 4px; border: 1px solid #1a4a78; }");
    layout->addWidget(planTable);

    tabs->addTab(tab, "定时计划");
}

void ScheduleDialog::setupHistoryTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    // 筛选栏
    auto *filterRow = new QHBoxLayout();
    historyDeviceFilter = new QComboBox();
    historyDeviceFilter->addItem("💧 灌溉泵", "pump");
    historyDeviceFilter->addItem("🪲 驱虫灯", "lamp");
    historyDeviceFilter->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455; padding: 4px;");
    filterRow->addWidget(new QLabel("设备："));
    filterRow->addWidget(historyDeviceFilter);

    historyRangeCombo = new QComboBox();
    historyRangeCombo->addItem("近7天", 7);
    historyRangeCombo->addItem("近30天", 30);
    historyRangeCombo->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455; padding: 4px;");
    filterRow->addWidget(new QLabel("范围："));
    filterRow->addWidget(historyRangeCombo);
    filterRow->addStretch();

    summaryLabel = new QLabel("总计：--");
    summaryLabel->setStyleSheet("color: #4fc3f7; font-weight: bold;");
    filterRow->addWidget(summaryLabel);

    auto *refreshBtn = new QPushButton("⟳ 刷新");
    refreshBtn->setStyleSheet("background: #1a3a5c; color: #90caf9; padding: 4px 12px; border-radius: 3px;");
    connect(refreshBtn, &QPushButton::clicked, this, &ScheduleDialog::refreshHistoryChart);
    connect(historyDeviceFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { refreshHistoryChart(); });
    connect(historyRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { refreshHistoryChart(); });
    filterRow->addWidget(refreshBtn);
    layout->addLayout(filterRow);

    // 图表
    auto *chart = new QChart();
    chart->setBackgroundBrush(QBrush(QColor("#0a1a30")));
    chart->legend()->setLabelColor(QColor("#90caf9"));
    chart->setTitle("");
    chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(200);
    layout->addWidget(chartView, 1);

    // 最近操作日志
    auto *logLabel = new QLabel("最近操作记录");
    logLabel->setStyleSheet("color: #90caf9; font-weight: bold; margin-top: 8px;");
    layout->addWidget(logLabel);
    logText = new QTextEdit();
    logText->setReadOnly(true);
    logText->setMaximumHeight(150);
    logText->setStyleSheet("background: #0a1628; color: #aaa; border: 1px solid #1a4a78; font-size: 12px;");
    layout->addWidget(logText);

    tabs->addTab(tab, "📊 历史图表");
}

void ScheduleDialog::setDeviceFilter(const QString &deviceType) {
    int idx = deviceFilter->findData(deviceType);
    if (idx >= 0) deviceFilter->setCurrentIndex(idx);
    int hidx = historyDeviceFilter->findData(deviceType);
    if (hidx >= 0) historyDeviceFilter->setCurrentIndex(hidx);
}

void ScheduleDialog::refresh() {
    refreshPlanTable();
    refreshHistoryChart();
}

void ScheduleDialog::onDeviceFilterChanged(const QString &) {
    refreshPlanTable();
}

void ScheduleDialog::refreshPlanTable() {
    QString devType = deviceFilter->currentData().toString();
    QVector<SchedulePlan> plans = ScheduleStore::instance()->queryPlans(devType);

    planTable->setRowCount(plans.size());
    QStringList dayAbbr = {"一","二","三","四","五","六","日"};

    for (int i = 0; i < plans.size(); ++i) {
        const auto &p = plans[i];

        // ID (hidden)
        auto *idItem = new QTableWidgetItem(QString::number(p.id));
        planTable->setItem(i, 0, idItem);

        // 星期徽章
        QStringList days = p.daysOfWeek.split(',', Qt::SkipEmptyParts);
        QString dayStr;
        for (const QString &d : days) {
            int di = d.trimmed().toInt() - 1;
            if (di >= 0 && di < 7) dayStr += dayAbbr[di] + " ";
        }
        if (dayStr.isEmpty()) dayStr = "无";
        planTable->setItem(i, 1, new QTableWidgetItem(dayStr.trimmed()));

        // 时间
        planTable->setItem(i, 2, new QTableWidgetItem(p.startTime));

        // 时长
        planTable->setItem(i, 3, new QTableWidgetItem(QString("%1 分钟").arg(p.durationMin)));

        // 预计流量
        if (p.deviceType == "pump") {
            double rate = QSettings().value("pump/flow_rate", 2.5).toDouble();
            planTable->setItem(i, 4, new QTableWidgetItem(QString("%1 L").arg(rate * p.durationMin, 0, 'f', 1)));
        } else {
            planTable->setItem(i, 4, new QTableWidgetItem("—"));
        }

        // 状态
        auto *statusItem = new QTableWidgetItem(p.enabled ? "✓ 启用" : "✗ 停用");
        statusItem->setForeground(p.enabled ? QColor("#81c784") : QColor("#666666"));
        planTable->setItem(i, 5, statusItem);

        // 操作按钮
        auto *opWidget = new QWidget();
        auto *opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(0, 0, 0, 0);
        opLayout->setSpacing(6);
        auto *editBtn = new QPushButton("✎");
        editBtn->setFixedSize(26, 26);
        editBtn->setStyleSheet("background: #1a3a5c; color: #ffcc80; border: none; border-radius: 3px; font-size: 14px;");
        int row = i;
        connect(editBtn, &QPushButton::clicked, this, [this, row]() { onEditPlan(row); });
        auto *delBtn = new QPushButton("✕");
        delBtn->setFixedSize(26, 26);
        delBtn->setStyleSheet("background: #3a1a2c; color: #ef5350; border: none; border-radius: 3px; font-size: 14px;");
        connect(delBtn, &QPushButton::clicked, this, [this, row]() { onDeletePlan(row); });
        opLayout->addWidget(editBtn);
        opLayout->addWidget(delBtn);
        planTable->setCellWidget(i, 6, opWidget);
    }
}

int ScheduleDialog::currentRowPlanId() const {
    int row = planTable->currentRow();
    if (row < 0) return -1;
    auto *item = planTable->item(row, 0);
    if (!item) return -1;
    return item->text().toInt();
}

void ScheduleDialog::onAddPlan() {
    PlanEditDialog dlg(this);
    // 预设当前筛选的设备类型
    SchedulePlan preset;
    preset.deviceType = deviceFilter->currentData().toString();
    preset.daysOfWeek = "1,2,3,4,5,6,7";
    preset.startTime = "07:00";
    preset.durationMin = 30;
    preset.enabled = true;
    dlg.setPlan(preset);

    if (dlg.exec() == QDialog::Accepted) {
        SchedulePlan p = dlg.plan();
        p.createdAt = QDateTime::currentDateTime();
        ScheduleStore::instance()->addPlan(p);
        refreshPlanTable();
    }
}

void ScheduleDialog::onEditPlan(int row) {
    auto *item = planTable->item(row, 0);
    if (!item) return;
    int id = item->text().toInt();
    SchedulePlan p = ScheduleStore::instance()->getPlan(id);
    PlanEditDialog dlg(this);
    dlg.setPlan(p);
    if (dlg.exec() == QDialog::Accepted) {
        SchedulePlan updated = dlg.plan();
        updated.id = id;
        ScheduleStore::instance()->updatePlan(updated);
        refreshPlanTable();
    }
}

void ScheduleDialog::onDeletePlan(int row) {
    auto *item = planTable->item(row, 0);
    if (!item) return;
    int id = item->text().toInt();
    if (QMessageBox::question(this, "确认删除", "确定要删除这个定时计划吗？") == QMessageBox::Yes) {
        ScheduleStore::instance()->deletePlan(id);
        refreshPlanTable();
    }
}

void ScheduleDialog::refreshHistoryChart() {
    QString devType = historyDeviceFilter->currentData().toString();
    int days = historyRangeCombo->currentData().toInt();

    // 清除旧图表
    auto *chart = chartView->chart();
    chart->removeAllSeries();
    // 清除所有轴
    const auto axes = chart->axes();
    for (auto *ax : axes) chart->removeAxis(ax);

    // 查询日汇总
    QMap<QDate, double> summary = ScheduleStore::instance()->dailySummary(devType, days);

    // 生成连续的日期列表
    QDate today = QDate::currentDate();
    QDate start = today.addDays(-(days - 1));
    QStringList categories;
    QBarSet *barSet = new QBarSet(devType == "pump" ? "灌溉量 (L)" : "运行次数");
    barSet->setColor(devType == "pump" ? QColor("#4fc3f7") : QColor("#ffcc80"));

    for (int i = 0; i < days; ++i) {
        QDate d = start.addDays(i);
        categories << d.toString("MM-dd");
        *barSet << summary.value(d, 0);
    }

    auto *series = new QBarSeries();
    series->append(barSet);
    chart->addSeries(series);

    auto *axisX = new QBarCategoryAxis();
    axisX->append(categories);
    axisX->setLabelsColor(QColor("#90caf9"));
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto *axisY = new QValueAxis();
    axisY->setLabelFormat("%.0f");
    axisY->setLabelsColor(QColor("#90caf9"));
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    // 更新汇总标签
    QDateTime from(start, QTime(0, 0));
    QDateTime to(today, QTime(23, 59, 59));
    if (devType == "pump") {
        double vol = ScheduleStore::instance()->totalVolume(from, to);
        summaryLabel->setText(QString("总计灌溉: %1 L").arg(vol, 0, 'f', 1));
    } else {
        double mins = ScheduleStore::instance()->totalMinutes(devType, from, to);
        summaryLabel->setText(QString("总运行次数: %1 次").arg((int)mins));
    }

    // 更新操作记录
    QVector<ScheduleLogEntry> logs = ScheduleStore::instance()->queryLogs(devType, from, to);
    QStringList lines;
    for (int i = 0; i < qMin(logs.size(), 50); ++i) {
        const auto &l = logs[i];
        QString icon = l.deviceType == "pump" ? "💧" : "🪲";
        QString deviceName = l.deviceType == "pump" ? "灌溉泵" : "驱虫灯";
        QString actName = l.action == "on" ? "开启" : "关闭";
        QString triggerName = l.trigger == "manual" ? "手动" : QString("计划#%1").arg(l.scheduleId);
        QString line = QString("%1 · %2 %3%4 · %5 · %6")
            .arg(l.ts.toString("MM-dd HH:mm"))
            .arg(icon)
            .arg(deviceName)
            .arg(actName)
            .arg(triggerName);
        if (l.deviceType == "pump" && l.action == "off" && l.volumeL > 0) {
            line += QString(" · 累计 %1 L").arg(l.volumeL, 0, 'f', 1);
        }
        lines.append(line);
    }
    logText->setPlainText(lines.join('\n'));
}
```

- [ ] **Step 2: Commit**

```bash
git add test/scheduledialog.cpp
git commit -m "feat: add scheduledialog.cpp — plan editor + history chart implementation"
```

---

### Task 6: 更新 TeaGardenSystem 头文件

**Files:**
- Modify: `test/teagardensystem.h`

- [ ] **Step 1: Add new slots and member variables**

Add the following new `private slots:` after line 61 (`onLampToggled`):

```cpp
    void checkSchedules();          // 调度检查（每分钟）
    void refreshFlowCard();         // 刷新流量卡片（每秒）
    void openScheduleDialog(const QString &deviceType);  // 打开定时对话框
```

Add after line 107 (`lampBtn`):

```cpp
    QPushButton *scheduleBtnPump = nullptr;  // 灌溉泵 ⏰ 按钮
    QPushButton *scheduleBtnLamp = nullptr;  // 驱虫灯 ⏰ 按钮
```

Add after line 113 (`commandTimers`):

```cpp

    // ── 定时调度 ──
    struct ActiveJob {
        QString deviceType;
        int scheduleId = 0;
        QDateTime startTime;
        QDateTime endTime;
        double flowRate = 2.5;
        int durationMin = 0;
    };
    QMap<QString, ActiveJob> activeJobs;  // key: deviceType ("pump"/"lamp")
    QTimer *scheduleTimer = nullptr;       // 60s 调度检查
    QTimer *flowCardTimer = nullptr;       // 1s 流量刷新

    // 流量状态卡片
    QFrame *flowCard = nullptr;
    QLabel *flowCurrentLabel = nullptr;    // 已灌溉量
    QLabel *flowExpectedLabel = nullptr;   // 预计总量
    QLabel *flowRemainingLabel = nullptr;  // 剩余时间
    QLabel *flowProgressLabel = nullptr;   // 进度文字 (e.g. "12/30 min")
    QFrame *flowProgressBar = nullptr;     // 进度条 track
    QFrame *flowProgressFill = nullptr;    // 进度条 fill
    QPushButton *flowStopBtn = nullptr;    // 停止灌溉
    QPushButton *flowSettingsBtn = nullptr;// 设置流速
    QLabel *flowPlanIdLabel = nullptr;     // 计划编号显示
```

- [ ] **Step 2: Commit**

```bash
git add test/teagardensystem.h
git commit -m "feat: add schedule/flow members and slots to teagardensystem.h"
```

---

### Task 7: 更新 TeaGardenSystem — setupUI 变更

**Files:**
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: Add #include for new files**

After line 5 (`#include "pestdata.h"`), add:

```cpp
#include "schedulestore.h"
#include "scheduledialog.h"
```

- [ ] **Step 2: Add ⏰ buttons next to pump/lamp buttons and flow card**

Replace lines 240–249 (the devRow section):

```cpp
    // ── 流量状态卡片（灌溉进行中才显示） ──
    flowCard = new QFrame();
    flowCard->setObjectName("flowCard");
    flowCard->setVisible(false);
    auto *fcLayout = new QVBoxLayout(flowCard);
    fcLayout->setContentsMargins(8, 8, 8, 8);
    fcLayout->setSpacing(4);

    auto *fcTitleRow = new QHBoxLayout();
    auto *fcIconLabel = new QLabel("💧 灌溉进行中");
    fcIconLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #a5d6a7;");
    fcTitleRow->addWidget(fcIconLabel);
    fcTitleRow->addStretch();
    flowPlanIdLabel = new QLabel("");
    flowPlanIdLabel->setStyleSheet("font-size: 11px; color: #ffcc80;");
    fcTitleRow->addWidget(flowPlanIdLabel);
    fcLayout->addLayout(fcTitleRow);

    auto *fcMetricsRow = new QHBoxLayout();
    fcMetricsRow->setSpacing(8);
    // 已灌溉
    auto *fcCurCol = new QVBoxLayout();
    flowCurrentLabel = new QLabel("0.0");
    flowCurrentLabel->setAlignment(Qt::AlignCenter);
    flowCurrentLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #ffffff;");
    fcCurCol->addWidget(flowCurrentLabel);
    auto *fcCurUnit = new QLabel("已灌溉 (L)");
    fcCurUnit->setAlignment(Qt::AlignCenter);
    fcCurUnit->setStyleSheet("font-size: 10px; color: #888;");
    fcCurCol->addWidget(fcCurUnit);
    fcMetricsRow->addLayout(fcCurCol);
    // 预计
    auto *fcExpCol = new QVBoxLayout();
    flowExpectedLabel = new QLabel("0.0");
    flowExpectedLabel->setAlignment(Qt::AlignCenter);
    flowExpectedLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #4fc3f7;");
    fcExpCol->addWidget(flowExpectedLabel);
    auto *fcExpUnit = new QLabel("预计总量 (L)");
    fcExpUnit->setAlignment(Qt::AlignCenter);
    fcExpUnit->setStyleSheet("font-size: 10px; color: #888;");
    fcExpCol->addWidget(fcExpUnit);
    fcMetricsRow->addLayout(fcExpCol);
    // 剩余时间
    auto *fcRemCol = new QVBoxLayout();
    flowRemainingLabel = new QLabel("--:--");
    flowRemainingLabel->setAlignment(Qt::AlignCenter);
    flowRemainingLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #ffcc80;");
    fcRemCol->addWidget(flowRemainingLabel);
    auto *fcRemUnit = new QLabel("剩余时间");
    fcRemUnit->setAlignment(Qt::AlignCenter);
    fcRemUnit->setStyleSheet("font-size: 10px; color: #888;");
    fcRemCol->addWidget(fcRemUnit);
    fcMetricsRow->addLayout(fcRemCol);
    fcLayout->addLayout(fcMetricsRow);

    // 进度条
    auto *fcProgressRow = new QHBoxLayout();
    flowProgressLabel = new QLabel("0/0 min");
    flowProgressLabel->setStyleSheet("font-size: 10px; color: #888;");
    fcProgressRow->addWidget(flowProgressLabel);
    flowProgressBar = new QFrame();
    flowProgressBar->setFixedHeight(5);
    flowProgressBar->setStyleSheet("background: #333; border-radius: 3px;");
    auto *fpBarLayout = new QHBoxLayout(flowProgressBar);
    fpBarLayout->setContentsMargins(0, 0, 0, 0);
    flowProgressFill = new QFrame();
    flowProgressFill->setFixedHeight(5);
    flowProgressFill->setStyleSheet("background: #4caf50; border-radius: 3px;");
    flowProgressFill->setMinimumWidth(0);
    fpBarLayout->addWidget(flowProgressFill);
    fpBarLayout->addStretch();
    fcProgressRow->addWidget(flowProgressBar, 1);
    fcLayout->addLayout(fcProgressRow);

    // 按钮行
    auto *fcBtnRow = new QHBoxLayout();
    flowStopBtn = new QPushButton("⏹ 停止灌溉");
    flowStopBtn->setStyleSheet("background: #c62828; color: #fff; padding: 4px 12px; font-size: 12px; border-radius: 3px;");
    connect(flowStopBtn, &QPushButton::clicked, this, [this]() {
        // 手动停止：发送关闭命令 + 取消 activeJob + 记日志
        scheduleLoraCommand("pump", 4);
        if (activeJobs.contains("pump")) {
            ActiveJob &job = activeJobs["pump"];
            ScheduleLogEntry log;
            log.deviceType = "pump";
            log.scheduleId = job.scheduleId;
            log.action = "off";
            log.trigger = "manual";
            log.flowRate = job.flowRate;
            double elapsedMin = job.startTime.secsTo(QDateTime::currentDateTime()) / 60.0;
            log.volumeL = job.flowRate * qMax(0.0, elapsedMin);
            log.ts = QDateTime::currentDateTime();
            ScheduleStore::instance()->appendLog(log);
            activeJobs.remove("pump");
        }
        pumpBtn->blockSignals(true);
        pumpBtn->setChecked(false);
        pumpBtn->blockSignals(false);
        flowCard->setVisible(false);
        appendChatMessage("系统", "💧 灌溉泵: 已手动停止");
    });
    fcBtnRow->addWidget(flowStopBtn);
    flowSettingsBtn = new QPushButton("⚙ 设置流速");
    flowSettingsBtn->setStyleSheet("background: #333; color: #ccc; padding: 4px 12px; font-size: 12px; border-radius: 3px;");
    fcBtnRow->addWidget(flowSettingsBtn);
    fcLayout->addLayout(fcBtnRow);

    leftPanel->addWidget(flowCard);

    // ── 设备控制按钮行 ──
    auto *devRow = new QHBoxLayout();
    devRow->setSpacing(4);
    pumpBtn = new QPushButton("💧 灌溉泵");
    pumpBtn->setCheckable(true);
    connect(pumpBtn, &QPushButton::toggled, this, &TeaGardenSystem::onPumpToggled);
    devRow->addWidget(pumpBtn);
    scheduleBtnPump = new QPushButton("⏰");
    scheduleBtnPump->setFixedSize(30, 30);
    scheduleBtnPump->setToolTip("灌溉定时计划");
    scheduleBtnPump->setCursor(Qt::PointingHandCursor);
    connect(scheduleBtnPump, &QPushButton::clicked, this, [this]() { openScheduleDialog("pump"); });
    devRow->addWidget(scheduleBtnPump);

    lampBtn = new QPushButton("🪲 驱虫灯");
    lampBtn->setCheckable(true);
    connect(lampBtn, &QPushButton::toggled, this, &TeaGardenSystem::onLampToggled);
    devRow->addWidget(lampBtn);
    scheduleBtnLamp = new QPushButton("⏰");
    scheduleBtnLamp->setFixedSize(30, 30);
    scheduleBtnLamp->setToolTip("驱虫灯定时计划");
    scheduleBtnLamp->setCursor(Qt::PointingHandCursor);
    connect(scheduleBtnLamp, &QPushButton::clicked, this, [this]() { openScheduleDialog("lamp"); });
    devRow->addWidget(scheduleBtnLamp);

    leftPanel->addLayout(devRow);
```

This replaces the old devRow section. The flowCard (initially hidden) sits between mockPestBtn and devRow.

- [ ] **Step 3: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: add ⏰ buttons and flow card to setupUI"
```

---

### Task 8: 更新 TeaGardenSystem — 构造函数变更

**Files:**
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: Add schedule store init and timers in constructor**

After line 108 (after `PestStore::instance()->init(...)`), add:

```cpp
    // 初始化定时调度存储
    ScheduleStore::instance()->init(QCoreApplication::applicationDirPath() + "/schedule.db");
```

After line 141 (after `refreshWeatherAlert();`), add:

```cpp
    // 定时调度检查（每分钟）
    scheduleTimer = new QTimer(this);
    connect(scheduleTimer, &QTimer::timeout, this, &TeaGardenSystem::checkSchedules);
    scheduleTimer->start(60000);
    checkSchedules();  // 启动时立刻检查一次

    // 流量卡片刷新（每秒）
    flowCardTimer = new QTimer(this);
    connect(flowCardTimer, &QTimer::timeout, this, &TeaGardenSystem::refreshFlowCard);
    flowCardTimer->start(1000);
```

- [ ] **Step 2: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: init ScheduleStore + schedule/flow timers in constructor"
```

---

### Task 9: 更新 TeaGardenSystem — 调度逻辑 + 流量刷新 + 对话框

**Files:**
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: Add checkSchedules(), refreshFlowCard(), openScheduleDialog() implementations**

Add the following new methods before the `// ── LoRa 下行控制 ──` comment (before line 1551):

```cpp
// ── 定时调度 ──

void TeaGardenSystem::checkSchedules() {
    QDateTime now = QDateTime::currentDateTime();
    int dayOfWeek = now.date().dayOfWeek();  // Qt: 1=Monday
    QString timeStr = now.toString("HH:mm");

    // 1) 检查该关闭的任务
    QStringList devices = {"pump", "lamp"};
    for (const QString &dev : devices) {
        if (activeJobs.contains(dev) && now >= activeJobs[dev].endTime) {
            ActiveJob &job = activeJobs[dev];
            int offCmd = (dev == "pump") ? 4 : 2;
            scheduleLoraCommand(dev, offCmd);

            ScheduleLogEntry log;
            log.deviceType = dev;
            log.scheduleId = job.scheduleId;
            log.action = "off";
            log.trigger = "schedule";
            log.flowRate = job.flowRate;
            log.volumeL = job.flowRate * job.durationMin;  // 满时长完成
            log.ts = now;
            ScheduleStore::instance()->appendLog(log);

            if (dev == "pump") {
                flowCard->setVisible(false);
                pumpBtn->blockSignals(true);
                pumpBtn->setChecked(false);
                pumpBtn->blockSignals(false);
                appendChatMessage("系统",
                    QString("💧 定时灌溉结束 · 计划#%1 · 累计 %2 L")
                        .arg(job.scheduleId).arg(log.volumeL, 0, 'f', 1));
            } else {
                lampBtn->blockSignals(true);
                lampBtn->setChecked(false);
                lampBtn->blockSignals(false);
                appendChatMessage("系统",
                    QString("🪲 定时驱虫结束 · 计划#%1").arg(job.scheduleId));
            }
            activeJobs.remove(dev);
        }
    }

    // 2) 检查该开启的任务
    for (const QString &dev : devices) {
        if (activeJobs.contains(dev)) continue;  // 已在运行
        QVector<SchedulePlan> plans = ScheduleStore::instance()->queryEnabledPlans(dev, dayOfWeek, timeStr);
        if (plans.isEmpty()) continue;

        // 取第一个匹配的计划（多个同时间的也只执行一个）
        const SchedulePlan &plan = plans.first();
        int onCmd = (dev == "pump") ? 3 : 1;
        scheduleLoraCommand(dev, onCmd);

        ScheduleLogEntry log;
        log.deviceType = dev;
        log.scheduleId = plan.id;
        log.action = "on";
        log.trigger = "schedule";
        double flowRate = (dev == "pump") ? QSettings().value("pump/flow_rate", 2.5).toDouble() : 0;
        log.flowRate = flowRate;
        log.volumeL = 0;
        log.ts = now;
        ScheduleStore::instance()->appendLog(log);

        ActiveJob job;
        job.deviceType = dev;
        job.scheduleId = plan.id;
        job.startTime = now;
        job.endTime = now.addSecs(plan.durationMin * 60);
        job.flowRate = flowRate;
        job.durationMin = plan.durationMin;
        activeJobs[dev] = job;

        if (dev == "pump") {
            pumpBtn->blockSignals(true);
            pumpBtn->setChecked(true);
            pumpBtn->blockSignals(false);
            flowPlanIdLabel->setText(QString("计划 #%1").arg(plan.id));
            flowCard->setVisible(true);
            refreshFlowCard();
            double expectedVol = flowRate * plan.durationMin;
            appendChatMessage("系统",
                QString("💧 定时灌溉开始 · 计划#%1 · 预计 %2 L · %3 分钟")
                    .arg(plan.id).arg(expectedVol, 0, 'f', 1).arg(plan.durationMin));
        } else {
            lampBtn->blockSignals(true);
            lampBtn->setChecked(true);
            lampBtn->blockSignals(false);
            appendChatMessage("系统",
                QString("🪲 定时驱虫开始 · 计划#%1 · %2 分钟")
                    .arg(plan.id).arg(plan.durationMin));
        }
    }
}

void TeaGardenSystem::refreshFlowCard() {
    if (!activeJobs.contains("pump") || !flowCard->isVisible()) return;

    ActiveJob &job = activeJobs["pump"];
    QDateTime now = QDateTime::currentDateTime();
    double elapsedMin = job.startTime.secsTo(now) / 60.0;
    if (elapsedMin < 0) elapsedMin = 0;
    double currentVol = job.flowRate * elapsedMin;
    double expectedVol = job.flowRate * job.durationMin;
    double remainingSec = now.secsTo(job.endTime);
    if (remainingSec < 0) remainingSec = 0;

    flowCurrentLabel->setText(QString("%1").arg(currentVol, 0, 'f', 1));
    flowExpectedLabel->setText(QString("%1").arg(expectedVol, 0, 'f', 1));
    int remMin = (int)remainingSec / 60;
    int remSec = (int)remainingSec % 60;
    flowRemainingLabel->setText(QString("%1:%2")
        .arg(remMin, 2, 10, QChar('0')).arg(remSec, 2, 10, QChar('0')));
    flowProgressLabel->setText(QString("%1/%2 min")
        .arg((int)elapsedMin).arg(job.durationMin));

    // 更新进度条
    double ratio = (job.durationMin > 0) ? qMin(elapsedMin / job.durationMin, 1.0) : 0;
    int barWidth = flowProgressBar->width();
    flowProgressFill->setFixedWidth((int)(barWidth * ratio));
}

void TeaGardenSystem::openScheduleDialog(const QString &deviceType) {
    auto *dlg = new ScheduleDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setDeviceFilter(deviceType);
    dlg->refresh();
    dlg->show();
}
```

- [ ] **Step 2: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: implement checkSchedules + refreshFlowCard + openScheduleDialog"
```

---

### Task 10: 更新 TeaGardenSystem — 重写按钮回调（手动干预处理）

**Files:**
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: Modify onPumpToggled and onLampToggled**

Replace the existing implementations (lines 1593–1603) with:

```cpp
void TeaGardenSystem::onPumpToggled(bool checked) {
    // 3 = 开启灌溉泵, 4 = 关闭灌溉泵
    scheduleLoraCommand("pump", checked ? 3 : 4);

    if (checked) {
        // 手动开启 — 如果定时也在运行则覆盖为手动模式
        if (activeJobs.contains("pump")) {
            // 定时运行中被手动切换 → 取消定时任务
            activeJobs.remove("pump");
            flowCard->setVisible(false);
        }
        ScheduleLogEntry log;
        log.deviceType = "pump";
        log.scheduleId = 0;
        log.action = "on";
        log.trigger = "manual";
        log.flowRate = QSettings().value("pump/flow_rate", 2.5).toDouble();
        log.volumeL = 0;
        log.ts = QDateTime::currentDateTime();
        ScheduleStore::instance()->appendLog(log);
        appendChatMessage("系统", "💧 灌溉泵: 已手动开启");
    } else {
        // 手动关闭
        if (activeJobs.contains("pump")) {
            ActiveJob &job = activeJobs["pump"];
            ScheduleLogEntry log;
            log.deviceType = "pump";
            log.scheduleId = job.scheduleId;
            log.action = "off";
            log.trigger = "manual";
            log.flowRate = job.flowRate;
            double elapsedMin = job.startTime.secsTo(QDateTime::currentDateTime()) / 60.0;
            log.volumeL = job.flowRate * qMax(0.0, elapsedMin);
            log.ts = QDateTime::currentDateTime();
            ScheduleStore::instance()->appendLog(log);
            activeJobs.remove("pump");
            flowCard->setVisible(false);
            appendChatMessage("系统",
                QString("💧 灌溉泵: 已手动关闭 · 本次 %1 L").arg(log.volumeL, 0, 'f', 1));
        } else {
            ScheduleLogEntry log;
            log.deviceType = "pump";
            log.scheduleId = 0;
            log.action = "off";
            log.trigger = "manual";
            log.flowRate = 0;
            log.volumeL = 0;
            log.ts = QDateTime::currentDateTime();
            ScheduleStore::instance()->appendLog(log);
            appendChatMessage("系统", "💧 灌溉泵: 已关闭");
        }
    }
}

void TeaGardenSystem::onLampToggled(bool checked) {
    // 1 = 开启驱虫灯, 2 = 关闭驱虫灯
    scheduleLoraCommand("lamp", checked ? 1 : 2);

    if (checked) {
        if (activeJobs.contains("lamp")) {
            activeJobs.remove("lamp");
        }
        ScheduleLogEntry log;
        log.deviceType = "lamp";
        log.scheduleId = 0;
        log.action = "on";
        log.trigger = "manual";
        log.ts = QDateTime::currentDateTime();
        ScheduleStore::instance()->appendLog(log);
        appendChatMessage("系统", "🪲 驱虫灯: 已手动开启");
    } else {
        if (activeJobs.contains("lamp")) {
            ActiveJob &job = activeJobs["lamp"];
            ScheduleLogEntry log;
            log.deviceType = "lamp";
            log.scheduleId = job.scheduleId;
            log.action = "off";
            log.trigger = "manual";
            log.ts = QDateTime::currentDateTime();
            ScheduleStore::instance()->appendLog(log);
            activeJobs.remove("lamp");
            appendChatMessage("系统",
                QString("🪲 驱虫灯: 已手动关闭 · 计划#%1 被中断").arg(job.scheduleId));
        } else {
            ScheduleLogEntry log;
            log.deviceType = "lamp";
            log.scheduleId = 0;
            log.action = "off";
            log.trigger = "manual";
            log.ts = QDateTime::currentDateTime();
            ScheduleStore::instance()->appendLog(log);
            appendChatMessage("系统", "🪲 驱虫灯: 已关闭");
        }
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: rewrite onPumpToggled/onLampToggled with manual override logging"
```

---

### Task 11: 更新 TeaGardenSystem — applyTheme 新增元素样式

**Files:**
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: Add theme variables for schedule buttons**

At line ~1046 (after `voiceBtnBg, videoStatusClr, chartBg;`), add:

```cpp
    QString schedBtnBg1, schedBtnBg2, schedBtnBorder, schedBtnText;
```

And in the dark mode assignment block (around line ~1070), add:

```cpp
        schedBtnBg1 = "#0c3050"; schedBtnBg2 = "#081830"; schedBtnBorder = "#1a6fa8"; schedBtnText = "#90caf9";
```

And in the light mode assignment block (around line ~1098), add:

```cpp
        schedBtnBg1 = "#e3f2fd"; schedBtnBg2 = "#bbdefb"; schedBtnBorder = "#64b5f6"; schedBtnText = "#0277bd";
```

- [ ] **Step 2: Add styling for schedule buttons and flow card**

After the lampBtn styling section (after the block around line ~1211), add:

```cpp
    // 定时按钮
    QString schedBtnStyle = QString(
        "QPushButton { background: %1; color: %2; font-size: 14px;"
        "border: 1px solid %3; border-radius: 3px; }"
        "QPushButton:hover { background: %4; }"
    ).arg(schedBtnBg1, schedBtnText, schedBtnBorder,
          isDarkMode ? "#124068" : "#bbdefb");
    if (scheduleBtnPump) scheduleBtnPump->setStyleSheet(schedBtnStyle);
    if (scheduleBtnLamp) scheduleBtnLamp->setStyleSheet(schedBtnStyle);

    // 流量卡片
    if (flowCard) {
        flowCard->setStyleSheet(QString(
            "QFrame#flowCard {"
            "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "    stop:0 %1, stop:1 %2);"
            "  border: 1px solid %3; border-left: 3px solid #4caf50;"
            "  border-radius: 6px;"
            "}"
        ).arg(isDarkMode ? "#1a3a2c" : "#e8f5e9",
              isDarkMode ? "#1e2a3a" : "#c8e6c9",
              border));
    }
```

- [ ] **Step 3: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: add theme styling for schedule buttons and flow card"
```

---

### Task 12: 构建验证

**Files:**
- Modify: (none — build only)

- [ ] **Step 1: Run qmake and make**

```bash
cd /home/lzy/文档/QTproject/test && qmake test.pro && make -j$(nproc) 2>&1
```

Expected: Compilation succeeds with no errors.

- [ ] **Step 2: Fix any compilation errors**

If compilation fails, check:
- Qt Charts module is linked (`QT += charts` is already in test.pro)
- All signals/slots have matching signatures
- `#include <QCheckBox>` in scheduledialog.cpp for PlanEditDialog
- `#include <QGroupBox>` in scheduledialog.cpp for the day selection group

- [ ] **Step 3: Quick smoke test — verify the binary runs**

```bash
cd /home/lzy/文档/QTproject/test && ./test 2>&1 &
```

Expected: Window opens without crash, ⏰ buttons visible, flow card hidden.

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "feat: irrigation and lamp scheduling — build verified"
```
