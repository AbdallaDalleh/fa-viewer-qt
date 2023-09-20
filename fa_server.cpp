#include "fa_server.h"

FastArchiverServer::FastArchiverServer(QString ipAddress, int port, QString configFile, QObject *parent)
    : QObject(parent),
      serverConnected(false)
{
    this->ipAddress = ipAddress;
    this->port = port;
    this->configFile = configFile;

    if(!initializeConnection())
        return;

    this->samplingFrequency = -1;
    if(!initializeConnection())
        return;
    readConfiguration();

    this->mainTimer = new QTimer(this);
    QObject::connect(this->mainTimer, &QTimer::timeout, this, &FastArchiverServer::mainLoop);

    setBPMID(2);
    setTimeBase(0.5);
    reconnectToServer(true);
    mainLoop();
}

FastArchiverServer::~FastArchiverServer()
{
}

bool FastArchiverServer::initializeConnection()
{
    int status;
    char buffer[50];
    float frequency;
    bool isOK;

    this->server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    srv.sin_port = htons(8888);
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(this->ipAddress.toStdString().c_str());
    this->serverConnected = ::connect(this->server, (struct sockaddr*) &srv, sizeof(struct sockaddr)) == 0;
    if(!this->serverConnected)
        return false;
    emit connectionChanged(this->serverConnected);

    status = ::write(this->server, FA_CMD_CF, strlen(FA_CMD_CF));
    if(status <= 0)
        return false;

    status = ::read(this->server, buffer, sizeof(buffer));
    if(status <= 0)
        return false;

    status = ::close(this->server);
    this->serverConnected = false;
    if(status != 0)
        return false;

    frequency = QString(buffer).toFloat(&isOK);
    if(isOK)
        this->samplingFrequency = frequency;

    return isOK;
}

void FastArchiverServer::readConfiguration()
{
    QFile file(this->configFile);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(0 , "Error", "Could not open FA configuration file.", QMessageBox::Ok);
    }

    char line[30];
    QTextStream config(&file);
    QJsonDocument faConfig = QJsonDocument::fromJson(QString(config.readAll()).toUtf8());
    QJsonObject object = faConfig.object();
    this->cells   = object.value("cells").toInt();
    this->bpms    = object.value("bpms_cell").toInt();
    this->format  = object.value("id_format").toString();
    this->firstID = object.value("first_id").toInt();
    this->ids     = object.value("ids").toInt();

    this->bpmIDs.clear();
    this->bpmIDs.reserve(0);

//    reconnectToServer(false);
//    if(!this->serverConnected)
//        return;
//
//    ::write(this->server, FA_CMD_CL, strlen(FA_CMD_CL));
//    while(this->server->bytesAvailable() > 0) {
//        this->server->readLine(line, sizeof(line));
//        QStringList items = QString(line).split(' ');
//        bpmIDs.push_back(items.last().trimmed());
//    }
//    this->server->disconnectFromHost();
}

void FastArchiverServer::reconnectToServer(bool requestData)
{
    int status;
    char xx;
    QString message = "S" + QString::number(this->id) + "\n";

    if(this->serverConnected) {
        status = ::close(this->server);
        if(status != 0)
            return;
    }

    this->server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    srv.sin_port = htons(8888);
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr(this->ipAddress.toStdString().c_str());
    status = ::connect(this->server, (struct sockaddr*) &srv, sizeof(struct sockaddr)) == 0;
    if(!status)
        return;

    if(this->serverConnected != status) {
        this->serverConnected = status;
        emit connectionChanged(this->serverConnected);
    }

    if(requestData) {
        this->mainTimer->stop();
        status = ::write(this->server, message.toStdString().c_str(), message.size());
        read(this->server, &xx, 1);
        this->mainTimer->start();
    }
}

QStringList FastArchiverServer::getIDsList()
{
    return this->bpmIDs;
}

QVector<int> FastArchiverServer::getConfiguration()
{
    return {this->cells, this->bpms, this->ids, this->firstID};
}

bool FastArchiverServer::isConnected()
{
    return this->serverConnected;
}

void FastArchiverServer::setBPMID(int id)
{
    if(id > this->ids)
        this->id = this->ids;
    else if(id < 0)
        this->id = 1;
    else
        this->id = id;
}

void FastArchiverServer::setTimeBase(float timeBase)
{
    float period;

    if(timeBase >= 1)
        period = 1;
    else
        period = timeBase;

    this->bufferSize = period * SAMPLING_RATE * 2 * sizeof(float);
    this->mainTimer->setInterval(period * 1000);
}

void FastArchiverServer::mainLoop()
{
    int size = 0;
    char* buffer;
    int32_t raw_x;
    int32_t raw_y;
    float value_x;
    float value_y;
    int i;

    buffer = new char[this->bufferSize];
    size = ::recv(this->server, buffer, this->bufferSize, MSG_WAITALL);
    for(i = 0; i < size; i += 8) {
        memcpy(&raw_x, buffer + i, sizeof(int32_t));
        memcpy(&raw_y, buffer + i + 4, sizeof(int32_t));

        value_x = (raw_x) / 1000.0;
        value_y = (raw_y) / 1000.0;

        if(this->internalBufferX.size() >= MAX_BUFFER_SIZE / 20)
            this->internalBufferX.erase(this->internalBufferX.begin());
        this->internalBufferX.push_back(value_x);

        if(this->internalBufferY.size() >= MAX_BUFFER_SIZE / 20)
            this->internalBufferY.erase(this->internalBufferY.begin());
        this->internalBufferY.push_back(value_y);
    }
    delete [] buffer;
}
