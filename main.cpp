#include "main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QString configFile = QString(argv[1]);
    if(configFile.isEmpty())
        configFile = ":/fa-config.json";
    MainWindow w(configFile);
    w.show();
    return a.exec();
}
