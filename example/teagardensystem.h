#ifndef TEAGARDENSYSTEM_H
#define TEAGARDENSYSTEM_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QFrame>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QUrl>

#include "loramanager.h"

class QWebSocket;
class GreenHomeClient;

// --- 主系统类 ---
class TeaGardenSystem : public QWidget {
    Q_OBJECT

public:
    explicit TeaGardenSystem(QWidget *parent = nullptr);
    virtual ~TeaGardenSystem(); // 明确声明虚析构函数

private slots:
    void refreshData();         // 定时刷新环境数据
    void handleChat();          // 处理用户聊天输入
    void onLoraData(double windSpeed,
                    double airTemp,
                    double airHumidity,
                    double lightLux,
                    double soilHumidity,
                    double soilTemp,
                    double soilConductivity,
                    double soilPh,
                    const QDateTime &timestamp);
    void onLoraStatus(const QString &status);
    void onLoraParseError(const QString &errorText);
    void onNetworkReply(QNetworkReply *reply);
    void startVoiceRecording();   // 开始录音 / 点击停止录音
    void sendXfAudioFrame();
    void onPumpToggled(bool checked);
    void onLampToggled(bool checked);
    void onRemoteDeviceCommand(const QString &deviceName, bool isOn);

private:
    void setupUI();
    QWidget* createDataCard(QString title, int index, QString unit);
    void appendChatMessage(QString role, QString message);
    void requestAIAnalysis(const QString &userMessage);
    void startXfUploadFromFile(const QString &pcmPath);
    void stopXfStreaming();
    void sendXfFrame(const QByteArray &pcm, int status);
    void handleXfMessage(const QString &message);
    QUrl buildXfIatUrl() const;
    bool sendLoraCommand(const QString &deviceKey, int value);
    bool sendLoraCommandNow(const QString &deviceKey, int value);
    void scheduleLoraCommand(const QString &deviceKey, int value, int delayMs);
    void flushPendingCommand(const QString &deviceKey);
    bool handleVoicePumpCommand(const QString &text);
    bool handleVoiceLampCommand(const QString &text);
    void updatePumpButtonUi(bool checked);

    // 成员变量
    QList<QLabel*> valueLabels;
    QLabel *loraStateLabel;
    QTextEdit *chatDisplay;
    QLineEdit *chatInput;
    QPushButton *voiceBtn;
    QPushButton *pumpBtn;
    QPushButton *lampBtn;
    QProcess *recordProcess = nullptr;  // 当前录音进程，非空时表示正在录音
    QString pendingPcmPath;
    QString recordStderr;
    QTimer *updateTimer;
    LoraManager *loraManager;
    QNetworkAccessManager *networkManager;
    QDateTime lastLoraFrameTime;
    QWebSocket *xfSocket = nullptr;
    QTimer *xfAudioTimer = nullptr;
    QByteArray xfAudioBuffer;
    QString xfTextBuffer;
    QString xfAppId;
    QMap<int, QString> xfResultPieces;
    bool xfStreaming = false;
    bool xfFirstFrame = true;
    bool xfStopRequested = false;
    bool xfFinalFrameSent = false;
    int xfSeq = 1;
    int xfFrameSize = 1280;
    GreenHomeClient *greenClient = nullptr;
    int commandMinIntervalMs = 800;
    QMap<QString, QDateTime> lastCommandTime;
    QMap<QString, int> pendingCommands;
    QMap<QString, QTimer*> commandTimers;

    // 当前环境数值（用于AI分析）
    double curTemp = 22.0;
    double curHumi = 60.0;
    int curLight = 2000;
    double curPh = 5.6;
    double curWindSpeed = 2.5;
    double curSoilTemp = 18.0;
    double curSoilHumi = 45.0;
    double curSoilEc = 1.2;
};

#endif // TEAGARDENSYSTEM_H
