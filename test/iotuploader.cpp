#include "iotuploader.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QCoreApplication>

// mosquitto C API
#include <mosquitto.h>

IotUploader* IotUploader::instance() {
    static IotUploader *inst = nullptr;
    if (!inst) {
        mosquitto_lib_init();
        inst = new IotUploader();
    }
    return inst;
}

IotUploader::IotUploader(QObject *parent) : QObject(parent) {
    reconnectTimer = new QTimer(this);
    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, &IotUploader::connectBroker);
}

IotUploader::~IotUploader() {
    if (m_handle) {
        mosquitto_disconnect(m_handle);
        mosquitto_destroy(m_handle);
    }
    mosquitto_lib_cleanup();
}

void IotUploader::init(const QString &configPath) {
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "IotUploader: cannot read config" << configPath;
        return;
    }
    QString content = QString::fromUtf8(f.readAll());
    f.close();

    // 多对象 JSON 解析
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

    connectBroker();
}

void IotUploader::connectBroker() {
    if (m_handle) {
        mosquitto_disconnect(m_handle);
        mosquitto_destroy(m_handle);
        m_handle = nullptr;
    }

    m_handle = mosquitto_new(clientId.toUtf8().constData(), true, this);
    if (!m_handle) {
        qWarning() << "[IoT] mosquitto_new failed";
        reconnectTimer->start(5000);
        return;
    }

    // 设凭证
    mosquitto_username_pw_set(m_handle,
        username.toUtf8().constData(),
        password.toUtf8().constData());

    // 回调
    mosquitto_connect_callback_set(m_handle, onConnect);
    mosquitto_publish_callback_set(m_handle, onPublish);
    mosquitto_disconnect_callback_set(m_handle, onDisconnect);
    mosquitto_log_callback_set(m_handle, onLog);

    // TLS: 跳过证书验证（同 mosquitto_pub --insecure）
    mosquitto_tls_insecure_set(m_handle, true);

    qDebug() << "[IoT] connecting..." << hostname << port;
    int rc = mosquitto_connect(m_handle,
        hostname.toUtf8().constData(), port, 30 /* keepalive */);

    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "[IoT] connect failed:" << mosquitto_strerror(rc);
        reconnectTimer->start(5000);
        return;
    }

    // 启动网络事件循环（libmosquitto 需要 mosquitto_loop 持续跑）
    // 用 QTimer 每 100ms tick 一次
    keepaliveTimer = new QTimer(this);
    connect(keepaliveTimer, &QTimer::timeout, this, [this]() {
        if (m_handle) mosquitto_loop(m_handle, 10, 1);
    });
    keepaliveTimer->start(100);
}

// 静态回调
void IotUploader::onConnect(struct mosquitto *, void *obj, int rc) {
    auto *self = static_cast<IotUploader*>(obj);
    if (rc == 0) {
        qDebug() << "[IoT] connected ✓";
        self->connected = true;
        // 连接成功后立即发送缓存的数据
        if (!self->pendingPayload.isEmpty()) {
            self->publish(self->pendingPayload);
            self->pendingPayload.clear();
        }
    } else {
        qWarning() << "[IoT] connect failed, rc=" << rc << mosquitto_strerror(rc);
        self->connected = false;
        self->reconnectTimer->start(5000);
    }
}

void IotUploader::onPublish(struct mosquitto *, void *, int) {
    // PUBACK 收到（QoS 1），静默
}

void IotUploader::onDisconnect(struct mosquitto *, void *obj, int rc) {
    auto *self = static_cast<IotUploader*>(obj);
    qWarning() << "[IoT] disconnected, rc=" << rc;
    self->connected = false;
    if (rc != 0) {
        self->reconnectTimer->start(3000);
    }
}

void IotUploader::onLog(struct mosquitto *, void *, int level, const char *str) {
    // 只记录重要日志
    if (level >= MOSQ_LOG_WARNING)
        qDebug() << "[IoT mosquitto]" << str;
}

void IotUploader::publish(const QString &payload) {
    if (!m_handle || !connected) return;

    QByteArray topic = QString("$oc/devices/%1/sys/properties/report")
                           .arg(deviceId).toUtf8();
    QByteArray msg = payload.toUtf8();

    int mid = 0;
    int rc = mosquitto_publish(m_handle, &mid, topic.constData(),
        msg.size(), msg.constData(), 1 /* QoS 1 */, false /* retain */);

    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "[IoT] publish failed:" << mosquitto_strerror(rc);
    }
}

// 上传数据（从 onLoraData/refreshData 每 3s 调用一次）
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
    QString payload = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    if (connected) {
        publish(payload);
    } else {
        // 缓存最新数据，连接成功时只发最新的
        pendingPayload = payload;
    }
}
