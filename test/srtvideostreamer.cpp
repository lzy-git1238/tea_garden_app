#include "srtvideostreamer.h"
#include <QDebug>
#include <QThread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
}

SrtVideoStreamer::SrtVideoStreamer(QObject *parent)
    : QThread(parent)
{
}

SrtVideoStreamer::~SrtVideoStreamer() {
    stop();
}

void SrtVideoStreamer::setUrl(const QString &url) {
    m_url = url;
}

void SrtVideoStreamer::setHardwareDecoding(bool enable) {
    m_hwDecode = enable;
}

void SrtVideoStreamer::stop() {
    m_running = false;
    // av_read_frame 会阻塞在 I/O 上，需要关闭 context 来中断
    if (m_ctx.fmtCtx) {
        AVFormatContext *fc = m_ctx.fmtCtx;
        // 关闭输入以中断阻塞的 av_read_frame
        if (fc->pb) fc->pb->error = 1;
    }
    if (isRunning()) {
        wait(3000);  // 最多等 3 秒
        if (isRunning()) {
            qWarning() << "[SRT] thread didn't stop, terminating";
            terminate();
            wait(1000);
        }
    }
}

void SrtVideoStreamer::run() {
    m_running = true;
    emit statusMessage("SRT 视频流初始化中...");

    while (m_running) {
        if (!openStream()) {
            if (!m_running) break;
            emit streamDisconnected();
            emit statusMessage("SRT 连接失败，5 秒后重试...");
            QThread::sleep(5);
            continue;
        }

        emit streamConnected();
        emit statusMessage(QString("SRT 视频流已连接 [%1]")
            .arg(m_ctx.codec->name));

        // 主解码循环
        int ret = 0;
        while (m_running) {
            ret = av_read_frame(m_ctx.fmtCtx, m_ctx.packet);
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    emit statusMessage("SRT 流结束，正在重连...");
                } else if (ret != AVERROR(EAGAIN)) {
                    char errBuf[256];
                    av_strerror(ret, errBuf, sizeof(errBuf));
                    emit statusMessage(QString("SRT 读取错误: %1").arg(errBuf));
                }
                break;  // 退出内层循环，重新 openStream
            }

            // 跳过非视频流
            if (m_ctx.packet->stream_index != m_ctx.videoStreamIdx) {
                av_packet_unref(m_ctx.packet);
                continue;
            }

            // 发送数据包到解码器
            ret = avcodec_send_packet(m_ctx.codecCtx, m_ctx.packet);
            av_packet_unref(m_ctx.packet);

            if (ret < 0) {
                // 解码器错误，丢弃当前帧继续
                continue;
            }

            // 接收解码后的帧
            while (m_running) {
                ret = avcodec_receive_frame(m_ctx.codecCtx, m_ctx.frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    break;
                }

                // ── 确定源像素格式 ──
                AVFrame *srcFrame = m_ctx.frame;
                int srcFmt = srcFrame->format;

                // rkmpp 硬解可能吐出 DRM_PRIME
                AVFrame *tmpFrame = nullptr;
                if (srcFmt == AV_PIX_FMT_DRM_PRIME) {
                    tmpFrame = av_frame_alloc();
                    if (tmpFrame && av_hwframe_transfer_data(tmpFrame, srcFrame, 0) == 0) {
                        srcFrame = tmpFrame;
                        srcFmt = srcFrame->format;
                    } else {
                        av_frame_free(&tmpFrame);
                        continue;
                    }
                }

                // 跳过非标准 YUV 格式
                if (srcFmt == AV_PIX_FMT_NONE || srcFmt == AV_PIX_FMT_DRM_PRIME) {
                    av_frame_free(&tmpFrame);
                    continue;
                }

                int w = srcFrame->width;
                int h = srcFrame->height;

                // ── 确保 sws_scale 上下文 ──
                if (!ensureSwscale(w, h, srcFmt)) {
                    av_frame_free(&tmpFrame);
                    continue;
                }

                // ── YUV → RGB24 ──
                sws_scale(m_ctx.swsCtx,
                          srcFrame->data, srcFrame->linesize,
                          0, h,
                          m_ctx.rgbFrame->data, m_ctx.rgbFrame->linesize);

                // ── 发射信号 ──
                uint8_t *rgb = m_ctx.rgbFrame->data[0];
                int ls = m_ctx.rgbFrame->linesize[0];

                // 1. UI 图像 — 深拷贝
                QImage img(rgb, w, h, ls, QImage::Format_RGB888);
                emit frameReady(img.copy());

                // 2. YOLO 预留 — 零拷贝指针
                emit frameReadyForYolo(rgb, w, h, ls);

                av_frame_free(&tmpFrame);

                // FPS 统计
                m_frameCount++;
                if (!m_fpsTimer.isValid()) {
                    m_fpsTimer.start();
                } else if (m_fpsTimer.elapsed() >= 2000) {
                    double fps = m_frameCount * 1000.0 / m_fpsTimer.elapsed();
                    emit fpsUpdated(fps);
                    m_frameCount = 0;
                    m_fpsTimer.restart();
                }
            }
        }

        // 内层循环退出 → 关闭当前流 → 重连
        closeStream();
        if (m_running) {
            emit streamDisconnected();
            emit statusMessage("SRT 流中断，5 秒后重连...");
            QThread::sleep(5);
        }
    }

    closeStream();
}

bool SrtVideoStreamer::ensureSwscale(int srcW, int srcH, int srcFmt) {
    if (m_ctx.swsCtx && m_ctx.rgbFrame &&
        m_ctx.rgbFrame->width == srcW && m_ctx.rgbFrame->height == srcH) {
        return true;  // 尺寸未变，复用
    }

    // 释放旧上下文
    if (m_ctx.swsCtx) {
        sws_freeContext(m_ctx.swsCtx);
        m_ctx.swsCtx = nullptr;
    }
    av_frame_free(&m_ctx.rgbFrame);

    m_ctx.swsCtx = sws_getContext(
        srcW, srcH, (AVPixelFormat)srcFmt,
        srcW, srcH, AV_PIX_FMT_RGB24,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_ctx.swsCtx) {
        qWarning() << "[SRT] sws_getContext failed for format" << srcFmt;
        return false;
    }

    m_ctx.rgbFrame = av_frame_alloc();
    m_ctx.rgbFrame->width  = srcW;
    m_ctx.rgbFrame->height = srcH;
    m_ctx.rgbFrame->format = AV_PIX_FMT_RGB24;
    av_frame_get_buffer(m_ctx.rgbFrame, 0);
    return true;
}

bool SrtVideoStreamer::openStream() {
    closeStream();

    // ── 1. 分配格式上下文 ──
    m_ctx.fmtCtx = avformat_alloc_context();
    if (!m_ctx.fmtCtx) {
        emit statusMessage("avformat_alloc_context 失败");
        return false;
    }

    // SRT 选项（不与 fflags 冲突，SRT 协议参数独立设置）
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "analyzeduration", "5000000", 0);   // 5 秒，允许 SRT 握手
    av_dict_set(&opts, "probesize", "5000000", 0);          // 5MB，足够 SRT 连接建立
    // 注意：不设 max_delay=0，那会让 SRT 握手失败

    // ── 2. 打开 SRT 流 ──
    int ret = avformat_open_input(&m_ctx.fmtCtx,
        m_url.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        emit statusMessage(QString("打开 SRT 流失败: %1").arg(errBuf));
        closeStream();
        return false;
    }

    // ── 3. 查找流信息 ──
    ret = avformat_find_stream_info(m_ctx.fmtCtx, nullptr);
    if (ret < 0) {
        emit statusMessage("无法获取流信息");
        closeStream();
        return false;
    }

    // ── 4. 找到第一个视频流 ──
    m_ctx.videoStreamIdx = -1;
    for (unsigned i = 0; i < m_ctx.fmtCtx->nb_streams; ++i) {
        if (m_ctx.fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_ctx.videoStreamIdx = i;
            break;
        }
    }
    if (m_ctx.videoStreamIdx < 0) {
        emit statusMessage("SRT 流中未找到视频轨道");
        closeStream();
        return false;
    }

    AVCodecParameters *codecPar = m_ctx.fmtCtx->streams[m_ctx.videoStreamIdx]->codecpar;

    // ── 5. 选择解码器 ──
    if (m_hwDecode) {
        m_ctx.codec = avcodec_find_decoder_by_name("h264_rkmpp");
        if (m_ctx.codec) {
            emit statusMessage("解码器: h264_rkmpp (硬件加速)");
        }
    }
    if (!m_ctx.codec) {
        m_ctx.codec = avcodec_find_decoder(codecPar->codec_id);
        if (!m_ctx.codec) {
            emit statusMessage("未找到 H.264 解码器");
            closeStream();
            return false;
        }
        emit statusMessage(QString("解码器: %1 (CPU 软解)").arg(m_ctx.codec->name));
    }

    // ── 6. 分配解码器上下文 ──
    m_ctx.codecCtx = avcodec_alloc_context3(m_ctx.codec);
    if (!m_ctx.codecCtx) {
        emit statusMessage("avcodec_alloc_context3 失败");
        closeStream();
        return false;
    }

    avcodec_parameters_to_context(m_ctx.codecCtx, codecPar);

    // 低延时 + 丢帧
    m_ctx.codecCtx->thread_count = 0;
    m_ctx.codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_ctx.codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;
    m_ctx.codecCtx->skip_frame = AVDISCARD_DEFAULT;

    // ── 7. 打开解码器 ──
    ret = avcodec_open2(m_ctx.codecCtx, m_ctx.codec, nullptr);
    if (ret < 0) {
        emit statusMessage("avcodec_open2 失败");
        closeStream();
        return false;
    }

    // ── 8. 分配 frame/packet ──
    m_ctx.frame  = av_frame_alloc();
    m_ctx.packet = av_packet_alloc();
    if (!m_ctx.frame || !m_ctx.packet) {
        emit statusMessage("av_frame_alloc / av_packet_alloc 失败");
        closeStream();
        return false;
    }

    return true;
}

void SrtVideoStreamer::closeStream() {
    av_frame_free(&m_ctx.rgbFrame);
    av_frame_free(&m_ctx.frame);
    av_packet_free(&m_ctx.packet);

    if (m_ctx.swsCtx) {
        sws_freeContext(m_ctx.swsCtx);
        m_ctx.swsCtx = nullptr;
    }

    if (m_ctx.codecCtx) {
        avcodec_free_context(&m_ctx.codecCtx);
    }
    m_ctx.codec = nullptr;

    if (m_ctx.fmtCtx) {
        avformat_close_input(&m_ctx.fmtCtx);
    }

    m_ctx.videoStreamIdx = -1;
    m_ctx.hwDeviceCtx = nullptr;
}
