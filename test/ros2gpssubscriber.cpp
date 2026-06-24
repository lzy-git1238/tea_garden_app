#include "ros2gpssubscriber.h"

#include <QMetaObject>
#include <QString>

#include <functional>
#include <thread>

#ifdef TEA_ENABLE_ROS2_GPS
#include <rclcpp/rclcpp.hpp>
#include <px4_msgs/msg/vehicle_global_position.hpp>
#endif

struct Ros2GpsSubscriber::Impl {
#ifdef TEA_ENABLE_ROS2_GPS
    bool initializedByThis = false;
    bool running = false;
    std::shared_ptr<rclcpp::Node> node;
    std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
    rclcpp::Subscription<px4_msgs::msg::VehicleGlobalPosition>::SharedPtr subscription;
    std::thread spinThread;
#endif
};

Ros2GpsSubscriber::Ros2GpsSubscriber(QObject *parent)
    : QObject(parent),
      impl(new Impl()) {
}

Ros2GpsSubscriber::~Ros2GpsSubscriber() {
    stop();
}

bool Ros2GpsSubscriber::start(const QString &topicName) {
#ifdef TEA_ENABLE_ROS2_GPS
    if (impl->running) {
        return true;
    }

    const QString topic = topicName.trimmed().isEmpty()
        ? QString("/fmu/out/vehicle_global_position") : topicName.trimmed();

    try {
        if (!rclcpp::ok()) {
            int argc = 0;
            char **argv = nullptr;
            rclcpp::init(argc, argv);
            impl->initializedByThis = true;
        }

        impl->node = std::make_shared<rclcpp::Node>("tea_garden_gps_subscriber");
        impl->executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();

        impl->subscription = impl->node->create_subscription<px4_msgs::msg::VehicleGlobalPosition>(
            topic.toStdString(),
            rclcpp::SensorDataQoS(),
            [this](px4_msgs::msg::VehicleGlobalPosition::ConstSharedPtr msg) {
                const double lat = msg->lat;
                const double lon = msg->lon;
                const double alt = msg->alt;
                QMetaObject::invokeMethod(this, [this, lat, lon, alt]() {
                    emit gpsPosition(lat, lon, alt);
                }, Qt::QueuedConnection);
            });

        impl->executor->add_node(impl->node);
        impl->running = true;
        impl->spinThread = std::thread([this]() {
            try {
                impl->executor->spin();
            } catch (const std::exception &e) {
                const QString err = QString::fromUtf8(e.what());
                QMetaObject::invokeMethod(this, [this, err]() {
                    emit statusMessage(QString("ROS2 GPS 线程异常: %1").arg(err));
                }, Qt::QueuedConnection);
            }
        });

        emit statusMessage(QString("ROS2 GPS 订阅已启动: %1").arg(topic));
        return true;
    } catch (const std::exception &e) {
        emit statusMessage(QString("ROS2 GPS 启动失败: %1").arg(QString::fromUtf8(e.what())));
        stop();
        return false;
    }
#else
    Q_UNUSED(topicName);
    emit statusMessage("ROS2 GPS 支持未编译启用（缺少 rclcpp/px4_msgs）。");
    return false;
#endif
}

void Ros2GpsSubscriber::stop() {
#ifdef TEA_ENABLE_ROS2_GPS
    if (!impl->running) {
        return;
    }

    if (impl->executor) {
        impl->executor->cancel();
    }

    if (impl->spinThread.joinable()) {
        impl->spinThread.join();
    }

    if (impl->executor && impl->node) {
        try {
            impl->executor->remove_node(impl->node);
        } catch (...) {
        }
    }

    impl->subscription.reset();
    impl->executor.reset();
    impl->node.reset();
    impl->running = false;

    if (impl->initializedByThis && rclcpp::ok()) {
        rclcpp::shutdown();
    }
    impl->initializedByThis = false;

    emit statusMessage("ROS2 GPS 订阅已停止");
#endif
}

bool Ros2GpsSubscriber::isRunning() const {
#ifdef TEA_ENABLE_ROS2_GPS
    return impl->running;
#else
    return false;
#endif
}
