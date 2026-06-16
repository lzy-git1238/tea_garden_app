#include "yolodetector.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>

YoloDetector::YoloDetector(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
    , m_heartbeatTimer(new QTimer(this))
{
    // Socket 信号
    connect(m_socket, &QTcpSocket::connected,
            this, &YoloDetector::onConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &YoloDetector::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &YoloDetector::onSocketError);

    // 断连自动重连
    connect(m_reconnectTimer, &QTimer::timeout, this, &YoloDetector::connectToServer);

    // 心跳
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            // 发送心跳包: [4B total_len=0][4B width=0][4B height=0]
            QByteArray header(12, 0);
            m_socket->write(header);
            m_socket->flush();
        }
    });
}

YoloDetector::~YoloDetector() {
    stop();
}

void YoloDetector::start(const QString &host, int port) {
    m_host = host;
    m_port = port;
    m_fpsTimer.start();
    m_frameCount = 0;
    connectToServer();
}

void YoloDetector::stop() {
    m_reconnectTimer->stop();
    m_heartbeatTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

void YoloDetector::setFpsLimit(int fps) {
    m_fpsLimit = qBound(1, fps, 30);
}

void YoloDetector::connectToServer() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    qDebug() << "[YOLO] 连接到推理服务:" << m_host << ":" << m_port;
    emit statusMessage("正在连接 YOLO 推理服务...");
    m_socket->connectToHost(m_host, m_port);
}

void YoloDetector::onConnected() {
    qDebug() << "[YOLO] 已连接到推理服务";
    emit statusMessage("YOLO 推理服务已连接");
    m_reconnectTimer->stop();
    m_heartbeatTimer->start(kHeartbeatMs);
}

void YoloDetector::onSocketError(QAbstractSocket::SocketError err) {
    Q_UNUSED(err)
    m_heartbeatTimer->stop();
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        emit statusMessage("YOLO 推理断开，3 秒后重连...");
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(kConnectRetryMs);
        }
    }
}

void YoloDetector::onNewFrame(const QImage &frame) {
    // 帧率限制
    if (m_fpsTimer.elapsed() < 1000.0 / m_fpsLimit) {
        return;
    }
    m_fpsTimer.restart();

    // 如果正在等待上一帧结果，跳过
    if (m_waitingResult) {
        return;
    }

    // 未连接时跳过
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    // 保存原始帧，降采样后发送
    m_lastFrame = frame;
    sendFrame(frame);
}

void YoloDetector::sendFrame(const QImage &frame) {
    // 降采样到 640x640
    QImage resized = frame.scaled(kTargetWidth, kTargetHeight,
                                  Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (resized.isNull()) {
        return;
    }

    // 转为 RGB888（确保格式正确）
    QImage rgb888 = resized.convertToFormat(QImage::Format_RGB888);
    if (rgb888.isNull()) {
        return;
    }

    m_waitingResult = true;
    m_lastResizedFrame = rgb888;

    // 构造二进制帧: [4B total_len][4B width][4B height][RGB data]
    int w = rgb888.width();
    int h = rgb888.height();
    int totalLen = w * h * 3;

    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(totalLen);
    stream << static_cast<quint32>(w);
    stream << static_cast<quint32>(h);

    // 发送 header + 像素数据
    m_socket->write(header);
    m_socket->write(reinterpret_cast<const char*>(rgb888.constBits()), totalLen);
    m_socket->flush();
}

void YoloDetector::onReadyRead() {
    m_recvBuffer.append(m_socket->readAll());

    // 尝试解析: 先读 4 字节长度前缀
    while (m_recvBuffer.size() >= 4) {
        quint32 jsonLen = 0;
        memcpy(&jsonLen, m_recvBuffer.constData(), 4);

        // 心跳响应（长度=0），跳过
        if (jsonLen == 0) {
            m_recvBuffer.remove(0, 4);
            continue;
        }

        // 等待完整的 JSON 数据
        if (m_recvBuffer.size() < 4 + static_cast<int>(jsonLen)) {
            return;  // 数据不完整，等待更多
        }

        // 提取 JSON
        QByteArray jsonData = m_recvBuffer.mid(4, jsonLen);
        m_recvBuffer.remove(0, 4 + jsonLen);

        // 心跳响应 / 非检测结果，跳过，不改变 m_waitingResult
        QJsonObject testObj = QJsonDocument::fromJson(jsonData).object();
        if (testObj.contains("status") && !testObj.contains("detections")) {
            continue;
        }

        m_waitingResult = false;
        parseDetectionJson(jsonData);
    }
}

void YoloDetector::parseDetectionJson(const QByteArray &json) {
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "[YOLO] JSON 解析失败";
        return;
    }

    QJsonObject obj = doc.object();
    QJsonArray dets = obj["detections"].toArray();
    double inferMs = obj["infer_ms"].toDouble();

    QVector<DetectionResult> results;
    float scaleX = (m_lastFrame.width() > 0)
        ? static_cast<float>(m_lastFrame.width()) / kTargetWidth : 1.0f;
    float scaleY = (m_lastFrame.height() > 0)
        ? static_cast<float>(m_lastFrame.height()) / kTargetHeight : 1.0f;

    for (const QJsonValue &val : dets) {
        QJsonObject det = val.toObject();
        DetectionResult r;
        r.className = det["class"].toString();
        r.score = det["score"].toDouble();

        QJsonArray bbox = det["bbox"].toArray();
        if (bbox.size() >= 4) {
            int x1 = static_cast<int>(bbox[0].toDouble() * scaleX);
            int y1 = static_cast<int>(bbox[1].toDouble() * scaleY);
            int x2 = static_cast<int>(bbox[2].toDouble() * scaleX);
            int y2 = static_cast<int>(bbox[3].toDouble() * scaleY);
            r.bbox = QRect(x1, y1, x2 - x1, y2 - y1);
        }

        results.append(r);
    }

    m_frameCount++;

    qDebug() << "[YOLO] 检测到" << results.size() << "个目标，推理耗时" << inferMs << "ms";
    emit detectionReady(m_lastFrame, results);
}
