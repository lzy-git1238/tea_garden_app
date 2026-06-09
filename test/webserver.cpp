#include "webserver.h"
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonDocument>
#include <QFile>
#include <QDir>

WebServer::WebServer(QObject *parent) : QObject(parent) {}

WebServer::~WebServer() { stop(); }

bool WebServer::start(int port) {
    // ── HTTP 服务（静态文件 + 首页） ──
    m_httpServer = new QTcpServer(this);
    connect(m_httpServer, &QTcpServer::newConnection, this, [this]() {
        while (m_httpServer->hasPendingConnections()) {
            QTcpSocket *sock = m_httpServer->nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [sock, this]() {
                QByteArray req = sock->readAll();
                if (!req.startsWith("GET")) {
                    sock->deleteLater();
                    return;
                }
                // 解析路径
                int p1 = req.indexOf(' ') + 1;
                int p2 = req.indexOf(' ', p1);
                QString path = QString::fromUtf8(req.mid(p1, p2 - p1));

                // 默认首页
                if (path == "/" || path.isEmpty()) {
                    QFile f(":/web/index.html");
                    if (f.open(QIODevice::ReadOnly)) {
                        QByteArray html = f.readAll();
                        f.close();
                        sock->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                                    "Content-Length: " + QByteArray::number(html.size()) + "\r\n"
                                    "Connection: close\r\n\r\n");
                        sock->write(html);
                    } else {
                        sock->write("HTTP/1.1 404 Not Found\r\n\r\n404");
                    }
                } else {
                    sock->write("HTTP/1.1 404 Not Found\r\n\r\n404");
                }
                sock->flush();
                sock->disconnectFromHost();
                connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
            });
        }
    });
    if (!m_httpServer->listen(QHostAddress::Any, port)) {
        qWarning() << "[WEB] HTTP server listen failed:" << m_httpServer->errorString();
        return false;
    }

    // ── WebSocket 服务 ──
    m_wsServer = new QWebSocketServer("TeaGarden", QWebSocketServer::NonSecureMode, this);
    if (!m_wsServer->listen(QHostAddress::Any, port)) {
        qWarning() << "[WEB] WS server listen failed:" << m_wsServer->errorString();
        return false;
    }
    connect(m_wsServer, &QWebSocketServer::newConnection, this, &WebServer::onWsConnected);

    qDebug() << "[WEB] 服务已启动 http://0.0.0.0:" << port;
    return true;
}

void WebServer::stop() {
    for (auto *c : m_clients) { c->close(); c->deleteLater(); }
    m_clients.clear();
    delete m_wsServer; m_wsServer = nullptr;
    delete m_httpServer; m_httpServer = nullptr;
    qDebug() << "[WEB] 服务已停止";
}

void WebServer::onWsConnected() {
    auto *client = m_wsServer->nextPendingConnection();
    m_clients.append(client);
    qDebug() << "[WEB] 客户端已连接:" << client->peerAddress().toString();
    connect(client, &QWebSocket::disconnected, this, &WebServer::onWsDisconnected);
    connect(client, &QWebSocket::textMessageReceived, this, &WebServer::onWsTextMessage);
}

void WebServer::onWsDisconnected() {
    auto *client = qobject_cast<QWebSocket*>(sender());
    if (client) {
        m_clients.removeAll(client);
        client->deleteLater();
        qDebug() << "[WEB] 客户端已断开";
    }
}

void WebServer::onWsTextMessage(const QString &msg) {
    QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;
    QJsonObject obj = doc.object();
    QString cmd = obj["cmd"].toString();

    if (cmd == "rover" && obj.contains("key")) {
        char key = obj["key"].toString().at(0).toLatin1();
        emit roverCommand(key);
    }
}

// ── 数据推送 ──

void WebServer::sendJson(const QJsonObject &obj) {
    QJsonDocument doc(obj);
    sendBinary(doc.toJson(QJsonDocument::Compact));
}

void WebServer::sendBinary(const QByteArray &data) {
    sendToAll(data, false);
}

void WebServer::sendToAll(const QByteArray &data, bool isBinary) {
    for (auto *c : m_clients) {
        if (c->state() == QAbstractSocket::ConnectedState) {
            if (isBinary)
                c->sendBinaryMessage(data);
            else
                c->sendTextMessage(QString::fromUtf8(data));
        }
    }
}

void WebServer::pushSensorData(const QJsonObject &data) {
    QJsonObject msg;
    msg["type"] = "sensor";
    msg["data"] = data;
    sendJson(msg);
}

void WebServer::pushDroneFrame(const QByteArray &jpeg) {
    QByteArray frame;
    frame.append("FRAME:");
    frame.append(QByteArray::number(jpeg.size()));
    frame.append(":");
    frame.append(jpeg);
    sendToAll(frame, true);
}

void WebServer::pushRoverFrame(const QByteArray &jpeg) {
    QByteArray frame;
    frame.append("ROVER:");
    frame.append(QByteArray::number(jpeg.size()));
    frame.append(":");
    frame.append(jpeg);
    sendToAll(frame, true);
}

void WebServer::pushYoloDetections(const QJsonArray &detections) {
    QJsonObject msg;
    msg["type"] = "yolo";
    msg["detections"] = detections;
    sendJson(msg);
}

void WebServer::pushWeather(const QJsonObject &weather) {
    QJsonObject msg;
    msg["type"] = "weather";
    msg["data"] = weather;
    sendJson(msg);
}
