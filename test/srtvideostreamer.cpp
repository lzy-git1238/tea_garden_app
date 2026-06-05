#include "srtvideostreamer.h"
#include <QDebug>
#include <QThread>
#include <QRegularExpression>

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

        // ── 构建 ffmpeg 命令行 ──
        QStringList args;
        args << "-fflags" << "nobuffer"
             << "-hwaccel" << "rkmpp"       // RK3588 官方硬解加速
             << "-i" << m_url
             << "-vf" << "hwdownload,format=nv12,format=rgb24"  // GPU→CPU→RGB
             << "-an"
             << "-f" << "rawvideo"
             << "-pix_fmt" << "rgb24"
             << "pipe:1";

        qDebug() << "[SRT] spawning: ffmpeg" << args;
        proc.start("ffmpeg", args, QIODevice::ReadOnly);

        if (!proc.waitForStarted(5000)) {
            emit statusMessage("ffmpeg 进程启动失败，5秒后重试...");
            emit streamDisconnected();
            if (m_running) QThread::sleep(5);
            continue;
        }

        emit streamConnected();
        emit statusMessage("SRT 视频流已连接 [h264_rkmpp]");

        // ── 主循环：读 raw RGB24 帧 ──
        // 连接 stderr 到 QProcess，实时查看 ffmpeg 错误
        proc.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&proc, &QProcess::readyReadStandardError, [&]() {
            QByteArray e = proc.readAllStandardError();
            // 直接写 stderr 保证可见
            ::write(STDERR_FILENO, e.constData(), e.size());
        });
            if (!resDetected && errBuf.size() > 200) {
                QString es = QString::fromUtf8(errBuf.right(2000));
                QRegularExpression re("(\\d{2,4})x(\\d{2,4})");
                auto m = re.match(es);
                if (m.hasMatch()) {
                    m_resWidth  = m.captured(1).toInt();
                    m_resHeight = m.captured(2).toInt();
                    frameSize   = m_resWidth * m_resHeight * 3;
                    resDetected = true;
                    m_fpsTimer.start();
                    m_frameCount = 0;
                    qDebug() << "[SRT] resolution:" << m_resWidth << "x" << m_resHeight
                             << "frameSize:" << frameSize;
                    emit statusMessage(QString("视频 %1x%2 [h264_rkmpp]")
                        .arg(m_resWidth).arg(m_resHeight));
                }
            }

            // 2. 读 stdout — raw RGB24 帧（每帧 w*h*3 字节）
            proc.waitForReadyRead(200);
            QByteArray data = proc.readAllStandardOutput();
            if (!data.isEmpty()) {
                stdoutBuf.append(data);
            }

            // 3. 尝试取出完整的帧
            while (resDetected && stdoutBuf.size() >= frameSize) {
                QByteArray frameData = stdoutBuf.left(frameSize);
                stdoutBuf.remove(0, frameSize);

                QImage img((const uchar*)frameData.constData(),
                           m_resWidth, m_resHeight,
                           QImage::Format_RGB888);
                QImage copy = img.copy();  // 深拷贝（原 frameData 即将被覆盖）

                if (!copy.isNull()) {
                    emit frameReadyForYolo(copy.constBits(),
                        copy.width(), copy.height(), copy.bytesPerLine());
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

        proc.close();

        if (m_running) {
            emit streamDisconnected();
            emit statusMessage("SRT 流中断，5 秒后重连...");
            QThread::sleep(5);
            resDetected = false;
        }
    }
}
