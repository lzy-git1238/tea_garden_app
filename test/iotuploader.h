#ifndef IOTUPLOADER_H
#define IOTUPLOADER_H

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QMqttClient>

class IotUploader : public QObject {
    Q_OBJECT
public:
    static IotUploader* instance();

    void init(const QString &configPath);
    void upload(const QJsonObject &sensorData);
    void uploadPest();

    // 新增：响应云端命令
    void sendCommandResponse(const QString &cmdName,
                             const QString &requestId,
                             int result);

signals:
    // 新增：收到云端设备命令
    void deviceCommandReceived(const QString &device,     // "pump" / "lamp"
                               bool on,                    // true=开 false=关
                               const QString &requestId);  // 华为云 request_id

private slots:
    void onConnected();
    void onMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void onDisconnected();
    void onError(QMqttClient::ClientError error);

private:
    explicit IotUploader(QObject *parent = nullptr);
    void doUpload(const QJsonObject &sensorData);
    void connectToHost();

    QMqttClient *m_client = nullptr;
    QString m_hostname;
    int     m_port = 8883;
    QString m_deviceId;
    QString m_username;
    QString m_password;
    QString m_clientId;

    QString m_reportTopic;
    QString m_commandTopic;

    // 保留重连定时器
    QTimer *m_reconnectTimer = nullptr;
    bool    m_connecting = false;
};

#endif // IOTUPLOADER_H
