#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QSplineSeries>
#include <QtEndian>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#include <iostream>
using std::cout;
using std::endl;

using namespace QT_CHARTS_NAMESPACE;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void reconnectToServer();

private slots:
    void pollServer();

    void on_cbCells_currentIndexChanged(int index);

    void on_cbID_currentIndexChanged(const QString &arg1);

    void on_cbShow_currentIndexChanged(int index);

    void on_cbTime_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;

    QTimer* timer;
    QChart* chart;
    QLineSeries* x_series;
    QLineSeries* y_series;
    QMap<QString, int> idsMap;
    QString format;
    QString message;
    QString ipAddress;

    struct sockaddr_in srv;

    float min;
    float max;
    int sock;
    int cells;
    int bpms;
    int firstID;
    int ids;
    int port;
    int samples;
    int bufferSize;
    int timerPeriod;
};
#endif // MAINWINDOW_H
