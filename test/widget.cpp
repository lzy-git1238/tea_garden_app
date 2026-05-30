////#include "widget.h"
////#include "ui_widget.h"
////#include <QIcon>
////#include <QPushButton>
////#include <QVBoxLayout> // 建议引入布局管理器
////Widget::Widget(QWidget *parent)
////    : QWidget(parent)
////    , ui(new Ui::Widget)
////{
////    this->setWindowTitle("茶园管理系统");
////    //this->setWindowIcon("/home/elf/Pictures/截图/截图 2026-04-19 19-57-37.jpg");
////    this->setWindowIcon(QIcon("/home/elf/Pictures/截图/截图 2026-04-19 19-57-37.jpg"));
////    this->setMinimumSize(640,480);

////    QPushButton *press=new QPushButton("开启",this);
////    press->show();

////    QPushButton *press1=new QPushButton("关闭",this);
////    press1->move(300,0);

//////ui->setupUi(this);
////}

////Widget::~Widget()
////{
////    delete ui;
////}

//#include "widget.h"
//#include <QRandomGenerator>
//#include <QVBoxLayout>
//#include <QGroupBox>

//Widget::Widget(QWidget *parent) : QWidget(parent) {
//    setupUI();

//    // 设置定时器，每 2 秒更新一次数据
//    dataTimer = new QTimer(this);
//    connect(dataTimer, &QTimer::timeout, this, &Widget::updateSensorData);
//    dataTimer->start(2000);

//    // 初始化显示一组数据
//    updateSensorData();
//}

//void Widget::setupUI() {
//    this->setWindowTitle("茶园智慧管理系统");
//    this->resize(800, 500);
//    this->setStyleSheet("QWidget { background-color: #F0F4F0; }"); // 浅绿背景
//    this->setWindowIcon(QIcon("/home/elf/Pictures/截图/截图 2026-04-19 19-57-37.jpg"));
//    // 主布局
//    QVBoxLayout *mainLayout = new QVBoxLayout(this);

//    // 标题
//    QLabel *titleLabel = new QLabel("茶园环境实时监测中心");
//    titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #2E7D32; margin: 10px;");
//    titleLabel->setAlignment(Qt::AlignCenter);
//    mainLayout->addWidget(titleLabel);

//    // 数据卡片布局
//    QGridLayout *gridLayout = new QGridLayout();

//    // 样式模板：大字体数值
//    QString cardStyle = "background-color: white; border-radius: 10px; padding: 15px; border: 1px solid #C8E6C9;";
//    QString labelStyle = "font-size: 18px; color: #555;";
//    QString valueStyle = "font-size: 28px; font-weight: bold; color: #2E7D32;";

//    // 创建四个数据展示框
//    auto createCard = [&](QString title, QLabel* &valLabel, QString unit, int row, int col) {
//        QGroupBox *box = new QGroupBox();
//        box->setStyleSheet(cardStyle);
//        QVBoxLayout *layout = new QVBoxLayout(box);

//        QLabel *tLabel = new QLabel(title);
//        tLabel->setStyleSheet(labelStyle);
//        valLabel = new QLabel("-- " + unit);
//        valLabel->setStyleSheet(valueStyle);

//        layout->addWidget(tLabel);
//        layout->addWidget(valLabel);
//        gridLayout->addWidget(box, row, col);
//    };

//    createCard("环境温度", tempLabel, "℃", 0, 0);
//    createCard("环境湿度", humiLabel, "% RH", 0, 1);
//    createCard("光照强度", lightLabel, " Lux", 1, 0);
//    createCard("土壤湿度", soilLabel, "%", 1, 1);

//    mainLayout->addLayout(gridLayout);

//    // 底部控制按钮
//    QPushButton *refreshBtn = new QPushButton("手动刷新数据");
//    refreshBtn->setFixedSize(150, 40);
//    refreshBtn->setStyleSheet("background-color: #4CAF50; color: white; border-radius: 5px; font-weight: bold;");
//    connect(refreshBtn, &QPushButton::clicked, this, &Widget::updateSensorData);
//    mainLayout->addWidget(refreshBtn, 0, Qt::AlignCenter);
//}

//void Widget::updateSensorData() {
//    // 模拟从传感器获取随机数据
//    double temp = QRandomGenerator::global()->bounded(150, 350) / 10.0; // 15.0 - 35.0
//    int humi = QRandomGenerator::global()->bounded(40, 80);             // 40% - 80%
//    int light = QRandomGenerator::global()->bounded(1000, 5000);        // 1000 - 5000 Lux
//    int soil = QRandomGenerator::global()->bounded(20, 60);             // 20% - 60%

//    tempLabel->setText(QString::number(temp, 'f', 1) + " ℃");
//    humiLabel->setText(QString::number(humi) + " % RH");
//    lightLabel->setText(QString::number(light) + " Lux");
//    soilLabel->setText(QString::number(soil) + " %");
//}

//Widget::~Widget() {}
