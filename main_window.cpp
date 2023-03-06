#include "main_window.h"
#include "ui_main_window.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    chart = new QChart;
    x_series = new QLineSeries(this);
    y_series = new QLineSeries(this);
    x_series->setName("X");
    y_series->setName("Y");

    chart->legend()->show();
    chart->addSeries(x_series);
    chart->addSeries(y_series);
    chart->createDefaultAxes();
    chart->axes(Qt::Horizontal).first()->setRange(0, 10000);
    chart->axes(Qt::Vertical).first()->setRange(-1000, 1000);

    ui->plot->setChart(chart);
    ui->plot->setRenderHint(QPainter::Antialiasing);

    this->timer = new QTimer(this);
    this->timer->setInterval(1000);
    QObject::connect(this->timer, &QTimer::timeout, this, &MainWindow::pollServer);

    this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in srv;
    srv.sin_port = htons(8888);
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr("10.4.1.22");
    ::connect(this->sock, (struct sockaddr*) &srv, sizeof(struct sockaddr));
    write(sock, "S1\n", 3);

    char c;
    read(sock, &c, 1);

    this->timer->start();
}

MainWindow::~MainWindow()
{
    delete ui;
    ::close(sock);
}

void MainWindow::pollServer()
{
    int i = 0;
    int size = 0;
    int32_t raw_x = 0;
    int32_t raw_y = 0;
    float value_x;
    float value_y;
    char* data;
    QVector<QPointF> xData;
    QVector<QPointF> yData;

    int samples = 10000;
    int bufferSize = samples * 2 * sizeof(float);
    data = new char[bufferSize];

    this->x_series->clear();
    this->y_series->clear();

    max = std::numeric_limits<float>::min();
    min = std::numeric_limits<float>::max();
    size = read(sock, data, bufferSize);
    for(i = 0; i < size; i += 8) {
        memcpy(&raw_x, data + i, sizeof(int32_t));
        memcpy(&raw_y, data + i + 4, sizeof(int32_t));

        value_x = raw_x / 1000.0;
        value_y = raw_y / 1000.0;
        xData.append(QPointF((int) (i / 8), value_x));
        yData.append(QPointF((int) (i / 8), value_y));
        max = qMax(value_x, max);
        max = qMax(value_y, max);
        min = qMin(value_x, min);
        min = qMin(value_y, min);
    }

    this->x_series->replace(xData);
    this->y_series->replace(yData);
    chart->axes(Qt::Horizontal).first()->setRange(0, size / 8);
    chart->axes(Qt::Vertical).first()->setRange(min, max);
    ui->plot->update();

    delete[] data;
}