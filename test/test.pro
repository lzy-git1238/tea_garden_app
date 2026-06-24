QT       += core gui serialport network charts sql websockets

qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += TEA_ENABLE_MAP_WEBVIEW
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# FFmpeg 满血版（通过 QProcess 调二进制，无需链接 API）
# 板载 ffmpeg 路径 /usr/bin/ffmpeg 已含 h264_rkmpp + libsrt

# Paho MQTT C++ (华为云 IoT 双向通信)
LIBS += -lpaho-mqttpp3 -lpaho-mqtt3as

# ROS2 GPS 订阅 (px4_msgs) — 桌面编译默认关闭，ARM 编译用 qmake TEA_ROS2_GPS_LIBS=...

SOURCES += \
    chartdashboard.cpp \
    datastore.cpp \
    iotuploader.cpp \
    loramanager.cpp \
    main.cpp \
    pestdashboard.cpp \
    pestdata.cpp \
    rovercontrolpanel.cpp \
    ros2gpssubscriber.cpp \
    scheduledialog.cpp \
    schedulestore.cpp \
    srtvideostreamer.cpp \
    teagardensystem.cpp \
    thresholddialog.cpp \
    widget.cpp \
    yolodetector.cpp

HEADERS += \
    chartdashboard.h \
    datastore.h \
    iotuploader.h \
    loramanager.h \
    pestdashboard.h \
    pestdata.h \
    rovercontrolpanel.h \
    ros2gpssubscriber.h \
    scheduledialog.h \
    schedulestore.h \
    srtvideostreamer.h \
    teagardensystem.h \
    thresholddialog.h \
    widget.h \
    yolodetector.h

FORMS += \
    widget.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
