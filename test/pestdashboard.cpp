#include "pestdashboard.h"
#include "pestdata.h"

#include <QVBoxLayout>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>

using namespace QtCharts;

PestDashboard::PestDashboard(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("茶树病虫害监测");
    resize(650, 560);
    setStyleSheet("background-color: #051623; color: #E0E0E0; font-family: 'Microsoft YaHei';");
    setupUI();
    refresh();
}

void PestDashboard::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);

    // 标题行
    auto *topRow = new QHBoxLayout();
    countLabel = new QLabel("共检测到 0 处异常");
    countLabel->setStyleSheet("color: #00E5FF; font-size: 16px; font-weight: bold;");
    topRow->addWidget(countLabel);
    topRow->addStretch();

    auto *clearBtn = new QPushButton("清空数据");
    clearBtn->setStyleSheet(
        "QPushButton { background: #5a3020; color: #f0a0a0; padding: 6px 14px; font-weight: bold; "
        "border-top: 1px solid #804040; border-bottom: 1px solid #200808; border-radius: 4px; }"
        "QPushButton:hover { background: #7a4030; color: #fff; }");
    connect(clearBtn, &QPushButton::clicked, this, &PestDashboard::onClearData);
    topRow->addWidget(clearBtn);

    mainLayout->addLayout(topRow);

    // 表格
    table = new QTableWidget();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"时间", "位置(纬度,经度)", "病虫害种类", "严重程度"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet(
        "QTableWidget { background: #0a1628; color: #E0E0E0; gridline-color: #1a2a3a; border: 1px solid #1a3040; }"
        "QHeaderView::section { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #1a3040, stop:1 #0d1f30); "
        "color: #90A4AE; font-weight: bold; padding: 5px; border: 1px solid #1a2a3a; }"
        "QTableWidget::item { padding: 4px 6px; }"
        "QTableWidget::item:selected { background: #1a4050; color: #00E5FF; }");
    mainLayout->addWidget(table, 1);

    // 饼图
    auto *chart = new QChart();
    chart->setBackgroundBrush(QBrush(QColor("#051623")));
    chart->legend()->setLabelColor(QColor("#90A4AE"));
    chart->setTitle("病虫害种类分布");
    QFont titleFont;
    titleFont.setPixelSize(14);
    chart->setTitleFont(titleFont);
    chart->setTitleBrush(QBrush(QColor("#00E5FF")));

    chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(200);
    chartView->setStyleSheet("background: transparent; border: none;");
    mainLayout->addWidget(chartView);

    // 底部按钮
    auto *btnRow = new QHBoxLayout();
    auto *refreshBtn = new QPushButton("🔄 刷新");
    refreshBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #2a5068, stop:1 #1a3040);"
        "  color: #c0d8f0; padding: 6px 16px; font-size: 13px; font-weight: bold; border-radius: 4px;"
        "  border-top: 1px solid #4a7080; border-bottom: 1px solid #0a1620; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #3a6080, stop:1 #284868); color: #fff; }");
    connect(refreshBtn, &QPushButton::clicked, this, &PestDashboard::refresh);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch();

    auto *closeBtn = new QPushButton("关闭");
    closeBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #1a3040, stop:1 #0d1f30);"
        "  color: #90A4AE; padding: 6px 16px; font-size: 13px; border-radius: 4px;"
        "  border-top: 1px solid #2a4050; border-bottom: 1px solid #081018; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #284868, stop:1 #183848); color: #fff; }");
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    btnRow->addWidget(closeBtn);
    mainLayout->addLayout(btnRow);
}

void PestDashboard::refresh() {
    refreshTable();
    refreshChart();
}

void PestDashboard::refreshTable() {
    QVector<PestRecord> data = PestStore::instance()->queryAll();
    int total = data.size();
    countLabel->setText(QString("共检测到 %1 处异常").arg(total));

    table->setRowCount(total);
    QStringList severities = {"", "轻微", "中等", "严重"};
    for (int i = 0; i < total; ++i) {
        const auto &r = data[i];
        table->setItem(i, 0, new QTableWidgetItem(r.timestamp.toString("MM-dd HH:mm:ss")));
        table->setItem(i, 1, new QTableWidgetItem(QString("%1, %2").arg(r.lat, 0, 'f', 6).arg(r.lng, 0, 'f', 6)));
        auto *typeItem = new QTableWidgetItem(r.pestType);
        // 种类颜色
        QString clr = "#4CAF50"; // 默认绿
        if (r.pestType.contains("绿叶蝉")) clr = "#FF9800";
        if (r.pestType.contains("炭疽")) clr = "#F44336";
        if (r.pestType.contains("尺蠖")) clr = "#4CAF50";
        typeItem->setForeground(QColor(clr));
        table->setItem(i, 2, typeItem);
        table->setItem(i, 3, new QTableWidgetItem(severities.value(r.severity, "?")));
    }

    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
}

void PestDashboard::refreshChart() {
    auto *chart = chartView->chart();
    chart->removeAllSeries();
    auto *series = new QPieSeries();

    QMap<QString, int> counts = PestStore::instance()->typeCounts();
    if (counts.isEmpty()) {
        series->append("暂无数据", 1);
        auto *sl = series->slices().first();
        sl->setColor(QColor("#455A64"));
        sl->setLabelVisible(false);
    } else {
        QMap<QString, QColor> colors;
        colors["茶尺蠖"]   = QColor("#4CAF50");
        colors["茶小绿叶蝉"] = QColor("#FF9800");
        colors["茶炭疽病"]  = QColor("#F44336");

        for (auto it = counts.begin(); it != counts.end(); ++it) {
            auto *sl = series->append(QString("%1 (%2)").arg(it.key()).arg(it.value()), it.value());
            sl->setColor(colors.value(it.key(), QColor("#2196F3")));
            sl->setLabelVisible(true);
            sl->setLabelColor(QColor("#E0E0E0"));
        }
    }

    chart->addSeries(series);
}

void PestDashboard::onClearData() {
    auto reply = QMessageBox::question(this, "确认清空", "确定清空所有病虫害数据？",
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (db.isOpen()) {
        QSqlQuery q(db);
        q.exec("DELETE FROM pest_log");
    }
    refresh();
}
