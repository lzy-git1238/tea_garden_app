QT       += core gui serialport network charts sql websockets

qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += TEA_ENABLE_MAP_WEBVIEW
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# FFmpeg 6.0.1 头文件（匹配板载 libavformat.so.60）
INCLUDEPATH += /home/elf/ffmpeg-6.0.1
LIBS += -L/usr/lib/aarch64-linux-gnu -lavformat -lavcodec -lavutil -lswscale

# Paho MQTT C++ (华为云 IoT 双向通信)
LIBS += -lpaho-mqttpp3 -lpaho-mqtt3as

# ROS2 视频流已替换为 HTTP MJPEG（桌面调试用），ARM 版本见 git 历史

SOURCES += \
    chartdashboard.cpp \
    datastore.cpp \
    iotuploader.cpp \
    loramanager.cpp \
    main.cpp \
    pestdashboard.cpp \
    pestdata.cpp \
    scheduledialog.cpp \
    schedulestore.cpp \
    srtvideostreamer.cpp \
    teagardensystem.cpp \
    thresholddialog.cpp \
    widget.cpp

HEADERS += \
    chartdashboard.h \
    datastore.h \
    iotuploader.h \
    loramanager.h \
    pestdashboard.h \
    pestdata.h \
    scheduledialog.h \
    schedulestore.h \
    srtvideostreamer.h \
    teagardensystem.h \
    thresholddialog.h \
    widget.h

FORMS += \
    widget.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
