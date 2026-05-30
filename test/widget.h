////#ifndef WIDGET_H
////#define WIDGET_H

////#include <QWidget>

////QT_BEGIN_NAMESPACE
////namespace Ui { class Widget; }
////QT_END_NAMESPACE

////class Widget : public QWidget
////{
////    Q_OBJECT

////public:
////    Widget(QWidget *parent = nullptr);
////    ~Widget();

////private:
////    Ui::Widget *ui;
////};
////#endif // WIDGET_H
//#ifndef WIDGET_H
//#define WIDGET_H

//#include <QWidget>
//#include <QLabel>
//#include <QPushButton>
//#include <QGridLayout>
//#include <QTimer>

//class Widget : public QWidget {
//    Q_OBJECT

//public:
//    Widget(QWidget *parent = nullptr);
//    ~Widget();

//private slots:
//    void updateSensorData(); // 模拟数据更新函数

//private:
//    // 数据显示标签
//    QLabel *tempLabel;    // 温度
//    QLabel *humiLabel;    // 湿度
//    QLabel *lightLabel;   // 光照强度
//    QLabel *soilLabel;    // 土壤湿度

//    QTimer *dataTimer;    // 定时器模拟实时监测
//    void setupUI();       // 初始化界面布局
//};

//#endif // WIDGET_H
