#ifndef THRESHOLDDIALOG_H
#define THRESHOLDDIALOG_H

#include <QDialog>
#include <QMap>

class QDoubleSpinBox;

class ThresholdDialog : public QDialog {
    Q_OBJECT
public:
    struct Thresholds {
        double upper = 9999;
        double lower = -9999;
    };

    explicit ThresholdDialog(QWidget *parent = nullptr);

    static QMap<int, Thresholds> load();
    static void save(const QMap<int, Thresholds> &t);

signals:
    void thresholdsChanged();

private:
    QMap<int, QDoubleSpinBox*> upperSpins;
    QMap<int, QDoubleSpinBox*> lowerSpins;
};

#endif // THRESHOLDDIALOG_H
