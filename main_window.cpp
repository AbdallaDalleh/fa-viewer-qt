#include "main_window.h"
#include "ui_main_window.h"

#pragma pack(4)

MainWindow::MainWindow(QString configFile, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    QFile file(configFile);
    if(configFile.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Could not open FA configuration file.", QMessageBox::Ok);
        ::exit(1);
    }

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
    this->yAxis->setTickType(QValueAxis::TicksFixed);
    this->yAxis->setMinorTickCount(2);
    this->yAxis->applyNiceNumbers();
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

    QFont font;
    font.setBold(true);
    font.setPixelSize(18);
    chart->setTitleFont(font);
    chart->legend()->show();

    ui->plot->setChart(chart);
    ui->plot->setRenderHint(QPainter::Antialiasing);

    this->timer = new QTimer(this);
    this->timer->setInterval(1000);
    QObject::connect(this->timer, &QTimer::timeout, this, &MainWindow::pollServer);
    QObject::connect(ui->btnConnect, &QPushButton::clicked, this, &MainWindow::reconnectToServer);

    QString id;
    QTextStream config(&file);
    QJsonDocument faConfig = QJsonDocument::fromJson(QString(config.readAll()).toUtf8());
    QJsonObject object = faConfig.object();
    if(object.value("ip_address").isNull() || object.value("port").isNull() ||
       object.value("id_format").isNull()  || object.value("ids").isNull()  ||
       object.value("bpms_cell").isNull()  || object.value("cells").isNull() ||
       object.value("first_id").isNull())
    {
        QMessageBox::warning(this, "Error", "Error parsing configuration file.", QMessageBox::Ok);
        ::exit(6);
    }

    this->cells   = object.value("cells").toInt();
    this->bpms    = object.value("bpms_cell").toInt();
    this->format  = object.value("id_format").toString();
    this->firstID = object.value("first_id").toInt();
    this->ids     = object.value("ids").toInt();
    this->ipAddress = object.value("ip_address").toString();
    this->port    = object.value("port").toInt();

    ui->txtBPM->setVisible(false);
    ui->txtBPM->setValidator(new QIntValidator(this->firstID, this->firstID + this->ids - 1));

    int status;
    char buffer[1600];
    ssize_t bytes;
    struct pollfd fds[1];
    fds[0].fd = this->sock;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    initSocket();
    ::write(this->sock, FA_CMD_CL, strlen(FA_CMD_CL));
    status = ::poll(fds, 1, 1000);
    if(status <= 0 && errno != 0 && errno != EINPROGRESS)
    {
        QMessageBox::warning(this, "Error", "Socket I/O Error: " + QString(strerror(errno)));
        ::close(this->sock);
        ::exit(5);
    }

    bytes = ::read(this->sock, buffer, sizeof(buffer));
    if(bytes >= 0) {
        buffer[bytes] = '\0';
        QString lines(buffer);
        for(QString item : lines.split('\n')) {
            if(!item.isEmpty())
                this->bpmIDs.push_back(item.split(' ').last());
        }
    }

    int currentID = this->firstID;
    for(int cell = 1; cell <= this->cells; cell++) {
        if(currentID > this->ids)
            break;

        ui->cbCells->addItem("Cell " + QString::number(cell));
        QStringList subIDs = this->bpmIDs.filter(QString::asprintf("C%02d", cell));
        for(QString item : subIDs) {
            id = QString().asprintf(this->format.toStdString().c_str(), cell, currentID, subIDs.indexOf(item) + 1);
            this->idsMap.insert(id, currentID++);
        }
    }

    this->resetLogFilter = true;

    ui->cbCells->setCurrentIndex(1);
    ui->cbTime->setCurrentIndex(3);
    ui->cbSignal->setCurrentText(0);
    on_cbSignal_currentIndexChanged(0);
    readFrequency();
    reconnectToServer();
}

MainWindow::~MainWindow()
{
    ::close(sock);
    delete ui;
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
    float min;
    float max;
    char* data;
    QVector<QPointF> xData;
    QVector<QPointF> yData;
    std::vector<float> data_x;
    std::vector<float> data_y;
    std::vector<float> fft_x;
    std::vector<float> fft_y;
    struct pollfd fds[1];
    int status;

    data = new char[bufferSize];
    this->x_series->clear();
    this->y_series->clear();
    max = std::numeric_limits<float>::min();
    min = std::numeric_limits<float>::max();

    fds[0].fd = this->sock;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    status = ::poll(fds, 1, 1000 * this->timerPeriod);
    if(status <= 0 && errno != 0 && errno != EINPROGRESS)
    {
        this->statusBar()->showMessage( QString::asprintf("FA Server disconnected. Code %d: %s", errno, strerror(errno)) );
        ::close(this->sock);
        return;
    }

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

    this->statusBar()->showMessage("FA Server Running ...");

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
    auto sum = [](float a, float b) { return a*a + b; };

    if(std::all_of(data_x.begin(), data_x.end(), compare_zero) &&
       std::all_of(data_y.begin(), data_y.end(), compare_zero)) {
        if(!this->chart->title().endsWith("(NC)"))
            this->chart->setTitle(this->chart->title() + " (NC)");
        return;
    }

    if(ui->cbSignal->currentIndex() == MODE_FFT_LOGF) {
        std::vector<float> fft_raw_x;
        std::vector<float> fft_raw_y;
        computeFFT(data_x, data_y, fft_raw_x, fft_raw_y);
        std::transform(fft_raw_x.begin(), fft_raw_x.end(), fft_raw_x.begin(), square);
        std::transform(fft_raw_y.begin(), fft_raw_y.end(), fft_raw_y.begin(), square);

        std::vector<int> gaps;
        std::vector<int> diffs;
        double delta = pow(10, log10(this->samples/2 - 2)/(this->samples / 2 - 1));
        for(int i = 0; i < (this->samples / 2); i++) {
            gaps.push_back(pow(delta, i));
        }

        for(size_t i = 1; i < gaps.size(); i++) {
            int diff = gaps[i] - gaps[i - 1];
            if(diff > 0) {
                diffs.push_back(diff);
            }
        }

        int start = 0;
        int step = 0;
        std::vector<float>::iterator condense_x = fft_raw_x.begin();
        std::vector<float>::iterator condense_y = fft_raw_y.begin();
        for(unsigned i = 0; i < diffs.size(); i++) {
            step = diffs[i];

            // xData.push_back( QPointF(i + 1, sqrt( std::accumulate(condense_x + start, condense_x + start + step, 0.0) ) ) );
            // yData.push_back( QPointF(i + 1, sqrt( std::accumulate(condense_y + start, condense_y + start + step, 0.0) ) ) );
            fft_x.push_back(std::sqrt( std::accumulate(condense_x + start, condense_x + start + step, 0.0) ));
            fft_y.push_back(std::sqrt( std::accumulate(condense_y + start, condense_y + start + step, 0.0) ));

            start += step;

            // std::tie(min, max) = calculateLimits(xData.last().y(), yData.last().y(), min, max);
        }

        if (ui->cbFilter->isChecked()) {
            std::vector<float> p_sum = std::vector<float>(diffs.size(), 0);
            std::partial_sum(diffs.begin(), diffs.end(), p_sum.begin());
            std::transform(p_sum.begin(), p_sum.end(), p_sum.begin(), [this](float a) { return this->samplingFrequency * a / this->samples; });
            std::transform(fft_x.begin(), fft_x.end(), p_sum.begin(), fft_x.begin(), std::multiplies<float>());
            std::transform(fft_y.begin(), fft_y.end(), p_sum.begin(), fft_y.begin(), std::multiplies<float>());
        }

        // fft_logf is "self.history"
        if(this->logFilter == 1) {
            // Do nothing, return FFT as it is.
        }
        else if(this->resetLogFilter) {
            this->resetLogFilter = false;
            fft_logf_x = std::vector<float>(fft_x.size(), 0);
            fft_logf_y = std::vector<float>(fft_y.size(), 0);
            std::transform(fft_x.begin(), fft_x.end(), fft_logf_x.begin(), square);
            std::transform(fft_y.begin(), fft_y.end(), fft_logf_y.begin(), square);
        }
        else {
            std::transform(fft_x.begin(), fft_x.end(), fft_logf_x.begin(), fft_x.begin(),
                           [this] (float a, float b) { return std::sqrt(this->logFilter * a * a + (1 - this->logFilter) * b); });
            std::transform(fft_y.begin(), fft_y.end(), fft_logf_y.begin(), fft_y.begin(),
                           [this] (float a, float b) { return std::sqrt(this->logFilter * a * a + (1 - this->logFilter) * b); });
        }

        for(int i = 1; i < qMin<int>(fft_x.size(), fft_y.size()); i++) {
            xData.push_back(QPointF(i, fft_x[i]));
            yData.push_back(QPointF(i, fft_y[i]));
            std::tie(min, max) = calculateLimits(xData.last().y(), yData.last().y(), min, max);
        }

        modifyAxes({xLogAxis, yLogAxis}, {xAxis, yAxis}, {1, diffs.size()}, {min, max}, {"Frequency (Hz)", "Amplitudes (um/√Hz)"});
    }
    else if(ui->cbSignal->currentIndex() == MODE_FFT) {
        if(ui->cbDecimation->currentIndex() == FFT_1_1) {
            computeFFT(data_x, data_y, fft_x, fft_y);
        }
        else { // FFT_10_1
            decimation_factor = 10;
            int decimation = this->samples / decimation_factor;
            std::vector<float> sum_x(decimation / 2, 0);
            std::vector<float> sum_y(decimation / 2, 0);
            std::vector<float> fft_mag_x;
            std::vector<float> fft_mag_y;

            for(int i = 0; i < this->samples; i += decimation) {
                std::vector<float> sub_x(data_x.begin() + i, data_x.begin() + i + decimation);
                std::vector<float> sub_y(data_y.begin() + i, data_y.begin() + i + decimation);

                computeFFT(sub_x, sub_y, fft_mag_x, fft_mag_y);
                std::transform(fft_mag_x.begin(), fft_mag_x.end(), sum_x.begin(), sum_x.begin(), sum);
                std::transform(fft_mag_y.begin(), fft_mag_y.end(), sum_y.begin(), sum_y.begin(), sum);
            }

            fft_x.clear();
            fft_y.clear();
            for(auto item : sum_x)
                fft_x.push_back(square_root_float(item));
            for(auto item : sum_y)
                fft_y.push_back(square_root_float(item));
        }

        for(int i = 0; i < (int)fft_x.size(); i++) {
            xData.push_back(QPointF(i * decimation_factor, ui->cbSquared->isChecked() ? square(fft_x[i]) : fft_x[i]));
            yData.push_back(QPointF(i * decimation_factor, ui->cbSquared->isChecked() ? square(fft_y[i]) : fft_y[i]));
            std::tie(min, max) = calculateLimits(xData.last().y(), yData.last().y(), min, max);
        }

        modifyAxes({xAxis, yLogAxis}, {xLogAxis, yAxis}, {0, this->samples / 2}, {min, max}, {"Frequencies (Hz)", ui->cbSquared->isChecked() ? "Amplitudes (um^2/Hz)" : "Amplitudes (um/√Hz)"});
    }
    else if(ui->cbSignal->currentIndex() == MODE_INTEGRATED)
    {
        std::vector<float> fft_raw_x;
        std::vector<float> fft_raw_y;
        std::vector<int> gaps;
        std::vector<int> diffs;
        double delta = pow(10, log10(this->samples/2 - 2)/(this->samples / 2 - 1));
        for(int i = 0; i < (this->samples / 2) - 1; i++) {
            gaps.push_back(pow(delta, i));
        }

        for(size_t i = 1; i < gaps.size(); i++) {
            int diff = gaps[i] - gaps[i - 1];
            if(diff > 0) {
                diffs.push_back(diff);
            }
        }

        std::cout << "Gaps: " << gaps.size() << std::endl;
        std::cout << "Diffs: " << diffs.size() << std::endl;

        computeFFT(data_x, data_y, fft_raw_x, fft_raw_y);
        std::transform(fft_raw_x.begin(), fft_raw_x.end(), fft_raw_x.begin(), square);
        std::transform(fft_raw_y.begin(), fft_raw_y.end(), fft_raw_y.begin(), square);

        int start = 0;
        int step = 0;
        std::vector<float>::iterator condense_x = fft_raw_x.begin() + 2;
        std::vector<float>::iterator condense_y = fft_raw_y.begin() + 2;
        for(unsigned i = 0; i < diffs.size(); i++) {
            step = diffs[i];

            // xData.push_back( QPointF(i + 1, sqrt( std::accumulate(condense_x + start, condense_x + start + step, 0.0) ) ) );
            // yData.push_back( QPointF(i + 1, sqrt( std::accumulate(condense_y + start, condense_y + start + step, 0.0) ) ) );
            fft_x.push_back( std::accumulate(condense_x + start, condense_x + start + step, 0.0) );
            fft_y.push_back( std::accumulate(condense_y + start, condense_y + start + step, 0.0) );

            start += step;

            // std::tie(min, max) = calculateLimits(xData.last().y(), yData.last().y(), min, max);
        }

        size_t N = qMin<size_t>(data_x.size(), data_y.size());
        std::vector<float> sum_x(fft_x.size(), 0);
        std::vector<float> sum_y(fft_y.size(), 0);

        std::partial_sum(fft_x.begin(), fft_x.end(), sum_x.begin());
        std::partial_sum(fft_y.begin(), fft_y.end(), sum_y.begin());

        std::transform(sum_x.begin(), sum_x.end(), sum_x.begin(), [this, N](float a) { return std::sqrt(this->samplingFrequency / N * a); });
        std::transform(sum_y.begin(), sum_y.end(), sum_y.begin(), [this, N](float a) { return std::sqrt(this->samplingFrequency / N * a); });

        for(int i = 1; i < qMin<int>(sum_x.size(), sum_y.size()); i++) {
            xData.push_back(QPointF(i, sum_x[i]));
            yData.push_back(QPointF(i, sum_y[i]));
            std::tie(min, max) = calculateLimits(xData.last().y(), yData.last().y(), min, max);
        }

        modifyAxes({xLogAxis, yLogAxis}, {xAxis, yAxis}, {1, diffs.size()}, {min, max}, {"Frequency (Hz)", "Cumulative Amplitudes (um/√Hz)"});
    }
    else {
        float item_x;
        float item_y;
        float sum_x = 0;
        float sum_y = 0;
        int index;

        for(unsigned i = 0; i < qMin(data_x.size(), data_y.size()); i++) {
            if(ui->cbDecimation->currentIndex() == DECIMATION_1_1) {
                item_x = data_x[i];
                item_y = data_y[i];
                index = i;
            }
            else if(ui->cbDecimation->currentText() == "100:1") {
                if(i == 0 || i % 100 != 0) {
                    sum_x += data_x[i];
                    sum_y += data_y[i];
                    continue;
                }
                // index = i / 100 - 1;
                index = i;
                item_x = sum_x / 100.0;
                item_y = sum_y / 100.0;
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
            std::tie(min, max) = calculateLimits(item_x, item_y, min, max);
        }

        modifyAxes({xAxis, yAxis}, {xLogAxis, yLogAxis}, {0, timerPeriod}, {min, max}, {"Time (ms)", "Positions (um)"});
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
    ui->cbID->setVisible(index > 0);
    ui->txtBPM->setVisible(index <= 0);

    if(index > 0) {
        item = QString().asprintf("SRC%02d", index);
        for(QString key : this->idsMap.keys()) {
            if(key.startsWith(item))
                ui->cbID->addItem(key);
        }
    }
    else {
        on_txtBPM_returnPressed();
    }
}

void MainWindow::on_cbID_currentIndexChanged(const QString &arg1)
{
    int currentID;

    this->chart->setTitle(arg1);
    currentID = this->idsMap[arg1];
    this->message = "S" + QString::number(currentID) + "\n";
    this->timer->stop();
    ::close(this->sock);
    reconnectToServer();
}

void MainWindow::reconnectToServer()
{
    char c;

    initSocket();
    ::write(sock, message.toStdString().c_str(), message.length());
    ::read(sock, &c, 1);

    if (c != 0) {
        this->statusBar()->showMessage("FA Server: No data currently available");
        return;
    }

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

    if(index < 3) {
        ui->cbDecimation->clear();
        if(ui->cbSignal->currentIndex() == MODE_RAW)
            ui->cbDecimation->addItems({"1:1", "Differential"});
        else if(ui->cbSignal->currentIndex() == MODE_FFT)
            ui->cbDecimation->addItems({"1:1"});
        else if(ui->cbSignal->currentIndex() == MODE_FFT_LOGF)
            ui->cbDecimation->addItems({"1 s"});
        else
            ui->cbDecimation->hide();
    }
    else {
        on_cbSignal_currentIndexChanged( ui->cbSignal->currentIndex() );
    }

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
    ui->cbTime->setCurrentIndex(3);
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
        ui->lblDec->setText("Decimation");
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
        ui->lblDec->setText("Decimation");
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
        ui->lblDec->show();
        ui->lblDec->setText("Filter");
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

std::tuple<float, float> MainWindow::calculateLimits(float a, float b, float &min, float &max)
{
    max = qMax(a, max);
    max = qMax(b, max);
    min = qMin(a, min);
    min = qMin(b, min);
    return std::make_tuple(min, max);
}

void MainWindow::modifyAxes(std::tuple<QAbstractAxis *, QAbstractAxis *> useAxes,
                            std::tuple<QAbstractAxis *, QAbstractAxis *> hideAxes,
                            std::tuple<float, float> rangeX, std::tuple<float, float> rangeY, QStringList axesTitles)
{
    //
    // Other options for unpacking tuples:
    //  1. std::tie with predefined variables
    //  2. std::get<index>(tuple)
    //

    auto[minX, maxX] = rangeX;
    auto[minY, maxY] = rangeY;
    auto[useXAxis, useYAxis] = useAxes;
    auto[hideXAxis, hideYAxis] = hideAxes;

    for(auto series : this->chart->series()) {
        if(series->attachedAxes().contains(hideYAxis))
            series->detachAxis(hideYAxis);
        if(series->attachedAxes().contains(hideXAxis))
            series->detachAxis(hideXAxis);
        if(!series->attachedAxes().contains(useYAxis))
            series->attachAxis(useYAxis);
        if(!series->attachedAxes().contains(useXAxis))
            series->attachAxis(useXAxis);
    }

    useXAxis->setRange(minX, maxX);
    useXAxis->setTitleText(axesTitles[0]);
    useYAxis->setRange(minY, maxY);
    useYAxis->setTitleText(axesTitles[1]);
    ((QValueAxis*)useYAxis)->applyNiceNumbers();

    hideYAxis->hide();
    hideXAxis->hide();
    useYAxis->show();
    useXAxis->show();
}

void MainWindow::computeFFT(std::vector<float> &data_x, std::vector<float> &data_y, std::vector<float> &fft_x, std::vector<float> &fft_y)
{
    size_t samples = qMin<size_t>(data_x.size(), data_y.size());

    if(ui->cbWindow->isChecked()) {
        float delta = (M_PI - -M_PI ) / (samples - 1);
        for(size_t i = 0; i < samples; i++) {
            data_x[i] *= 1 + cos(-M_PI + delta * i);
            data_y[i] *= 1 + cos(-M_PI + delta * i);
        }
    }
    cv::dft(data_x, data_x);
    cv::dft(data_y, data_y);

    fft_x.clear();
    fft_y.clear();
    fft_x.push_back(abs(data_x[0]) * sqrt(2 / (this->samplingFrequency * samples)));
    fft_y.push_back(abs(data_y[0]) * sqrt(2 / (this->samplingFrequency * samples)));
    for(int i = 1; i < qMin<int>(data_x.size(), data_y.size()) - 2; i += 2) {
        fft_x.push_back( std::abs( std::complex<float>(data_x[i], data_x[i+1]) ) * sqrt(2 / (this->samplingFrequency * samples)));
        fft_y.push_back( std::abs( std::complex<float>(data_y[i], data_y[i+1]) ) * sqrt(2 / (this->samplingFrequency * samples)));
    }
}

void MainWindow::initSocket()
{
    int args;
    int status;
    struct timeval tv;
    fd_set read_fds;
    fd_set write_fds;
    QString ip = resolveHostname(this->ipAddress);
    if(ip.isEmpty())
    {
        QMessageBox::warning(this, "Error", "Could not resolve hostname/IP " + this->ipAddress, QMessageBox::Ok);
        ::exit(1);
    }

    this->sock = socket(AF_INET, SOCK_STREAM, 0);
    srv.sin_port = htons(this->port);
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(ip.toStdString().c_str());

    args  = fcntl(this->sock, F_GETFL, NULL);
    args |= O_NONBLOCK;
    status = ::fcntl(this->sock, F_SETFL, args);
    status = ::connect(this->sock, (struct sockaddr*) &srv, sizeof(struct sockaddr));
    if(status < 0) {
        if(errno == EINPROGRESS)
        {
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);
            FD_SET(this->sock, &read_fds);
            FD_SET(this->sock, &write_fds);
            status = ::select(this->sock + 1, &read_fds, &write_fds, NULL, &tv);
            if(status <= 0 && errno != EINTR)
            {
                QMessageBox::warning(this, "Error", "Server connection timeout.");
                ::exit(4);
            }
            else {
                ::close(this->sock);
                this->sock = socket(AF_INET, SOCK_STREAM, 0);
                srv.sin_port = htons(this->port);
                srv.sin_family = AF_INET;
                srv.sin_addr.s_addr = inet_addr(ip.toStdString().c_str());
                args = fcntl(this->sock, F_GETFL, NULL);
                args &= ~(O_NONBLOCK);
                status = fcntl(this->sock, F_SETFL, args);
                status = ::connect(this->sock, (struct sockaddr*) &srv, sizeof(struct sockaddr));
            }
        }
    }
}

void MainWindow::readFrequency()
{
    char buffer[12];

    initSocket();
    ::write(sock, FA_CMD_CF, strlen(FA_CMD_CF));
    ::read(sock, buffer, sizeof(buffer));
    ::close(sock);

    buffer[11] = '\0';
    this->samplingFrequency = atof(buffer);
}

void MainWindow::on_cbDecimation_currentIndexChanged(int index)
{
    if(ui->cbSignal->currentIndex() == MODE_FFT_LOGF) {
        this->resetLogFilter = true;
        this->logFilter = 1.0 / std::pow(10, index);
    }
}

QString MainWindow::resolveHostname(QString hostname)
{
    int status;
    char ip[16];
    struct addrinfo hints;
    struct addrinfo *info;
    struct sockaddr_in* host;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo(hostname.toStdString().c_str(), "http", &hints, &info);
    if(status != 0)
        return "";

    host = (struct sockaddr_in*) info->ai_addr;
    strncpy(ip, inet_ntoa(host->sin_addr), sizeof(ip));
    freeaddrinfo(info);
    return QString(ip);
}

void MainWindow::on_txtBPM_returnPressed()
{
    int id;

    id = ui->txtBPM->text().toInt();
    if(id >= this->firstID && id <= (this->firstID + this->ids - 1))
    {
        for(auto item : this->idsMap.toStdMap()) {
            if(item.second == id)
                this->chart->setTitle(item.first);
        }

        this->message = "S" + QString::number(ui->txtBPM->text().toInt()) + "\n";
        this->timer->stop();
        ::close(this->sock);
        reconnectToServer();
    }
}
