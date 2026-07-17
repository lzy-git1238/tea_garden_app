#ifndef TEAGARDENSYSTEM_H
#define TEAGARDENSYSTEM_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
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
#include "yolodetector.h"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QtCharts/QChartView>

struct GpsPosition {
    double lat = 0.0;
    double lon = 0.0;
    double alt = 0.0;
};

class ChartDashboard;
class SrtVideoStreamer;
class RoverControlPanel;

// --- 主系统类 ---
class TeaGardenSystem : public QWidget {
    Q_OBJECT

public:
    explicit TeaGardenSystem(QWidget *parent = nullptr);
    virtual ~TeaGardenSystem();
    void setScreenDpiScale(double scale) { m_scale = scale; }

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
    void checkSchedules();          // 调度检查（每分钟）
    void refreshFlowCard();         // 刷新流量卡片（每秒）
    void openScheduleDialog(const QString &deviceType);  // 打开定时对话框
    void onIotCommand(const QString &device, bool on, const QString &requestId);
    void onVideoFrame(const QImage &image);
    void onVideoStatus(const QString &msg);
    void onYoloResults(const QImage &frame, const QVector<DetectionResult> &results);
    void openRoverPanel();
private:
    void drawDronePath();
    void setupUI();
    void setupMapView(QVBoxLayout *midPanel);
    void setupMjpegStream();
    bool sendLoraCommand(const QString &deviceKey, int value);
    void scheduleLoraCommand(const QString &deviceKey, int value);
    void flushPendingCommand(const QString &deviceKey);
    QWidget* createDataCard(QString title, int index, QString unit);
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
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
    QFrame *headerFrame = nullptr;
    QLabel *headerTitleLabel = nullptr;
    QFrame *leftPanelFrame = nullptr;
    QFrame *midPanelFrame = nullptr;
    QFrame *rightPanelFrame = nullptr;
    QLabel *metricsTitleLabel = nullptr;
    QLabel *mapTitleLabel = nullptr;
    QLabel *weatherSectionLabel = nullptr;
    QLabel *controlSectionLabel = nullptr;
    QFrame *controlFrame = nullptr;       // 快捷控制框（与天气卡片对称）
    QLabel *loraStateLabel;
    QLabel *videoLabel = nullptr;
    QLabel *videoTitleLabel = nullptr;
    QLabel *roverVideoLabel = nullptr;
    SrtVideoStreamer *m_roverStreamer = nullptr;
    QTextEdit *chatDisplay;
    QLineEdit *chatInput;
    QPushButton *voiceBtn;
    QPushButton *sendBtn = nullptr;
    QLabel *aiTitleLabel = nullptr;
    QPushButton *chartBtn = nullptr;
    QPushButton *pestBtn = nullptr;
    QPushButton *mockPestBtn = nullptr;
    QPushButton *pestDetailBtn = nullptr;
    QPushButton *weatherRefreshBtn = nullptr;
    QLabel *pestPieTitle = nullptr;
    QPushButton *pumpBtn = nullptr;
    QPushButton *lampBtn = nullptr;
    QPushButton *scheduleBtnPump = nullptr;  // 定时 ⏰ 按钮

    // LoRa 下行控制防抖
    int commandMinIntervalMs = 800;
    QMap<QString, QDateTime> lastCommandTime;
    QMap<QString, int> pendingCommands;
    QMap<QString, QTimer*> commandTimers;

    // ── 定时调度 ──
    struct ActiveJob {
        QString deviceType;
        int scheduleId = 0;
        QDateTime startTime;
        QDateTime endTime;
        double flowRate = 2.5;
        int durationMin = 0;
    };
    QMap<QString, ActiveJob> activeJobs;  // key: deviceType ("pump"/"lamp")
    QTimer *scheduleTimer = nullptr;       // 60s 调度检查
    QTimer *flowCardTimer = nullptr;       // 1s 流量刷新

    // 流量状态卡片
    QFrame *flowCard = nullptr;
    QLabel *flowCurrentLabel = nullptr;    // 已灌溉量
    QLabel *flowExpectedLabel = nullptr;   // 预计总量
    QLabel *flowRemainingLabel = nullptr;  // 剩余时间
    QLabel *flowProgressLabel = nullptr;   // 进度文字 (e.g. "12/30 min")
    QFrame *flowProgressBar = nullptr;     // 进度条 track
    QFrame *flowProgressFill = nullptr;    // 进度条 fill
    QPushButton *flowStopBtn = nullptr;    // 停止灌溉
    QPushButton *flowSettingsBtn = nullptr;// 设置流速
    QLabel *flowPlanIdLabel = nullptr;     // 计划编号显示

    QProcess *recordProcess = nullptr;  // 当前录音进程，非空时表示正在录音
    QString  pendingWavPath;            // 录音目标文件路径
    QTimer *updateTimer;
    LoraManager *loraManager;
    QNetworkAccessManager *networkManager;
    QWidget *mapView = nullptr;
    bool m_mapReloading = false;
    int m_mapRetryMs = 1000;

    // 无人机 GPS 路径
    QLabel *m_popupImageLabel = nullptr;
    QProcess *m_gpsProcess = nullptr;
    QByteArray m_gpsBuf;
    QTimer *m_drawPathTimer = nullptr;
    QVector<GpsPosition> m_dronePath;
    bool m_gpsFirstFix = true;
    static constexpr int kMaxPathPoints = 200;
    QDateTime lastLoraFrameTime;

    // MJPEG 视频流
    class QWebSocket *videoSocket = nullptr;
    QByteArray mjpegBuffer;
    SrtVideoStreamer *m_videoStreamer = nullptr;
    YoloDetector *m_yoloDetector = nullptr;

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

    // 屏幕缩放
    double m_scale = 1.0;

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
    QPushButton *roverBtn = nullptr;
    class RoverControlPanel *roverPanel = nullptr;
};

#endif // TEAGARDENSYSTEM_H
