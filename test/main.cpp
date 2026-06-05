#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QtGlobal>
#include <unistd.h>
#include "teagardensystem.h"

extern "C" {
#include <libavformat/avformat.h>
}

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

    // 强制使用 xcb (X11) 后端，ibus 中文输入在 Qt5+X11 下最稳定
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        if (qEnvironmentVariableIsEmpty("DISPLAY")) {
            qputenv("DISPLAY", ":0");
        }
        qputenv("QT_QPA_PLATFORM", "xcb");
    }

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
    w.show();

    return a.exec();
}
