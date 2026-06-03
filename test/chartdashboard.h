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
    void applyTheme(bool isDarkMode);

private slots:
    void onPreset1h();
    void onPreset6h();
    void onPreset24h();
    void onPreset7d();
    void onPreset30d();
    void onCustomQuery();
    void onOpenThresholds();
    void onExportExcel();
    void onCardSliderChanged(int value);

private:
    void setupUI();
    void loadData(QDateTime from, QDateTime to);
    void syncAllSliders();
    void syncSliderFromTimes(QScrollBar *slider, QDateTime from, QDateTime to);
    void zoomChartX(int sensorIndex, double factor);
    void zoomChartY(int sensorIndex, double factor);
    QWidget* createChartCard(const QString &title, const QString &unit, int sensorIndex);
    bool eventFilter(QObject *obj, QEvent *event) override;

    QScrollArea *scrollArea;
    QDateTimeEdit *fromTime, *toTime;
    QPushButton *thresholdBtn, *exportBtn;
    QMap<int, QtCharts::QChartView*> charts;
    QMap<int, QWidget*> chartCards;
    QMap<int, QScrollBar*> cardSliders;

    QPoint resizeStartPos;
    int   resizeStartH = 0;
    int   activeResizeIndex = -1;

    double dbEarliestSec = 0;
    double dbLatestSec   = 0;
    static constexpr int sliderSteps = 720;
    bool isDarkMode = true;
};

#endif // CHARTDASHBOARD_H
