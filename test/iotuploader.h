#ifndef IOTUPLOADER_H
#define IOTUPLOADER_H

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QString>
#include <QTimer>

class IotUploader : public QObject {
    Q_OBJECT
public:
    static IotUploader* instance();

    void init(const QString &configPath);
    void upload(const QJsonObject &sensorData);

private:
    explicit IotUploader(QObject *parent = nullptr);
    void startProcess();
    void sendPayload(const QString &payload);

    QString deviceId;
    QString username;
    QString password;
    QString clientId;
    QString hostname;
    int     port = 8883;

    QProcess *proc = nullptr;
    bool     ready = false;
    QString  pendingPayload;
    QTimer   *reconnectTimer = nullptr;
};

#endif // IOTUPLOADER_H
