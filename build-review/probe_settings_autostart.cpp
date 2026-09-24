// Integration test for the two things that cannot be verified by reading code:
//
//   1. Settings round trip through QSettings (isolated under a throwaway
//      organisation name, so the real configuration is untouched).
//   2. WinAutoStart against the real HKCU Run key. The original value is saved
//      first and restored at the end, so the machine is left exactly as found.

#include <QApplication>
#include <QSettings>
#include <cstdio>

#include "core/IAutoStart.h"
#include "core/PlatformFactory.h"
#include "core/Settings.h"

using namespace qshot;

namespace {

constexpr auto kRunKeyPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kValueName = "QShot";

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

void testSettingsRoundTrip() {
    std::printf("settings round trip (isolated organisation):\n");

    Settings& s = Settings::instance();

    s.setHotkey(QKeySequence(QStringLiteral("Ctrl+Shift+F9")));
    s.setLanguage(Language::English);
    s.setAutoStart(true);
    s.setSaveDirectory(QStringLiteral("D:/tmp/shots"));
    s.setSaveFormat(SaveFormat::Jpeg);
    s.setJpegQuality(71);
    s.setIncludeCursor(true);
    s.setRememberToolSettings(true);

    ToolSettings pen;
    pen.color = QColor(30, 136, 229);
    pen.lineWidth = 6;
    s.setToolSettings(AnnotationType::Pen, pen);

    // Read back through a fresh QSettings instance, i.e. from storage rather
    // than from the cache.
    QSettings raw;
    raw.sync();

    check(raw.value("hotkey").toString() == QLatin1String("Ctrl+Shift+F9"), "hotkey stored");
    check(raw.value("language").toString() == QLatin1String("en"), "language stored");
    check(raw.value("autoStart").toBool(), "autoStart stored");
    check(raw.value("save/directory").toString() == QLatin1String("D:/tmp/shots"), "save dir stored");
    check(raw.value("save/format").toString() == QLatin1String("jpeg"), "format stored");
    check(raw.value("save/jpegQuality").toInt() == 71, "jpeg quality stored");
    check(raw.value("capture/includeCursor").toBool(), "includeCursor stored");
    // HexArgb, not "#1e88e5": Settings deliberately stores the alpha channel, because
    // QColor::name() without a format would write the highlighter's translucent colour out
    // as fully opaque. The leading #ff is the point, not noise -- do not "fix" it away.
    check(raw.value("annotations/pen/color").toString() == QLatin1String("#ff1e88e5"),
          "pen colour stored as HexArgb (the alpha is deliberate)");
    check(raw.value("annotations/pen/lineWidth").toInt() == 6, "pen width stored");

    // The cached accessors must agree with what was written.
    check(s.hotkey() == QKeySequence(QStringLiteral("Ctrl+Shift+F9")), "hotkey read back");
    check(s.language() == Language::English, "language read back");
    check(s.saveFormat() == SaveFormat::Jpeg, "format read back");
    check(s.jpegQuality() == 71, "quality read back");
    check(s.includeCursor(), "includeCursor read back");

    const ToolSettings penBack = s.toolSettings(AnnotationType::Pen);
    check(penBack.lineWidth == 6 && penBack.color == QColor(30, 136, 229), "pen settings read back");

    // A tool that was never written must still come back as the shipped default.
    const ToolSettings rectBack = s.toolSettings(AnnotationType::Rectangle);
    check(rectBack.color == QColor(251, 140, 0) && rectBack.lineWidth == 4,
          "untouched tool falls back to defaults");

    // Turning the preference off must mask stored values.
    s.setRememberToolSettings(false);
    const ToolSettings masked = s.toolSettings(AnnotationType::Pen);
    check(masked.lineWidth == 4 && masked.color == QColor(251, 140, 0),
          "rememberToolSettings=off masks stored values");

    // Out-of-range quality must be clamped rather than stored verbatim.
    s.setJpegQuality(999);
    check(s.jpegQuality() == 100, "jpeg quality clamped to 100");

    s.restoreDefaults();
    check(s.hotkey() == QKeySequence(QStringLiteral("Alt+A")), "restoreDefaults resets hotkey");
    check(s.language() == Language::Chinese, "restoreDefaults resets language");
    check(!s.autoStart(), "restoreDefaults resets autoStart");
    check(s.saveFormat() == SaveFormat::Png, "restoreDefaults resets format");
    check(s.includeCursor() == false, "restoreDefaults resets includeCursor");
}

void testAutoStart() {
    std::printf("autostart registry round trip (original value preserved):\n");

    // Snapshot whatever is there now.
    QSettings run(kRunKeyPath, QSettings::NativeFormat);
    const bool hadValue = run.contains(QLatin1String(kValueName));
    const QString originalValue = run.value(QLatin1String(kValueName)).toString();
    std::printf("  before: present=%d value=%s\n", hadValue ? 1 : 0,
                qPrintable(originalValue));

    auto autoStart = PlatformFactory::createAutoStart();
    if (!autoStart) {
        std::printf("  no autostart implementation on this platform - skipped\n");
        return;
    }

    const bool enableOk = autoStart->setEnabled(true);
    check(enableOk, "setEnabled(true) reported success");
    check(autoStart->isEnabled(), "isEnabled() true after enabling");

    QSettings after(kRunKeyPath, QSettings::NativeFormat);
    const QString written = after.value(QLatin1String(kValueName)).toString();
    std::printf("  written value: %s\n", qPrintable(written));
    check(written.startsWith(QLatin1Char('"')) && written.endsWith(QLatin1Char('"')),
          "value is quoted (paths with spaces survive)");
    check(written.contains(QLatin1String("qshot"), Qt::CaseInsensitive)
              || written.contains(QLatin1String("probe"), Qt::CaseInsensitive),
          "value points at this executable");

    const bool disableOk = autoStart->setEnabled(false);
    check(disableOk, "setEnabled(false) reported success");
    check(!autoStart->isEnabled(), "isEnabled() false after disabling");

    // --- restore -----------------------------------------------------------
    if (hadValue) {
        QSettings restore(kRunKeyPath, QSettings::NativeFormat);
        restore.setValue(QLatin1String(kValueName), originalValue);
        restore.sync();
        std::printf("  restored original value\n");
    } else {
        QSettings verify(kRunKeyPath, QSettings::NativeFormat);
        std::printf("  cleanup verified: entry absent=%d\n",
                    verify.contains(QLatin1String(kValueName)) ? 0 : 1);
        check(!verify.contains(QLatin1String(kValueName)), "no Run entry left behind");
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Throwaway settings location so the real configuration is never touched.
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("Integration"));

    testSettingsRoundTrip();
    testAutoStart();

    std::printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    std::fflush(stdout);
    return failures == 0 ? 0 : 1;
}
