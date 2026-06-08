#ifndef YOLODETECTOR_H
#define YOLODETECTOR_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QByteArray>

struct DetectionResult {
    QString className;   // "Brown Blight" 等英文类名
    double score;        // 0.0~1.0
    QRect  bbox;         // 基于原始帧坐标系
};

class YoloDetector : public QObject {
    Q_OBJECT
public:
    explicit YoloDetector(QObject *parent = nullptr);
    ~YoloDetector();

    void start(const QString &host = "127.0.0.1", int port = 9800);
    void stop();
    void setFpsLimit(int fps);

signals:
    void detectionReady(const QImage &frame, const QVector<DetectionResult> &results);
    void statusMessage(const QString &msg);
    void fpsUpdated(double inferFps);

public slots:
    void onNewFrame(const QImage &frame);

private slots:
    void onConnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);

private:
    void connectToServer();
    void sendFrame(const QImage &frame);
    void parseDetectionJson(const QByteArray &json);

    QTcpSocket  *m_socket = nullptr;
    QTimer      *m_reconnectTimer = nullptr;
    QTimer      *m_heartbeatTimer = nullptr;
    QString      m_host;
    int          m_port = 9800;

    QImage       m_lastFrame;         // 用于画框的原始帧
    QImage       m_lastResizedFrame;  // 降采样后的帧（发送用）
    int          m_fpsLimit = 3;
    QElapsedTimer m_fpsTimer;
    int          m_frameCount = 0;
    double       m_currentFps = 0;

    QByteArray   m_recvBuffer;        // 接收缓冲区
    bool         m_waitingResult = false;  // 是否正在等待推理结果

    static constexpr int kTargetWidth  = 640;
    static constexpr int kTargetHeight = 640;
    static constexpr int kConnectRetryMs = 3000;
    static constexpr int kHeartbeatMs    = 5000;
    static constexpr int kInferTimeoutMs = 3000;
};

#endif // YOLODETECTOR_H
