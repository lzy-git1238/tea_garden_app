#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QTextStream>
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

    // Wayland autologin（板子桌面） → 指定 socket
    if (QFile::exists("/run/user/1000/wayland-0") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("WAYLAND_DISPLAY", "wayland-0");
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

    // MIPI 7寸屏 1024x600：最大话 GNOME 难靠 API，改最小尺寸 + 铺满
    w.setMinimumSize(1024, 600);
    w.resize(1024, 600);
    w.move(0, 0);
    w.show();

    return a.exec();
}
