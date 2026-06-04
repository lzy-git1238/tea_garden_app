# 华为云 IoT 命令下发 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 QMqttClient 替换 mosquitto_pub，实现华为云 IoT 双向通信：属性上报 + 接收 pump_control/light_control 命令 + 响应云端。

**Architecture:** 重写 IotUploader（QProcess → QMqttClient），信号/槽驱动命令转发链：`messageReceived → emit deviceCommandReceived → TeaGardenSystem::onIotCommand → LoRa发送 → sendCommandResponse`。

**Tech Stack:** Qt 5 MQTT (`libqt5mqtt5-dev`), QMqttClient, QJsonDocument

**Spec:** `docs/superpowers/specs/2026-06-04-huaweiiot-command-design.md`

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `test/iotuploader.h` | 重写 | QProcess → QMqttClient，新增 deviceCommandReceived 信号 + sendCommandResponse |
| `test/iotuploader.cpp` | 重写 | MQTT 连接/TLS、topic subscribe、命令解析、响应 publish |
| `test/test.pro` | 修改 | `QT += mqtt` |
| `test/teagardensystem.h` | 修改 | 新增 `onIotCommand` 槽函数声明 |
| `test/teagardensystem.cpp` | 修改 | 连接 IotUploader 信号 + 实现 onIotCommand |

---

### Task 1: 更新 test.pro

**Files:**
- Modify: `test/test.pro:1`

- [ ] **Step 1: Add `mqtt` to QT config**

Change line 1 of `/home/lzy/文档/QTproject/test/test.pro` from:

```
QT       += core gui serialport network charts sql websockets
```

to:

```
QT       += core gui serialport network charts sql websockets mqtt
```

- [ ] **Step 2: Commit**

```bash
cd /home/lzy/文档/QTproject
git add test/test.pro
git commit -m "feat: add QT += mqtt for IoT command support"
```

---

### Task 2: 重写 iotuploader.h

**Files:**
- Modify: `test/iotuploader.h` (replace entire file)

- [ ] **Step 1: Write new iotuploader.h**

Replace the entire content of `/home/lzy/文档/QTproject/test/iotuploader.h` with:

```cpp
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
```

- [ ] **Step 2: Commit**

```bash
git add test/iotuploader.h
git commit -m "feat: rewrite iotuploader.h — QMqttClient replacing mosquitto_pub"
```

---

### Task 3: 重写 iotuploader.cpp

**Files:**
- Modify: `test/iotuploader.cpp` (replace entire file)

- [ ] **Step 1: Write new iotuploader.cpp**

Replace the entire content of `/home/lzy/文档/QTproject/test/iotuploader.cpp` with:

```cpp
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
```

- [ ] **Step 2: Commit**

```bash
git add test/iotuploader.cpp
git commit -m "feat: rewrite iotuploader.cpp — QMqttClient with command subscribe and response"
```

---

### Task 4: 更新 teagardensystem.h

**Files:**
- Modify: `test/teagardensystem.h`

- [ ] **Step 1: Add new slot declaration**

After line 64 (`openScheduleDialog`), add:

```cpp
    void onIotCommand(const QString &device, bool on, const QString &requestId);
```

- [ ] **Step 2: Commit**

```bash
git add test/teagardensystem.h
git commit -m "feat: add onIotCommand slot to teagardensystem.h"
```

---

### Task 5: 更新 teagardensystem.cpp — 信号连接 + onIotCommand

**Files:**
- Modify: `test/teagardensystem.cpp`

- [ ] **Step 1: Add connect in constructor**

After line 113 (`IotUploader::instance()->init(...)`), add:

```cpp
    // 连接 IoT 命令信号
    connect(IotUploader::instance(), &IotUploader::deviceCommandReceived,
            this, &TeaGardenSystem::onIotCommand);
```

- [ ] **Step 2: Add onIotCommand implementation**

Before the `// ── LoRa 下行控制 ──` comment block, add:

```cpp
// ── IoT 云端命令 ──

void TeaGardenSystem::onIotCommand(const QString &device, bool on,
                                    const QString &requestId) {
    qDebug() << "[TeaGarden] IoT command:" << device << (on ? "on" : "off");

    // 云端命令覆盖本地定时
    if (activeJobs.contains(device)) {
        activeJobs.remove(device);
        if (device == "pump") flowCard->setVisible(false);
    }

    // 同步按钮状态
    if (device == "pump") {
        pumpBtn->blockSignals(true);
        pumpBtn->setChecked(on);
        pumpBtn->blockSignals(false);
    } else {
        lampBtn->blockSignals(true);
        lampBtn->setChecked(on);
        lampBtn->blockSignals(false);
    }

    // LoRa 发送（走防抖，改用直接发送确保即时响应）
    int cmd = (device == "pump") ? (on ? 3 : 4) : (on ? 1 : 2);
    bool ok = sendLoraCommand(device, cmd);
    int result = ok ? 0 : 1;

    // 记录日志
    ScheduleLogEntry log;
    log.deviceType = device;
    log.scheduleId = 0;
    log.action = on ? "on" : "off";
    log.trigger = "manual";  // 云端命令标注为手动
    log.ts = QDateTime::currentDateTime();
    ScheduleStore::instance()->appendLog(log);

    // 聊天通知
    QString devName = (device == "pump") ? "灌溉泵" : "驱虫灯";
    appendChatMessage("云端", QString("📡 %1: %2 %3")
        .arg(devName)
        .arg(on ? "开启" : "关闭")
        .arg(ok ? "" : "(LoRa发送失败)"));

    // 响应云端
    QString cmdName = (device == "pump") ? "pump_control" : "light_control";
    IotUploader::instance()->sendCommandResponse(cmdName, requestId, result);
}
```

- [ ] **Step 3: Commit**

```bash
git add test/teagardensystem.cpp
git commit -m "feat: add onIotCommand — IoT cloud command to LoRa relay"
```

---

### Task 6: 安装依赖 + 构建验证

**Files:**
- (none — build only)

- [ ] **Step 1: Install Qt MQTT development package**

```bash
sudo apt install -y libqt5mqtt5-dev 2>&1
```

Expected: Package installs successfully (may already be installed).

- [ ] **Step 2: Rebuild project**

```bash
cd /home/lzy/文档/QTproject/test
qmake test.pro && make -j$(nproc) 2>&1
```

Expected: Compilation succeeds with no errors.

- [ ] **Step 3: Fix any compilation errors**

Common issues:
- `QMqttClient` not found → verify `libqt5mqtt5-dev` is installed
- `#include <QMqttClient>` → Qt5 MQTT header path, may need `<QtMqtt/QMqttClient>` on some systems
- `moc` issues → run `qmake` again after header changes

- [ ] **Step 4: Quick smoke test**

```bash
cd /home/lzy/文档/QTproject/test
timeout 5 ./test 2>&1 || true
```

Expected: Binary runs, note `[IoT] connecting to...` log line. No crash on startup.

- [ ] **Step 5: Commit if any fixes were made, otherwise finalize**

```bash
git add -A
git commit -m "feat: IoT command delivery — build verified"
```
