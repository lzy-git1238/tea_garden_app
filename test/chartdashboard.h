#ifndef CHARTDASHBOARD_H
#define CHARTDASHBOARD_H

#include <QWidget>
#include <QMap>
#include <QDateTime>
#include <QtCharts/QChartView>

class QScrollArea;
class QDateTimeEdit;
class QPushButton;

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
    QMap<int, QtCharts::QChartView*> charts;
    QMap<int, QWidget*> chartCards;
};

#endif // CHARTDASHBOARD_H
