#include "iotuploader.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QCoreApplication>

IotUploader* IotUploader::instance() {
    static IotUploader *inst = nullptr;
    if (!inst) inst = new IotUploader();
    return inst;
}

IotUploader::IotUploader(QObject *parent) : QObject(parent) {
    reconnectTimer = new QTimer(this);
    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, &IotUploader::startProcess);
}

void IotUploader::init(const QString &configPath) {
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "IotUploader: cannot read config" << configPath;
        return;
    }
    QString content = QString::fromUtf8(f.readAll());
    f.close();

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
            }
        }
    }

    deviceId = merged["device_id"].toString();
    username = merged["username"].toString();
    password = merged["password"].toString();
    clientId = merged["clientId"].toString();
    hostname = merged["hostname"].toString();
    port     = merged["port"].toInt(8883);

    qDebug() << "[IoT] device:" << deviceId << "host:" << hostname << ":" << port;
    startProcess();
}

void IotUploader::startProcess() {
    if (proc) {
        proc->kill();
        proc->deleteLater();
        proc = nullptr;
    }
    ready = false;

    // 用 mosquitto_pub -l 保持长连接，stdin 持续发
    proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    QString topic = QString("$oc/devices/%1/sys/properties/report").arg(deviceId);

    QStringList args;
    args << "-h" << hostname
         << "-p" << QString::number(port)
         << "-u" << username
         << "-P" << password
         << "-i" << clientId
         << "-t" << topic
         << "-l"           // 保持连接，从 stdin 读行发送
         << "--insecure"
         << "-q" << "1"
         << "-V" << "mqttv311";

    qDebug() << "[IoT] starting persistent mosquitto_pub -l...";
    proc->start("mosquitto_pub", args);

    connect(proc, &QProcess::started, this, [this]() {
        qDebug() << "[IoT] process started, pid:" << proc->processId();
        ready = true;
        if (!pendingPayload.isEmpty()) {
            QByteArray line = pendingPayload.toUtf8().replace('\n', ' ') + '\n';
            proc->write(line);
            qDebug() << "[IoT] sent pending payload";
            pendingPayload.clear();
        }
    });

    connect(proc, &QProcess::readyReadStandardError, this, [this]() {
        QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        if (!err.isEmpty())
            qDebug() << "[IoT]" << err;
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        qWarning() << "[IoT] mosquitto_pub exited, code:" << code;
        ready = false;
        reconnectTimer->start(5000);
    });

    // 5秒内没收到 started 信号说明启动失败
    QTimer::singleShot(5000, this, [this]() {
        if (proc && !ready) {
            qWarning() << "[IoT] startup timeout, restarting...";
            proc->kill();
        }
    });
}

void IotUploader::upload(const QJsonObject &sensorData) {
    if (deviceId.isEmpty() || hostname.isEmpty()) return;

    QJsonObject root;
    QJsonArray services;
    QJsonObject svc;
    svc["service_id"] = "tea_sensor";
    svc["properties"] = sensorData;
    svc["event_time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    services.append(svc);
    root["services"] = services;

    QJsonDocument doc(root);
    sendPayload(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void IotUploader::uploadPest(const QJsonObject &pestData) {
    if (deviceId.isEmpty() || hostname.isEmpty()) return;

    // 用 QJsonDocument 安全编码字符串字段
    QJsonObject props;
    props["pest_type"]   = pestData["pest_type"];
    props["pest_severity"] = pestData["pest_severity"];
    // 先放占位符，之后替换为高精度数字
    props["pest_lat"]    = "@@LAT@@";
    props["pest_lng"]    = "@@LNG@@";
    props["pest_total_count"] = pestData["pest_total_count"];

    QJsonObject svc;
    svc["service_id"] = "tea_pest";
    svc["properties"] = props;
    svc["event_time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonArray services; services.append(svc);
    QJsonObject root; root["services"] = services;

    QString payload = QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Compact));

    // 替换占位符为 6 位精度的经纬度数字
    payload.replace("\"@@LAT@@\"", QString::number(pestData["pest_lat"].toDouble(), 'f', 6));
    payload.replace("\"@@LNG@@\"", QString::number(pestData["pest_lng"].toDouble(), 'f', 6));

    sendPayload(payload);
}

void IotUploader::sendPayload(const QString &payload) {
    if (ready && proc && proc->state() == QProcess::Running) {
        // mosquitto_pub -l 从 stdin 逐行读取 JSON
        QByteArray line = payload.toUtf8().replace('\n', ' ') + '\n';
        proc->write(line);
    } else {
        // 进程还没就绪，缓存最新一条数据
        pendingPayload = payload;
        if (!ready) startProcess();
    }
}
