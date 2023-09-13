#include "main_window.h"
#include "ui_main_window.h"

#pragma pack(4)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    x_series = new QLineSeries(this);
    y_series = new QLineSeries(this);
    x_series->setName("Horizontal");
    y_series->setName("Vertical");
    y_series->setPen(QPen(Qt::red));

    this->xAxis = new QValueAxis;
    this->xAxis->setTitleText("Samples");
    this->xAxis->setTickCount(6);
    this->xAxis->setLabelFormat("%d");

    this->yAxis = new QValueAxis;
    this->yAxis->setTitleText("Positions");

    this->yLogAxis = new QLogValueAxis;
    this->yLogAxis->setTitleText("Amplitudes");
    this->yLogAxis->setBase(10);
    this->yLogAxis->setMinorGridLineVisible(false);
    this->yLogAxis->setLabelFormat("%g");

    chart = new QChart;
    chart->addSeries(x_series);
    chart->addSeries(y_series);
    this->chart->addAxis(this->xAxis, Qt::AlignBottom);
    this->chart->addAxis(this->yAxis, Qt::AlignLeft);
    this->chart->addAxis(this->yLogAxis, Qt::AlignLeft);
    this->x_series->attachAxis(this->xAxis);
    this->x_series->attachAxis(this->yAxis);
    this->y_series->attachAxis(this->xAxis);
    this->y_series->attachAxis(this->yAxis);

    chart->legend()->show();

    ui->plot->setChart(chart);
    ui->plot->setRenderHint(QPainter::Antialiasing);

    this->timer = new QTimer(this);
    this->timer->setInterval(1000);
    QObject::connect(this->timer, &QTimer::timeout, this, &MainWindow::pollServer);
    QObject::connect(ui->btnConnect, &QPushButton::clicked, this, &MainWindow::reconnectToServer);

    QFile file(":/fa-config.json");
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Could not open FA configuration file.", QMessageBox::Ok);
        QApplication::exit(0);
    }

    QString id;
    QTextStream config(&file);
    QJsonDocument faConfig = QJsonDocument::fromJson(QString(config.readAll()).toUtf8());
    QJsonObject object = faConfig.object();
    this->cells   = object.value("cells").toInt();
    this->bpms    = object.value("bpms_cell").toInt();
    this->format  = object.value("id_format").toString();
    this->firstID = object.value("first_id").toInt();
    this->ids     = object.value("ids").toInt();

    int currentID = this->firstID;
    for(int cell = 1; cell <= this->cells; cell++) {
        if(currentID > this->ids)
            break;

        ui->cbCells->addItem("Cell " + QString::number(cell));
        for(int bpm = 1; bpm <= this->bpms; bpm++) {
            id = QString().asprintf(this->format.toStdString().c_str(), cell, currentID, bpm);
            this->idsMap.insert(id, currentID++);
        }
    }

    ui->cbCells->setCurrentIndex(1);
    ui->cbTime->setCurrentIndex(3);
    reconnectToServer();
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
    std::vector<float> data_x;
    std::vector<float> data_y;
    std::vector<float> fft_x;
    std::vector<float> fft_y;

    data = new char[bufferSize];
    this->x_series->clear();
    this->y_series->clear();
    max = std::numeric_limits<float>::min();
    min = std::numeric_limits<float>::max();

    int buffer = bufferSize;
    int ptr = 0;
    if(buffer > MAX_BUFFER_SIZE) {
        while(buffer > 0) {
            size += recv(sock, data + ptr, buffer < MAX_BUFFER_SIZE ? buffer : MAX_BUFFER_SIZE, MSG_WAITALL);
            buffer -= MAX_BUFFER_SIZE;
            ptr += MAX_BUFFER_SIZE;
        }
    }
    else
        size += recv(sock, data, buffer, MSG_WAITALL);

    for(i = 0; i < size && size > 27; i += 8) {
        memcpy(&raw_x, data + i, sizeof(int32_t));
        memcpy(&raw_y, data + i + 4, sizeof(int32_t));

        value_x = (raw_x) / 1000.0;
        value_y = (raw_y) / 1000.0;
        data_x.push_back(value_x);
        data_y.push_back(value_y);
    }

    if(min == 0 && max == std::numeric_limits<float>::min()) {
        this->yAxis->setRange(-1000, 1000);
        this->yLogAxis->setRange(-1000, 1000);
        return;
    }

    if(ui->cbSignal->currentIndex() > 0) {
        cv::dft(data_x, data_x);
        cv::dft(data_y, data_y);

        fft_x.push_back(abs(data_x[0]));
        for(unsigned i = 1; i < data_x.size() - 2; i += 2)
            fft_x.push_back( sqrt( pow(data_x[i], 2) + pow(data_x[i+1], 2) ));

        fft_y.push_back(abs(data_y[0]));
        for(unsigned i = 1; i < data_y.size() - 2; i += 2)
            fft_y.push_back( sqrt( pow(data_y[i], 2) + pow(data_y[i+1], 2) ));

        for(int i = 0; i < (int)fft_x.size(); i++) {
            xData.push_back(QPointF(i, fft_x[i]));
            yData.push_back(QPointF(i, fft_y[i]));
            max = qMax(fft_x[i], max);
            max = qMax(fft_y[i], max);
            min = qMin(fft_x[i], min);
            min = qMin(fft_y[i], min);
        }

        for(auto series : this->chart->series()) {
            if(series->attachedAxes().contains(this->yAxis))
                series->detachAxis(this->yAxis);
            if(!series->attachedAxes().contains(this->yLogAxis))
                series->attachAxis(this->yLogAxis);
        }

        this->yLogAxis->setRange(min, max);
        this->xAxis->setRange(0, this->samples / 2);
        this->yAxis->hide();
        this->yLogAxis->show();
    }
    else {
        for(unsigned i = 0; i < qMin(data_x.size(), data_y.size()); i++) {
            xData.push_back(QPointF(i, data_x[i]));
            yData.push_back(QPointF(i, data_y[i]));

            max = qMax(data_x[i], max);
            max = qMax(data_y[i], max);
            min = qMin(data_x[i], min);
            min = qMin(data_y[i], min);
        }

        for(auto series : this->chart->series()) {
            if(series->attachedAxes().contains(this->yLogAxis))
                series->detachAxis(this->yLogAxis);
            if(!series->attachedAxes().contains(this->yAxis))
                series->attachAxis(this->yAxis);
        }

        this->yAxis->setRange(min, max);
        this->xAxis->setRange(0, this->samples);
        this->yLogAxis->hide();
        this->yAxis->show();
    }
    this->x_series->replace(xData);
    this->y_series->replace(yData);
    ui->plot->update();

    delete[] data;
}

void MainWindow::on_cbCells_currentIndexChanged(int index)
{
    QString item;

    ui->cbID->clear();
    if(index > 0) {
        item = QString().asprintf("SRC%02d", index);
        for(QString key : this->idsMap.keys()) {
            if(key.startsWith(item))
                ui->cbID->addItem(key);
        }
    }
}

void MainWindow::on_cbID_currentIndexChanged(const QString &arg1)
{
    int currentID;

    currentID = this->idsMap[arg1];
    this->message = "S" + QString::number(currentID) + "\n";
    this->timer->stop();
    ::close(this->sock);
    reconnectToServer();
}

void MainWindow::reconnectToServer()
{
    char c;

    this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    srv.sin_port = htons(8888);
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr("10.4.1.22");
    ::connect(this->sock, (struct sockaddr*) &srv, sizeof(struct sockaddr));
    ::write(sock, message.toStdString().c_str(), message.length());
    ::read(sock, &c, 1);
    this->timer->start();
}

void MainWindow::on_cbShow_currentIndexChanged(int index)
{
    if(index == 0) {
        x_series->setVisible(true);
        y_series->setVisible(true);
    }
    else if(index == 1) {
        x_series->setVisible(true);
        y_series->setVisible(false);
    }
    else {
        x_series->setVisible(false);
        y_series->setVisible(true);
    }
}

void MainWindow::on_cbTime_currentIndexChanged(int index)
{
    QString item;
    QStringList items;

    Q_UNUSED(index);
    item = ui->cbTime->currentText();
    items = item.split(" ");
    if(items[1] == "ms") {
        // The 10 is equal to 10000/1000 (Convert to seconds and multiply by the FA rate 10KHz).
        this->samples = items[0].toInt() * 10;
        this->timerPeriod = items[0].toInt();
    }
    else {
        this->samples = items[0].toDouble() * 10000;
        this->timerPeriod = items[0].toDouble() * 1000;
    }

    this->bufferSize = this->samples * 2 * sizeof(float);
    this->timer->setInterval(this->timerPeriod);
}
