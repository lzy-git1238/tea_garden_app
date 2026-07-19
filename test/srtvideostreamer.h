#ifndef SRTVIDEOSTREAMER_H
#define SRTVIDEOSTREAMER_H

#include <QThread>
#include <QImage>
#include <QString>
#include <QElapsedTimer>
#include <QProcess>
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

    // 共享帧缓存（ffmpeg 线程写，主线程读）
    mutable QMutex m_frameMutex;
    QImage m_latestFrame;
};

#endif // SRTVIDEOSTREAMER_H
