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
             << "-flags"  << "low_delay"
             << "-i" << m_url
             << "-vcodec" << (m_hwDecode ? "h264_rkmpp" : "h264")
             << "-flags"  << "low_delay"    // 编码器级别 low_delay
             << "-an"
             << "-f" << "rawvideo"         // raw RGB24 输出，每帧固定大小
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

        QByteArray stdoutBuf;
        bool resDetected = false;

        while (m_running && proc.state() == QProcess::Running) {
            // 读 stderr 提取分辨率
            QByteArray err = proc.readAllStandardError();
            if (!resDetected && !err.isEmpty()) {
                QString es = QString::fromUtf8(err);
                QRegularExpression re("(\\d{2,4})x(\\d{2,4})");
                auto m = re.match(es);
                if (m.hasMatch()) {
                    m_resWidth  = m.captured(1).toInt();
                    m_resHeight = m.captured(2).toInt();
                    resDetected = true;
                    qDebug() << "[SRT] detected resolution:" << m_resWidth << "x" << m_resHeight;
                    emit statusMessage(QString("视频 %1x%2 [h264_rkmpp]")
                        .arg(m_resWidth).arg(m_resHeight));
                }
            }

            // 读 stdout — JPEG 帧
            proc.waitForReadyRead(200);
            QByteArray data = proc.readAllStandardOutput();
            if (!data.isEmpty()) {
                stdoutBuf.append(data);

                // 扫描 JPEG 边界: SOI=FFD8, EOI=FFD9
                while (stdoutBuf.size() > 4) {
                    int soi = stdoutBuf.indexOf("\xFF\xD8");
                    if (soi < 0) { stdoutBuf.clear(); break; }
                    if (soi > 0) stdoutBuf.remove(0, soi);

                    int eoi = stdoutBuf.indexOf("\xFF\xD9", 2);
                    if (eoi < 0) break;  // 帧不完整

                    QByteArray jpeg = stdoutBuf.left(eoi + 2);
                    stdoutBuf.remove(0, eoi + 2);

                    QImage img = QImage::fromData(jpeg, "JPEG");
                    if (!img.isNull()) {
                        // YOLO 预留 — 先转成 RGB24 再发射指针
                        QImage rgb = img.convertToFormat(QImage::Format_RGB888);
                        emit frameReadyForYolo(rgb.constBits(),
                            rgb.width(), rgb.height(), rgb.bytesPerLine());
                        emit frameReady(rgb);  // QImage 隐式深拷贝

                        m_frameCount++;
                        if (!m_fpsTimer.isValid()) m_fpsTimer.start();
                        else if (m_fpsTimer.elapsed() >= 2000) {
                            double fps = m_frameCount * 1000.0 / m_fpsTimer.elapsed();
                            emit fpsUpdated(fps);
                            m_frameCount = 0;
                            m_fpsTimer.restart();
                        }
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
