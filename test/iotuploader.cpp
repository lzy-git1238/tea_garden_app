#include "iotuploader.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>

IotUploader* IotUploader::instance() {
    static IotUploader *inst = nullptr;
    if (!inst) inst = new IotUploader();
    return inst;
}

IotUploader::IotUploader(QObject *parent) : QObject(parent) {}

void IotUploader::init(const QString &configPath) {
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "IotUploader: cannot read config" << configPath;
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (!doc.isObject()) return;
    QJsonObject cfg = doc.object();

    deviceId = cfg["device_id"].toString();
    secret   = cfg["secret"].toString();
    username = cfg["username"].toString();
    password = cfg["password"].toString();
    clientId = cfg["clientId"].toString();
    hostname = cfg["hostname"].toString();
    port     = cfg["port"].toInt(8883);

    qDebug() << "[IoT] Config loaded:" << deviceId << "->" << hostname << ":" << port;
}

void IotUploader::upload(const QJsonObject &sensorData) {
    if (deviceId.isEmpty() || hostname.isEmpty()) return;
    if (uploading) return; // 跳过并发

    QJsonObject root;
    QJsonArray services;

    QJsonObject svc;
    svc["service_id"] = "sensor";
    svc["properties"] = sensorData;
    svc["event_time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    services.append(svc);
    root["services"] = services;

    QJsonDocument doc(root);
    QString payload = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    runUpload(payload);
}

void IotUploader::runUpload(const QString &payload) {
    uploading = true;
    process = new QProcess(this);

    // 构建 mosquitto_pub 命令
    QString cm = QString("mosquitto_pub")
        + " -h " + hostname
        + " -p " + QString::number(port)
        + " -u "  + username
        + " -P "  + password
        + " -i "  + clientId
        + " -t "  + "$oc/devices/" + deviceId + "/sys/properties/report"
        + " -m '" + payload + "'"
        + " -d"   // 调试输出
        + " -q 1" // QoS 1
        + " --cafile /etc/ssl/certs/ca-certificates.crt"
        + " -V mqttv311";

    qDebug() << "[IoT] upload payload:" << payload.left(200);
    process->start("sh", QStringList() << "-c" << cm);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        if (code != 0) {
            QString err = process->readAllStandardError();
            qWarning() << "[IoT] upload failed (exit" << code << "):" << err.left(200);
        } else {
            qDebug() << "[IoT] upload OK";
        }
        process->deleteLater();
        process = nullptr;
        uploading = false;
    });

    // 15 秒超时强杀
    QTimer::singleShot(15000, this, [this]() {
        if (process && process->state() != QProcess::NotRunning) {
            qWarning() << "[IoT] upload timeout, killing";
            process->kill();
        }
    });
}
