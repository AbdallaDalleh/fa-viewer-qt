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
    std::vector<float> fft_in_x;
    std::vector<float> fft_in_y;
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
        xData.append(QPointF((int) (i / 8), value_x));
        yData.append(QPointF((int) (i / 8), value_y));
        fft_in_x.push_back(value_x);
        fft_in_y.push_back(value_y);
        max = qMax(value_x, max);
        max = qMax(value_y, max);
        min = qMin(value_x, min);
        min = qMin(value_y, min);
    }

    cout << min << endl;
    cout << max << endl;

    if(ui->cbSignal->currentIndex() > 0) {
        if(min == 0 && max == std::numeric_limits<float>::min()) {
            this->yAxis->setRange(-1000, 1000);
            this->yLogAxis->setRange(-1000, 1000);
            return;
        }

        cv::dft(fft_in_x, fft_in_x);
        cv::dft(fft_in_y, fft_in_y);

        fft_x.push_back(abs(fft_in_x[0]));
        for(unsigned i = 1; i < fft_in_x.size() - 2; i += 2)
            fft_x.push_back( sqrt( pow(fft_in_x[i], 2) + pow(fft_in_x[i+1], 2) ));

        fft_y.push_back(abs(fft_in_y[0]));
        for(unsigned i = 1; i < fft_in_y.size() - 2; i += 2)
            fft_y.push_back( sqrt( pow(fft_in_y[i], 2) + pow(fft_in_y[i+1], 2) ));

        for(int i = 0; i < (int)fft_x.size(); i++) {
            xData[i].setY(fft_x[i]);
            yData[i].setY(fft_y[i]);
            max = qMax((float)xData[i].y(), max);
            max = qMax((float)yData[i].y(), max);
            min = qMin((float)xData[i].y(), min);
            min = qMin((float)yData[i].y(), min);
        }

        this->x_series->replace(QVector<QPointF>(xData.begin(), xData.begin() + fft_x.size()));
        this->y_series->replace(QVector<QPointF>(yData.begin(), yData.begin() + fft_y.size()));

        if(this->x_series->attachedAxes().contains(this->yAxis))
            this->x_series->detachAxis(this->yAxis);
        if(this->y_series->attachedAxes().contains(this->yAxis))
            this->y_series->detachAxis(this->yAxis);

        if(!this->x_series->attachedAxes().contains(this->yLogAxis))
            this->x_series->attachAxis(this->yLogAxis);
        if(!this->y_series->attachedAxes().contains(this->yLogAxis))
            this->y_series->attachAxis(this->yLogAxis);

        this->xAxis->setRange(0, this->samples / 2);
        this->yLogAxis->setRange(min, max);
        this->yAxis->hide();
        this->yLogAxis->show();
    }
    else {
        this->x_series->replace(xData);
        this->y_series->replace(yData);

        if(this->x_series->attachedAxes().contains(this->yLogAxis))
            this->x_series->detachAxis(this->yLogAxis);
        if(this->y_series->attachedAxes().contains(this->yLogAxis))
            this->y_series->detachAxis(this->yLogAxis);

        if(!this->x_series->attachedAxes().contains(this->yAxis))
            this->x_series->attachAxis(this->yAxis);
        if(!this->y_series->attachedAxes().contains(this->yAxis))
            this->y_series->attachAxis(this->yAxis);

        if(min == 0 && max == std::numeric_limits<float>::min())
            this->yAxis->setRange(-1000, 1000);
        else
            this->yAxis->setRange(min, max);

        this->xAxis->setRange(0, this->samples);
        this->yLogAxis->hide();
        this->yAxis->show();
    }
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
