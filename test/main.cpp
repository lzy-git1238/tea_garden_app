#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
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
        // Wayland socket 存在 → wayland
        if (QFile::exists("/run/user/1000/wayland-0")) {
            qputenv("WAYLAND_DISPLAY", "wayland-0");
        } else if (qEnvironmentVariableIsEmpty("DISPLAY")) {
            qputenv("DISPLAY", ":0");
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
    }

    // 高 DPI 缩放
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1");
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    // QtWebEngine sandbox
    if (geteuid() == 0 && qEnvironmentVariableIsEmpty("QTWEBENGINE_DISABLE_SANDBOX"))
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");

    QApplication a(argc, argv);

    // ── QScreen 动态 DPI 缩放 ──
    QScreen *screen = a.primaryScreen();
    double dpiScale = 1.0;
    if (screen) {
        qreal dpi = screen->logicalDotsPerInch();
        if (dpi > 0) dpiScale = dpi / 96.0;
        // 7寸 1024x600 ≈ 170 DPI → dpiScale ≈ 1.77
        qDebug() << "[MAIN] screen:" << screen->geometry()
                 << "DPI:" << dpi << "scale:" << dpiScale;
    }

    // 全局字体缩放
    QFont appFont = a.font();
    appFont.setPointSizeF(appFont.pointSizeF() * dpiScale);
    a.setFont(appFont);

    TeaGardenSystem w;
    w.setScreenDpiScale(dpiScale);
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.showFullScreen();

    return a.exec();
}
