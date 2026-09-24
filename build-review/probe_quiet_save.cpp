// Verification for the silent save path -- the one that writes a file without a dialog.
//
// Why this needs its own probe: saveImageQuietly() is the only code in the project that
// *chooses a file name for the user*, and every way it can go wrong is silent. A name
// collision that overwrote the previous capture, an extension that does not match the
// encoder, a folder that was never created, a null image written as a 0x0 PNG -- none of
// those raise anything, and with no dialog on screen there is nothing to notice them by.
//
// What is checked:
//
//  1. The name has the documented shape and the extension follows the format setting.
//  2. A name that is already taken is never reused, and the file that holds it is left
//     untouched. "Save" silently overwriting the previous capture is the worst possible
//     outcome of this feature, and second resolution makes the collision easy to hit.
//  3. A save folder that does not exist is created. The setting can name a folder the
//     user typed and never made.
//  4. The bytes match the setting: a PNG round-trips losslessly, a .jpg really is JPEG
//     (and really is lossy). Getting this wrong would write PNG bytes into a .jpg.
//  5. The history store adopts the quiet-saved file rather than re-encoding it, which is
//     what keeps a JPEG capture byte-identical to what was written.
//  6. A null image writes nothing -- the overlay can produce one for an empty selection.
//  7. The settings this probe has to change are put back, checked by reading the stored
//     values back rather than by trusting the accessors that just wrote them.
//
// Settings is a singleton with no injection point, so the sections that need a particular
// format or history state change the real values and restore them on every exit path. The
// *directory* is injectable -- saveImageQuietly()'s third parameter -- precisely so that
// this probe never writes into the user's save folder.
//
// Run: probe_quiet_save.exe      (no widgets, no windows, nothing flashes)

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <cstdio>

#include "core/HistoryStore.h"
#include "core/ImageExport.h"
#include "core/Settings.h"

using namespace qshot;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void checkStr(const QString& got, const QString& want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got \"%s\", want \"%s\")\n", ok ? "ok" : "FAIL", what,
           qPrintable(got), qPrintable(want));
}

static void checkInt(int got, int want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL", what, got, want);
}

/// A small image with structure in it. Deliberately not flat: a single-colour image would
/// survive JPEG at any quality, so a lossy round-trip could not be distinguished from a
/// lossless one.
static QImage testImage() {
    QImage img(37, 23, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const int checker = ((x / 3) + (y / 3)) % 2 ? 200 : 40;
            img.setPixelColor(x, y, QColor(checker, (x * 255) / img.width(),
                                           (y * 255) / img.height()));
        }
    }
    return img;
}

/// QImage::operator== compares the format as well as the pixels, and an image that has been
/// through a file never comes back in the premultiplied format the compositor handed us.
/// Every comparison in this probe goes through here. Comparing raw makes "lossless" fail and
/// "lossy" pass, both for the wrong reason -- which is what happened on the first run.
static QImage normalised(const QImage& img) {
    return img.convertToFormat(QImage::Format_ARGB32);
}

/// -1 when the sizes differ, which is a different kind of failure from "no pixels differ".
static int differingPixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return -1;
    int n = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) ++n;
        }
    }
    return n;
}

/// Puts the stored settings back on every exit path. A probe that left the user's image
/// format switched to JPEG would be a bug report, not a test.
struct SettingsGuard {
    SaveFormat format;
    int quality;
    bool historyEnabled;

    SettingsGuard()
        : format(Settings::instance().saveFormat())
        , quality(Settings::instance().jpegQuality())
        , historyEnabled(Settings::instance().historyEnabled()) {}

    ~SettingsGuard() {
        Settings::instance().setSaveFormat(format);
        Settings::instance().setJpegQuality(quality);
        Settings::instance().setHistoryEnabled(historyEnabled);
    }
};

static QString stamp(const QDateTime& t) {
    return t.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

static void plantFile(const QString& path, int bytes) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray(bytes, 'x'));
    }
}

int main(int argc, char** argv) {
    // QCoreApplication, not QApplication: every call below passes a null `parent`, so the
    // failure warning in saveImageQuietly() is never constructed and no widget is ever
    // needed. Linking QtWidgets happens anyway -- ImageExport.cpp is shared with the
    // application -- but this probe does not use it.
    QCoreApplication app(argc, argv);

    const QString root = QDir::tempPath() + QStringLiteral("/qshot-quiet-save-probe");
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    SettingsGuard guard;
    const QImage image = testImage();

    printf("[1] the generated name follows the documented shape\n");
    {
        Settings::instance().setSaveFormat(SaveFormat::Png);
        const QString dir = root + QStringLiteral("/shaped");
        const QString path = saveImageQuietly(image, nullptr, dir);
        printf("       %s\n", qPrintable(QFileInfo(path).fileName()));

        check(QFileInfo::exists(path), "a file was written");
        // Anchored, and the date part is checked for shape rather than for today's value:
        // asserting "starts with screenshot_" would pass on a name with no timestamp at all.
        static const QRegularExpression shape(
            QStringLiteral("^screenshot_\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2}\\.png$"));
        check(shape.match(QFileInfo(path).fileName()).hasMatch(),
              "it is screenshot_<date>_<time>.png");
        checkStr(QFileInfo(path).absolutePath(), dir, "it went into the directory it was given");
    }

    printf("\n[2] a taken name is never reused\n");
    {
        Settings::instance().setSaveFormat(SaveFormat::Png);
        const QString dir = root + QStringLiteral("/collision");
        QDir().mkpath(dir);

        // The collision is forced rather than hoped for. Two calls landing in the same
        // second would exercise it too, but only sometimes -- and a check that exercises
        // its subject only sometimes is not a check.
        bool exercised = false;
        for (int attempt = 0; attempt < 3 && !exercised; ++attempt) {
            const QDateTime before = QDateTime::currentDateTime();
            const QString stem = QStringLiteral("screenshot_") + stamp(before);
            const QString taken = dir + QLatin1Char('/') + stem + QStringLiteral(".png");
            plantFile(taken, 12);

            const QString path = saveImageQuietly(image, nullptr, dir);
            if (stamp(QDateTime::currentDateTime()) != stamp(before)) {
                continue; // the clock crossed a second mid-test; the names differ naturally
            }
            exercised = true;

            check(path != taken, "the occupied name was not chosen");
            checkStr(QFileInfo(path).fileName(), stem + QStringLiteral("_2.png"),
                     "the suffix was bumped");
            checkInt(int(QFileInfo(taken).size()), 12,
                     "the file that was already there is untouched");
        }
        check(exercised, "the collision path was actually reached");
    }

    printf("\n[3] a save folder that does not exist is created\n");
    {
        Settings::instance().setSaveFormat(SaveFormat::Png);
        const QString dir = root + QStringLiteral("/made/up/depth");
        check(!QDir(dir).exists(), "the directory is not there to begin with");

        const QString path = saveImageQuietly(image, nullptr, dir);
        check(QDir(dir).exists(), "it was created");
        check(QFileInfo::exists(path), "and the file went into it");
    }

    printf("\n[4] the bytes match the format setting\n");
    {
        Settings::instance().setSaveFormat(SaveFormat::Png);
        const QString pngPath = saveImageQuietly(image, nullptr, root + QStringLiteral("/png"));
        QImageReader pngReader(pngPath);
        checkStr(QString::fromLatin1(pngReader.format()), QStringLiteral("png"),
                 "a PNG setting writes PNG bytes");
        checkInt(differingPixels(normalised(pngReader.read()), normalised(image)), 0,
                 "PNG round-trips losslessly (differing pixels)");

        Settings::instance().setSaveFormat(SaveFormat::Jpeg);
        const QString jpgPath = saveImageQuietly(image, nullptr, root + QStringLiteral("/jpg"));
        printf("       %s\n", qPrintable(QFileInfo(jpgPath).fileName()));
        check(jpgPath.endsWith(QStringLiteral(".jpg")), "a JPEG setting writes a .jpg name");

        QImageReader jpgReader(jpgPath);
        checkStr(QString::fromLatin1(jpgReader.format()), QStringLiteral("jpeg"),
                 "and JPEG bytes -- not PNG bytes under a .jpg name");
        const QImage jpgBack = jpgReader.read();
        checkInt(jpgBack.width(), image.width(), "the read-back width matches");
        checkInt(jpgBack.height(), image.height(), "the read-back height matches");
        // The one thing that separates a real JPEG from a PNG that happens to be called
        // .jpg: it threw information away. A count rather than a bare !=, because "0 pixels
        // differ" and "the formats differ" are different failures and the number says which.
        const int changed = differingPixels(normalised(jpgBack), normalised(image));
        printf("       JPEG changed %d of %d pixels\n", changed,
               image.width() * image.height());
        check(changed > 0, "and it is lossy, so the encoder really was JPEG");
    }

    printf("\n[5] the history store adopts the quiet-saved file\n");
    {
        Settings::instance().setSaveFormat(SaveFormat::Jpeg);
        // Forced on: the store refuses to add anything while history is off, which would
        // turn this section into a silent no-op on a machine configured that way.
        Settings::instance().setHistoryEnabled(true);

        const QString dir = root + QStringLiteral("/adopted");
        const QString path = saveImageQuietly(image, nullptr, dir);
        const qint64 written = QFileInfo(path).size();

        HistoryStore store(root + QStringLiteral("/store"));
        const bool added = store.add(image, path);
        check(added, "the entry was added");
        checkInt(store.count(), 1, "there is one entry");

        if (added && store.count() == 1) {
            const HistoryEntry entry = store.entries().first();
            check(entry.filePath != path, "the store kept its own file, not the user's path");
            // The suffix is what proves the store *copied* the file instead of re-encoding
            // it: a re-encode always lands on .png, so a .jpg suffix can only come from the
            // copy branch.
            check(entry.filePath.endsWith(QStringLiteral(".jpg")),
                  "the suffix carried over, so it was copied and not re-encoded");
            checkInt(int(entry.bytes), int(written), "the copy is the same size as the original");

            // And the copy is real: the user deleting their file must not empty the history.
            check(QFile::remove(path), "the user's own file is deleted");
            QImage reread;
            check(reread.load(entry.filePath) && !reread.isNull(),
                  "the entry still reads back after the original is gone");
        }
    }

    printf("\n[6] a null image writes nothing\n");
    {
        const QString dir = root + QStringLiteral("/null");
        const QString path = saveImageQuietly(QImage(), nullptr, dir);
        check(path.isEmpty(), "no path is returned");
        check(!QDir(dir).exists(), "and no directory was created for it either");
    }

    printf("\n[7] the settings this probe changed were put back\n");
    {
        // Read the stored values back rather than calling the accessors that just wrote
        // them: the point is to catch a restore that only updated the cache.
        const QSettings store;
        checkStr(store.value(QStringLiteral("save/format"), QStringLiteral("png")).toString(),
                 guard.format == SaveFormat::Jpeg ? QStringLiteral("jpeg") : QStringLiteral("png"),
                 "the image format is back where it started");
        checkInt(store.value(QStringLiteral("save/jpegQuality"), 92).toInt(), guard.quality,
                 "the JPEG quality is back where it started");
        check(store.value(QStringLiteral("history/enabled"), true).toBool() == guard.historyEnabled,
              "the history toggle is back where it started");
    }

    QDir(root).removeRecursively();

    printf("\n%d checks, %d failures\n", checks, failures);
    // Explicit, and not optional: with stdout redirected to a pipe or a file it is fully
    // buffered, and this process does not flush it on the way out. Without this line the
    // probe runs all 29 assertions, exits 0, and prints nothing at all -- so a runner that
    // only looks at the exit code reports it as passing while every assertion is invisible.
    // That is exactly what happened: the assertion total was 29 short for months.
    fflush(stdout);
    return failures == 0 ? 0 : 1;
}
