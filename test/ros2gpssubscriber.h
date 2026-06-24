#ifndef ROS2GPSSUBSCRIBER_H
#define ROS2GPSSUBSCRIBER_H

#include <QObject>
#include <memory>

struct GpsPosition {
    double lat = 0.0;
    double lon = 0.0;
    double alt = 0.0;
};

class Ros2GpsSubscriber : public QObject {
    Q_OBJECT

public:
    explicit Ros2GpsSubscriber(QObject *parent = nullptr);
    ~Ros2GpsSubscriber();

    bool start(const QString &topicName = "/fmu/out/vehicle_global_position");
    void stop();
    bool isRunning() const;

signals:
    void gpsPosition(double lat, double lon, double alt);
    void statusMessage(const QString &status);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif // ROS2GPSSUBSCRIBER_H
