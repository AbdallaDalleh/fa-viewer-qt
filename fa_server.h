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
#include <QtConcurrent>

#include <iostream>
using std::cout;
using std::endl;

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SAMPLING_RATE   10000
#define MAX_TIMEBASE    50
#define MAX_BUFFER_SIZE (SAMPLING_RATE * MAX_TIMEBASE)
#define DEFAULT_PORT    8888
#define DEFAULT_CONFIG  ":/fa-config.json"
#define FA_CMD_CF       "CF\n"
#define FA_CMD_CL       "CL\n"

#define MODE_RAW            0
#define MODE_FFT            1
#define MODE_FFT_LOGF       2
#define MODE_INTEGRATED     3

typedef enum {
    RawData = 0,
    FFT,
    LogarithmicFFT,
    Integrated
} signal_mode_t;

typedef enum {
    RawData_1_1 = 0,
    RawData_100_1,
    RawData_Dec,
    FFT_1_1,
    FFT_10_1
} decimation_mode_t;

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
    void setSignalMode(signal_mode_t mode);

    QStringList  getIDsList();
    QVector<int> getConfiguration();

    float samplingFrequency;

    std::vector<float> internalBufferX;
    std::vector<float> internalBufferY;
    std::vector<float> outputBufferX;
    std::vector<float> outputBufferY;

signals:
    void connectionChanged(bool status);
    void dataReady();

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
    float timeBase;
    QString format;
    QStringList bpmIDs;
    QTimer* mainTimer;
    signal_mode_t mode;
    decimation_mode_t decimation;
};

#endif // FASTARCHIVERSERVER_H
