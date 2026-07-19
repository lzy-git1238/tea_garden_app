#ifndef PESTDATA_H
#define PESTDATA_H

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QMap>

struct PestRecord {
    int id = 0;             // 数据库自增主键
    QDateTime timestamp;
    double lat = 0;
    double lng = 0;
    QString pestType;       // "茶尺蠖" / "茶小绿叶蝉" / "茶炭疽病" / ...
    int severity = 1;       // 1轻微 2中等 3严重
};

class PestStore {
public:
    static PestStore* instance();

    void init(const QString &dbPath);
    void append(const PestRecord &rec);
    QVector<PestRecord> queryAll();
    int totalCount();
    // 返回种类→数量
    QMap<QString, int> typeCounts();

    // 新增：查询 0.5m 内是否已有同种类病虫害
    bool hasNearby(const QString &pestType, double lat, double lng, double distanceM = 0.5);
    // 新增：按 id 硬删除
    bool deleteById(int id);

private:
    PestStore() = default;
    QString dbPath;
};

#endif // PESTDATA_H
