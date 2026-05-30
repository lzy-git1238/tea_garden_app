QT       += core gui serialport network

qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += TEA_ENABLE_MAP_WEBVIEW
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

ROS2_PREFIX = $$(TEA_ROS_PREFIX)
isEmpty(ROS2_PREFIX):!isEmpty($$(ROS_DISTRO)) {
    ROS2_PREFIX = /opt/ros/$$(ROS_DISTRO)
}
isEmpty(ROS2_PREFIX) {
    ROS2_PREFIX = /opt/ros/humble
}

exists($$ROS2_PREFIX/include/rclcpp/rclcpp/rclcpp.hpp):exists($$ROS2_PREFIX/include/sensor_msgs/sensor_msgs/msg/image.hpp):exists($$ROS2_PREFIX/lib/librclcpp.so):exists($$ROS2_PREFIX/lib/libsensor_msgs__rosidl_typesupport_cpp.so) {
    ROS2_INCLUDE_DIRS = $$files($$ROS2_PREFIX/include/*)
    DEFINES += TEA_ENABLE_ROS2_IMAGE
    INCLUDEPATH += $$ROS2_PREFIX/include $$ROS2_INCLUDE_DIRS
    QMAKE_LFLAGS += -Wl,--copy-dt-needed-entries
    QMAKE_RPATHDIR += $$ROS2_PREFIX/lib
    LIBS += -L$$ROS2_PREFIX/lib \
        -lrclcpp \
        -lrosidl_typesupport_cpp \
        -lsensor_msgs__rosidl_typesupport_cpp
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    loramanager.cpp \
    main.cpp \
    ros2imagesubscriber.cpp \
    teagardensystem.cpp \
    widget.cpp

HEADERS += \
    loramanager.h \
    ros2imagesubscriber.h \
    teagardensystem.h \
    widget.h

FORMS += \
    widget.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
