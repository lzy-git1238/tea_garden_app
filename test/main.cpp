#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QScreen>
#include <QFont>
#include <QtGlobal>
#include <unistd.h>
#include "teagardensystem.h"

namespace {
QFile *gRuntimeLogFile = nullptr;

void runtimeMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    if (!gRuntimeLogFile || !gRuntimeLogFile->isOpen()) return;
    const char *level = "INFO";
    if (type == QtWarningMsg)  level = "WARN";
    if (type == QtCriticalMsg) level = "ERROR";
    if (type == QtFatalMsg)    level = "FATAL";
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

    // ── 显示后端检测 ──
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        if (QFile::exists("/run/user/1000/wayland-0")) {
            // Wayland socket 存在 → wayland
            qputenv("WAYLAND_DISPLAY", "wayland-0");
        } else if (QDir("/tmp/.X11-unix").entryList(QDir::Files).isEmpty()) {
            // 无 X11 socket → 嵌入式环境，用 eglfs 直接渲染到 DRM
            qputenv("QT_QPA_PLATFORM", "eglfs");
        }
        // 有 X11 socket → 让 Qt 自动选择 xcb
    }

    // 高 DPI 缩放
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1");
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    // QtWebEngine sandbox
    if (geteuid() == 0 && qEnvironmentVariableIsEmpty("QTWEBENGINE_DISABLE_SANDBOX"))
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");

    QApplication a(argc, argv);

    // ── 按屏幕宽度比例缩放 ──
    // 参考宽度 1920px，其他屏幕按比例缩放
    // 1920px → 1.0x, 1024px → 0.53x, 1280px → 0.67x
    QScreen *screen = a.primaryScreen();
    double scale = 1.0;
    if (screen) {
        qreal screenWidth = screen->geometry().width();
        scale = screenWidth / 1920.0;
        scale = qBound(0.4, scale, 2.0);
        qDebug() << "[MAIN] screen:" << screen->geometry()
                 << "width:" << screenWidth << "scale:" << scale;
    }

    // 全局字体缩放
    QFont appFont = a.font();
    appFont.setPointSizeF(appFont.pointSizeF() * scale);
    a.setFont(appFont);

    TeaGardenSystem w;
    w.setScreenDpiScale(scale);
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.showFullScreen();

    return a.exec();
}
