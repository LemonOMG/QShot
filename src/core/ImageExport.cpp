#include "ImageExport.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>
#include <QStandardPaths>
#include <QWidget>

#include "Settings.h"
#include "Strings.h"

namespace qshot {

namespace {

/// How many times the silent save will bump the numeric suffix before giving up. A bound
/// rather than "loop until free": an unwritable directory makes `exists()` lie forever,
/// and an unbounded loop there would hang the application.
constexpr int kMaxNameAttempts = 999;

/// The folder a capture goes into, with the same fallback chain in both save paths.
/// Shared rather than written out twice so the dialog and the silent save cannot end up
/// disagreeing about what "the save folder" means -- the dialog would then preselect one
/// directory while the silent save wrote into another.
QString resolvedSaveDirectory() {
    QString dir = Settings::instance().saveDirectory();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    if (dir.isEmpty()) dir = QDir::homePath();
    return dir;
}

/**
 * The one place that decides which encoder, and which quality, a file gets.
 *
 * Both save paths end here. They disagree about where the format comes from -- the dialog
 * reads the extension the user typed, the silent path reads the setting -- but the
 * encoding itself must not differ, or "save as .jpg" and "quiet save as JPEG" would
 * produce different files from the same image.
 */
bool writeImage(const QImage& image, const QString& path) {
    const bool asJpeg = path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
                        || path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive);
    return image.save(path, asJpeg ? "JPEG" : "PNG",
                      asJpeg ? Settings::instance().jpegQuality() : -1);
}

} // namespace

void copyImageToClipboard(const QImage& image) {
    if (image.isNull()) return;
    QApplication::clipboard()->setPixmap(QPixmap::fromImage(image));
}

QString saveImageQuietly(const QImage& image, QWidget* parent, const QString& directory) {
    if (image.isNull()) return QString();

    const Settings& settings = Settings::instance();
    const QString dir = directory.isEmpty() ? resolvedSaveDirectory() : directory;

    // Derived from the format setting, and it has to agree with writeImage()'s rule that
    // ".jpg"/".jpeg" means JPEG -- which is exactly why it is derived here rather than
    // hardcoded in both places.
    const QString ext = settings.saveFormat() == SaveFormat::Jpeg ? QStringLiteral("jpg")
                                                                 : QStringLiteral("png");
    const QString stem = QStringLiteral("screenshot_")
                       + QDateTime::currentDateTime()
                             .toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));

    // Second resolution is coarse enough that two captures inside one second are easy to
    // produce by tapping the hotkey twice, and a save that silently overwrote the previous
    // capture would be the worst possible outcome of an action called "save". So the name
    // is bumped until it is free.
    QString path = dir + QLatin1Char('/') + stem + QLatin1Char('.') + ext;
    for (int n = 2; QFileInfo::exists(path); ++n) {
        if (n > kMaxNameAttempts) {
            path.clear();
            break;
        }
        path = dir + QLatin1Char('/') + stem + QStringLiteral("_%1").arg(n)
             + QLatin1Char('.') + ext;
    }

    // The configured folder may be one the user typed and never created, or one that has
    // since been deleted. Creating it is what naming it asked for.
    const bool written = !path.isEmpty() && QDir().mkpath(dir) && writeImage(image, path);

    // A silent save that failed must not stay silent: with no dialog there is nothing else
    // to report it, and the caller has already moved on. `parent` is nullptr only where the
    // caller reports failures itself, or in a probe, which must not block on a modal box.
    if (!written && parent) {
        QMessageBox::warning(parent, text(Str::SaveFailedTitle),
                             text(Str::SaveFailedBody).arg(path.isEmpty() ? dir : path));
    }
    return written ? path : QString();
}

QString saveImageWithDialog(QWidget* parent, const QImage& image) {
    if (image.isNull()) return QString();

    const Settings& settings = Settings::instance();
    const bool preferJpeg = settings.saveFormat() == SaveFormat::Jpeg;
    const QString pngFilter = text(Str::SaveFilterPng);
    const QString jpegFilter = text(Str::SaveFilterJpeg);

    QString defaultPath = resolvedSaveDirectory();
    defaultPath += preferJpeg ? QStringLiteral("/screenshot.jpg")
                              : QStringLiteral("/screenshot.png");

    // The configured format is listed first so the dialog preselects it.
    const QString filters = preferJpeg ? (jpegFilter + QStringLiteral(";;") + pngFilter)
                                       : (pngFilter + QStringLiteral(";;") + jpegFilter);

    QString selectedFilter;
    QString filePath = QFileDialog::getSaveFileName(
        parent,
        text(Str::SaveDialogTitle),
        defaultPath,
        filters,
        &selectedFilter);
    if (filePath.isEmpty()) return QString(); // cancelled

    // Non-native dialogs do not append an extension for us.
    if (QFileInfo(filePath).suffix().isEmpty()) {
        // Honour the filter the user actually picked; the configured format is only
        // the preselected default.
        bool asJpeg = preferJpeg;
        if (selectedFilter == pngFilter)  asJpeg = false;
        if (selectedFilter == jpegFilter) asJpeg = true;
        filePath += asJpeg ? QStringLiteral(".jpg") : QStringLiteral(".png");
    }

    // The encoder is derived inside writeImage() from the final extension, not from the
    // setting: the user may have typed a different one in the dialog.
    if (!writeImage(image, filePath)) {
        QMessageBox::warning(parent, text(Str::SaveFailedTitle),
                             text(Str::SaveFailedBody).arg(filePath));
        return QString();
    }
    return filePath;
}

} // namespace qshot
