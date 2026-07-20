#include "srtvideostreamer.h"
#include <QDebug>
#include <QThread>
#include <QRegularExpression>
#include <unistd.h>
#include "im2d.h"

SrtVideoStreamer::SrtVideoStreamer(QObject *parent) : QThread(parent) {}
SrtVideoStreamer::~SrtVideoStreamer() { stop(); }
void SrtVideoStreamer::setUrl(const QString &url) { m_url = url; }
void SrtVideoStreamer::setResolution(int w, int h) {
    m_resWidth = w; m_resHeight = h; m_haveResolution = true;
}
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
        args << "-vcodec" << (m_hwDecode ? "h264_rkmpp" : "h264")
             << "-f" << "mpegts"
             << "-fflags" << "nobuffer"
             << "-flags"  << "low_delay"
             << "-i" << m_url
             << "-an"
             << "-f" << "rawvideo"
             << "-pix_fmt" << "nv12"
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
        int frameSize = m_haveResolution ? (m_resWidth * m_resHeight * 3 / 2) : 0;

        while (m_running && proc.state() == QProcess::Running) {
            proc.waitForReadyRead(500);
            QByteArray data = proc.readAllStandardOutput();
            if (!data.isEmpty()) {
                stdoutBuf.append(data);
                // 安全帽：buffer 超过 10MB 丢弃旧数据，防止内存耗尽崩溃
                static const int kMaxBuf = 10 * 1024 * 1024;
                if (stdoutBuf.size() > kMaxBuf) {
                    qWarning() << "[SRT] buffer overflow, trimming";
                    stdoutBuf = stdoutBuf.right(kMaxBuf / 2);
                    if (frameSize > 0) {
                        int rem = stdoutBuf.size() % frameSize;
                        if (rem > 0) stdoutBuf.remove(0, rem);
                    }
                }
            }

            // 从 stderr 提取分辨率
            if (frameSize == 0 && streamReady) {
                QString es = QString::fromUtf8(errAccum);
                // 扫描全部 stderr，找第一个 数字x数字 模式
                QRegularExpression re("(\\d{2,4})x(\\d{2,4})");
                auto it = re.globalMatch(es);
                int bestW = 0, bestH = 0;
                while (it.hasNext()) {
                    auto m = it.next();
                    int w = m.captured(1).toInt();
                    int h = m.captured(2).toInt();
                    // 前面的可能是像素格式；后面的才是分辨率；取最后或最大的一个
                    if (w >= 320 && h >= 240) { bestW = w; bestH = h; /* 继续找，取最后一个 */ }
                }
                if (bestW > 0 && bestH > 0) {
                    m_resWidth  = bestW;
                    m_resHeight = bestH;
                    frameSize   = bestW * bestH * 3;
                    qDebug() << "[SRT] resolution(stderr):" << bestW << "x" << bestH
                             << "frameSize:" << frameSize;
                    emit statusMessage(QString("视频 %1x%2").arg(bestW).arg(bestH));
                } else {
                    // 将 stderr 最后 500 字节打印出来方便排查
                    QString tail = es.right(500);
                    qDebug() << "[SRT] resolution NOT found in stderr, tail:" << tail;
                }
            }

            // 切完整帧
            while (frameSize > 0 && stdoutBuf.size() >= frameSize) {
                QByteArray frameData = stdoutBuf.left(frameSize);
                stdoutBuf.remove(0, frameSize);

                if (!frameData.isEmpty()) {
                    {
                        QMutexLocker lock(&m_frameRawMutex);
                        m_latestFrameRaw = frameData;   // 深拷贝 NV12 字节
                        m_frameRawSize = frameData.size();
                    }
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

QImage SrtVideoStreamer::latestFrame() const {
    QMutexLocker lock(&m_frameRawMutex);
    if (m_latestFrameRaw.isEmpty() || m_frameRawSize <= 0)
        return QImage();
    if (m_resWidth <= 0 || m_resHeight <= 0)
        return QImage();

    // RGA 硬件 NV12 → RGB888
    rga_buffer_t src = wrapbuffer_virtualaddr(
        (void*)m_latestFrameRaw.constData(),
        m_resWidth, m_resHeight, RK_FORMAT_YCbCr_420_SP);
    int rgbSize = m_resWidth * m_resHeight * 3;
    QByteArray rgbBuf(rgbSize, Qt::Uninitialized);
    rga_buffer_t dst = wrapbuffer_virtualaddr(
        (void*)rgbBuf.data(),
        m_resWidth, m_resHeight, RK_FORMAT_RGB_888);
    im_rect src_rect = {0, 0, m_resWidth, m_resHeight};
    im_rect dst_rect = {0, 0, m_resWidth, m_resHeight};
    IM_STATUS ret = imcheck(src, dst, src_rect, dst_rect);
    if (ret != IM_STATUS_NOERROR) {
        fprintf(stderr, "[SRT] RGA check error: %s\n", imStrError(ret));
        return QImage();
    }
    ret = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_RGB_888);
    if (ret != IM_STATUS_NOERROR) {
        fprintf(stderr, "[SRT] RGA cvtcolor error: %s\n", imStrError(ret));
        return QImage();
    }

    return QImage((const uchar*)rgbBuf.constData(),
                  m_resWidth, m_resHeight, QImage::Format_RGB888).copy();
}
