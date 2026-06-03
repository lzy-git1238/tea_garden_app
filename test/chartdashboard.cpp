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
#include <QFrame>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QMouseEvent>
#include <QtMath>

using namespace QtCharts;

// ── 大数据平台风格按钮 / 控件样式 ──
static const char* k3dSmallBtnStyle = ""
    "QPushButton {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #124068, stop:0.5 #0c3050, stop:1 #081830);"
    "  color: #90caf9; font-size: 14px; font-weight: bold; min-width: 38px; min-height: 32px; padding: 0 6px;"
    "  border: 1px solid #1a6fa8; border-top: 1px solid #2a9fd8;"
    "  border-radius: 3px;"
    "}"
    "QPushButton:hover {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #1a5080, stop:0.5 #124068, stop:1 #0c3050);"
    "  color: #ffffff;"
    "}"
    "QPushButton:pressed { padding-top: 1px; }";

static const char* k3dSmallBtnStyleLight = ""
    "QPushButton {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #e3f2fd, stop:0.5 #bbdefb, stop:1 #90caf9);"
    "  color: #0277bd; font-size: 14px; font-weight: bold; min-width: 38px; min-height: 32px; padding: 0 6px;"
    "  border: 1px solid #64b5f6; border-radius: 3px;"
    "}"
    "QPushButton:hover { color: #01579b; }"
    "QPushButton:pressed { padding-top: 1px; }";

static const char* k3dZoomGreenStyle = ""
    "QPushButton {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #0088cc, stop:0.5 #006699, stop:1 #004466);"
    "  color: #e3f2fd; font-size: 14px; font-weight: bold; min-width: 38px; min-height: 32px; padding: 0 6px;"
    "  border: 1px solid #2a9fd8; border-radius: 3px;"
    "}"
    "QPushButton:hover { color: #ffffff; }"
    "QPushButton:pressed { padding-top: 1px; }";

static const char* k3dZoomOrangeStyle = ""
    "QPushButton {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #e65100, stop:0.5 #bf360c, stop:1 #8d2a00);"
    "  color: #ffe0b2; font-size: 14px; font-weight: bold; min-width: 38px; min-height: 32px; padding: 0 6px;"
    "  border: 1px solid #ff8f00; border-radius: 3px;"
    "}"
    "QPushButton:hover { color: #ffffff; }"
    "QPushButton:pressed { padding-top: 1px; }";

static const char* k3dSliderStyle = ""
    "QScrollBar:horizontal {"
    "  background: #081830; height: 10px; border-radius: 3px;"
    "  border: 1px solid #1a4a78;"
    "}"
    "QScrollBar::handle:horizontal {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #2a9fd8, stop:0.5 #0088cc, stop:1 #005588);"
    "  min-width: 24px; border-radius: 3px; margin: 1px 0;"
    "  border-top: 1px solid #64b5f6;"
    "}"
    "QScrollBar::handle:horizontal:hover {"
    "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #42a5f5, stop:0.5 #1e88e5, stop:1 #0277bd);"
    "}"
    "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
    "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: #081830; }";

static const char* kChartLineColors[] = {
    "#00d4ff", "#ffb74d", "#ce93d8", "#81c784",
    "#4fc3f7", "#ff8a65", "#aed581", "#ba68c8"
};

// ── 构造 ──

ChartDashboard::ChartDashboard(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("数据折线看板");
    resize(1050, 780);
    setStyleSheet("background-color: #0a1628; color: #E8F4FF; font-family: 'Microsoft YaHei';");

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
    toolbar->setSpacing(4);
    toolbar->setContentsMargins(6, 4, 6, 4);

    auto makeBtn = [](const QString &text) {
        auto *b = new QPushButton(text);
        b->setStyleSheet(k3dSmallBtnStyle);
        return b;
    };

    auto *btn1h  = makeBtn("1h");   btn1h->setFixedWidth(42);
    auto *btn6h  = makeBtn("6h");   btn6h->setFixedWidth(42);
    auto *btn24h = makeBtn("24h");  btn24h->setFixedWidth(48);
    auto *btn7d  = makeBtn("7d");   btn7d->setFixedWidth(42);
    auto *btn30d = makeBtn("30d");  btn30d->setFixedWidth(48);

    connect(btn1h,  &QPushButton::clicked, this, &ChartDashboard::onPreset1h);
    connect(btn6h,  &QPushButton::clicked, this, &ChartDashboard::onPreset6h);
    connect(btn24h, &QPushButton::clicked, this, &ChartDashboard::onPreset24h);
    connect(btn7d,  &QPushButton::clicked, this, &ChartDashboard::onPreset7d);
    connect(btn30d, &QPushButton::clicked, this, &ChartDashboard::onPreset30d);
    toolbar->addWidget(btn1h);
    toolbar->addWidget(btn6h);
    toolbar->addWidget(btn24h);
    toolbar->addWidget(btn7d);
    toolbar->addWidget(btn30d);
    toolbar->addSpacing(8);

    auto *fromLbl = new QLabel("自");
    toolbar->addWidget(fromLbl);
    fromTime = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(-3600));
    fromTime->setDisplayFormat("MM-dd HH:mm");
    toolbar->addWidget(fromTime);

    auto *toLbl = new QLabel("至");
    toolbar->addWidget(toLbl);
    toTime = new QDateTimeEdit(QDateTime::currentDateTime());
    toTime->setDisplayFormat("MM-dd HH:mm");
    toolbar->addWidget(toTime);

    auto *queryBtn = new QPushButton("查询");
    queryBtn->setStyleSheet(k3dZoomGreenStyle);
    queryBtn->setFixedWidth(50);
    connect(queryBtn, &QPushButton::clicked, this, &ChartDashboard::onCustomQuery);
    toolbar->addWidget(queryBtn);

    toolbar->addStretch();

    thresholdBtn = new QPushButton("⚙ 阈值");
    thresholdBtn->setStyleSheet(k3dSmallBtnStyle);
    thresholdBtn->setFixedWidth(86);
    connect(thresholdBtn, &QPushButton::clicked, this, &ChartDashboard::onOpenThresholds);
    toolbar->addWidget(thresholdBtn);

    exportBtn = new QPushButton("📤 导出");
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
    exportBtn->setFixedWidth(90);
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
    card->setStyleSheet("background: rgba(10, 35, 70, 0.55); border: 1px solid #1a4a78; border-top: 2px solid #2a9fd8; border-radius: 4px;");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(8, 4, 8, 4);
    cardLayout->setSpacing(4);

    // ── 标题行：标题 + 缩放按钮 ──
    auto *titleRow = new QHBoxLayout();

    auto *header = new QPushButton("📈 " + title + "  ▼");
    header->setStyleSheet("QPushButton { background: transparent; color: #00d4ff; "
                          "font-size: 13px; font-weight: bold; text-align: left; "
                          "border: none; padding: 4px 0 4px 8px; border-left: 3px solid #00d4ff; }");
    header->setCursor(Qt::PointingHandCursor);
    titleRow->addWidget(header);
    titleRow->addStretch();

    // X 轴缩放按钮
    auto *xLabel = new QLabel("X");
    xLabel->setStyleSheet("color: #64b5f6; font-size: 11px; font-weight: bold; border: none;");
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
    yLabel->setStyleSheet("color: #ffb74d; font-size: 11px; font-weight: bold; border: none;");
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
    chart->setBackgroundBrush(QBrush(QColor("#0a1628")));
    chart->legend()->hide();
    chart->setAnimationOptions(QChart::SeriesAnimations);

    auto *axisX = new QDateTimeAxis();
    axisX->setFormat("HH:mm");
    axisX->setLabelsColor(QColor("#7eb8d4"));
    axisX->setGridLineColor(QColor("#1a4a78"));
    chart->addAxis(axisX, Qt::AlignBottom);

    auto *axisY = new QValueAxis();
    axisY->setLabelsColor(QColor("#7eb8d4"));
    axisY->setGridLineColor(QColor("#1a4a78"));
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

    // ── 纵向拖拽把手（拉高/压矮图表） ──
    auto *resizeHandle = new QFrame();
    resizeHandle->setFixedHeight(7);
    resizeHandle->setCursor(Qt::SplitVCursor);
    resizeHandle->setStyleSheet(
        "QFrame {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #1a3040, stop:0.4 #102838, stop:0.6 #081820, stop:1 #1a3040);"
        "  border-top: 1px solid #304860; border-bottom: 1px solid #081018;"
        "  border-radius: 2px;"
        "}"
        "QFrame:hover {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #284868, stop:0.4 #183848, stop:0.6 #102838, stop:1 #284868);"
        "  border-top: 1px solid #5088c0;"
        "}");
    resizeHandle->installEventFilter(this);
    resizeHandle->setProperty("sensorIndex", sensorIndex);
    cardLayout->addWidget(resizeHandle);

    // ── 底部时间滑块（居中，宽度约 40%） ──
    auto *sliderRow = new QHBoxLayout();
    sliderRow->addStretch(3);
    auto *slider = new QScrollBar(Qt::Horizontal);
    slider->setMinimum(0);
    slider->setMaximum(sliderSteps);
    slider->setValue(0);
    slider->setPageStep(sliderSteps / 10);
    slider->setStyleSheet(k3dSliderStyle);
    cardSliders[sensorIndex] = slider;
    sliderRow->addWidget(slider, 4);
    sliderRow->addStretch(3);

    connect(slider, &QScrollBar::valueChanged, this, &ChartDashboard::onCardSliderChanged);
    cardLayout->addLayout(sliderRow);

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
        // 自适应时间格式
        qint64 rangeMs = to.toMSecsSinceEpoch() - from.toMSecsSinceEpoch();
        if (rangeMs <= 60'000)             axisX->setFormat("HH:mm:ss");
        else if (rangeMs <= 3600'000)       axisX->setFormat("HH:mm:ss");
        else if (rangeMs <= 86'400'000)     axisX->setFormat("HH:mm");
        else if (rangeMs <= 604'800'000)    axisX->setFormat("MM-dd HH:mm");
        else                                axisX->setFormat("MM-dd");
        axisX->setLabelsColor(QColor("#90A4AE"));
        axisX->setGridLineColor(QColor("#1a2a3a"));
        axisX->setRange(from, to);
        chart->addAxis(axisX, Qt::AlignBottom);

        auto *axisY = new QValueAxis();
        axisY->setLabelsColor(QColor("#90A4AE"));
        axisY->setGridLineColor(QColor("#1a2a3a"));
        chart->addAxis(axisY, Qt::AlignLeft);

        auto *lineSeries = new QLineSeries();
        lineSeries->setPen(QPen(QColor(kChartLineColors[i % 8]), 1.5));

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

    for (auto *ax : chart->axes(Qt::Horizontal)) {
        auto *dtAxis = qobject_cast<QDateTimeAxis*>(ax);
        if (!dtAxis) continue;
        QDateTime center(QDateTime::fromMSecsSinceEpoch(
            (dtAxis->min().toMSecsSinceEpoch() + dtAxis->max().toMSecsSinceEpoch()) / 2));
        qint64 halfRange = (dtAxis->max().toMSecsSinceEpoch() - dtAxis->min().toMSecsSinceEpoch()) / 2;
        qint64 newHalf = static_cast<qint64>(halfRange * factor);
        if (newHalf < 1000) newHalf = 1000;
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

// ── 纵向拖拽把手事件 ──

bool ChartDashboard::eventFilter(QObject *obj, QEvent *event) {
    if (obj->isWidgetType() && obj->inherits("QFrame")) {
        auto *handle = static_cast<QWidget*>(obj);
        bool ok = false;
        int idx = handle->property("sensorIndex").toInt(&ok);
        if (ok && charts.contains(idx)) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    activeResizeIndex = idx;
                    resizeStartPos = me->globalPos();
                    resizeStartH  = charts[idx]->minimumHeight();
                    return true;
                }
            } else if (event->type() == QEvent::MouseMove && activeResizeIndex == idx) {
                auto *me = static_cast<QMouseEvent*>(event);
                int deltaY = me->globalPos().y() - resizeStartPos.y();
                int newH = resizeStartH + deltaY;
                if (newH < 120) newH = 120;
                if (newH > 600) newH = 600;
                charts[idx]->setMinimumHeight(newH);
                return true;
            } else if (event->type() == QEvent::MouseButtonRelease && activeResizeIndex == idx) {
                activeResizeIndex = -1;
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
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
void ChartDashboard::onPreset30d() {
    QDateTime to = QDateTime::currentDateTime();
    QDateTime from = to.addDays(-30);
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
