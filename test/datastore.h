#ifndef DATASTORE_H
#define DATASTORE_H

#include <QDateTime>
#include <QVector>
#include <QString>
#include <cmath>

struct SensorSnapshot {
    QDateTime timestamp;
    double windSpeed       = NAN;
    double airTemp         = NAN;
    double airHumidity     = NAN;
    double lightLux        = NAN;
    double soilHumidity    = NAN;
    double soilTemp        = NAN;
    double soilConductivity = NAN;
    double soilPh          = NAN;
};

class DataStore {
public:
    static DataStore* instance();

    void init(const QString &dbPath);
    void append(const SensorSnapshot &snap);
    QVector<SensorSnapshot> query(QDateTime from, QDateTime to);

private:
    DataStore() = default;
    QString dbPath;
};

#endif // DATASTORE_H
