#include "ros2imagesubscriber.h"

#include <QMetaObject>
#include <QString>

#include <functional>
#include <thread>

#ifdef TEA_ENABLE_ROS2_IMAGE
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/image.hpp>
#endif

struct Ros2ImageSubscriber::Impl {
#ifdef TEA_ENABLE_ROS2_IMAGE
    bool initializedByThis = false;
    bool running = false;
    std::shared_ptr<rclcpp::Node> node;
    std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription;
    std::thread spinThread;
#endif
};

#ifdef TEA_ENABLE_ROS2_IMAGE
namespace {
QImage imageMsgToQImage(const sensor_msgs::msg::Image &msg) {
    if (msg.width == 0 || msg.height == 0 || msg.data.empty()) {
        return QImage();
    }

    const int width = static_cast<int>(msg.width);
    const int height = static_cast<int>(msg.height);
    const int bytesPerLine = static_cast<int>(msg.step);

    if (msg.encoding == sensor_msgs::image_encodings::RGB8) {
        QImage image(msg.data.data(), width, height, bytesPerLine, QImage::Format_RGB888);
        return image.copy();
    }
    if (msg.encoding == sensor_msgs::image_encodings::BGR8) {
        QImage image(msg.data.data(), width, height, bytesPerLine, QImage::Format_RGB888);
        return image.rgbSwapped().copy();
    }
    if (msg.encoding == sensor_msgs::image_encodings::MONO8) {
        QImage image(msg.data.data(), width, height, bytesPerLine, QImage::Format_Grayscale8);
        return image.copy();
    }
    if (msg.encoding == sensor_msgs::image_encodings::RGBA8) {
        QImage image(msg.data.data(), width, height, bytesPerLine, QImage::Format_RGBA8888);
        return image.copy();
    }
    if (msg.encoding == sensor_msgs::image_encodings::BGRA8) {
        QImage image(msg.data.data(), width, height, bytesPerLine, QImage::Format_RGBA8888);
        return image.rgbSwapped().copy();
    }

    return QImage();
}
}
#endif

Ros2ImageSubscriber::Ros2ImageSubscriber(QObject *parent)
    : QObject(parent),
      impl(new Impl()) {
}

Ros2ImageSubscriber::~Ros2ImageSubscriber() {
    stop();
}

bool Ros2ImageSubscriber::start(const QString &topicName) {
#ifdef TEA_ENABLE_ROS2_IMAGE
    if (impl->running) {
        return true;
    }

    const QString topic = topicName.trimmed().isEmpty() ? QString("/camera/color/image_raw") : topicName.trimmed();

    try {
        if (!rclcpp::ok()) {
            int argc = 0;
            char **argv = nullptr;
            rclcpp::init(argc, argv);
            impl->initializedByThis = true;
        }

        impl->node = std::make_shared<rclcpp::Node>("tea_garden_qt_image_viewer");
        impl->executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();

        impl->subscription = impl->node->create_subscription<sensor_msgs::msg::Image>(
            topic.toStdString(),
            rclcpp::SensorDataQoS(),
            [this](sensor_msgs::msg::Image::ConstSharedPtr msg) {
                const QImage frame = imageMsgToQImage(*msg);
                if (frame.isNull()) {
                    return;
                }
                QMetaObject::invokeMethod(this, [this, frame]() {
                    emit frameReady(frame);
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
                    emit statusMessage(QString("ROS2 图像线程异常: %1").arg(err));
                }, Qt::QueuedConnection);
            }
        });

        emit statusMessage(QString("ROS2 图像订阅已启动: %1").arg(topic));
        return true;
    } catch (const std::exception &e) {
        emit statusMessage(QString("ROS2 启动失败: %1").arg(QString::fromUtf8(e.what())));
        stop();
        return false;
    }
#else
    Q_UNUSED(topicName);
    emit statusMessage("ROS2 图像支持未编译启用（缺少 rclcpp/sensor_msgs）。");
    return false;
#endif
}

void Ros2ImageSubscriber::stop() {
#ifdef TEA_ENABLE_ROS2_IMAGE
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

    emit statusMessage("ROS2 图像订阅已停止");
#endif
}

bool Ros2ImageSubscriber::isRunning() const {
#ifdef TEA_ENABLE_ROS2_IMAGE
    return impl->running;
#else
    return false;
#endif
}
