#ifndef CHARTDASHBOARD_H
#define CHARTDASHBOARD_H

#include <QWidget>
#include <QMap>
#include <QDateTime>
#include <QScrollBar>
#include <QtCharts/QChartView>

class QScrollArea;
class QDateTimeEdit;
class QPushButton;
class QLabel;

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
    void onTimeSliderChanged(int value);
    void onResetZoom();

private:
    void setupUI();
    void loadData(QDateTime from, QDateTime to);
    void updateSliderFromTimes(QDateTime from, QDateTime to);
    QWidget* createChartCard(const QString &title, const QString &unit, int sensorIndex);

    QScrollArea *scrollArea;
    QDateTimeEdit *fromTime, *toTime;
    QPushButton *thresholdBtn, *exportBtn, *resetZoomBtn;
    QMap<int, QtCharts::QChartView*> charts;
    QMap<int, QWidget*> chartCards;

    // 时间拖拉滑块
    QScrollBar *timeSlider;
    QLabel *sliderLabel;
    double dbEarliestSec = 0;
    double dbLatestSec   = 0;
    int    sliderSteps   = 720;  // 滑块总步数（每步可调节精度）
};

#endif // CHARTDASHBOARD_H
