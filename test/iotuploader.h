#ifndef IOTUPLOADER_H
#define IOTUPLOADER_H

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QTimer>

// libmosquitto 句柄（PIMPL 隐藏，避免头文件依赖）
struct mosquitto;

class IotUploader : public QObject {
    Q_OBJECT
public:
    static IotUploader* instance();

    void init(const QString &configPath);
    void upload(const QJsonObject &sensorData);

private:
    explicit IotUploader(QObject *parent = nullptr);
    void connectBroker();
    void publish(const QString &payload);

    // libmosquitto 回调
    static void onConnect(struct mosquitto *m, void *obj, int rc);
    static void onPublish(struct mosquitto *m, void *obj, int mid);
    static void onDisconnect(struct mosquitto *m, void *obj, int rc);
    static void onLog(struct mosquitto *m, void *obj, int level, const char *str);
    void handleDisconnect(int rc);

    struct mosquitto *m_handle = nullptr;
    QString deviceId;
    QString username;
    QString password;
    QString clientId;
    QString hostname;
    int     port = 8883;
    bool    connected = false;

    QString pendingPayload;   // 连接成功前缓存最新的待发数据
    QTimer *reconnectTimer;
    QTimer *keepaliveTimer;
};

#endif // IOTUPLOADER_H
