#include "srtvideostreamer.h"
#include <QDebug>
#include <QThread>
#include <unistd.h>

SrtVideoStreamer::SrtVideoStreamer(QObject *parent) : QThread(parent) {}
SrtVideoStreamer::~SrtVideoStreamer() { stop(); }
void SrtVideoStreamer::setUrl(const QString &url) { m_url = url; }
void SrtVideoStreamer::setHardwareDecoding(bool enable) { m_hwDecode = enable; }

void SrtVideoStreamer::stop() {
    m_running = false;
    if (isRunning()) { wait(3000); if (isRunning()) { terminate(); wait(1000); } }
}

void SrtVideoStreamer::run() {
    m_running = true;
    emit statusMessage("SRT 视频流初始化中...");
    m_fpsTimer.start();
    m_frameCount = 0;

    while (m_running) {
        QProcess proc;

        QString srtUrl = m_url;
        // SRT 抗丢包参数：大缓冲 + NAK 重传 + 时间戳交付
        if (!srtUrl.contains("latency="))
            srtUrl += "&transtype=live&latency=20000000"       // 20秒缓冲
                      "&rcvbuf=12582912"                        // 12MB 接收缓存
                      "&nakreport=1"                            // NAK 负确认重传（抗丢包关键）
                      "&tlpktdrop=0"                            // 禁止 TLPKTDROP（不因延迟丢包）
                      "&messageapi=1";                          // 消息模式保持 SPS/PPS 完整性

        QStringList args;
        args << "-fflags" << "nobuffer"
             << "-f" << "h264"
             << "-i" << srtUrl
             << "-an"
             << "-f" << "image2pipe"
             << "-vcodec" << "mjpeg"
             << "-q:v" << "5"
             << "pipe:1";

        qDebug() << "[SRT] spawning: ffmpeg" << args;
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        proc.start("ffmpeg", args, QIODevice::ReadOnly);

        if (!proc.waitForStarted(5000)) {
            emit statusMessage("ffmpeg 进程启动失败，5秒后重试...");
            emit streamDisconnected();
            if (m_running) QThread::sleep(5);
            continue;
        }

        // 转发 stderr
        QObject::connect(&proc, &QProcess::readyReadStandardError, [&]() {
            QByteArray e = proc.readAllStandardError();
            ::write(STDERR_FILENO, e.constData(), e.size());
        });

        // 检测是否进入解码流
        bool streamReady = false;
        QByteArray errAccum;
        QObject::connect(&proc, &QProcess::readyReadStandardError, [&]() {
            if (!streamReady) {
                errAccum.append(proc.readAllStandardError());
                if (errAccum.contains("Stream #0:0")) {
                    streamReady = true;
                    emit streamConnected();
                    emit statusMessage("SRT 视频流已连接 [硬解]");
                }
            }
        });

        QByteArray stdoutBuf;

        while (m_running && proc.state() == QProcess::Running) {
            proc.waitForReadyRead(500);
            QByteArray data = proc.readAllStandardOutput();
            if (!data.isEmpty())
                stdoutBuf.append(data);

            // JPEG 帧切分: SOI=FFD8, EOI=FFD9
            while (stdoutBuf.size() > 4) {
                int soi = stdoutBuf.indexOf("\xFF\xD8");
                if (soi < 0) { stdoutBuf.clear(); break; }
                if (soi > 0) stdoutBuf.remove(0, soi);

                int eoi = stdoutBuf.indexOf("\xFF\xD9", 2);
                if (eoi < 0) break;

                QByteArray jpeg = stdoutBuf.left(eoi + 2);
                stdoutBuf.remove(0, eoi + 2);

                QImage img = QImage::fromData(jpeg, "JPEG");
                if (!img.isNull()) {
                    emit frameReady(img);  // QImage 隐式深拷贝
                    m_frameCount++;
                    if (m_fpsTimer.elapsed() >= 2000) {
                        double fps = m_frameCount * 1000.0 / m_fpsTimer.elapsed();
                        emit fpsUpdated(fps);
                        m_frameCount = 0;
                        m_fpsTimer.restart();
                    }
                }
            }
        }

        proc.kill();
        proc.waitForFinished(2000);

        if (m_running) {
            emit streamDisconnected();
            emit statusMessage("SRT 流中断，5 秒后重连...");
            QThread::sleep(5);
        }
    }
}
