#ifndef LORAMANAGER_H
#define LORAMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QDateTime>

class QSerialPort;

class LoraManager : public QObject {
    Q_OBJECT

public:
    explicit LoraManager(QObject *parent = nullptr);

    void setConnection(const QString &portName, qint32 baudRate);
    bool start();
    void stop();
    bool isRunning() const;
    bool sendCommand(int value);
    bool sendRaw(const QByteArray &data);

signals:
    void sensorDataUpdated(double windSpeed,
                           double airTemp,
                           double airHumidity,
                           double lightLux,
                           double soilHumidity,
                           double soilTemp,
                           double soilConductivity,
                           double soilPh,
                           const QDateTime &timestamp);
    void statusChanged(const QString &status);
    void parseError(const QString &errorText);

private slots:
    void onReadyRead();

private:
    void parseTextLines();
    void parseTextLine(const QString &line);
    void emitLatestSnapshot();

    QSerialPort *serialPort;
    QByteArray rxBuffer;
    QString serialPortName;
    qint32 serialBaudRate;

    double latestWindSpeed;
    double latestAirTemp;
    double latestAirHumidity;
    double latestLightLux;
    double latestSoilHumidity;
    double latestSoilTemp;
    double latestSoilConductivity;
    double latestSoilPh;
};

#endif // LORAMANAGER_H
