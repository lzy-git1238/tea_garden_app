#include "iotuploader.h"
#include "pestdata.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <cstdio>
#include <mqtt/connect_options.h>
#include <mqtt/ssl_options.h>
#include <mqtt/message.h>

IotUploader* IotUploader::instance() {
    static IotUploader *inst = nullptr;
    if (!inst) inst = new IotUploader();
    return inst;
}

IotUploader::IotUploader(QObject *parent) : QObject(parent) {
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &IotUploader::connectToHost);
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

    // 构造 MQTT 连接 URI（MQTTS 用 ssl:// 前缀）
    QString uri = QString("ssl://%1:%2").arg(m_hostname).arg(m_port);
    m_client = std::make_unique<mqtt::async_client>(
        uri.toStdString(), m_clientId.toStdString());
    m_client->set_callback(*this);

    connectToHost();
}

void IotUploader::connectToHost() {
    if (m_connecting || !m_client) return;
    m_connecting = true;

    mqtt::connect_options connOpts;
    connOpts.set_user_name(m_username.toStdString());
    connOpts.set_password(m_password.toStdString());
    connOpts.set_clean_session(true);

    // TLS 配置（华为云 MQTTS 端口 8883）
    mqtt::ssl_options sslOpts;
    sslOpts.set_enable_server_cert_auth(true);
    // 系统 CA 证书验证华为云证书
    connOpts.set_ssl(sslOpts);

    qDebug() << "[IoT] connecting to" << m_hostname << ":" << m_port;
    try {
        m_client->connect(connOpts);
    } catch (const mqtt::exception &e) {
        qWarning() << "[IoT] connect exception:" << e.what();
        m_connecting = false;
        m_reconnectTimer->start(5000);
    }
}

// ── mqtt::callback 重写 ──

void IotUploader::connected(const std::string & /*cause*/) {
    m_connecting = false;
    qDebug() << "[IoT] MQTT connected, subscribing" << m_commandTopic;
    try {
        m_client->subscribe(m_commandTopic.toStdString(), 1);
        qDebug() << "[IoT] subscribed to commands";
    } catch (const mqtt::exception &e) {
        qWarning() << "[IoT] subscribe failed:" << e.what();
    }
}

void IotUploader::connection_lost(const std::string &cause) {
    m_connecting = false;
    qWarning() << "[IoT] MQTT disconnected:" << QString::fromStdString(cause);
    QMetaObject::invokeMethod(this, [this]() {
        m_reconnectTimer->start(5000);
    }, Qt::QueuedConnection);
}

void IotUploader::message_arrived(mqtt::const_message_ptr msg) {
    QString payloadStr = QString::fromStdString(msg->get_payload_str());
    fprintf(stderr, "[IOT-MSG] arrived, len=%d\n", (int)payloadStr.size());
    fprintf(stderr, "[IOT-MSG] payload: %s\n", payloadStr.toUtf8().constData());

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(payloadStr.toUtf8(), &err);
    if (doc.isNull()) {
        qWarning() << "[IoT] bad JSON:" << err.errorString();
        return;
    }
    QJsonObject obj = doc.object();
    QString serviceId = obj["service_id"].toString();
    QString cmdName   = obj["command_name"].toString();
    QString requestId = obj["request_id"].toString();
    QJsonObject paras = obj["paras"].toObject();

    qDebug() << "[IoT] parsed: serviceId=" << serviceId
             << "cmdName=" << cmdName
             << "requestId=" << requestId
             << "paras=" << paras;

    if (cmdName == "pump_control") {
        bool on = paras["pump"].toBool();
        bool ok = QMetaObject::invokeMethod(this, [this, on, requestId]() {
            qDebug() << "[IoT] emit deviceCommandReceived(pump," << on << ")";
            emit deviceCommandReceived("pump", on, requestId);
        }, Qt::QueuedConnection);
        qDebug() << "[IoT] invokeMethod queued:" << ok;
    } else if (cmdName == "light") {
        bool on = paras["light"].toBool();
        bool ok = QMetaObject::invokeMethod(this, [this, on, requestId]() {
            qDebug() << "[IoT] emit deviceCommandReceived(lamp," << on << ")";
            emit deviceCommandReceived("lamp", on, requestId);
        }, Qt::QueuedConnection);
        qDebug() << "[IoT] invokeMethod queued:" << ok;
    } else {
        qDebug() << "[IoT] unknown command:" << cmdName << "(service:" << serviceId << ") - ignored";
    }
}

void IotUploader::delivery_complete(mqtt::delivery_token_ptr /*token*/) {
    // 发布完成通知，通常不需要处理
}

// ── 公共接口 ──

void IotUploader::sendCommandResponse(const QString &cmdName,
                                       const QString &requestId,
                                       int result) {
    qDebug() << "[IoT] sendCommandResponse: cmdName=" << cmdName
             << "requestId=" << requestId << "result=" << result;

    if (!m_client) {
        qWarning() << "[IoT] no client, cannot respond";
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
    qDebug() << "[IoT] response topic:" << topic;
    qDebug() << "[IoT] response payload:" << payload;

    // 直接 publish，不检查 is_connected()——该函数在回调→主线程切换
    // 的几百毫秒间可能因 TCP 抖动返回 false，导致响应被静默丢弃。
    // Paho publish 内部自己会处理连接状态。
    try {
        m_client->publish(topic.toStdString(),
                          (const void*)payload.constData(),
                          (size_t)payload.size(), 1, false);
        qDebug() << "[IoT] response published OK:" << cmdName << "result:" << result;
    } catch (const mqtt::exception &e) {
        qWarning() << "[IoT] publish response failed:" << e.what();
        // Paho 连接可能真的断了——等待异步重连后重试一次
        QTimer::singleShot(2000, this, [this, topic, payload, cmdName, result]() {
            try {
                if (m_client) {
                    m_client->publish(topic.toStdString(),
                                      (const void*)payload.constData(),
                                      (size_t)payload.size(), 1, false);
                    qDebug() << "[IoT] response published (retry) OK:" << cmdName << "result:" << result;
                }
            } catch (const mqtt::exception &e2) {
                qWarning() << "[IoT] response retry also failed:" << e2.what();
            }
        });
    }
}

void IotUploader::upload(const QJsonObject &sensorData) {
    if (m_deviceId.isEmpty() || m_hostname.isEmpty()) return;
    doUpload(sensorData);
}

void IotUploader::doUpload(const QJsonObject &sensorData) {
    if (!m_client || !m_client->is_connected()) {
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
    try {
        m_client->publish(m_reportTopic.toStdString(),
                        (const void*)payload.constData(),
                        (size_t)payload.size(), 1, false);
    } catch (const mqtt::exception &e) {
        qWarning() << "[IoT] publish failed:" << e.what();
    }
}

void IotUploader::uploadPest() {
    if (m_deviceId.isEmpty() || m_hostname.isEmpty()) return;
    if (!m_client || !m_client->is_connected()) {
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
    try {
        m_client->publish(m_reportTopic.toStdString(),
                        (const void*)payload.constData(),
                        (size_t)payload.size(), 1, false);
    } catch (const mqtt::exception &e) {
        qWarning() << "[IoT] publish pest failed:" << e.what();
    }
}
