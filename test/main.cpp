#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QtGlobal>
#include <unistd.h>
#include "teagardensystem.h"

namespace {
QFile *gRuntimeLogFile = nullptr;

void runtimeMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    if (!gRuntimeLogFile || !gRuntimeLogFile->isOpen()) {
        return;
    }

    const char *level = "INFO";
    if (type == QtWarningMsg) {
        level = "WARN";
    } else if (type == QtCriticalMsg) {
        level = "ERROR";
    } else if (type == QtFatalMsg) {
        level = "FATAL";
    }

    QTextStream ts(gRuntimeLogFile);
    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
       << " [" << level << "] " << msg << "\n";
    ts.flush();
}
}

int main(int argc, char *argv[]) {
    QFile runtimeLog("./test_runtime.log");
    if (runtimeLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        gRuntimeLogFile = &runtimeLog;
        qInstallMessageHandler(runtimeMessageHandler);
    }

    // RK3588 MIPI 屏：eglfs 直接驱动 DRM，自动全屏 1024x600
    qputenv("QT_QPA_PLATFORM", "eglfs");

    // 强制启用 ibus 中文输入法支持
    if (qEnvironmentVariableIsEmpty("QT_IM_MODULE")) {
        qputenv("QT_IM_MODULE", "ibus");
    }
    if (qEnvironmentVariableIsEmpty("XMODIFIERS")) {
        qputenv("XMODIFIERS", "@im=ibus");
    }
    if (qEnvironmentVariableIsEmpty("GTK_IM_MODULE")) {
        qputenv("GTK_IM_MODULE", "ibus");
    }

    // QtWebEngine running as root requires disabling Chromium sandbox.
    if (geteuid() == 0 && qEnvironmentVariableIsEmpty("QTWEBENGINE_DISABLE_SANDBOX")) {
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    }

    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qWarning() << "No GUI display session detected. Trying DISPLAY=:0 fallback.";
    }

    QApplication a(argc, argv);

    TeaGardenSystem w;
    w.show();  // eglfs 自动全屏填满 MIPI 1024x600

    return a.exec();
}
