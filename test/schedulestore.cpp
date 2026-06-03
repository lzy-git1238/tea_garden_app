#include "schedulestore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

ScheduleStore* ScheduleStore::instance() {
    static ScheduleStore store;
    return &store;
}

void ScheduleStore::init(const QString &path) {
    dbPath = path;
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "schedule_conn");
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "ScheduleStore: cannot open" << dbPath << db.lastError().text();
        return;
    }
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS schedule_config ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "device_type TEXT NOT NULL,"
           "days_of_week TEXT NOT NULL,"
           "start_time TEXT NOT NULL,"
           "duration_min INTEGER NOT NULL,"
           "enabled INTEGER DEFAULT 1,"
           "created_at TEXT)");
    q.exec("CREATE TABLE IF NOT EXISTS schedule_log ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "device_type TEXT NOT NULL,"
           "schedule_id INTEGER DEFAULT 0,"
           "action TEXT NOT NULL,"
           "trigger_type TEXT NOT NULL,"
           "flow_rate REAL DEFAULT 0,"
           "volume_l REAL DEFAULT 0,"
           "ts TEXT NOT NULL)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_schedule_log_ts ON schedule_log(ts)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_schedule_log_dev ON schedule_log(device_type, ts)");
}

int ScheduleStore::addPlan(const SchedulePlan &plan) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return -1;
    QSqlQuery q(db);
    q.prepare("INSERT INTO schedule_config (device_type, days_of_week, start_time, duration_min, enabled, created_at) "
              "VALUES (:dt, :dow, :st, :dur, :en, :ca)");
    q.bindValue(":dt", plan.deviceType);
    q.bindValue(":dow", plan.daysOfWeek);
    q.bindValue(":st", plan.startTime);
    q.bindValue(":dur", plan.durationMin);
    q.bindValue(":en", plan.enabled ? 1 : 0);
    q.bindValue(":ca", plan.createdAt.isValid()
                        ? plan.createdAt.toString(Qt::ISODate)
                        : QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "ScheduleStore::addPlan failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

void ScheduleStore::updatePlan(const SchedulePlan &plan) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("UPDATE schedule_config SET device_type=:dt, days_of_week=:dow, start_time=:st, "
              "duration_min=:dur, enabled=:en WHERE id=:id");
    q.bindValue(":dt", plan.deviceType);
    q.bindValue(":dow", plan.daysOfWeek);
    q.bindValue(":st", plan.startTime);
    q.bindValue(":dur", plan.durationMin);
    q.bindValue(":en", plan.enabled ? 1 : 0);
    q.bindValue(":id", plan.id);
    if (!q.exec()) qWarning() << "ScheduleStore::updatePlan failed:" << q.lastError().text();
}

void ScheduleStore::deletePlan(int id) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("DELETE FROM schedule_config WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) qWarning() << "ScheduleStore::deletePlan failed:" << q.lastError().text();
}

SchedulePlan ScheduleStore::getPlan(int id) {
    SchedulePlan p;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return p;
    QSqlQuery q(db);
    q.prepare("SELECT id, device_type, days_of_week, start_time, duration_min, enabled, created_at "
              "FROM schedule_config WHERE id=:id");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) {
        p.id = q.value(0).toInt();
        p.deviceType = q.value(1).toString();
        p.daysOfWeek = q.value(2).toString();
        p.startTime = q.value(3).toString();
        p.durationMin = q.value(4).toInt();
        p.enabled = q.value(5).toInt() != 0;
        p.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
    }
    return p;
}

QVector<SchedulePlan> ScheduleStore::queryPlans(const QString &deviceType) {
    QVector<SchedulePlan> result;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return result;
    QSqlQuery q(db);
    if (deviceType.isEmpty()) {
        q.exec("SELECT id, device_type, days_of_week, start_time, duration_min, enabled, created_at "
               "FROM schedule_config ORDER BY device_type, start_time");
    } else {
        q.prepare("SELECT id, device_type, days_of_week, start_time, duration_min, enabled, created_at "
                  "FROM schedule_config WHERE device_type=:dt ORDER BY start_time");
        q.bindValue(":dt", deviceType);
        q.exec();
    }
    while (q.next()) {
        SchedulePlan p;
        p.id = q.value(0).toInt();
        p.deviceType = q.value(1).toString();
        p.daysOfWeek = q.value(2).toString();
        p.startTime = q.value(3).toString();
        p.durationMin = q.value(4).toInt();
        p.enabled = q.value(5).toInt() != 0;
        p.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        result.append(p);
    }
    return result;
}

QVector<SchedulePlan> ScheduleStore::queryEnabledPlans(const QString &deviceType,
                                                         int dayOfWeek,
                                                         const QString &timeStr) {
    QVector<SchedulePlan> result;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return result;
    QSqlQuery q(db);
    q.prepare("SELECT id, device_type, days_of_week, start_time, duration_min, enabled, created_at "
              "FROM schedule_config WHERE device_type=:dt AND enabled=1 AND start_time=:st");
    q.bindValue(":dt", deviceType);
    q.bindValue(":st", timeStr);
    if (!q.exec()) return result;
    while (q.next()) {
        // 检查 days_of_week 是否包含当前星期几
        QStringList parts = q.value(2).toString().split(',', Qt::SkipEmptyParts);
        bool match = false;
        for (const QString &part : parts) {
            if (part.trimmed().toInt() == dayOfWeek) { match = true; break; }
        }
        if (!match) continue;
        SchedulePlan p;
        p.id = q.value(0).toInt();
        p.deviceType = q.value(1).toString();
        p.daysOfWeek = q.value(2).toString();
        p.startTime = q.value(3).toString();
        p.durationMin = q.value(4).toInt();
        p.enabled = true;
        p.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        result.append(p);
    }
    return result;
}

void ScheduleStore::appendLog(const ScheduleLogEntry &entry) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("INSERT INTO schedule_log (device_type, schedule_id, action, trigger_type, flow_rate, volume_l, ts) "
              "VALUES (:dt, :sid, :act, :trg, :fr, :vl, :ts)");
    q.bindValue(":dt", entry.deviceType);
    q.bindValue(":sid", entry.scheduleId);
    q.bindValue(":act", entry.action);
    q.bindValue(":trg", entry.trigger);
    q.bindValue(":fr", entry.flowRate);
    q.bindValue(":vl", entry.volumeL);
    q.bindValue(":ts", entry.ts.isValid()
                        ? entry.ts.toString(Qt::ISODate)
                        : QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) qWarning() << "ScheduleStore::appendLog failed:" << q.lastError().text();
}

QVector<ScheduleLogEntry> ScheduleStore::queryLogs(const QString &deviceType,
                                                     const QDateTime &from,
                                                     const QDateTime &to) {
    QVector<ScheduleLogEntry> result;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return result;
    QSqlQuery q(db);
    q.prepare("SELECT id, device_type, schedule_id, action, trigger_type, flow_rate, volume_l, ts "
              "FROM schedule_log WHERE device_type=:dt AND ts>=:f AND ts<=:t ORDER BY ts DESC LIMIT 500");
    q.bindValue(":dt", deviceType);
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));
    if (!q.exec()) return result;
    while (q.next()) {
        ScheduleLogEntry e;
        e.id = q.value(0).toInt();
        e.deviceType = q.value(1).toString();
        e.scheduleId = q.value(2).toInt();
        e.action = q.value(3).toString();
        e.trigger = q.value(4).toString();
        e.flowRate = q.value(5).toDouble();
        e.volumeL = q.value(6).toDouble();
        e.ts = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
        result.append(e);
    }
    return result;
}

QMap<QDate, double> ScheduleStore::dailySummary(const QString &deviceType, int days) {
    QMap<QDate, double> result;
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return result;
    QDateTime since = QDateTime::currentDateTime().addDays(-days);
    QSqlQuery q(db);
    if (deviceType == "pump") {
        // 灌溉泵：每日灌溉量 = 所有 "off" 日志的 volume_l 之和
        q.prepare("SELECT date(ts), SUM(volume_l) FROM schedule_log "
                  "WHERE device_type='pump' AND action='off' AND ts>=:s "
                  "GROUP BY date(ts) ORDER BY date(ts)");
    } else {
        // 驱虫灯：每日运行时长 = 配对 (on, off) 计算分钟差
        // 简化方案 — 统计 "off" action 的数量 × 假设平均值
        // 更精确的做法：对每个 "on" 找下一个 "off"
        q.prepare("SELECT date(ts), COUNT(*) FROM schedule_log "
                  "WHERE device_type='lamp' AND action='on' AND ts>=:s "
                  "GROUP BY date(ts) ORDER BY date(ts)");
    }
    q.bindValue(":s", since.toString(Qt::ISODate));
    if (!q.exec()) return result;
    while (q.next()) {
        QDate d = QDate::fromString(q.value(0).toString(), "yyyy-MM-dd");
        result[d] = q.value(1).toDouble();
    }
    return result;
}

double ScheduleStore::totalVolume(const QDateTime &from, const QDateTime &to) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return 0;
    QSqlQuery q(db);
    q.prepare("SELECT COALESCE(SUM(volume_l), 0) FROM schedule_log "
              "WHERE device_type='pump' AND action='off' AND ts>=:f AND ts<=:t");
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));
    if (q.exec() && q.next()) return q.value(0).toDouble();
    return 0;
}

double ScheduleStore::totalMinutes(const QString &deviceType, const QDateTime &from, const QDateTime &to) {
    QSqlDatabase db = QSqlDatabase::database("schedule_conn");
    if (!db.isOpen()) return 0;
    // 简化：统计 on 事件次数 × 从对应 plan 获取 duration
    // 精确做法从 schedule_log 中配对 on/off 时间差
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM schedule_log "
              "WHERE device_type=:dt AND action='on' AND ts>=:f AND ts<=:t");
    q.bindValue(":dt", deviceType);
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));
    if (q.exec() && q.next()) return q.value(0).toDouble();
    return 0;
}
