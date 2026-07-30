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

SOURCES += \
    chartdashboard.cpp \
    datastore.cpp \
    iotuploader.cpp \
    loramanager.cpp \
    main.cpp \
    pestdashboard.cpp \
    pestdata.cpp \
    rovercontrolpanel.cpp \
    scheduledialog.cpp \
    schedulestore.cpp \
    srtvideostreamer.cpp \
    teagardensystem.cpp \
    thresholddialog.cpp \
    widget.cpp \
    rknn/yolov8/rkYolov5s.cc \
    rknn/yolov8/postprocess.cc \
    rknn/yolov8/preprocess.cc

HEADERS += \
    chartdashboard.h \
    datastore.h \
    iotuploader.h \
    loramanager.h \
    pestdashboard.h \
    pestdata.h \
    rovercontrolpanel.h \
    scheduledialog.h \
    schedulestore.h \
    srtvideostreamer.h \
    teagardensystem.h \
    thresholddialog.h \
    widget.h \
    rknn/yolov8/rkYolov5s.hpp \
    rknn/yolov8/rknnPool.hpp \
    rknn/yolov8/ThreadPool.hpp \
    rknn/yolov8/postprocess.h \
    rknn/yolov8/preprocess.h \
    rknn/yolov8/coreNum.hpp \
    rknn/yolov8/rga_func.h \
    rknn/yolov8/drm_func.h \
    rknn/yolov8/rknn_api.h \
    rknn/yolov8/rknn_matmul_api.h

FORMS += \
    widget.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# OpenCV (板端已通过 apt 安装 libopencv-dev)
unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += opencv4

# RKNN 运行时库 (ARM 板端 only)
unix:!macx: LIBS += -lrga
contains(QMAKE_HOST.arch, aarch64): LIBS += -L$$PWD/../lib -lrknnrt
INCLUDEPATH += $$PWD $$PWD/rknn/yolov8
unix:!macx: INCLUDEPATH += /usr/include/rga
