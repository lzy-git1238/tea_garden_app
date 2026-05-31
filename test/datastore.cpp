#include "datastore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

DataStore* DataStore::instance() {
    static DataStore store;
    return &store;
}

void DataStore::init(const QString &path) {
    dbPath = path;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "sensor_conn");
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "DataStore: cannot open" << dbPath << db.lastError().text();
        return;
    }

    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS sensor_log ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "ts REAL NOT NULL,"
           "wind_speed REAL,"
           "air_temp REAL,"
           "air_humidity REAL,"
           "light_lux REAL,"
           "soil_humidity REAL,"
           "soil_temp REAL,"
           "soil_conductivity REAL,"
           "soil_ph REAL)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_ts ON sensor_log(ts)");
}

void DataStore::append(const SensorSnapshot &snap) {
    QSqlDatabase db = QSqlDatabase::database("sensor_conn");
    if (!db.isOpen()) return;

    QSqlQuery q(db);
    q.prepare("INSERT INTO sensor_log (ts, wind_speed, air_temp, air_humidity, "
              "light_lux, soil_humidity, soil_temp, soil_conductivity, soil_ph) "
              "VALUES (:ts, :ws, :at, :ah, :ll, :sh, :st, :sc, :sp)");
    q.bindValue(":ts", snap.timestamp.toMSecsSinceEpoch() / 1000.0);

    auto bindD = [&](const char *name, double val) {
        q.bindValue(name, std::isfinite(val) ? QVariant(val) : QVariant());
    };
    bindD(":ws", snap.windSpeed);
    bindD(":at", snap.airTemp);
    bindD(":ah", snap.airHumidity);
    bindD(":ll", snap.lightLux);
    bindD(":sh", snap.soilHumidity);
    bindD(":st", snap.soilTemp);
    bindD(":sc", snap.soilConductivity);
    bindD(":sp", snap.soilPh);

    if (!q.exec()) {
        qWarning() << "DataStore::append failed:" << q.lastError().text();
    }
}

QVector<SensorSnapshot> DataStore::query(QDateTime from, QDateTime to) {
    QVector<SensorSnapshot> result;
    QSqlDatabase db = QSqlDatabase::database("sensor_conn");
    if (!db.isOpen()) return result;

    double fromSec = from.toMSecsSinceEpoch() / 1000.0;
    double toSec   = to.toMSecsSinceEpoch() / 1000.0;
    double rangeSec = toSec - fromSec;
    if (rangeSec <= 0) return result;

    // 动态降采样：先数实际行数，超过 800 条则按桶 AVG
    static constexpr int kMaxPoints = 800;

    int actualCount = 0;
    {
        QSqlQuery cnt(db);
        cnt.prepare("SELECT COUNT(*) FROM sensor_log WHERE ts BETWEEN :from AND :to");
        cnt.bindValue(":from", fromSec);
        cnt.bindValue(":to", toSec);
        cnt.exec();
        if (cnt.next()) actualCount = cnt.value(0).toInt();
    }

    auto readRow = [&](QSqlQuery &q) -> SensorSnapshot {
        SensorSnapshot s;
        s.timestamp = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(q.value(0).toDouble() * 1000.0));
        auto rd = [&](int col) -> double {
            return q.value(col).isNull() ? NAN : q.value(col).toDouble();
        };
        s.windSpeed = rd(1); s.airTemp = rd(2);
        s.airHumidity = rd(3); s.lightLux = rd(4);
        s.soilHumidity = rd(5); s.soilTemp = rd(6);
        s.soilConductivity = rd(7); s.soilPh = rd(8);
        return s;
    };

    if (actualCount <= kMaxPoints) {
        QSqlQuery q(db);
        q.prepare("SELECT ts, wind_speed, air_temp, air_humidity, light_lux, "
                  "soil_humidity, soil_temp, soil_conductivity, soil_ph "
                  "FROM sensor_log WHERE ts BETWEEN :from AND :to ORDER BY ts");
        q.bindValue(":from", fromSec);
        q.bindValue(":to", toSec);
        q.exec();
        while (q.next()) result.append(readRow(q));
    } else {
        double bucket = rangeSec / static_cast<double>(kMaxPoints);
        QSqlQuery q(db);
        q.prepare("SELECT AVG(ts), AVG(wind_speed), AVG(air_temp), "
                  "AVG(air_humidity), AVG(light_lux), AVG(soil_humidity), "
                  "AVG(soil_temp), AVG(soil_conductivity), AVG(soil_ph) "
                  "FROM sensor_log WHERE ts BETWEEN :from AND :to "
                  "GROUP BY CAST((ts - :from) / :bucket AS INTEGER) "
                  "ORDER BY AVG(ts)");
        q.bindValue(":from", fromSec);
        q.bindValue(":to", toSec);
        q.bindValue(":bucket", bucket);
        q.exec();
        while (q.next()) result.append(readRow(q));
    }
    return result;
}

DataStore::TimeRange DataStore::fullTimeRange() {
    TimeRange tr;
    QSqlDatabase db = QSqlDatabase::database("sensor_conn");
    if (!db.isOpen()) return tr;

    QSqlQuery q(db);
    q.exec("SELECT MIN(ts), MAX(ts) FROM sensor_log");
    if (q.next() && !q.value(0).isNull() && !q.value(1).isNull()) {
        tr.earliestSec = q.value(0).toDouble();
        tr.latestSec   = q.value(1).toDouble();
        tr.valid = true;
    }
    return tr;
}
