# 华为云 IoT 命令下发 — 设计文档

> 日期：2026-06-04  
> 状态：设计完成，待评审

---

## 1. 功能概述

在现有华为云 IoT 属性上报（上行）基础上，新增云端命令接收（下行）和响应功能。RK3588 端接收 `pump_control` 和 `light_control` 两个命令，转发到 LoRa 控制灌溉泵/灭虫灯，并给云端返回执行结果。

---

## 2. 设计决策

| 决策项 | 选择 | 说明 |
|--------|------|------|
| MQTT 通信方式 | **Qt MQTT 模块 (QMqttClient)** | 替换 mosquitto_pub，单连接双向通信 |
| 响应时机 | **LoRa 发送后立即响应** | 不等设备确认，LoRa 是单向的 |
| result 含义 | **0=成功, 1=失败** | 简单二元 |
| 云端命令 vs 本地定时 | **云端优先** | 收到云端命令时取消当前定时任务，等同手动干预 |
| 未识别命令 | **静默忽略** | 不响应，debug 日志记录 |

---

## 3. 云端命令协议

### 3.1 pump_control

**下发：**
```json
{
  "service_id": "pump_control",
  "command_name": "pump_control",
  "paras": { "pump": true }
}
```

**响应：**
```json
{
  "result_code": 0,
  "response_name": "pump_control",
  "paras": { "result": 0 }
}
```

### 3.2 light_control

**下发：**
```json
{
  "service_id": "light_control",
  "command_name": "light",
  "paras": { "light": false }
}
```

**响应：**
```json
{
  "result_code": 0,
  "response_name": "light_control",
  "paras": { "result": 0 }
}
```

---

## 4. 文件变更

| 文件 | 操作 | 说明 |
|------|------|------|
| `test/iotuploader.h` | 重写 | QProcess → QMqttClient，新增信号/槽 |
| `test/iotuploader.cpp` | 重写 | MQTT 连接、subscribe、解析命令、发响应 |
| `test/test.pro` | 修改 | `QT += mqtt` |
| `test/teagardensystem.h` | 修改 | 新增 `onIotCommand` 槽函数声明 |
| `test/teagardensystem.cpp` | 修改 | 连接 IotUploader 信号、实现 onIotCommand |

---

## 5. MQTT Topic 定义

| 用途 | Topic | 方向 | QoS |
|------|-------|------|-----|
| 属性上报 | `$oc/devices/{id}/sys/properties/report` | 设备 → 云 | 1 |
| 命令订阅 | `$oc/devices/{id}/sys/commands/#` | 云 → 设备 | 1 |
| 命令响应 | `$oc/devices/{id}/sys/commands/response/request_id={rid}` | 设备 → 云 | 1 |

---

## 6. IotUploader 接口设计

### 6.1 公有接口（保持兼容）

| 方法 | 说明 |
|------|------|
| `static IotUploader* instance()` | 单例 |
| `void init(const QString &configPath)` | 从 IOT_DATA 读取凭据，建立 MQTT 连接 |
| `void upload(const QJsonObject &sensorData)` | 属性上报（接口不变，实现改为 QMqttClient::publish） |
| `void uploadPest()` | 病虫害上报（接口不变） |
| `void sendCommandResponse(cmdName, requestId, result)` | **新增**：响应云端命令 |

### 6.2 新增信号

```cpp
signals:
    void deviceCommandReceived(const QString &device,   // "pump" / "lamp"
                               bool on,                  // true=开 false=关
                               const QString &requestId);// 华为云 request_id
```

### 6.3 命令解析逻辑

```
messageReceived(topic, payload)
  → 解析 service_id
  → "pump_control" → emit deviceCommandReceived("pump", paras["pump"], requestId)
  → "light_control" → emit deviceCommandReceived("lamp", paras["light"], requestId)
  → 其他 → 静默忽略，debug 日志
```

---

## 7. TeaGardenSystem 集成

### 7.1 信号连接

```cpp
// 构造函数中
connect(IotUploader::instance(), &IotUploader::deviceCommandReceived,
        this, &TeaGardenSystem::onIotCommand);
```

### 7.2 onIotCommand 槽函数

```cpp
void TeaGardenSystem::onIotCommand(const QString &device, bool on,
                                    const QString &requestId) {
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

    // LoRa 发送
    int cmd = (device == "pump") ? (on ? 3 : 4) : (on ? 1 : 2);
    bool ok = loraManager->sendCommand(cmd);
    int result = ok ? 0 : 1;

    // 记录日志
    ScheduleLogEntry log;
    log.deviceType = device;
    log.scheduleId = 0;
    log.action = on ? "on" : "off";
    log.trigger = "manual";  // 云端命令归为手动
    log.ts = QDateTime::currentDateTime();
    ScheduleStore::instance()->appendLog(log);

    // 响应云端
    QString cmdName = (device == "pump") ? "pump_control" : "light_control";
    IotUploader::instance()->sendCommandResponse(cmdName, requestId, result);
}
```

---

## 8. 错误处理 & 边界情况

| 场景 | 处理 |
|------|------|
| MQTT 断连 | QMqttClient 内建自动重连，丢失命令由华为云超时重试 |
| LoRa 离线 | sendCommand 返回 false → result: 1 → 响应失败 |
| 未识别命令 | 静默忽略，debug 日志 |
| 与定时调度冲突 | 云端命令优先，取消当前 activeJob，等同手动干预 |
| TLS 证书 | Qt MQTT 使用系统 CA 证书，华为云证书链已内置 |
| 重复 request_id | 不处理幂等，华为云侧负责去重 |

---

## 9. 依赖变更

RK3588 / 开发机上安装 Qt MQTT：

```bash
sudo apt install libqt5mqtt5-dev
```

`test.pro` 添加：

```qmake
QT += mqtt
```

---

## 10. 测试要点

- [ ] 程序启动 → QMqttClient 连接成功 → subscribe commands topic
- [ ] 华为云控制台下发 pump_control {pump: true} → LoRa 发 3 → 响应 result:0
- [ ] 下发 light_control {light: false} → LoRa 发 2 → 响应 result:0
- [ ] LoRa 离线时下发命令 → 响应 result:1
- [ ] 断网重连 → QMqttClient 自动恢复
- [ ] 属性上报 (tea_sensor / tea_pest) 功能不退化
- [ ] 云端命令覆盖本地定时计划
