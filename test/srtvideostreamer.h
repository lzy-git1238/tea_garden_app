#ifndef SRTVIDEOSTREAMER_H
#define SRTVIDEOSTREAMER_H

#include <QThread>
#include <QImage>
#include <QString>
#include <QElapsedTimer>
#include <QProcess>
#include <atomic>

class SrtVideoStreamer : public QThread {
    Q_OBJECT
public:
    explicit SrtVideoStreamer(QObject *parent = nullptr);
    ~SrtVideoStreamer();

    void setUrl(const QString &url);
    void setHardwareDecoding(bool enable);
    void stop();

signals:
    void frameReady(const QImage &image);
    void frameReadyForYolo(const uint8_t *rgb24,
                           int width, int height, int linesize);
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
};

#endif // SRTVIDEOSTREAMER_H
