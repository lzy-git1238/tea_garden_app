#ifndef IOTUPLOADER_H
#define IOTUPLOADER_H

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QString>

class IotUploader : public QObject {
    Q_OBJECT
public:
    static IotUploader* instance();

    void init(const QString &configPath);
    void upload(const QJsonObject &sensorData);

private:
    explicit IotUploader(QObject *parent = nullptr);
    void runUpload(const QString &payload);

    QString deviceId;
    QString secret;
    QString username;
    QString password;
    QString clientId;
    QString hostname;
    int     port = 8883;
    QProcess *process = nullptr;
    bool     uploading = false;
};

#endif // IOTUPLOADER_H
