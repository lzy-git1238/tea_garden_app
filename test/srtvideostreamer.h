#ifndef SRTVIDEOSTREAMER_H
#define SRTVIDEOSTREAMER_H

#include <QThread>
#include <QImage>
#include <QString>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <QMutex>
#include <atomic>

class SrtVideoStreamer : public QThread {
    Q_OBJECT
public:
    explicit SrtVideoStreamer(QObject *parent = nullptr);
    ~SrtVideoStreamer();

    void setUrl(const QString &url);
    void setResolution(int w, int h);
    void setHardwareDecoding(bool enable);
    void stop();

    /// 取最新解码帧（线程安全，返回深拷贝）
    QImage latestFrame() const;

    /// 启动主线程拉取 timer（替代 run() 中逐帧 emit）
    void startPulling(int intervalMs = 33);

signals:
    void frameReady(const QImage &image);
    void statusMessage(const QString &msg);
    void streamConnected();
    void streamDisconnected();
    void fpsUpdated(double fps);

protected:
    void run() override;

private:
    QString m_url;
    bool    m_hwDecode = true;
    std::atomic<bool> m_running{false};

    int     m_frameCount = 0;
    QElapsedTimer m_fpsTimer;
    int     m_resWidth  = 640;
    int     m_resHeight = 480;
    bool    m_haveResolution = false;

    // 共享原始帧缓存（ffmpeg 线程写 NV12 字节，主线程通过 latestFrame() 用 RGA 转 RGB）
    mutable QMutex m_frameRawMutex;
    QByteArray m_latestFrameRaw;
    int m_frameRawSize = 0;

    // 主线程拉取 timer，定频 emit frameReady
    QTimer *m_pullTimer = nullptr;
};

#endif // SRTVIDEOSTREAMER_H
