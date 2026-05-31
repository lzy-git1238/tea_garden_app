#ifndef PESTDASHBOARD_H
#define PESTDASHBOARD_H

#include <QWidget>
#include "pestdata.h"

class QLabel;

class PestDashboard : public QWidget {
    Q_OBJECT
public:
    explicit PestDashboard(QWidget *parent = nullptr);
    void refresh();  // 外部调用刷新

private:
    void setupUI();
    void refreshTable();
    void refreshChart();
    void onClearData();

    QLabel *countLabel;
    class QTableWidget *table;
    class QtCharts::QChartView *chartView;
};

#endif // PESTDASHBOARD_H
