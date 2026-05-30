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
#include <QDir>
#include <QtMath>

using namespace QtCharts;

ChartDashboard::ChartDashboard(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("数据折线看板");
    resize(900, 750);
    setStyleSheet("background-color: #051623; color: #E0E0E0; font-family: 'Microsoft YaHei';");

    // 先查 DB 时间范围，用于滑块
    auto tr = DataStore::instance()->fullTimeRange();
    if (tr.valid) {
        dbEarliestSec = tr.earliestSec;
        dbLatestSec   = tr.latestSec;
    }

    setupUI();
    loadData(QDateTime::currentDateTime().addSecs(-3600), QDateTime::currentDateTime());
}

void ChartDashboard::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);

    // ===== top toolbar =====
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

    resetZoomBtn = new QPushButton("🔍 重置缩放");
    resetZoomBtn->setStyleSheet("background: #455A64; color: white; padding: 6px 12px; "
                                "border: 1px solid #334455; border-radius: 4px;");
    connect(resetZoomBtn, &QPushButton::clicked, this, &ChartDashboard::onResetZoom);
    toolbar->addWidget(resetZoomBtn);

    mainLayout->addLayout(toolbar);

    // ===== chart scroll area =====
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

    // ===== 底部时间拖拉滑块 =====
    auto *sliderBar = new QHBoxLayout();

    sliderLabel = new QLabel("时间轴拖动");
    sliderLabel->setStyleSheet("color: #90A4AE; font-size: 11px;");
    sliderBar->addWidget(sliderLabel);

    timeSlider = new QScrollBar(Qt::Horizontal);
    timeSlider->setMinimum(0);
    timeSlider->setMaximum(sliderSteps);
    timeSlider->setValue(0);
    timeSlider->setPageStep(sliderSteps / 10);
    timeSlider->setStyleSheet(
        "QScrollBar:horizontal { background: #1a2a3a; height: 20px; border-radius: 4px; }"
        "QScrollBar::handle:horizontal { background: #00E5FF; min-width: 30px; border-radius: 4px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: #0a1a2a; }");
    connect(timeSlider, &QScrollBar::valueChanged, this, &ChartDashboard::onTimeSliderChanged);
    sliderBar->addWidget(timeSlider, 1);

    mainLayout->addLayout(sliderBar);
}

QWidget* ChartDashboard::createChartCard(const QString &title, const QString &unit, int sensorIndex) {
    auto *card = new QWidget();
    card->setStyleSheet("background: rgba(0,0,0,0.3); border: 1px solid #1a2a3a; border-radius: 6px;");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(8, 4, 8, 4);

    auto *header = new QPushButton("📈 " + title + "  ▼");
    header->setStyleSheet("QPushButton { background: transparent; color: #00E5FF; "
                          "font-size: 13px; font-weight: bold; text-align: left; "
                          "border: none; padding: 4px 0; }");
    header->setCursor(Qt::PointingHandCursor);

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
    // 启用框选缩放 + 滚轮缩放
    chartView->setRubberBand(QChartView::RectangleRubberBand);
    chartView->setDragMode(QGraphicsView::NoDrag);
    charts[sensorIndex] = chartView;

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

    struct SensorDef {
        QString unit;
        double SensorSnapshot::*field;
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

        chart->removeAllSeries();
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

        auto *lineSeries = new QLineSeries();
        lineSeries->setPen(QPen(QColor("#00E5FF"), 1.5));

        auto *alertScatter = new QScatterSeries();
        alertScatter->setMarkerSize(10);
        alertScatter->setColor(QColor("#FF5252"));
        alertScatter->setBorderColor(QColor("#FF5252"));

        auto *upperLine = new QLineSeries();
        upperLine->setPen(QPen(QColor("#FF5252"), 1, Qt::DashLine));

        auto *lowerLine = new QLineSeries();
        lowerLine->setPen(QPen(QColor("#FF9800"), 1, Qt::DashLine));

        double minY = 1e18, maxY = -1e18;
        int alertCount = 0;

        ThresholdDialog::Thresholds th = thresholds.value(i);

        for (const auto &snap : data) {
            double fieldVal = snap.*(sensors[i].field);
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

        qreal fromEpoch = from.toMSecsSinceEpoch();
        qreal toEpoch   = to.toMSecsSinceEpoch();
        upperLine->append(fromEpoch, th.upper);
        upperLine->append(toEpoch,   th.upper);
        lowerLine->append(fromEpoch, th.lower);
        lowerLine->append(toEpoch,   th.lower);

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

        auto *card = chartCards.value(i);
        if (card) {
            auto *header = card->findChild<QPushButton*>();
            if (header) {
                QString txt = header->text();
                int spIdx = txt.indexOf("  ");
                QString base = (spIdx >= 0) ? txt.left(spIdx) : txt;
                if (alertCount > 0) {
                    header->setText(base + QString("  ⚠ 超限 %1 处").arg(alertCount));
                } else {
                    header->setText(base + "  ✓ 正常");
                }
            }
        }
    }

    // 同步时间滑块
    updateSliderFromTimes(from, to);
}

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

// ---- 时间拖拉滑块 ----

void ChartDashboard::updateSliderFromTimes(QDateTime from, QDateTime to) {
    // 刷新 DB 范围（有新数据到达时范围会扩大）
    auto tr = DataStore::instance()->fullTimeRange();
    if (tr.valid) {
        dbEarliestSec = tr.earliestSec;
        dbLatestSec   = tr.latestSec;
    }

    double totalRange = dbLatestSec - dbEarliestSec;
    if (totalRange <= 0) {
        timeSlider->setEnabled(false);
        return;
    }
    timeSlider->setEnabled(true);

    double fromSec = from.toMSecsSinceEpoch() / 1000.0;
    double toSec   = to.toMSecsSinceEpoch() / 1000.0;
    double visibleWidth = toSec - fromSec;

    int pos  = static_cast<int>((fromSec - dbEarliestSec) / totalRange * sliderSteps);
    int page = static_cast<int>(visibleWidth / totalRange * sliderSteps);
    if (page < 1) page = 1;
    if (page > sliderSteps) page = sliderSteps;

    timeSlider->blockSignals(true);
    timeSlider->setPageStep(page);
    timeSlider->setValue(pos);
    timeSlider->blockSignals(false);

    sliderLabel->setText(
        QString("时间轴 %1 ~ %2")
            .arg(from.toString("MM-dd hh:mm"))
            .arg(to.toString("MM-dd hh:mm")));
}

void ChartDashboard::onTimeSliderChanged(int value) {
    double totalRange = dbLatestSec - dbEarliestSec;
    if (totalRange <= 0) return;

    double visibleSec = static_cast<double>(timeSlider->pageStep()) / sliderSteps * totalRange;
    double newFromSec = dbEarliestSec + static_cast<double>(value) / sliderSteps * totalRange;
    double newToSec   = newFromSec + visibleSec;

    QDateTime from = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(newFromSec * 1000.0));
    QDateTime to   = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(newToSec * 1000.0));

    fromTime->setDateTime(from);
    toTime->setDateTime(to);
    loadData(from, to);
}

// ---- 缩放重置 ----

void ChartDashboard::onResetZoom() {
    for (auto *cv : charts) {
        if (cv) cv->chart()->zoomReset();
    }
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

    QTemporaryFile scriptFile;
    scriptFile.setFileTemplate(QDir::tempPath() + "/tea_export_XXXXXX.py");
    scriptFile.open();

    QTextStream py(&scriptFile);
    py << "import sys, json, os\n"
          "try:\n"
          "    from openpyxl import Workbook\n"
          "    from openpyxl.styles import Font, Alignment, PatternFill\n"
          "except ImportError:\n"
          "    os.system(f'{sys.executable} -m pip install openpyxl')\n"
          "    from openpyxl import Workbook\n"
          "    from openpyxl.styles import Font, Alignment, PatternFill\n"
          "\n"
          "wb = Workbook()\n"
          "ws = wb.active\n"
          "ws.title = '传感器数据'\n"
          "headers = ['时间','风速(m/s)','空气温度(℃)','空气湿度(%)','光照(Lux)',"
          "'土壤湿度(%)','土壤温度(℃)','土壤电导率(mS/cm)','土壤pH']\n"
          "for c, h in enumerate(headers, 1):\n"
          "    cell = ws.cell(row=1, column=c, value=h)\n"
          "    cell.font = Font(bold=True, color='FFFFFF')\n"
          "    cell.fill = PatternFill('solid', fgColor='00897B')\n"
          "    cell.alignment = Alignment(horizontal='center')\n"
          "\n"
          "arr = json.loads(sys.stdin.read())\n"
          "for r, row in enumerate(arr, 2):\n"
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
