#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <QtEndian>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>

#include <cstdio>
#include <cmath>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>

#include <iostream>
#include <numeric>
using std::cout;
using std::endl;

#include <opencv2/core/core.hpp>

using namespace QT_CHARTS_NAMESPACE;

#define MAX_BUFFER_SIZE (80000)

#define MODE_RAW            0
#define MODE_FFT            1
#define MODE_FFT_LOGF       2
#define MODE_INTEGRATED     3

#define DECIMATION_1_1      0
#define DECIMATION_100_1    1
#define DECIMATION_DIFF     2

#define FFT_1_1     0
#define FFT_10_1    1

#define FA_CMD_CF       "CF\n"
#define FA_CMD_CL       "CL\n"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QString configFile, QWidget *parent = nullptr);
    ~MainWindow();

    void reconnectToServer();

    void initSocket();

    QString resolveHostname(QString hostname);

    void readFrequency();

    void computeFFT(std::vector<float>& data_x, std::vector<float>& data_y, std::vector<float>& fft_x, std::vector<float>& fft_y);

    std::tuple<float, float> calculateLimits(float a, float b, float& min, float& max);

    void modifyAxes(std::tuple<QAbstractAxis*, QAbstractAxis*> useAxes, std::tuple<QAbstractAxis*,
                    QAbstractAxis*> hideAxes, std::tuple<float, float> rangeX, std::tuple<float, float> rangeY, QStringList axesTitles);

private slots:
    void pollServer();

    void on_cbCells_currentIndexChanged(int index);

    void on_cbID_currentIndexChanged(const QString &arg1);

    void on_cbShow_currentIndexChanged(int index);

    void on_cbTime_currentIndexChanged(int index);

    void on_cbSignal_currentIndexChanged(int index);

    void on_cbDecimation_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;

    QTimer* timer;
    QChart* chart;
    QLineSeries* x_series;
    QLineSeries* y_series;
    QValueAxis* xAxis;
    QValueAxis* yAxis;
    QLogValueAxis* yLogAxis;
    QLogValueAxis* xLogAxis;
    QMap<QString, int> idsMap;
    QString format;
    QString message;
    QString ipAddress;
    QStringList bpmIDs;

    std::vector<float> fft_logf_x;
    std::vector<float> fft_logf_y;

    struct sockaddr_in srv;

    float samplingFrequency;
    int sock;
    int cells;
    int bpms;
    int firstID;
    int ids;
    int port;
    int samples;
    int bufferSize;
    int timerPeriod;
    bool resetLogFilter;
    float logFilter;
};
#endif // MAINWINDOW_H
