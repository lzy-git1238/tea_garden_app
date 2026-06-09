#include "loramanager.h"

#include <QSerialPort>
#include <QRegularExpression>
#include <QDebug>
#include <limits>

namespace {
const QRegularExpression kWindRegex(
    "Wind\\s+Speed\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)\\s*m\\/s",
    QRegularExpression::CaseInsensitiveOption);

const QRegularExpression kAirRegex(
    "Temperature\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)\\s*C\\s*,\\s*Humidity\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)\\s*%",
    QRegularExpression::CaseInsensitiveOption);

const QRegularExpression kLightRegex(
    "(?:Initial|Total|Tal)?\\s*Light\\s+Intensity\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)\\s*lx",
    QRegularExpression::CaseInsensitiveOption);

const QRegularExpression kSoilPrefixRegex(
    "(?:Soil|Oil)\\s+Sensor\\s+Data\\s*:",
    QRegularExpression::CaseInsensitiveOption);

const QRegularExpression kSoilHumidityRegex(
    "Humidity\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)",
    QRegularExpression::CaseInsensitiveOption);

const QRegularExpression kSoilTempRegex(
    "Temperature\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)",
    QRegularExpression::CaseInsensitiveOption);

const QRegularExpression kSoilConductivityRegex(
    "Conductivity\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)",
    QRegularExpression::CaseInsensitiveOption);

const QRegularExpression kSoilPhRegex(
    "pH\\s*:\\s*([-+]?\\d+(?:\\.\\d+)?)",
    QRegularExpression::CaseInsensitiveOption);
}

LoraManager::LoraManager(QObject *parent)
    : QObject(parent),
      serialPort(new QSerialPort(this)),
      serialPortName("/dev/ttyUSB0"),
      serialBaudRate(QSerialPort::Baud9600),
      latestWindSpeed(std::numeric_limits<double>::quiet_NaN()),
      latestAirTemp(std::numeric_limits<double>::quiet_NaN()),
      latestAirHumidity(std::numeric_limits<double>::quiet_NaN()),
      latestLightLux(std::numeric_limits<double>::quiet_NaN()),
      latestSoilHumidity(std::numeric_limits<double>::quiet_NaN()),
      latestSoilTemp(std::numeric_limits<double>::quiet_NaN()),
      latestSoilConductivity(std::numeric_limits<double>::quiet_NaN()),
      latestSoilPh(std::numeric_limits<double>::quiet_NaN()) {
    connect(serialPort, &QSerialPort::readyRead, this, &LoraManager::onReadyRead);
}

void LoraManager::setConnection(const QString &portName, qint32 baudRate) {
    serialPortName = portName;
    serialBaudRate = baudRate;
}

bool LoraManager::start() {
    if (serialPort->isOpen()) {
        return true;
    }

    serialPort->setPortName(serialPortName);
    serialPort->setBaudRate(serialBaudRate);
    serialPort->setDataBits(QSerialPort::Data8);
    serialPort->setParity(QSerialPort::NoParity);
    serialPort->setStopBits(QSerialPort::OneStop);
    serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (!serialPort->open(QIODevice::ReadWrite)) {
        emit statusChanged(QString("LoRa 串口打开失败: %1 (%2)").arg(serialPortName, serialPort->errorString()));
        return false;
    }

    rxBuffer.clear();
    emit statusChanged(QString("LoRa 已连接: %1 @ %2").arg(serialPortName).arg(serialBaudRate));
    return true;
}

void LoraManager::stop() {
    if (!serialPort->isOpen()) {
        return;
    }
    serialPort->close();
    emit statusChanged("LoRa 已断开");
}

bool LoraManager::isRunning() const {
    return serialPort->isOpen();
}

bool LoraManager::sendCommand(int value) {
    if (!serialPort->isOpen()) {
        emit statusChanged("LoRa 未连接，无法发送命令");
        return false;
    }

    const QByteArray payload = QByteArray::number(value) + "\n";
    // LoRa 无线链路存在丢包风险，重复 100 次以带宽换可靠性
    const int repeatCount = 100;
    for (int i = 0; i < repeatCount; ++i) {
        const qint64 written = serialPort->write(payload);
        if (written != payload.size()) {
            emit statusChanged("LoRa 命令发送失败");
            return false;
        }
    }

    serialPort->flush();
    return true;
}

bool LoraManager::sendRaw(const QByteArray &data) {
    if (!serialPort->isOpen()) {
        emit statusChanged("LoRa 未连接，无法发送命令");
        return false;
    }

    const QByteArray payload = data + "\n";
    const int repeatCount = 50;
    for (int i = 0; i < repeatCount; ++i) {
        const qint64 written = serialPort->write(payload);
        if (written != payload.size()) {
            emit statusChanged("LoRa 命令发送失败");
            return false;
        }
    }
    serialPort->flush();
    return true;
}

void LoraManager::onReadyRead() {
    rxBuffer.append(serialPort->readAll());
    parseTextLines();
}

void LoraManager::parseTextLines() {
    while (true) {
        const int newlinePos = rxBuffer.indexOf('\n');
        if (newlinePos < 0) {
            return;
        }

        const QByteArray lineBytes = rxBuffer.left(newlinePos + 1);
        rxBuffer.remove(0, newlinePos + 1);

        const QString line = QString::fromUtf8(lineBytes).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        parseTextLine(line);
    }
}

void LoraManager::parseTextLine(const QString &line) {
    bool updated = false;
    QRegularExpressionMatch match;

    match = kWindRegex.match(line);
    if (match.hasMatch()) {
        bool ok = false;
        const double value = match.captured(1).toDouble(&ok);
        if (ok) {
            latestWindSpeed = value;
            updated = true;
        }
    }

    match = kAirRegex.match(line);
    if (match.hasMatch()) {
        bool okTemp = false;
        bool okHumidity = false;
        const double temperature = match.captured(1).toDouble(&okTemp);
        const double humidity = match.captured(2).toDouble(&okHumidity);
        if (okTemp && okHumidity) {
            latestAirTemp = temperature;
            latestAirHumidity = humidity;
            updated = true;
        }
    }

    match = kLightRegex.match(line);
    if (match.hasMatch()) {
        bool ok = false;
        const double value = match.captured(1).toDouble(&ok);
        if (ok) {
            latestLightLux = value;
            updated = true;
        }
    }

    if (kSoilPrefixRegex.match(line).hasMatch()) {
        // 只要匹配到土壤传感器前缀，该行就算已处理，不再触发 parseError
        updated = true;

        match = kSoilHumidityRegex.match(line);
        if (match.hasMatch()) {
            bool ok = false;
            const double humidity = match.captured(1).toDouble(&ok);
            if (ok) {
                latestSoilHumidity = humidity;
            }
        }

        match = kSoilTempRegex.match(line);
        if (match.hasMatch()) {
            bool ok = false;
            const double temperature = match.captured(1).toDouble(&ok);
            if (ok) {
                latestSoilTemp = temperature;
            }
        }

        match = kSoilConductivityRegex.match(line);
        if (match.hasMatch()) {
            bool ok = false;
            const double conductivity = match.captured(1).toDouble(&ok);
            if (ok) {
                latestSoilConductivity = conductivity;
            }
        }

        match = kSoilPhRegex.match(line);
        if (match.hasMatch()) {
            bool ok = false;
            const double ph = match.captured(1).toDouble(&ok);
            if (ok) {
                latestSoilPh = ph;
            }
        }
    }

    if (updated) {
        emitLatestSnapshot();
        return;
    }

    qDebug() << "Unrecognized LoRa line:" << line;
    emit parseError(QString("LoRa 文本协议无法识别: %1").arg(line));
}

void LoraManager::emitLatestSnapshot() {
    emit sensorDataUpdated(latestWindSpeed,
                           latestAirTemp,
                           latestAirHumidity,
                           latestLightLux,
                           latestSoilHumidity,
                           latestSoilTemp,
                           latestSoilConductivity,
                           latestSoilPh,
                           QDateTime::currentDateTime());
}
