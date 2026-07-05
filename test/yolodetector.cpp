#include "yolodetector.h"
#include <QDebug>
#include <opencv2/opencv.hpp>

YoloDetector::YoloDetector(QObject *parent)
    : QObject(parent)
{
}

YoloDetector::~YoloDetector() {
    stop();
}

bool YoloDetector::init(const std::string &modelPath,
                         const std::string &labelsPath,
                         int coreMask) {
    m_initialized = m_engine.init(modelPath, labelsPath, coreMask);
    if (!m_initialized) {
        qCritical() << "[YOLO] Engine init FAILED";
        emit statusMessage("YOLO 引擎初始化失败");
        return false;
    }

    m_engine.setConfThreshold(0.25f);
    m_engine.setNmsThreshold(0.45f);

    m_running = true;
    m_inferThread = std::thread(&YoloDetector::inferenceLoop, this);

    printf("[YOLO] Engine READY, thread started\n");
    fflush(stdout);
    emit statusMessage("YOLO 引擎就绪");
    return true;
}

void YoloDetector::stop() {
    m_running = false;
    m_cv.notify_all();
    if (m_inferThread.joinable()) {
        m_inferThread.join();
    }
}

void YoloDetector::setFpsLimit(int fps) {
    m_fpsLimit = qBound(1, fps, 30);
}

void YoloDetector::setConfThreshold(float v) {
    m_engine.setConfThreshold(v);
}

void YoloDetector::setNmsThreshold(float v) {
    m_engine.setNmsThreshold(v);
}

void YoloDetector::onNewFrame(const QImage &frame) {
    if (m_fpsTimer.isValid() &&
        m_fpsTimer.elapsed() < 1000.0 / m_fpsLimit) {
        return;
    }
    m_fpsTimer.restart();

    if (!m_initialized) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestFrame = frame;
        m_hasFrame = true;
    }
    m_cv.notify_one();
    static int frame_cnt = 0;
    if (++frame_cnt % 60 == 0) {
        printf("[YOLO] Frames received: %d\n", frame_cnt);
        fflush(stdout);
    }
}

void YoloDetector::inferenceLoop() {
    printf("[YOLO] Inference thread started\n");
    fflush(stdout);
    while (m_running) {
        QImage qtFrame;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(100),
                          [this] { return m_hasFrame; });
            if (!m_hasFrame) continue;
            qtFrame = m_latestFrame;
            m_hasFrame = false;
        }

        // QImage (RGB888) -> cv::Mat (BGR)
        QImage rgb = qtFrame.convertToFormat(QImage::Format_RGB888);
        cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3,
                    const_cast<uchar*>(rgb.bits()),
                    rgb.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);

        // 推理 (draw=false，画框由 Qt QPainter 完成)
        std::vector<slam_car_yolo::Detection> detections;
        if (!m_engine.infer(bgr, detections, false)) {
            continue;
        }

        // 转换结果
        QVector<DetectionResult> results;
        for (auto &d : detections) {
            DetectionResult r;
            r.className = QString::fromUtf8(d.name);
            r.score = d.confidence;
            r.bbox = QRect(d.left, d.top,
                           d.right - d.left,
                           d.bottom - d.top);
            results.append(r);
        }

        m_frameCount++;
        if (m_frameCount % 30 == 0) {
            printf("[YOLO] Frame %d  Detections: %d\n", m_frameCount, (int)results.size());
            fflush(stdout);
        }

        emit detectionReady(qtFrame, results);
    }
}
