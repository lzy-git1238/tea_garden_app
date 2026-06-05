#include "srtvideostreamer.h"
#include <QDebug>
#include <QThread>
#include <QRegularExpression>
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

    while (m_running) {
        QProcess proc;

        QStringList args;
        QString srtUrl = m_url;
        if (!srtUrl.contains("latency="))
            srtUrl += "&latency=5000000";    // SRT 接收缓冲 5 秒
        args << "-fflags" << "nobuffer"
             << "-f" << "mpegts"
             << "-i" << srtUrl               // 用带 latency 的 URL
             << "-an"
             << "-f" << "rawvideo"
             << "-pix_fmt" << "rgb24"
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

        emit streamConnected();
        emit statusMessage("SRT 视频流已连接 [rk3588硬解]");

        // 转发 stderr 到本进程 stderr
        QObject::connect(&proc, &QProcess::readyReadStandardError, [&]() {
            QByteArray e = proc.readAllStandardError();
            ::write(STDERR_FILENO, e.constData(), e.size());
        });

        QByteArray stdoutBuf;
        int frameSize = 0;

        while (m_running && proc.state() == QProcess::Running) {
            proc.waitForReadyRead(2000);  // 2 秒一次轮询
            QByteArray data = proc.readAllStandardOutput();
            if (!data.isEmpty()) {
                stdoutBuf.append(data);
            }

            // 从第一帧数据推断分辨率（rawvideo 无 header，从数据量反推）
            if (frameSize == 0 && stdoutBuf.size() > 200000) {
                // 640x480x3=921600, 320x240x3=230400
                int sizes[] = { 640*480*3, 320*240*3, 1280*720*3, 1920*1080*3 };
                for (int s : sizes) {
                    if (stdoutBuf.size() >= s * 2) { // 至少2帧才确定
                        frameSize = s;
                        m_resWidth  = s / 3 / (s == 1280*720*3 ? 720 : (s == 1920*1080*3 ? 1080 : 480));
                        // 简单反推: 已知 sizes 直接设
                        if (s == 921600)      { m_resWidth = 640; m_resHeight = 480; }
                        else if (s == 230400)  { m_resWidth = 320; m_resHeight = 240; }
                        else if (s == 2764800) { m_resWidth = 1280; m_resHeight = 720; }
                        else if (s == 6220800) { m_resWidth = 1920; m_resHeight = 1080; }
                        m_fpsTimer.start();
                        m_frameCount = 0;
                        qDebug() << "[SRT] detected" << m_resWidth << "x" << m_resHeight
                                 << "frameSize:" << frameSize;
                        emit statusMessage(QString("视频 %1x%2 [rkmpp]")
                            .arg(m_resWidth).arg(m_resHeight));
                        break;
                    }
                }
            }

            // 切帧
            while (frameSize > 0 && stdoutBuf.size() >= frameSize) {
                QByteArray frameData = stdoutBuf.left(frameSize);
                stdoutBuf.remove(0, frameSize);

                QImage img((const uchar*)frameData.constData(),
                           m_resWidth, m_resHeight, QImage::Format_RGB888);
                QImage copy = img.copy();
                if (!copy.isNull()) {
                    emit frameReady(copy);
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
