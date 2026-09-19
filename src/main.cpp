#include <QApplication>
#include "app/ShotApplication.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Do not quit when the last window is closed, as this is a tray application
    QApplication::setQuitOnLastWindowClosed(false);

    qshot::ShotApplication shotApp;

    return app.exec();
}
