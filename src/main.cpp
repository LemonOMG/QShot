#include <QApplication>
#include "app/ShotApplication.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Do not quit when the last window is closed, as this is a tray application
    QApplication::setQuitOnLastWindowClosed(false);

    // QSettings derives its storage location from these two, so they have to be
    // set before the first Settings access. On Windows that yields
    // HKEY_CURRENT_USER\Software\QShot\QShot.
    QCoreApplication::setOrganizationName(QStringLiteral("QShot"));
    QCoreApplication::setApplicationName(QStringLiteral("QShot"));

    qshot::ShotApplication shotApp;

    return app.exec();
}
