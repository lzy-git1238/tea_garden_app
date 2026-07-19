#include "pestdata.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

PestStore* PestStore::instance() {
    static PestStore store;
    return &store;
}

void PestStore::init(const QString &path) {
    dbPath = path;
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "pest_conn");
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "PestStore: cannot open" << dbPath << db.lastError().text();
        return;
    }
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS pest_log ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "ts REAL NOT NULL,"
           "lat REAL, lng REAL,"
           "pest_type TEXT,"
           "severity INT)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_pest_ts ON pest_log(ts)");
}

void PestStore::append(const PestRecord &rec) {
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("INSERT INTO pest_log (ts, lat, lng, pest_type, severity) "
              "VALUES (:ts, :lat, :lng, :pt, :sv)");
    q.bindValue(":ts", rec.timestamp.toMSecsSinceEpoch() / 1000.0);
    q.bindValue(":lat", rec.lat);
    q.bindValue(":lng", rec.lng);
    q.bindValue(":pt", rec.pestType);
    q.bindValue(":sv", rec.severity);
    if (!q.exec()) qWarning() << "PestStore::append failed:" << q.lastError().text();
}

QVector<PestRecord> PestStore::queryAll() {
    QVector<PestRecord> result;
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return result;
    QSqlQuery q(db);
    q.exec("SELECT id, ts, lat, lng, pest_type, severity FROM pest_log ORDER BY ts DESC");
    while (q.next()) {
        PestRecord r;
        r.id        = q.value(0).toInt();
        r.timestamp = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(q.value(1).toDouble() * 1000.0));
        r.lat       = q.value(2).toDouble();
        r.lng       = q.value(3).toDouble();
        r.pestType  = q.value(4).toString();
        r.severity  = q.value(5).toInt();
        result.append(r);
    }
    return result;
}

int PestStore::totalCount() {
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return 0;
    QSqlQuery q(db);
    q.exec("SELECT COUNT(*) FROM pest_log");
    if (q.next()) return q.value(0).toInt();
    return 0;
}

QMap<QString, int> PestStore::typeCounts() {
    QMap<QString, int> m;
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return m;
    QSqlQuery q(db);
    q.exec("SELECT pest_type, COUNT(*) FROM pest_log GROUP BY pest_type");
    while (q.next()) m[q.value(0).toString()] = q.value(1).toInt();
    return m;
}

bool PestStore::hasNearby(const QString &pestType, double lat, double lng, double distanceM) {
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    // 1° ≈ 111320m，threshold = (distanceM / 111320)^2
    double threshold = (distanceM / 111320.0) * (distanceM / 111320.0);
    q.prepare("SELECT COUNT(*) FROM pest_log "
              "WHERE pest_type = :type "
              "  AND (lat - :lat)*(lat - :lat) + (lng - :lng)*(lng - :lng) < :th");
    q.bindValue(":type", pestType);
    q.bindValue(":lat", lat);
    q.bindValue(":lng", lng);
    q.bindValue(":th", threshold);
    if (!q.exec()) {
        qWarning() << "PestStore::hasNearby failed:" << q.lastError().text();
        return false;
    }
    if (q.next())
        return q.value(0).toInt() > 0;
    return false;
}

bool PestStore::deleteById(int id) {
    QSqlDatabase db = QSqlDatabase::database("pest_conn");
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("DELETE FROM pest_log WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec()) {
        qWarning() << "PestStore::deleteById failed:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}
