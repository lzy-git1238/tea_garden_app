#ifndef PESTDATA_H
#define PESTDATA_H

#include <QDateTime>
#include <QString>
#include <QVector>

struct PestRecord {
    QDateTime timestamp;
    double lat = 0;
    double lng = 0;
    QString pestType;   // "茶尺蠖" / "茶小绿叶蝉" / "茶炭疽病" / ...
    int severity = 1;   // 1轻微 2中等 3严重
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

private:
    PestStore() = default;
    QString dbPath;
};

#endif // PESTDATA_H
