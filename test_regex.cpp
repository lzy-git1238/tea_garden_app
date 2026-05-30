#include <QCoreApplication>
#include <QRegularExpression>
#include <QDebug>

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);
    QString line = "Temperature: 24.86 C, Humidity: 56.22 %";
    QRegularExpression kAirRegex("\\bTemperature\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)\\s*C\\s*,\\s*Humidity\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)\\s*%", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = kAirRegex.match(line);
    qDebug() << "Match:" << match.hasMatch();
    if (match.hasMatch()) {
        qDebug() << "Temp:" << match.captured(1) << "Humi:" << match.captured(2);
    }
    return 0;
}
