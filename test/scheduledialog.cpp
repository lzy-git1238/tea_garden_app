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
#include <QtCharts/QChartView>
#include <QBarSeries>
#include <QBarSet>
#include <QBarCategoryAxis>
#include <QValueAxis>
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
    // 先清除所有勾选，再按 plan 的数据设置
    for (int i = 0; i < 7; ++i) dayChecks[i]->setChecked(false);
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
