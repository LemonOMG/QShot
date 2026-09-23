#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <memory>
#include "app/ShotApplication.h"
#include "core/PlatformFactory.h"
#include "core/Strings.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Do not quit when the last window is closed, as this is a tray application
    QApplication::setQuitOnLastWindowClosed(false);

    // QSettings derives its storage location from these two, so they have to be
    // set before the first Settings access. On Windows that yields
    // HKEY_CURRENT_USER\Software\QShot\QShot.
    QCoreApplication::setOrganizationName(QStringLiteral("QShot"));
    QCoreApplication::setApplicationName(QStringLiteral("QShot"));

    // One instance only, and taken before anything else is built: a second copy would
    // fail to register the same global hotkey, then sit in the tray looking like a
    // working QShot whose shortcut is inexplicably taken, retrying forever and racing
    // the first copy for the same history directory.
    //
    // The guard is declared here so that it outlives `shotApp` -- it releases the name
    // when it is destroyed, and the first instance must not drop it while it is still
    // starting up. nullptr on platforms without an implementation, in which case the
    // application runs unguarded rather than refusing to start.
    std::unique_ptr<qshot::ISingleInstance> instanceGuard =
        qshot::PlatformFactory::createSingleInstance();
    if (instanceGuard && !instanceGuard->acquire()) {
        // A message rather than a silent exit: the user double-clicked the icon, so
        // something has to happen, and "already running, look in the tray" is the whole
        // answer. Read after the application name is set, so it follows the language
        // preference like every other string.
        QMessageBox::information(nullptr, qshot::text(qshot::Str::AlreadyRunningTitle),
                                 qshot::text(qshot::Str::AlreadyRunningBody));
        return 0;
    }

    // The taskbar button and the alt-tab entry. Without this they show Qt's default icon,
    // which is the most visible place an application can look unfinished. Set from the
    // embedded multi-size .ico so Windows can pick the resolution it needs.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/qshot.ico")));

    qshot::ShotApplication shotApp;

    return app.exec();
}
