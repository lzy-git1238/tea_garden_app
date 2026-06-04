#ifndef IOTUPLOADER_H
#define IOTUPLOADER_H

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QMutex>
#include <memory>
#include <mqtt/async_client.h>

class IotUploader : public QObject, public virtual mqtt::callback {
    Q_OBJECT
public:
    static IotUploader* instance();

    void init(const QString &configPath);
    void upload(const QJsonObject &sensorData);
    void uploadPest();

    // 响应云端命令
    void sendCommandResponse(const QString &cmdName,
                             const QString &requestId,
                             int result);

signals:
    // 收到云端设备命令
    void deviceCommandReceived(const QString &device,
                               bool on,
                               const QString &requestId);

private:
    // ── mqtt::callback 重写 ──
    void connected(const std::string &cause) override;
    void connection_lost(const std::string &cause) override;
    void message_arrived(mqtt::const_message_ptr msg) override;
    void delivery_complete(mqtt::delivery_token_ptr token) override;

private:
    explicit IotUploader(QObject *parent = nullptr);
    void doUpload(const QJsonObject &sensorData);
    void connectToHost();

    std::unique_ptr<mqtt::async_client> m_client;

    QString m_hostname;
    int     m_port = 8883;
    QString m_deviceId;
    QString m_username;
    QString m_password;
    QString m_clientId;

    QString m_reportTopic;
    QString m_commandTopic;

    QTimer *m_reconnectTimer = nullptr;
    bool    m_connecting = false;
};

#endif // IOTUPLOADER_H
