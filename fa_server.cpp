#include "fa_server.h"

FastArchiverServer::FastArchiverServer(QString ipAddress, int port, QString configFile, QObject *parent)
    : QObject(parent),
      serverConnected(false)
{
    this->ipAddress = ipAddress;
    this->port = port;
    this->configFile = configFile;

    this->server = new QTcpSocket(this);
    this->server->connectToHost(this->ipAddress, this->port, QIODevice::ReadWrite);
    this->serverConnected = this->server->waitForConnected(1000);
    if(!this->serverConnected)
        return;
    emit connectionChanged(this->serverConnected);

    if(!readSamplingFrequency())
        return;

    readConfiguration();
}

FastArchiverServer::~FastArchiverServer()
{
}

bool FastArchiverServer::readSamplingFrequency()
{
    char buffer[50];
    float frequency;
    bool isOK;

    this->server->write(FA_CMD_CFCK, strlen(FA_CMD_CFCK));
    this->server->waitForReadyRead();
    this->server->readLine(buffer, sizeof(buffer));
    this->server->disconnectFromHost();

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

    reconnectToServer();
    if(!this->serverConnected)
        return;

    this->server->write(FA_CMD_CL, strlen(FA_CMD_CL));
    this->server->waitForReadyRead();
    while(this->server->bytesAvailable() > 0) {
        this->server->readLine(line, sizeof(line));
        QStringList items = QString(line).split(' ');
        bpmIDs.push_back(items.last().trimmed());
    }
}

void FastArchiverServer::reconnectToServer()
{
    bool status;
    this->server->disconnectFromHost();
    this->server->connectToHost(this->ipAddress, this->port, QIODevice::ReadWrite);
    status = this->server->waitForConnected(1000);
    if(this->serverConnected != status) {
        this->serverConnected = status;
        emit connectionChanged(this->serverConnected);
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
