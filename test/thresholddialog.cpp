#include "thresholddialog.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include <QCoreApplication>

ThresholdDialog::ThresholdDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("阈值设置");
    setMinimumWidth(400);
    setStyleSheet("background: #0d1f2d; color: #E0E0E0;");

    auto *mainLayout = new QVBoxLayout(this);

    QLabel *title = new QLabel("传感器阈值上下限");
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #00E5FF;");
    mainLayout->addWidget(title);

    QStringList names = {
        "空气温度 (℃)", "空气湿度 (%)", "光照强度 (Lux)", "土壤 pH",
        "风速 (m/s)", "土壤温度 (℃)", "土壤湿度 (%)", "土壤电导率 (mS/cm)"
    };

    QMap<int, Thresholds> current = load();

    auto *form = new QFormLayout();
    for (int i = 0; i < names.size(); ++i) {
        auto *upper = new QDoubleSpinBox();
        upper->setRange(-9999, 9999);
        upper->setDecimals(1);
        upper->setValue(current[i].upper);
        upper->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455;");
        upperSpins[i] = upper;

        auto *lower = new QDoubleSpinBox();
        lower->setRange(-9999, 9999);
        lower->setDecimals(1);
        lower->setValue(current[i].lower);
        lower->setStyleSheet("background: #1a2a3a; color: white; border: 1px solid #334455;");
        lowerSpins[i] = lower;

        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(names[i]));
        row->addWidget(new QLabel("上限"));
        row->addWidget(upper);
        row->addWidget(new QLabel("下限"));
        row->addWidget(lower);
        form->addRow(row);
    }
    mainLayout->addLayout(form);

    auto *btnRow = new QHBoxLayout();
    auto *resetBtn = new QPushButton("恢复默认");
    resetBtn->setStyleSheet("background: #455A64; color: white; padding: 6px 16px;");
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        double defaults[8][2] = {
            {35, 0}, {90, 20}, {80000, 0}, {7.5, 4.5},
            {20, 0}, {40, 0}, {85, 15}, {5, 0}
        };
        for (int i = 0; i < 8; ++i) {
            upperSpins[i]->setValue(defaults[i][0]);
            lowerSpins[i]->setValue(defaults[i][1]);
        }
    });

    auto *saveBtn = new QPushButton("保存");
    saveBtn->setStyleSheet("background: #00897B; color: white; padding: 6px 16px; font-weight: bold;");
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        QMap<int, Thresholds> t;
        for (int i = 0; i < 8; ++i) {
            Thresholds th;
            th.upper = upperSpins[i]->value();
            th.lower = lowerSpins[i]->value();
            t[i] = th;
        }
        save(t);
        emit thresholdsChanged();
        accept();
    });

    auto *cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet("background: #607D8B; color: white; padding: 6px 16px;");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    mainLayout->addLayout(btnRow);
}

QMap<int, ThresholdDialog::Thresholds> ThresholdDialog::load() {
    QMap<int, Thresholds> t;
    const QString iniPath = QCoreApplication::applicationDirPath() + "/thresholds.ini";
    QSettings settings(iniPath, QSettings::IniFormat);

    QStringList keys = {"air_temp", "air_humidity", "light_lux", "soil_ph",
                        "wind_speed", "soil_temp", "soil_humidity", "soil_conductivity"};
    double defaults[8][2] = {
        {35, 0}, {90, 20}, {80000, 0}, {7.5, 4.5},
        {20, 0}, {40, 0}, {85, 15}, {5, 0}
    };
    for (int i = 0; i < keys.size(); ++i) {
        Thresholds th;
        th.upper = settings.value("thresholds/" + keys[i] + "_upper", defaults[i][0]).toDouble();
        th.lower = settings.value("thresholds/" + keys[i] + "_lower", defaults[i][1]).toDouble();
        t[i] = th;
    }
    return t;
}

void ThresholdDialog::save(const QMap<int, Thresholds> &t) {
    const QString iniPath = QCoreApplication::applicationDirPath() + "/thresholds.ini";
    QSettings settings(iniPath, QSettings::IniFormat);

    QStringList keys = {"air_temp", "air_humidity", "light_lux", "soil_ph",
                        "wind_speed", "soil_temp", "soil_humidity", "soil_conductivity"};
    for (int i = 0; i < keys.size(); ++i) {
        settings.setValue("thresholds/" + keys[i] + "_upper", t[i].upper);
        settings.setValue("thresholds/" + keys[i] + "_lower", t[i].lower);
    }
}
