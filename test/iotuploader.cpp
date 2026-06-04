#include "iotuploader.h"
#include "pestdata.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QSslConfiguration>

IotUploader* IotUploader::instance() {
    static IotUploader *inst = nullptr;
    if (!inst) inst = new IotUploader();
    return inst;
}

IotUploader::IotUploader(QObject *parent) : QObject(parent) {
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &IotUploader::connectToHost);

    m_client = new QMqttClient(this);
    connect(m_client, &QMqttClient::connected,    this, &IotUploader::onConnected);
    connect(m_client, &QMqttClient::disconnected, this, &IotUploader::onDisconnected);
    connect(m_client, &QMqttClient::errorChanged, this, &IotUploader::onError);
    connect(m_client, &QMqttClient::messageReceived, this, &IotUploader::onMessageReceived);
}

void IotUploader::init(const QString &configPath) {
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "IotUploader: cannot read config" << configPath;
        return;
    }
    QString content = QString::fromUtf8(f.readAll());
    f.close();

    // 解析合并的 JSON（每行一个 { ... } 对象）
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

    m_deviceId = merged["device_id"].toString();
    m_username = merged["username"].toString();
    m_password = merged["password"].toString();
    m_clientId = merged["clientId"].toString();
    m_hostname = merged["hostname"].toString();
    m_port     = merged["port"].toInt(8883);

    m_reportTopic  = QString("$oc/devices/%1/sys/properties/report").arg(m_deviceId);
    m_commandTopic = QString("$oc/devices/%1/sys/commands/#").arg(m_deviceId);

    qDebug() << "[IoT] device:" << m_deviceId << "host:" << m_hostname << ":" << m_port;
    connectToHost();
}

void IotUploader::connectToHost() {
    if (m_connecting) return;
    m_connecting = true;

    m_client->setHostname(m_hostname);
    m_client->setPort(m_port);
    m_client->setUsername(m_username);
    m_client->setPassword(m_password);
    m_client->setClientId(m_clientId);
    m_client->setProtocolVersion(QMqttClient::MQTT_3_1_1);

    qDebug() << "[IoT] connecting to" << m_hostname << ":" << m_port;
    m_client->connectToHost();
}

void IotUploader::onConnected() {
    m_connecting = false;
    qDebug() << "[IoT] MQTT connected, subscribing" << m_commandTopic;
    m_client->subscribe(m_commandTopic, 1);
}

void IotUploader::onDisconnected() {
    m_connecting = false;
    qWarning() << "[IoT] MQTT disconnected, reconnecting in 5s";
    m_reconnectTimer->start(5000);
}

void IotUploader::onError(QMqttClient::ClientError error) {
    m_connecting = false;
    qWarning() << "[IoT] MQTT error:" << error;
    m_reconnectTimer->start(5000);
}

void IotUploader::onMessageReceived(const QByteArray &message, const QMqttTopicName & /*topic*/) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message, &err);
    if (doc.isNull()) {
        qWarning() << "[IoT] bad JSON:" << err.errorString();
        return;
    }
    QJsonObject msg = doc.object();
    QString serviceId = msg["service_id"].toString();
    QString cmdName   = msg["command_name"].toString();
    QString requestId = msg["request_id"].toString();
    QJsonObject paras = msg["paras"].toObject();

    qDebug() << "[IoT] received command:" << serviceId << cmdName << requestId;

    if (serviceId == "pump_control") {
        bool on = paras["pump"].toBool();
        emit deviceCommandReceived("pump", on, requestId);
    } else if (serviceId == "light_control") {
        bool on = paras["light"].toBool();
        emit deviceCommandReceived("lamp", on, requestId);
    } else {
        qDebug() << "[IoT] unknown service:" << serviceId << "- ignored";
    }
}

void IotUploader::sendCommandResponse(const QString &cmdName,
                                       const QString &requestId,
                                       int result) {
    if (m_client->state() != QMqttClient::Connected) {
        qWarning() << "[IoT] cannot respond, not connected";
        return;
    }

    QString topic = QString("$oc/devices/%1/sys/commands/response/request_id=%2")
                        .arg(m_deviceId, requestId);

    QJsonObject body;
    body["result_code"]   = 0;
    body["response_name"] = cmdName;
    QJsonObject r;
    r["result"] = result;
    body["paras"] = r;

    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_client->publish(topic, payload, 1);
    qDebug() << "[IoT] command response:" << cmdName << "result:" << result;
}

void IotUploader::upload(const QJsonObject &sensorData) {
    if (m_deviceId.isEmpty() || m_hostname.isEmpty()) return;
    doUpload(sensorData);
}

void IotUploader::doUpload(const QJsonObject &sensorData) {
    if (m_client->state() != QMqttClient::Connected) {
        qDebug() << "[IoT] skip upload, not connected";
        return;
    }

    QJsonObject root;
    QJsonArray services;
    QJsonObject svc;
    svc["service_id"] = "tea_sensor";
    svc["properties"]  = sensorData;
    svc["event_time"]  = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    services.append(svc);
    root["services"] = services;

    QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    m_client->publish(m_reportTopic, payload, 1);
}

void IotUploader::uploadPest() {
    if (m_deviceId.isEmpty() || m_hostname.isEmpty()) return;
    if (m_client->state() != QMqttClient::Connected) {
        qDebug() << "[IoT] skip pest upload, not connected";
        return;
    }

    // 1. 种类统计 → JSON 字符串
    QMap<QString, int> counts = PestStore::instance()->typeCounts();
    QJsonObject countObj;
    for (auto it = counts.begin(); it != counts.end(); ++it)
        countObj[it.key()] = it.value();
    QString pestCounts = QString::fromUtf8(
        QJsonDocument(countObj).toJson(QJsonDocument::Compact));

    // 2. 位置列表（最近 20 条）→ JSON 字符串
    QVector<PestRecord> records = PestStore::instance()->queryAll();
    QJsonArray locArray;
    for (int i = 0; i < qMin(20, records.size()); ++i) {
        const auto &r = records[i];
        QJsonObject loc;
        loc["lat"]  = QString::number(r.lat, 'f', 6);
        loc["lng"]  = QString::number(r.lng, 'f', 6);
        loc["type"] = r.pestType;
        loc["sev"]  = r.severity;
        loc["time"] = r.timestamp.toString("MM-dd HH:mm");
        locArray.append(loc);
    }
    QString pestLocs = QString::fromUtf8(
        QJsonDocument(locArray).toJson(QJsonDocument::Compact));

    // 3. 总数
    int total = PestStore::instance()->totalCount();

    QJsonObject props;
    props["pest_counts"]    = pestCounts;
    props["pest_locations"] = pestLocs;
    props["pest_total"]     = total;

    QJsonObject svc;
    svc["service_id"] = "tea_pest";
    svc["properties"]  = props;
    svc["event_time"]  = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonArray services; services.append(svc);
    QJsonObject root; root["services"] = services;

    QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    m_client->publish(m_reportTopic, payload, 1);
}
