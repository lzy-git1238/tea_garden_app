#ifndef ROVERCONTROLPANEL_H
#define ROVERCONTROLPANEL_H

#include <QWidget>
#include <QProcess>
#include <QElapsedTimer>

class QLabel;
class QPushButton;
class LoraManager;

class RoverControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit RoverControlPanel(LoraManager *lora, QWidget *parent = nullptr);
    ~RoverControlPanel();

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onForwardPressed();
    void onForwardReleased();
    void onBackwardPressed();
    void onBackwardReleased();
    void onLeftPressed();
    void onLeftReleased();
    void onRightPressed();
    void onRightReleased();
    void onSprayPressed();
    void onSprayReleased();
    void onVideoFrame();

private:
    void setupUI();
    void sendRoverCmd(char cmd);
    void startVideoCapture();
    void stopVideoCapture();

    LoraManager *m_lora;
    QProcess *m_ffmpegProc = nullptr;
    QLabel *m_videoLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_forwardBtn = nullptr;
    QPushButton *m_backwardBtn = nullptr;
    QPushButton *m_leftBtn = nullptr;
    QPushButton *m_rightBtn = nullptr;
    QPushButton *m_sprayBtn = nullptr;

    QByteArray m_videoBuf;
    int m_frameW = 640;
    int m_frameH = 480;

    bool m_keyW = false;
    bool m_keyA = false;
    bool m_keyS = false;
    bool m_keyD = false;
    bool m_spraying = false;
};

#endif // ROVERCONTROLPANEL_H
