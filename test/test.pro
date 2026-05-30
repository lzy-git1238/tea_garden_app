QT       += core gui serialport network charts sql

qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += TEA_ENABLE_MAP_WEBVIEW
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# ROS2 视频流已替换为 HTTP MJPEG（桌面调试用），ARM 版本见 git 历史

SOURCES += \
    chartdashboard.cpp \
    datastore.cpp \
    loramanager.cpp \
    main.cpp \
    teagardensystem.cpp \
    thresholddialog.cpp \
    widget.cpp

HEADERS += \
    chartdashboard.h \
    datastore.h \
    loramanager.h \
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
