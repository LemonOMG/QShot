#include "WinAutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace qshot {

namespace {
// QSettings' NativeFormat maps straight onto the registry, so the Run key is
// reached with a plain path instead of hand-rolled RegOpenKeyEx/RegSetValueEx
// calls. Windows parses the value as a command line, so it is quoted - paths
// with spaces would otherwise be split at the first space.
constexpr auto kRunKeyPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
} // namespace

const char* WinAutoStart::valueName() {
    return "QShot";
}

QString WinAutoStart::commandLine() {
    const QString exe = QDir::toNativeSeparators(
        QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath());
    return QStringLiteral("\"%1\"").arg(exe);
}

bool WinAutoStart::isEnabled() const {
    QSettings run(kRunKeyPath, QSettings::NativeFormat);
    // Deliberately not comparing against commandLine(): an entry left over from
    // an older install path still means "autostart is on" to the user, and the
    // path is refreshed on the next launch anyway (see ShotApplication).
    return run.contains(QString::fromLatin1(valueName()));
}

bool WinAutoStart::setEnabled(bool enabled) {
    QSettings run(kRunKeyPath, QSettings::NativeFormat);

    if (enabled) {
        run.setValue(QString::fromLatin1(valueName()), commandLine());
    } else {
        run.remove(QString::fromLatin1(valueName()));
    }

    run.sync();
    return run.status() == QSettings::NoError;
}

} // namespace qshot
