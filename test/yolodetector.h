#ifndef YOLODETECTOR_H
#define YOLODETECTOR_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <QElapsedTimer>

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "yolo_engine.hpp"

struct DetectionResult {
    QString className;   // 英文类名
    double score;        // 0.0~1.0
    QRect  bbox;         // 原图坐标
};

class YoloDetector : public QObject {
    Q_OBJECT
public:
    explicit YoloDetector(QObject *parent = nullptr);
    ~YoloDetector();

    /// 初始化引擎 + 启动推理线程
    /// @param modelPath  tealeaf.rknn 路径
    /// @param labelsPath labels_list.txt 路径
    /// @param coreMask  NPU 核心掩码，默认单核 Core0
    bool init(const std::string &modelPath,
              const std::string &labelsPath,
              int coreMask = 1);
    void stop();
    void setFpsLimit(int fps);
    void setConfThreshold(float v);
    void setNmsThreshold(float v);

signals:
    void detectionReady(const QImage &frame,
                        const QVector<DetectionResult> &results);
    void statusMessage(const QString &msg);

public slots:
    void onNewFrame(const QImage &frame);

private:
    void inferenceLoop();

    slam_car_yolo::YoloEngine m_engine;
    bool m_initialized = false;

    // 推理工作线程
    std::thread m_inferThread;
    std::atomic<bool> m_running{false};

    // 帧缓存 (拉模式: 推理线程取最新帧，Qt 主线程写)
    std::mutex m_mutex;
    std::condition_variable m_cv;
    QImage m_latestFrame;
    bool m_hasFrame = false;

    // 帧率控制
    int m_fpsLimit = 3;
    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;
};

#endif // YOLODETECTOR_H
