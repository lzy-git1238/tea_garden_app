#ifndef SCHEDULESTORE_H
#define SCHEDULESTORE_H

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QMap>
#include <QDate>

struct SchedulePlan {
    int id = 0;
    QString deviceType;   // "pump" 或 "lamp"
    QString daysOfWeek;   // "1,3,5" (1=周一)
    QString startTime;    // "HH:mm"
    int durationMin = 0;
    bool enabled = true;
    QDateTime createdAt;
};

struct ScheduleLogEntry {
    int id = 0;
    QString deviceType;    // "pump" / "lamp"
    int scheduleId = 0;    // 0 = 手动操作
    QString action;        // "on" / "off"
    QString trigger;       // "schedule" / "manual"
    double flowRate = 0;   // L/min, lamp 固定为 0
    double volumeL = 0;    // 累计灌溉量 L, lamp 固定为 0
    QDateTime ts;
};

class ScheduleStore {
public:
    static ScheduleStore* instance();

    void init(const QString &dbPath);

    // ── 计划 CRUD ──
    int  addPlan(const SchedulePlan &plan);       // 返回新 ID
    void updatePlan(const SchedulePlan &plan);
    void deletePlan(int id);
    SchedulePlan getPlan(int id);
    QVector<SchedulePlan> queryPlans(const QString &deviceType = QString());
    // 按星期几 + 当前时间查找启用的计划
    QVector<SchedulePlan> queryEnabledPlans(const QString &deviceType,
                                             int dayOfWeek,      // 1=周一
                                             const QString &timeStr); // "HH:mm"

    // ── 日志 ──
    void appendLog(const ScheduleLogEntry &entry);
    QVector<ScheduleLogEntry> queryLogs(const QString &deviceType,
                                         const QDateTime &from,
                                         const QDateTime &to);

    // ── 统计 ──
    // 返回 date → 日灌溉量(L) 或 日运行时长(分钟)
    QMap<QDate, double> dailySummary(const QString &deviceType, int days);
    double totalVolume(const QDateTime &from, const QDateTime &to);
    double totalMinutes(const QString &deviceType, const QDateTime &from, const QDateTime &to);

private:
    ScheduleStore() = default;
    QString dbPath;
};

#endif // SCHEDULESTORE_H
