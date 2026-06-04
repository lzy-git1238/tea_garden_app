#ifndef SCHEDULEDIALOG_H
#define SCHEDULEDIALOG_H

#include <QDialog>
#include "schedulestore.h"

#include <QtCharts/QChartView>

class QTabWidget;
class QComboBox;
class QTableWidget;
class QPushButton;
class QLabel;
class QCheckBox;
class QTimeEdit;
class QSpinBox;
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
    QtCharts::QChartView *chartView;
    QTextEdit *logText;
    QLabel *summaryLabel;
};

#endif // SCHEDULEDIALOG_H
