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

// ── 立体 3D 按钮样式 ──
static const char* k3dSmallBtnStyle = ""
    "QPushButton {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #3a5068, stop:0.5 #1e3448, stop:1 #0f1f30);"
    "  color: #c0d8f0; font-size: 11px; font-weight: bold; min-width: 28px; max-width: 28px;"
    "  min-height: 24px; max-height: 24px; padding: 0;"
    "  border-top: 1px solid #5080b0; border-left: 1px solid #406890;"
    "  border-right: 1px solid #102030; border-bottom: 2px solid #081018;"
    "  border-radius: 4px;"
    "}"
    "QPushButton:hover {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #4a7090, stop:0.5 #2e5070, stop:1 #1a3048);"
    "  color: #ffffff;"
    "  border-top: 1px solid #70b0e0; border-left: 1px solid #5090c0;"
    "}"
    "QPushButton:pressed {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #0f1f30, stop:0.5 #1e3448, stop:1 #3a5068);"
    "  border-top: 1px solid #102030; border-bottom: 1px solid #5080b0;"
    "  padding-top: 2px;"
    "}";

static const char* k3dZoomGreenStyle = ""
    "QPushButton {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #3a6850, stop:0.5 #1e4830, stop:1 #0f3018);"
    "  color: #c0f0d0; font-size: 11px; font-weight: bold; min-width: 28px; max-width: 28px;"
    "  min-height: 24px; max-height: 24px; padding: 0;"
    "  border-top: 1px solid #50b070; border-left: 1px solid #409060;"
    "  border-right: 1px solid #103020; border-bottom: 2px solid #081810;"
    "  border-radius: 4px;"
    "}"
    "QPushButton:hover {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #4a9070, stop:0.5 #2e7050, stop:1 #1a4830);"
    "  color: #ffffff;"
    "  border-top: 1px solid #70e0b0; border-left: 1px solid #50c090;"
    "}"
    "QPushButton:pressed {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #0f3018, stop:0.5 #1e4830, stop:1 #3a6850);"
    "  border-top: 1px solid #103020; border-bottom: 1px solid #50b070;"
    "  padding-top: 2px;"
    "}";

static const char* k3dZoomOrangeStyle = ""
    "QPushButton {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #684830, stop:0.5 #482818, stop:1 #301810);"
    "  color: #f0d8c0; font-size: 11px; font-weight: bold; min-width: 28px; max-width: 28px;"
    "  min-height: 24px; max-height: 24px; padding: 0;"
    "  border-top: 1px solid #b08050; border-left: 1px solid #906040;"
    "  border-right: 1px solid #302010; border-bottom: 2px solid #181008;"
    "  border-radius: 4px;"
    "}"
    "QPushButton:hover {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #907050, stop:0.5 #705030, stop:1 #483020);"
    "  color: #ffffff;"
    "  border-top: 1px solid #e0b070; border-left: 1px solid #c09050;"
    "}"
    "QPushButton:pressed {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #301810, stop:0.5 #482818, stop:1 #684830);"
    "  border-top: 1px solid #302010; border-bottom: 1px solid #b08050;"
    "  padding-top: 2px;"
    "}";

static const char* k3dSliderStyle = ""
    "QScrollBar:horizontal {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #0a1620, stop:0.5 #111d28, stop:1 #0a1620);"
    "  height: 18px; border-radius: 4px;"
    "  border-top: 1px solid #1a3040; border-bottom: 1px solid #051018;"
    "}"
    "QScrollBar::handle:horizontal {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #28c8e8, stop:0.4 #18a0c0, stop:0.6 #108090, stop:1 #0a5060);"
    "  min-width: 36px; border-radius: 4px; margin: 1px 0;"
    "  border-top: 1px solid #60e0f8; border-left: 1px solid #38c8e0;"
    "  border-right: 1px solid #085060; border-bottom: 2px solid #043040;"
    "}"
    "QScrollBar::handle:horizontal:hover {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #38e0ff, stop:0.4 #20c8e8, stop:0.6 #10a0b8, stop:1 #0a7088);"
    "}"
    "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
    "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: #0a1620; }";

// ── 构造 ──

ChartDashboard::ChartDashboard(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("数据折线看板");
    resize(900, 780);
    setStyleSheet("background-color: #051623; color: #E0E0E0; font-family: 'Microsoft YaHei';");

    auto tr = DataStore::instance()->fullTimeRange();
    if (tr.valid) {
        dbEarliestSec = tr.earliestSec;
        dbLatestSec   = tr.latestSec;
    }

    setupUI();
    loadData(QDateTime::currentDateTime().addSecs(-3600), QDateTime::currentDateTime());
}

// ── setupUI ──

void ChartDashboard::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);

    // ===== 顶部工具栏 =====
    auto *toolbar = new QHBoxLayout();

    auto makeBtn = [](const QString &text) {
        auto *b = new QPushButton(text);
        b->setStyleSheet(k3dSmallBtnStyle);
        return b;
    };

    auto *btn1h  = makeBtn("1h");   btn1h->setFixedWidth(48);
    auto *btn6h  = makeBtn("6h");   btn6h->setFixedWidth(48);
    auto *btn24h = makeBtn("24h");  btn24h->setFixedWidth(52);
    auto *btn7d  = makeBtn("7d");    btn7d->setFixedWidth(48);

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
    fromTime->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455; padding: 2px 4px;");
    toolbar->addWidget(fromTime);

    toolbar->addWidget(new QLabel("至"));
    toTime = new QDateTimeEdit(QDateTime::currentDateTime());
    toTime->setDisplayFormat("yyyy-MM-dd hh:mm");
    toTime->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455; padding: 2px 4px;");
    toolbar->addWidget(toTime);

    auto *queryBtn = new QPushButton("查询");
    queryBtn->setStyleSheet(k3dZoomGreenStyle);
    queryBtn->setFixedWidth(56);
    connect(queryBtn, &QPushButton::clicked, this, &ChartDashboard::onCustomQuery);
    toolbar->addWidget(queryBtn);

    toolbar->addStretch();

    thresholdBtn = new QPushButton("⚙ 阈值设置");
    thresholdBtn->setStyleSheet(k3dSmallBtnStyle);
    thresholdBtn->setFixedWidth(100);
    connect(thresholdBtn, &QPushButton::clicked, this, &ChartDashboard::onOpenThresholds);
    toolbar->addWidget(thresholdBtn);

    exportBtn = new QPushButton("📤 导出Excel");
    exportBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #3a6840, stop:0.5 #1e4820, stop:1 #0f3010);"
        "  color: #d0f0d0; font-size: 13px; font-weight: bold; padding: 6px 14px;"
        "  border-top: 1px solid #50b060; border-left: 1px solid #409050;"
        "  border-right: 1px solid #103018; border-bottom: 2px solid #081808;"
        "  border-radius: 5px;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #4a9050, stop:0.5 #2e7030, stop:1 #1a4818);"
        "  color: #ffffff;"
        "}"
        "QPushButton:pressed {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #0f3010, stop:0.5 #1e4820, stop:1 #3a6840);"
        "  border-top: 1px solid #103018; border-bottom: 1px solid #50b060;"
        "  padding-top: 2px;"
        "}"
    );
    connect(exportBtn, &QPushButton::clicked, this, &ChartDashboard::onExportExcel);
    toolbar->addWidget(exportBtn);

    mainLayout->addLayout(toolbar);

    // ===== 图表滚动区 =====
    scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");
    auto *scrollContent = new QWidget();
    auto *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setSpacing(8);

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
}

// ── createChartCard：每张图带缩放按钮 + 滑块 ──

QWidget* ChartDashboard::createChartCard(const QString &title, const QString &unit, int sensorIndex) {
    Q_UNUSED(unit);

    auto *card = new QWidget();
    card->setStyleSheet("background: rgba(0,0,0,0.3); border: 1px solid #1a2a3a; border-radius: 6px;");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(8, 4, 8, 4);
    cardLayout->setSpacing(4);

    // ── 标题行：标题 + 缩放按钮 ──
    auto *titleRow = new QHBoxLayout();

    auto *header = new QPushButton("📈 " + title + "  ▼");
    header->setStyleSheet("QPushButton { background: transparent; color: #00E5FF; "
                          "font-size: 13px; font-weight: bold; text-align: left; "
                          "border: none; padding: 4px 0; }");
    header->setCursor(Qt::PointingHandCursor);
    titleRow->addWidget(header);
    titleRow->addStretch();

    // X 轴缩放按钮
    auto *xLabel = new QLabel("X");
    xLabel->setStyleSheet("color: #7890a8; font-size: 11px; font-weight: bold; border: none;");
    titleRow->addWidget(xLabel);

    auto *xOut = new QPushButton("−");
    auto *xIn  = new QPushButton("+");
    xOut->setStyleSheet(k3dSmallBtnStyle);
    xIn->setStyleSheet(k3dSmallBtnStyle);
    titleRow->addWidget(xOut);
    titleRow->addWidget(xIn);

    titleRow->addSpacing(8);

    // Y 轴缩放按钮
    auto *yLabel = new QLabel("Y");
    yLabel->setStyleSheet("color: #a08860; font-size: 11px; font-weight: bold; border: none;");
    titleRow->addWidget(yLabel);

    auto *yOut = new QPushButton("−");
    auto *yIn  = new QPushButton("+");
    yOut->setStyleSheet(k3dZoomOrangeStyle);
    yIn->setStyleSheet(k3dZoomOrangeStyle);
    titleRow->addWidget(yOut);
    titleRow->addWidget(yIn);

    cardLayout->addLayout(titleRow);

    // ── 图表 ──
    auto *chart = new QChart();
    chart->setTitle("");
    chart->setBackgroundBrush(QBrush(QColor("#051623")));
    chart->legend()->hide();
    chart->setAnimationOptions(QChart::SeriesAnimations);

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
    chartView->setMinimumHeight(180);
    chartView->setStyleSheet("background: transparent; border: none;");
    charts[sensorIndex] = chartView;

    // 缩放按钮信号
    connect(xOut, &QPushButton::clicked, this, [this, sensorIndex]() { zoomChartX(sensorIndex, 0.7); });
    connect(xIn,  &QPushButton::clicked, this, [this, sensorIndex]() { zoomChartX(sensorIndex, 1.3); });
    connect(yOut, &QPushButton::clicked, this, [this, sensorIndex]() { zoomChartY(sensorIndex, 0.7); });
    connect(yIn,  &QPushButton::clicked, this, [this, sensorIndex]() { zoomChartY(sensorIndex, 1.3); });

    // 折叠/展开
    connect(header, &QPushButton::clicked, this, [chartView, header]() {
        bool vis = !chartView->isVisible();
        chartView->setVisible(vis);
        header->setText(header->text().replace(vis ? "▶" : "▼", vis ? "▼" : "▶"));
    });

    cardLayout->addWidget(chartView);

    // ── 底部时间滑块 ──
    auto *slider = new QScrollBar(Qt::Horizontal);
    slider->setMinimum(0);
    slider->setMaximum(sliderSteps);
    slider->setValue(0);
    slider->setPageStep(sliderSteps / 10);
    slider->setStyleSheet(k3dSliderStyle);
    cardSliders[sensorIndex] = slider;

    connect(slider, &QScrollBar::valueChanged, this, &ChartDashboard::onCardSliderChanged);
    cardLayout->addWidget(slider);

    return card;
}

// ── loadData ──

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

    syncSliderFromTimes(nullptr, from, to);
}

// ── 滑块同步 ──

void ChartDashboard::syncSliderFromTimes(QScrollBar *slider, QDateTime from, QDateTime to) {
    auto tr = DataStore::instance()->fullTimeRange();
    if (tr.valid) {
        dbEarliestSec = tr.earliestSec;
        dbLatestSec   = tr.latestSec;
    }

    double totalRange = dbLatestSec - dbEarliestSec;
    if (totalRange <= 0) {
        for (auto *s : cardSliders) s->setEnabled(false);
        return;
    }

    double fromSec = from.toMSecsSinceEpoch() / 1000.0;
    double toSec   = to.toMSecsSinceEpoch() / 1000.0;
    double visibleWidth = toSec - fromSec;

    int pos  = static_cast<int>((fromSec - dbEarliestSec) / totalRange * sliderSteps);
    int page = static_cast<int>(visibleWidth / totalRange * sliderSteps);
    if (page < 1) page = 1;
    if (page > sliderSteps) page = sliderSteps;

    QList<QScrollBar*> targets;
    if (slider) {
        targets.append(slider);
    } else {
        targets = cardSliders.values();
    }

    for (auto *s : targets) {
        s->setEnabled(true);
        s->blockSignals(true);
        s->setPageStep(page);
        s->setValue(pos);
        s->blockSignals(false);
    }
}

void ChartDashboard::onCardSliderChanged(int value) {
    auto *senderSlider = qobject_cast<QScrollBar*>(sender());
    if (!senderSlider) return;

    double totalRange = dbLatestSec - dbEarliestSec;
    if (totalRange <= 0) return;

    double visibleSec = static_cast<double>(senderSlider->pageStep()) / sliderSteps * totalRange;
    double newFromSec = dbEarliestSec + static_cast<double>(value) / sliderSteps * totalRange;
    double newToSec   = newFromSec + visibleSec;

    QDateTime from = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(newFromSec * 1000.0));
    QDateTime to   = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(newToSec * 1000.0));

    fromTime->setDateTime(from);
    toTime->setDateTime(to);
    loadData(from, to);
}

// ── 按钮缩放 ──

void ChartDashboard::zoomChartX(int sensorIndex, double factor) {
    auto *chartView = charts.value(sensorIndex);
    if (!chartView) return;
    auto *chart = chartView->chart();

    // 找到 X 轴
    for (auto *ax : chart->axes(Qt::Horizontal)) {
        auto *dtAxis = qobject_cast<QDateTimeAxis*>(ax);
        if (!dtAxis) continue;
        QDateTime center(QDateTime::fromMSecsSinceEpoch(
            (dtAxis->min().toMSecsSinceEpoch() + dtAxis->max().toMSecsSinceEpoch()) / 2));
        qint64 halfRange = (dtAxis->max().toMSecsSinceEpoch() - dtAxis->min().toMSecsSinceEpoch()) / 2;
        qint64 newHalf = static_cast<qint64>(halfRange * factor);
        if (newHalf < 1000) newHalf = 1000; // 最小 1 秒
        dtAxis->setRange(center.addMSecs(-newHalf), center.addMSecs(newHalf));
    }
}

void ChartDashboard::zoomChartY(int sensorIndex, double factor) {
    auto *chartView = charts.value(sensorIndex);
    if (!chartView) return;
    auto *chart = chartView->chart();

    for (auto *ax : chart->axes(Qt::Vertical)) {
        auto *valAxis = qobject_cast<QValueAxis*>(ax);
        if (!valAxis) continue;
        double center = (valAxis->min() + valAxis->max()) / 2.0;
        double halfRange = (valAxis->max() - valAxis->min()) / 2.0;
        double newHalf = halfRange * factor;
        if (newHalf < 0.001) newHalf = 0.001;
        valAxis->setRange(center - newHalf, center + newHalf);
    }
}

// ── 时间预设 ──

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

// ── Excel 导出 ──

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
