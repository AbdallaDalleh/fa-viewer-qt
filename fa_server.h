#ifndef FASTARCHIVERSERVER_H
#define FASTARCHIVERSERVER_H

#include <QObject>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include <iostream>
using std::cout;
using std::endl;

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define DEFAULT_PORT    8888
#define DEFAULT_CONFIG  ":/fa-config.json"
#define FA_CMD_CFCK     "CFCK\n"
#define FA_CMD_CL       "CL\n"

class FastArchiverServer : public QObject
{
    Q_OBJECT
public:
    explicit FastArchiverServer(QString ipAddress, int port = DEFAULT_PORT, QString configFile = DEFAULT_CONFIG, QObject *parent = nullptr);
    ~FastArchiverServer();

    bool readSamplingFrequency();
    bool isConnected();
    void readConfiguration();
    void reconnectToServer();
    QStringList getIDsList();
    QVector<int> getConfiguration();


    float samplingFrequency;

signals:
    void connectionChanged(bool status);

private:
    QString configFile;
    QString ipAddress;
    int port;
    int cells;
    int bpms;
    int firstID;
    int ids;
    bool serverConnected;
    QString format;
    QTcpSocket* server;
    QStringList bpmIDs;
};

#endif // FASTARCHIVERSERVER_H
