#include "main_window.h"
#include "ui_main_window.h"

#pragma pack(4)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    FastArchiverServer* fa = new FastArchiverServer("10.4.1.22", 8888, DEFAULT_CONFIG, this);
    return;

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

    this->xLogAxis = new QLogValueAxis;
    this->xLogAxis->setTitleText("Frequency (Hz)");
    this->xLogAxis->setBase(10);
    this->xLogAxis->setMinorGridLineVisible(false);
    this->xLogAxis->setLabelFormat("%g");

    chart = new QChart;
    chart->addSeries(x_series);
    chart->addSeries(y_series);
    this->chart->addAxis(this->xAxis, Qt::AlignBottom);
    this->chart->addAxis(this->yAxis, Qt::AlignLeft);
    this->chart->addAxis(this->xLogAxis, Qt::AlignBottom);
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
    on_cbSignal_currentIndexChanged(2);
    ui->cbSignal->setCurrentIndex(2);
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
    int decimation_factor = 1;
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
    std::vector<float> fft_logf_x;
    std::vector<float> fft_logf_y;

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

    auto compare_zero = [](float i){ return i == 0.0; };
    auto square_root_float = [](float a){ return sqrt(a / 10.0); };
    auto square = [](float a){ return a * a; };

    if(std::all_of(data_x.begin(), data_x.end(), compare_zero) &&
       std::all_of(data_y.begin(), data_y.end(), compare_zero)) {
        return;
    }

    if(ui->cbSignal->currentIndex() == MODE_FFT_LOGF) {

        if(ui->cbWindow->isChecked()) {
            float delta = (M_PI - -M_PI ) / (this->samples - 1);
            for(int i = 0; i < this->samples; i++) {
                data_x[i] *= 1 + cos(-M_PI + delta * i);
                data_y[i] *= 1 + cos(-M_PI + delta * i);
            }
        }
        cv::dft(data_x, data_x);
        cv::dft(data_y, data_y);

        fft_x.push_back(abs(data_x[0]) * sqrt(2 / (this->samplingFrequency * this->samples)));
        fft_y.push_back(abs(data_y[0]) * sqrt(2 / (this->samplingFrequency * this->samples)));
        for(int i = 1; i < this->samples - 2; i += 2) {
            fft_x.push_back( sqrt( pow(data_x[i], 2) + pow(data_x[i+1], 2) ) * sqrt(2 / (this->samplingFrequency * this->samples)));
            fft_y.push_back( sqrt( pow(data_y[i], 2) + pow(data_y[i+1], 2) ) * sqrt(2 / (this->samplingFrequency * this->samples)));
        }

        std::transform(fft_x.begin(), fft_x.end(), fft_x.begin(), [](float a){ return a * a; });
        std::transform(fft_y.begin(), fft_y.end(), fft_y.begin(), [](float a){ return a * a; });

        std::vector<int> gaps;
        std::vector<int> diffs;
        double delta = pow(10, log10(this->samples/2 - 2)/(this->samples / 2 - 1));
        for(int i = 0; i < (this->samples / 2); i++) {
            gaps.push_back(pow(delta, i));
        }

        for(size_t i = 1; i < gaps.size(); i++) {
            int diff = gaps[i] - gaps[i - 1];
            if(diff > 0)
                diffs.push_back(diff);
        }

        int start = 0;
        int step = 0;
        std::vector<float>::iterator condense_x = fft_x.begin();
        std::vector<float>::iterator condense_y = fft_y.begin();
        for(unsigned i = 0; i < diffs.size(); i++) {
            step = diffs[i];
            xData.push_back( QPointF(i + 1, sqrt( std::accumulate(condense_x + start, condense_x + start + step, 0.0) ) ) );
            yData.push_back( QPointF(i + 1, sqrt( std::accumulate(condense_y + start, condense_y + start + step, 0.0) ) ) );
            start += step;

            max = qMax<float>(xData.last().y(), max);
            max = qMax<float>(yData.last().y(), max);
            min = qMin<float>(xData.last().y(), min);
            min = qMin<float>(yData.last().y(), min);
        }

        for(auto series : this->chart->series()) {
            if(series->attachedAxes().contains(this->yAxis))
                series->detachAxis(this->yAxis);
            if(series->attachedAxes().contains(this->xAxis))
                series->detachAxis(this->xAxis);
            if(!series->attachedAxes().contains(this->yLogAxis))
                series->attachAxis(this->yLogAxis);
            if(!series->attachedAxes().contains(this->xLogAxis))
                series->attachAxis(this->xLogAxis);
        }

        this->yLogAxis->setRange(min, max);
        this->yLogAxis->setTitleText("Amplitudes (um/√Hz)");
        this->xLogAxis->setRange(1, diffs.size());
        this->xLogAxis->setTitleText("Frequency (Hz2)");
        this->yAxis->hide();
        this->xAxis->hide();
        this->yLogAxis->show();
        this->xLogAxis->show();
    }
    else if(ui->cbSignal->currentIndex() == MODE_FFT) {
        if(ui->cbDecimation->currentIndex() == FFT_1_1) {
            if(ui->cbWindow->isChecked()) {
                float delta = (M_PI - -M_PI ) / (this->samples - 1);
                for(int i = 0; i < this->samples; i++) {
                    data_x[i] *= 1 + cos(-M_PI + delta * i);
                    data_y[i] *= 1 + cos(-M_PI + delta * i);
                }
            }
            cv::dft(data_x, data_x);
            cv::dft(data_y, data_y);

            fft_x.push_back(abs(data_x[0]) * sqrt(2 / (this->samplingFrequency * this->samples)));
            fft_y.push_back(abs(data_y[0]) * sqrt(2 / (this->samplingFrequency * this->samples)));
            for(int i = 1; i < this->samples - 2; i += 2) {
                fft_x.push_back( sqrt( pow(data_x[i], 2) + pow(data_x[i+1], 2) ) * sqrt(2 / (this->samplingFrequency * this->samples)));
                fft_y.push_back( sqrt( pow(data_y[i], 2) + pow(data_y[i+1], 2) ) * sqrt(2 / (this->samplingFrequency * this->samples)));
            }
        }
        else { // FFT_10_1
            decimation_factor = 10;
            int decimation = this->samples / decimation_factor;
            std::vector<float> sum_x(decimation / 2, 0);
            std::vector<float> sum_y(decimation / 2, 0);

            fft_x = std::vector<float>(decimation / 2, 0);
            fft_y = std::vector<float>(decimation / 2, 0);
            for(int i = 0; i < this->samples; i += decimation) {
                std::vector<float> sub_x(data_x.begin() + i, data_x.begin() + i + decimation);
                std::vector<float> sub_y(data_y.begin() + i, data_y.begin() + i + decimation);

                if(ui->cbWindow->isChecked()) {
                    float delta = (M_PI - -M_PI ) / (decimation - 1);
                    for(int i = 0; i < decimation; i++) {
                        sub_x[i] *= 1 + cos(-M_PI + delta * i);
                        sub_y[i] *= 1 + cos(-M_PI + delta * i);
                    }
                }

                cv::dft(sub_x, sub_x);
                cv::dft(sub_y, sub_y);

                sum_x[0] += square(sub_x[0]) * (2 / (this->samplingFrequency * this->samples));
                sum_y[0] += square(sub_y[0]) * (2 / (this->samplingFrequency * this->samples));
                for(int i = 1; i < decimation - 2; i += 2) {
                    sum_x[(i - 1) / 2] += (square(sub_x[i]) + square(sub_x[i+1])) * (2 / (this->samplingFrequency * this->samples));
                    sum_y[(i - 1) / 2] += (square(sub_y[i]) + square(sub_y[i+1])) * (2 / (this->samplingFrequency * this->samples));
                }
                sum_x[decimation / 2 - 1] += square(*(sub_x.end() - 1)) * (2 / (this->samplingFrequency * this->samples));
                sum_y[decimation / 2 - 1] += square(*(sub_y.end() - 1)) * (2 / (this->samplingFrequency * this->samples));
            }

            std::transform(sum_x.begin(), sum_x.end(), fft_x.begin(), square_root_float);
            std::transform(sum_y.begin(), sum_y.end(), fft_y.begin(), square_root_float);
        }

        for(int i = 0; i < (int)fft_x.size(); i++) {
            xData.push_back(QPointF(i * decimation_factor, ui->cbSquared->isChecked() ? square(fft_x[i]) : fft_x[i]));
            yData.push_back(QPointF(i * decimation_factor, ui->cbSquared->isChecked() ? square(fft_y[i]) : fft_y[i]));
            max = qMax<float>(xData.last().y(), max);
            max = qMax<float>(yData.last().y(), max);
            min = qMin<float>(xData.last().y(), min);
            min = qMin<float>(yData.last().y(), min);
        }

        for(auto series : this->chart->series()) {
            if(series->attachedAxes().contains(this->yAxis))
                series->detachAxis(this->yAxis);
            if(!series->attachedAxes().contains(this->yLogAxis))
                series->attachAxis(this->yLogAxis);
        }

        this->yLogAxis->setRange(min, max);
        this->yLogAxis->setTitleText("Amplitudes (um/√Hz)");
        this->xAxis->setRange(0, this->samples / 2);
        this->xAxis->setTitleText("Frequency (Hz)");
        this->yAxis->hide();
        this->yLogAxis->show();
    }
    else {
        float item_x;
        float item_y;
        float sum_x = 0;
        float sum_y = 0;
        int index;
        int scale_x = 1;

        for(unsigned i = 0; i < qMin(data_x.size(), data_y.size()); i++) {
            if(ui->cbDecimation->currentIndex() == DECIMATION_1_1) {
                item_x = data_x[i];
                item_y = data_y[i];
                index = i;
            }
            else if(ui->cbDecimation->currentIndex() == DECIMATION_100_1) {
                if(i == 0 || i % 100 != 0) {
                    sum_x += data_x[i];
                    sum_y += data_y[i];
                    continue;
                }
                index = i / 100 - 1;
                item_x = sum_x / 100.0;
                item_y = sum_y / 100.0;
                scale_x = 100;
                sum_x = 0;
                sum_y = 0;
            }
            else { // DECIMATION_DIFF
                if(i == 0)
                    continue;
                index = i - 1;
                item_x = data_x[i] - data_x[i - 1];
                item_y = data_y[i] - data_y[i - 1];
            }

            xData.push_back(QPointF(index / 10.0, item_x));
            yData.push_back(QPointF(index / 10.0, item_y));
            max = qMax(item_x, max);
            max = qMax(item_y, max);
            min = qMin(item_x, min);
            min = qMin(item_y, min);
        }

        for(auto series : this->chart->series()) {
            if(series->attachedAxes().contains(this->yLogAxis))
                series->detachAxis(this->yLogAxis);
            if(series->attachedAxes().contains(this->xLogAxis))
                series->detachAxis(this->xLogAxis);
            if(!series->attachedAxes().contains(this->yAxis))
                series->attachAxis(this->yAxis);
            if(!series->attachedAxes().contains(this->xAxis))
                series->attachAxis(this->xAxis);
        }

        this->yAxis->setRange(min, max);
        this->yAxis->setTitleText("Positions (um)");
        this->xAxis->setRange(0, this->timerPeriod / scale_x);
        this->xAxis->setTitleText("Time (us)");

        this->yLogAxis->hide();
        this->xLogAxis->hide();
        this->yAxis->show();
        this->xAxis->show();
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
    char buffer[12];

    this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    srv.sin_port = htons(8888);
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr("10.4.1.22");
    ::connect(this->sock, (struct sockaddr*) &srv, sizeof(struct sockaddr));

    ::write(sock, "CFCK\n", 5);
    ::read(sock, buffer, 13);
    ::close(sock);

    buffer[11] = '\0';
    this->samplingFrequency = atof(buffer);

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

void MainWindow::on_cbSignal_currentIndexChanged(int index)
{
    if(index == MODE_RAW) {
        ui->cbDecimation->show();
        ui->cbDecimation->clear();
        ui->cbDecimation->addItems({"1:1", "100:1", "Differential"});
        ui->cbWindow->hide();
        ui->cbSquared->hide();
        ui->cbFilter->hide();
        ui->cbLinear->hide();
        ui->cbReverse->hide();
        ui->lblDec->show();
    }
    else if(index == MODE_FFT) {
        ui->cbDecimation->show();
        ui->cbDecimation->clear();
        ui->cbDecimation->addItems({"1:1", "10:1"});
        ui->cbWindow->show();
        ui->cbSquared->show();
        ui->cbFilter->hide();
        ui->cbLinear->hide();
        ui->cbReverse->hide();
        ui->lblDec->show();
    }
    else if(index == MODE_FFT_LOGF) {
        ui->cbDecimation->show();
        ui->cbDecimation->clear();
        ui->cbDecimation->addItems({"1 s", "10 s", "100 s"});
        ui->cbWindow->show();
        ui->cbSquared->hide();
        ui->cbFilter->show();
        ui->cbLinear->hide();
        ui->cbReverse->hide();
        ui->lblDec->hide();
    }
    else {
        ui->cbDecimation->hide();
        ui->cbWindow->hide();
        ui->cbSquared->hide();
        ui->cbFilter->hide();
        ui->cbLinear->show();
        ui->cbReverse->show();
        ui->lblDec->hide();
    }
}
