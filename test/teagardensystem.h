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

#include "loramanager.h"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QtCharts/QChartView>

class ChartDashboard;

// --- 主系统类 ---
class TeaGardenSystem : public QWidget {
    Q_OBJECT

public:
    explicit TeaGardenSystem(QWidget *parent = nullptr);
    virtual ~TeaGardenSystem();

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
    void onVoiceRecorded(const QString &wavFilePath);
    void openChartDashboard();
    void refreshWeatherAlert();  // 定时刷新天气预警
    void toggleTheme();          // 切换日间/夜间模式
    void onPumpToggled(bool checked);
    void onLampToggled(bool checked);

private:
    void setupUI();
    void setupMapView(QVBoxLayout *midPanel);
    void setupMjpegStream();
    bool sendLoraCommand(const QString &deviceKey, int value);
    void scheduleLoraCommand(const QString &deviceKey, int value);
    void flushPendingCommand(const QString &deviceKey);
    QWidget* createDataCard(QString title, int index, QString unit);
    bool eventFilter(QObject *obj, QEvent *event) override;
    void showWeatherDetail();   // 24h预报详情弹窗
    void appendChatMessage(QString role, QString message);
    void requestAIAnalysis(const QString &userMessage);
    void applyTheme();  // 根据 isDarkMode 刷新整体样式
    static QString sectionTitleStyle(const QString &accent);
    static QString dashPanelStyle(const QString &panelBg, const QString &border, const QString &borderHi);
    static QString techBtnStyle(const QString &bg1, const QString &bg2, const QString &border, const QString &text);

    // 成员变量
    QList<QLabel*> valueLabels;
    QList<QFrame*>  dataCards;      // 卡片 Frame
    QList<QLabel*>  dataCardTitles; // 卡片标题（空气温度等）
    QLabel *leftPanelTitle = nullptr;
    QLabel *loraStateLabel;
    QLabel *videoLabel = nullptr;
    QLabel *videoTitleLabel = nullptr;
    QTextEdit *chatDisplay;
    QLineEdit *chatInput;
    QPushButton *voiceBtn;
    QPushButton *sendBtn = nullptr;
    QLabel *aiTitleLabel = nullptr;
    QPushButton *chartBtn = nullptr;
    QPushButton *pumpBtn = nullptr;
    QPushButton *lampBtn = nullptr;

    // LoRa 下行控制防抖
    int commandMinIntervalMs = 800;
    QMap<QString, QDateTime> lastCommandTime;
    QMap<QString, int> pendingCommands;
    QMap<QString, QTimer*> commandTimers;
    QProcess *recordProcess = nullptr;  // 当前录音进程，非空时表示正在录音
    QString  pendingWavPath;            // 录音目标文件路径
    QTimer *updateTimer;
    LoraManager *loraManager;
    QNetworkAccessManager *networkManager;
    QWidget *mapView = nullptr;
    QDateTime lastLoraFrameTime;

    // MJPEG 视频流
    class QWebSocket *videoSocket = nullptr;
    QByteArray mjpegBuffer;

    // 当前环境数值（用于AI分析）
    double curTemp = 22.0;
    double curHumi = 60.0;
    int curLight = 2000;
    double curPh = 5.6;
    double curWindSpeed = 2.5;
    double curSoilTemp = 18.0;
    double curSoilHumi = 45.0;
    double curSoilEc = 1.2;

    // 数据看板
    ChartDashboard *chartDashboard = nullptr;
    QSystemTrayIcon *trayIcon = nullptr;
    QMap<int, QDateTime> lastAlertTime;

    // 天气预警
    QFrame *weatherCard = nullptr;
    QLabel *weatherTitleLabel = nullptr;
    QLabel *weatherLevelLabel = nullptr;
    QLabel *weatherDetailLabel = nullptr;
    QLabel *weatherTimeLabel = nullptr;
    QTimer *weatherTimer = nullptr;
    QNetworkReply *weatherReply = nullptr;
    QJsonArray weatherForecastData;  // 缓存24h逐时预报数据，用于弹窗
    QString weatherLocationPath;     // 缓存地点名称

    // 主题
    bool isDarkMode = true;
    QPushButton *themeBtn = nullptr;

    // 病虫害
    void openPestDashboard();
    void injectMockPest();
    void injectPestMarkers();
    void refreshPestPie();
    class PestDashboard *pestDashboard = nullptr;
    class QtCharts::QChartView *pestPieView = nullptr;
};

#endif // TEAGARDENSYSTEM_H
