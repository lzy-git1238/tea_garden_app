#ifndef ROS2IMAGESUBSCRIBER_H
#define ROS2IMAGESUBSCRIBER_H

#include <QObject>
#include <QImage>
#include <memory>

class Ros2ImageSubscriber : public QObject {
    Q_OBJECT

public:
    explicit Ros2ImageSubscriber(QObject *parent = nullptr);
    ~Ros2ImageSubscriber();

    bool start(const QString &topicName);
    void stop();
    bool isRunning() const;

signals:
    void frameReady(const QImage &frame);
    void statusMessage(const QString &status);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif // ROS2IMAGESUBSCRIBER_H
