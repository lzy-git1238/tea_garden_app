#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QList>

class QTcpServer;
class QTcpSocket;
class QWebSocketServer;
class QWebSocket;

class WebServer : public QObject {
    Q_OBJECT
public:
    explicit WebServer(QObject *parent = nullptr);
    ~WebServer();

    bool start(int port = 8080);
    void stop();

    void pushSensorData(const QJsonObject &data);
    void pushDroneFrame(const QByteArray &jpeg);
    void pushRoverFrame(const QByteArray &jpeg);
    void pushYoloDetections(const QJsonArray &detections);
    void pushWeather(const QJsonObject &weather);

signals:
    void roverCommand(char key);

private slots:
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessage(const QString &msg);

private:
    void sendJson(const QJsonObject &obj);
    void sendBinary(const QByteArray &data);
    void sendToAll(const QByteArray &data, bool isBinary = false);

    QTcpServer *m_httpServer = nullptr;
    QWebSocketServer *m_wsServer = nullptr;
    QList<QWebSocket*> m_clients;
};

#endif // WEBSERVER_H
