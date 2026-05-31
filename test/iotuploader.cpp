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
    QString content = QString::fromUtf8(f.readAll());
    f.close();

    // 文件包含两个独立 JSON 对象，逐对象解析并合并
    QJsonObject merged;
    int depth = 0, start = -1;
    for (int i = 0; i < content.size(); ++i) {
        if (content[i] == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (content[i] == '}') {
            depth--;
            if (depth == 0 && start >= 0) {
                QByteArray chunk = content.mid(start, i - start + 1).toUtf8();
                QJsonDocument doc = QJsonDocument::fromJson(chunk);
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    for (auto it = obj.begin(); it != obj.end(); ++it)
                        merged[it.key()] = it.value();
                }
                start = -1;
            }
        }
    }

    deviceId = merged["device_id"].toString();
    secret   = merged["secret"].toString();
    username = merged["username"].toString();
    password = merged["password"].toString();
    clientId = merged["clientId"].toString();
    hostname = merged["hostname"].toString();
    port     = merged["port"].toInt(8883);

    qDebug() << "[IoT] device:" << deviceId;
    qDebug() << "[IoT] host:" << hostname << ":" << port;
    qDebug() << "[IoT] username:" << username;
    qDebug() << "[IoT] clientId:" << clientId;
    qDebug() << "[IoT] password length:" << password.size();
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
