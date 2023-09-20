#ifndef FASTARCHIVERSERVER_H
#define FASTARCHIVERSERVER_H

#include <QObject>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

#include <iostream>
using std::cout;
using std::endl;

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SAMPLING_RATE   10000
#define MAX_TIMEBASE    50
#define MAX_BUFFER_SIZE (2 * sizeof(float) * SAMPLING_RATE * MAX_TIMEBASE)
#define DEFAULT_PORT    8888
#define DEFAULT_CONFIG  ":/fa-config.json"
#define FA_CMD_CF       "CF\n"
#define FA_CMD_CL       "CL\n"

class FastArchiverServer : public QObject
{
    Q_OBJECT

public:
    explicit FastArchiverServer(QString ipAddress, int port = DEFAULT_PORT, QString configFile = DEFAULT_CONFIG, QObject *parent = nullptr);
    ~FastArchiverServer();

    bool initializeConnection();
    bool isConnected();
    void readConfiguration();
    void reconnectToServer(bool requestData = false);
    void setBPMID(int id);
    void setTimeBase(float timeBase);

    QStringList  getIDsList();
    QVector<int> getConfiguration();

    float samplingFrequency;

signals:
    void connectionChanged(bool status);

private slots:
    void mainLoop();

private:
    QString configFile;
    QString ipAddress;
    sockaddr_in srv;
    int server;
    int port;
    int cells;
    int bpms;
    int firstID;
    int ids;
    int id;
    int bufferSize;
    bool serverConnected;
    QString format;
    QStringList bpmIDs;
    QTimer* mainTimer;

    std::vector<float> internalBufferX;
    std::vector<float> internalBufferY;
};

#endif // FASTARCHIVERSERVER_H
