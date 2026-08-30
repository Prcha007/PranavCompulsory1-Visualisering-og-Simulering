#include "MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    // window.move(200, 100);   //moves the program to a spesific place on screen
    window.show();
    window.start();
    return app.exec();
}
