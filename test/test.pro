QT       += core gui serialport network charts sql websockets

qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += TEA_ENABLE_MAP_WEBVIEW
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# FFmpeg 满血版（手动编译，含 rkmpp + libsrt + rkrga）
FFMPEG_ROOT = /home/elf/qt_sdk_aarch64/aarch64-buildroot-linux-gnu/sysroot/usr
INCLUDEPATH += $${FFMPEG_ROOT}/include
LIBS += -L$${FFMPEG_ROOT}/lib -lavformat -lavcodec -lavutil -lswscale

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
