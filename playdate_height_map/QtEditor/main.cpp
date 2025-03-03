#include "MainWindow.h"

#include <QApplication>
extern "C"
{
#include "display/display.h"
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    initDisplay();

    MainWindow w;
    w.show();
    return a.exec();
}
