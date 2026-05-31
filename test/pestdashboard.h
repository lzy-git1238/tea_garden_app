#ifndef PESTDASHBOARD_H
#define PESTDASHBOARD_H

#include <QWidget>
#include "pestdata.h"
#include <QtCharts/QChartView>

class QLabel;
class QTableWidget;

class PestDashboard : public QWidget {
    Q_OBJECT
public:
    explicit PestDashboard(QWidget *parent = nullptr);
    void refresh();

private:
    void setupUI();
    void refreshTable();
    void refreshChart();
    void onClearData();

    QLabel *countLabel;
    QTableWidget *table;
    QtCharts::QChartView *chartView;
};

#endif // PESTDASHBOARD_H
