// Runs *inside* the deployed folder and asks the questions the static checks cannot.
//
// tools/deploy.sh resolves every import of every PE file in the staging folder, which proves
// nothing is missing. It cannot prove that Qt finds what it needs, because "where Qt looks"
// is a runtime decision:
//
//   1. the platform plugin. Qt locates plugins relative to QLibraryInfo's prefix, which for a
//      deployed application is derived from the location of Qt6Core.dll -- not from PATH. If
//      that resolution is wrong the app dies with "could not find or load the Qt platform
//      plugin windows", a message that says nothing about which directory was searched.
//
//   2. the translation catalogue. Strings.cpp loads "qtbase_zh_CN" from
//      QLibraryInfo::TranslationsPath. Getting the directory or the file name wrong does not
//      fail loudly: QTranslator::load returns false, installQtTranslations() gives up, and
//      every Qt-supplied string silently reverts to English. That is precisely the bug this
//      probe was written for -- `windeployqt --translations zh_CN` deploys the 99-byte
//      qt_zh_CN.qm meta-catalogue and not the 147KB qtbase_zh_CN.qm the app actually loads.
//
//   3. the image format plugins. These are loaded by name at runtime, so unlike the platform
//      plugin there is no import for the closure check to resolve -- and unlike a missing DLL,
//      dropping one produces no error at all, just a feature that stops working. The tray icon
//      (an embedded .ico) and "save as .jpg" both depend on one.
//
// So the executable has to be copied next to the deployed Qt libraries and run from there.
// tools/smoke_deploy.sh does that; running it from anywhere else reports a clear error rather
// than a misleading pass.

#include <QApplication>
#include <QCoreApplication>
#include <QImageReader>
#include <QLibraryInfo>
#include <QTranslator>
#include <QDir>
#include <QFileInfo>
#include <cstdio>

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static bool hasCjk(const QString& s) {
    for (const QChar& c : s) {
        if (c.unicode() >= 0x2E80) return true;
    }
    return false;
}

// The strings Qt itself puts in front of the user. Chosen because they are the visible ones:
// QMessageBox's buttons come from QPlatformTheme, and the line-edit context menu -- which the
// text annotation tool shows on right-click -- comes from QLineEdit.
struct Probe { const char* context; const char* source; };

static const Probe kProbes[] = {
    { "QPlatformTheme", "OK" },
    { "QPlatformTheme", "Cancel" },
    { "QPlatformTheme", "&Yes" },
    { "QLineEdit", "&Undo" },
    { "QLineEdit", "Cu&t" },
    { "QFileDialog", "&Open" },
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    printf("[1] this is running from the deployed folder\n");
    const QString appDir = QCoreApplication::applicationDirPath();
    printf("       applicationDirPath: %s\n", qPrintable(appDir));
    // The tell-tale: in a deployed folder the Qt libraries sit beside the executable. If they
    // are not there, the process picked them up from PATH and every conclusion below would be
    // about the Qt *installation*, not about the folder being tested.
    check(QFileInfo::exists(appDir + QStringLiteral("/Qt6Core.dll")),
          "Qt6Core.dll is beside the executable");

    printf("\n[2] the platform plugin loaded\n");
    const QString platform = QGuiApplication::platformName();
    printf("       platformName: %s\n", qPrintable(platform));
    check(platform == QStringLiteral("windows"),
          "the windows platform plugin was found and loaded");

    printf("\n[3] Qt resolves its translation directory inside the folder\n");
    const QString trDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    printf("       TranslationsPath: %s\n", qPrintable(trDir));
    check(QDir(trDir) == QDir(appDir + QStringLiteral("/translations")),
          "TranslationsPath is <appdir>/translations");

    printf("\n[4] the plugins the application loads by name are deployed\n");
    // The gap this closes: plugins are loaded by name at runtime, not linked, so no import
    // scan can see them. tools/deploy.sh checks that platforms/qwindows.dll exists, but the
    // image format plugins are invisible to it -- and dropping one does not crash anything,
    // it just makes a feature stop working. QImageReader::supportedImageFormats() reports
    // what the *loaded* plugins actually provide, which is the thing worth asserting.
    //
    // Note that most of the list below needs no plugin at all: bmp/pbm/pgm/ppm/xbm/xpm/png
    // are built into QtGui. Only gif, ico/cur, jpeg/jfif/jpg and svg/svgz come from
    // imageformats/, which is exactly why it is easy to drop one without noticing.
    {
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        QStringList names;
        for (const QByteArray& f : formats) names << QString::fromLatin1(f);
        printf("       supported: %s\n", qPrintable(names.join(QStringLiteral(" "))));

        // "ico" is what the tray and window icons are: resources/resources.qrc embeds a
        // multi-size .ico and both ShotApplication and main.cpp hand it to a QIcon, which
        // reads it through QImageReader. Without this plugin the application still starts and
        // the tray icon is simply blank -- no error anywhere.
        check(formats.contains("ico"), "ico is available, so the embedded icon can load");
        // "jpeg" is one of the two formats ImageExport offers. PNG is built into Qt and needs
        // no plugin, which is exactly why the plugin set is easy to get wrong.
        check(formats.contains("jpeg"), "jpeg is available, so saving as .jpg works");

        // The other direction, and the reason this section is a tripwire rather than just a
        // presence check. tools/deploy.sh prunes Qt6Svg, iconengines/qsvgicon and
        // imageformats/qsvg+qgif, because the toolbar glyphs are drawn in code
        // (src/overlay/ToolbarIcons.cpp) and nothing reads a .gif. Asserting their absence
        // means the prune list cannot drift out of sync with reality in silence: if someone
        // later adopts SVG icons, this fails and names the file to edit, instead of the
        // symptom appearing as a toolbar of blank buttons.
        check(!formats.contains("svg"),
              "svg is pruned -- the toolbar glyphs are drawn in code, not loaded");
        check(!formats.contains("gif"), "gif is pruned -- nothing in the app reads a .gif");
    }

    printf("\n[5] before the catalogue is loaded, Qt's strings are English\n");
    // A baseline, so [6] measures the catalogue rather than some other translator Qt may have
    // installed on its own.
    int translatedBefore = 0;
    for (const Probe& probe : kProbes) {
        if (QCoreApplication::translate(probe.context, probe.source)
            != QString::fromLatin1(probe.source)) {
            ++translatedBefore;
        }
    }
    check(translatedBefore == 0, "none of the probed strings is translated yet");

    printf("\n[6] qtbase_zh_CN.qm loads and actually translates\n");
    QTranslator translator;
    const bool loaded = translator.load(QStringLiteral("qtbase_zh_CN"), trDir);
    check(loaded, "QTranslator::load(\"qtbase_zh_CN\", TranslationsPath) succeeded");

    int translated = 0;
    if (loaded) {
        QCoreApplication::installTranslator(&translator);
        for (const Probe& probe : kProbes) {
            const QString got = QCoreApplication::translate(probe.context, probe.source);
            const bool cjk = hasCjk(got);
            if (cjk) ++translated;
            printf("       %-14s %-8s -> %s%s\n", probe.context, probe.source,
                   qPrintable(got), cjk ? "" : "   (not translated)");
        }
    }

    // Not "all of them": a Qt release may add a string this catalogue predates, and failing on
    // that would be failing on Qt's translation coverage rather than on the deployment. Most
    // of them being Chinese is the claim that matters.
    check(translated >= 3, "most probed strings come back in Chinese");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
