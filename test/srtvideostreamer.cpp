#include "srtvideostreamer.h"
#include <QDebug>
#include <QElapsedTimer>
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
    m_fpsTimer.start();
    m_frameCount = 0;

    while (m_running) {
        QProcess proc;

        QStringList args;
        // 参照已验证可行的 ffplay 管线:
        // ffplay -vcodec h264_rkmpp -f mpegts -fflags nobuffer -flags low_delay
        //        -framedrop -strict experimental -i "srt://...?mode=listener"
        args << "-vcodec" << "h264_rkmpp"
             << "-f" << "mpegts"
             << "-fflags" << "nobuffer"
             << "-flags"  << "low_delay"
             << "-i" << m_url              // 简洁 URL，不加额外 SRT 参数
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

        // 转发 stderr 并检测 Stream
        bool streamReady = false;
        QByteArray errAccum;
        QObject::connect(&proc, &QProcess::readyReadStandardError, [&]() {
            QByteArray e = proc.readAllStandardError();
            ::write(STDERR_FILENO, e.constData(), e.size());
            if (!streamReady) {
                errAccum.append(e);
                if (errAccum.contains("Stream #0:0")) {
                    streamReady = true;
                    emit streamConnected();
                    emit statusMessage("SRT 视频流已连接");
                }
            }
        });

        QByteArray stdoutBuf;
        int frameSize = 0;
        QElapsedTimer resoTimer;
        resoTimer.start();

        while (m_running && proc.state() == QProcess::Running) {
            // 连上流后 5 秒内未检测到分辨率 → 重启 ffmpeg
            if (streamReady && frameSize == 0 && resoTimer.elapsed() > 5000) {
                qDebug() << "[SRT] resolution timeout, restarting ffmpeg...";
                break;
            }
            proc.waitForReadyRead(500);
            QByteArray data = proc.readAllStandardOutput();
            if (!data.isEmpty()) {
                stdoutBuf.append(data);
                // 安全帽：buffer 超过 10MB 丢弃旧数据，防止内存耗尽崩溃
                if (stdoutBuf.size() > 10 * 1024 * 1024) {
                    stdoutBuf = stdoutBuf.right(5 * 1024 * 1024);
                    if (frameSize > 0) {
                        // 对齐到帧边界
                        int rem = stdoutBuf.size() % frameSize;
                        if (rem > 0) stdoutBuf.remove(0, rem);
                    }
                }
            }

            // 从 stderr 提取分辨率
            if (frameSize == 0 && streamReady) {
                if (!resoTimer.isValid()) resoTimer.start();
                QString es = QString::fromUtf8(errAccum);
                QRegularExpression re("(\\d{2,4})x(\\d{2,4})");
                auto m = re.match(es);
                if (m.hasMatch()) {
                    m_resWidth  = m.captured(1).toInt();
                    m_resHeight = m.captured(2).toInt();
                    frameSize   = m_resWidth * m_resHeight * 3;
                    qDebug() << "[SRT] resolution:" << m_resWidth << "x" << m_resHeight
                             << "frameSize:" << frameSize;
                    emit statusMessage(QString("视频 %1x%2").arg(m_resWidth).arg(m_resHeight));
                }
            }

            // 切完整帧
            while (frameSize > 0 && stdoutBuf.size() >= frameSize) {
                QByteArray frameData = stdoutBuf.left(frameSize);
                stdoutBuf.remove(0, frameSize);

                QImage img((const uchar*)frameData.constData(),
                           m_resWidth, m_resHeight,
                           QImage::Format_RGB888);
                QImage copy = img.copy();
                if (!copy.isNull()) {
                    emit frameReady(copy);
                    emit frameReadyForYolo(copy);
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
