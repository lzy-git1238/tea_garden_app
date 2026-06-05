#ifndef SRTVIDEOSTREAMER_H
#define SRTVIDEOSTREAMER_H

#include <QThread>
#include <QImage>
#include <QString>
#include <QElapsedTimer>
#include <atomic>

// FFmpeg C 结构体前向声明
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct SwsContext;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;

class SrtVideoStreamer : public QThread {
    Q_OBJECT
public:
    explicit SrtVideoStreamer(QObject *parent = nullptr);
    ~SrtVideoStreamer();

    // 设置 SRT 流地址，如 "srt://0.0.0.0:9000?mode=listener"
    void setUrl(const QString &url);

    // 是否开启硬件解码（默认 true，优先 h264_rkmpp）
    void setHardwareDecoding(bool enable);

    // 安全停止解码线程
    void stop();

signals:
    // 解码后的 RGB 图像帧（深拷贝，可在主线程安全使用）
    void frameReady(const QImage &image);

    // YOLO (RKNN) 预留接口：零拷贝 RGB24 指针
    // 调用方需在下一帧到达前消费完毕，否则数据被覆盖
    void frameReadyForYolo(const uint8_t *rgb24,
                           int width, int height, int linesize);

    void statusMessage(const QString &msg);
    void streamConnected();
    void streamDisconnected();
    void fpsUpdated(double fps);

protected:
    void run() override;

private:
    // 初始化 FFmpeg 上下文（打开流 + 查找解码器）
    bool openStream();
    // 销毁 FFmpeg 上下文
    void closeStream();

    struct Context {
        AVFormatContext *fmtCtx  = nullptr;
        AVCodecContext  *codecCtx = nullptr;
        const AVCodec   *codec   = nullptr;
        SwsContext      *swsCtx  = nullptr;
        AVFrame         *frame   = nullptr;
        AVFrame         *rgbFrame = nullptr;
        AVPacket        *packet  = nullptr;
        int              videoStreamIdx = -1;
        AVBufferRef     *hwDeviceCtx = nullptr;
    };

    inline bool ensureSwscale(int srcW, int srcH, int srcFmt);

    QString m_url;
    bool    m_hwDecode = true;
    std::atomic<bool> m_running{false};
    Context m_ctx;

    // 统计
    int     m_frameCount = 0;
    QElapsedTimer m_fpsTimer;
};

#endif // SRTVIDEOSTREAMER_H
