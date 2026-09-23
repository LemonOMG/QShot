#include "Settings.h"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>

namespace qshot {

namespace {
// Keys are grouped by concern so the registry layout stays readable.
constexpr auto kKeyHotkey       = "hotkey";
constexpr auto kKeyLanguage     = "language";
constexpr auto kKeyAutoStart    = "autoStart";
constexpr auto kKeySaveDir      = "save/directory";
constexpr auto kKeySaveFormat   = "save/format";
constexpr auto kKeyJpegQuality  = "save/jpegQuality";
constexpr auto kKeyQuietSave    = "save/quiet";
constexpr auto kKeySaveOnCopy   = "save/onCopy";
constexpr auto kKeyIncludeCursor = "capture/includeCursor";
constexpr auto kKeyRememberTools = "annotations/remember";
constexpr auto kKeyHistoryEnabled = "history/enabled";
constexpr auto kKeyHistoryLimit   = "history/limit";
constexpr auto kKeyHistoryNotify  = "history/notifyOnCopy";

constexpr auto kDefaultHotkey = "Alt+A";
} // namespace

Settings& Settings::instance() {
    static Settings s;
    return s;
}

Settings::Settings(QObject* parent)
    : QObject(parent)
{
    load();
}

void Settings::load() {
    QSettings store;

    hotkey_ = QKeySequence(store.value(kKeyHotkey, kDefaultHotkey).toString(),
                           QKeySequence::PortableText);
    if (hotkey_.isEmpty()) {
        hotkey_ = QKeySequence(kDefaultHotkey, QKeySequence::PortableText);
    }

    language_ = store.value(kKeyLanguage, "zh").toString() == "en"
                    ? Language::English
                    : Language::Chinese;

    autoStart_ = store.value(kKeyAutoStart, false).toBool();

    // The default mirrors what saveToFile() used to hardcode, so an existing
    // user sees no behaviour change until they touch this setting.
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    saveDirectory_ = store.value(kKeySaveDir, defaultDir).toString();

    saveFormat_ = store.value(kKeySaveFormat, "png").toString() == "jpeg"
                      ? SaveFormat::Jpeg
                      : SaveFormat::Png;

    jpegQuality_ = qBound(1, store.value(kKeyJpegQuality, 92).toInt(), 100);
    // Both off by default, and that is the point rather than a placeholder: turning either
    // one on changes where the user's files appear, and a screenshot tool that starts
    // writing files nobody asked for is worse than one that asks every time.
    quietSave_ = store.value(kKeyQuietSave, false).toBool();
    saveOnCopy_ = store.value(kKeySaveOnCopy, false).toBool();
    includeCursor_ = store.value(kKeyIncludeCursor, false).toBool();
    rememberToolSettings_ = store.value(kKeyRememberTools, true).toBool();

    historyEnabled_ = store.value(kKeyHistoryEnabled, true).toBool();
    historyLimit_ = qBound(kMinHistoryLimit, store.value(kKeyHistoryLimit, kDefaultHistoryLimit).toInt(),
                           kMaxHistoryLimit);
    // On by default: the copy path closes the overlay and shows nothing else, so
    // without a notification the user has no way to tell whether Ctrl+C worked.
    historyNotifyOnCopy_ = store.value(kKeyHistoryNotify, true).toBool();

    const AnnotationType tools[] = {
        AnnotationType::Rectangle, AnnotationType::Ellipse, AnnotationType::Arrow,
        AnnotationType::Pen, AnnotationType::Mosaic, AnnotationType::Text,
        AnnotationType::Number, AnnotationType::Highlight
    };
    toolSettings_.clear();
    for (AnnotationType type : tools) {
        const QString prefix = QStringLiteral("annotations/") + toolKey(type);

        // Start from the shipped defaults for *this* tool and only override what is
        // stored, so a partially written config (or a new field added later) still yields
        // a complete ToolSettings. Per-tool rather than a single shared default: the
        // highlighter and the badge deliberately do not use the shared orange.
        ToolSettings s = defaultToolSettings(type);
        // HexArgb, not the default HexRgb: the highlighter's whole identity is its alpha,
        // and `name()` without a format would silently store it as fully opaque.
        s.color = QColor::fromString(
            store.value(prefix + "/color", s.color.name(QColor::HexArgb)).toString());
        if (!s.color.isValid()) s.color = defaultToolSettings(type).color;
        s.lineWidth = store.value(prefix + "/lineWidth", s.lineWidth).toInt();
        s.mosaicSize = store.value(prefix + "/mosaicSize", s.mosaicSize).toInt();
        s.fontSize = store.value(prefix + "/fontSize", s.fontSize).toInt();
        s.badgeDiameter = store.value(prefix + "/badgeDiameter", s.badgeDiameter).toInt();
        toolSettings_.insert(type, s);
    }
}

QString Settings::toolKey(AnnotationType type) const {
    switch (type) {
        case AnnotationType::Rectangle: return QStringLiteral("rectangle");
        case AnnotationType::Ellipse:   return QStringLiteral("ellipse");
        case AnnotationType::Arrow:     return QStringLiteral("arrow");
        case AnnotationType::Pen:       return QStringLiteral("pen");
        case AnnotationType::Mosaic:    return QStringLiteral("mosaic");
        case AnnotationType::Text:      return QStringLiteral("text");
        case AnnotationType::Number:    return QStringLiteral("number");
        case AnnotationType::Highlight: return QStringLiteral("highlight");
        case AnnotationType::None:      break;
    }
    return QString();
}

void Settings::setHotkey(const QKeySequence& seq) {
    if (seq.isEmpty() || seq == hotkey_) return;
    hotkey_ = seq;
    QSettings store;
    store.setValue(kKeyHotkey, seq.toString(QKeySequence::PortableText));
    emit hotkeyChanged();
}

void Settings::setLanguage(Language lang) {
    if (lang == language_) return;
    language_ = lang;
    QSettings store;
    store.setValue(kKeyLanguage, lang == Language::English ? "en" : "zh");
    emit languageChanged();
}

void Settings::setAutoStart(bool enabled) {
    if (enabled == autoStart_) return;
    autoStart_ = enabled;
    QSettings store;
    store.setValue(kKeyAutoStart, enabled);
    emit changed();
}

void Settings::setSaveDirectory(const QString& dir) {
    if (dir.isEmpty() || dir == saveDirectory_) return;
    saveDirectory_ = dir;
    QSettings store;
    store.setValue(kKeySaveDir, dir);
    emit changed();
}

void Settings::setSaveFormat(SaveFormat format) {
    if (format == saveFormat_) return;
    saveFormat_ = format;
    QSettings store;
    store.setValue(kKeySaveFormat, format == SaveFormat::Jpeg ? "jpeg" : "png");
    emit changed();
}

void Settings::setJpegQuality(int quality) {
    const int clamped = qBound(1, quality, 100);
    if (clamped == jpegQuality_) return;
    jpegQuality_ = clamped;
    QSettings store;
    store.setValue(kKeyJpegQuality, clamped);
    emit changed();
}

void Settings::setQuietSave(bool on) {
    if (on == quietSave_) return;
    quietSave_ = on;
    QSettings store;
    store.setValue(kKeyQuietSave, on);
    emit changed();
}

void Settings::setSaveOnCopy(bool on) {
    if (on == saveOnCopy_) return;
    saveOnCopy_ = on;
    QSettings store;
    store.setValue(kKeySaveOnCopy, on);
    emit changed();
}

void Settings::setIncludeCursor(bool on) {
    if (on == includeCursor_) return;
    includeCursor_ = on;
    QSettings store;
    store.setValue(kKeyIncludeCursor, on);
    emit changed();
}

void Settings::setRememberToolSettings(bool on) {
    if (on == rememberToolSettings_) return;
    rememberToolSettings_ = on;
    QSettings store;
    store.setValue(kKeyRememberTools, on);
    emit changed();
}

void Settings::setHistoryEnabled(bool on) {
    if (on == historyEnabled_) return;
    historyEnabled_ = on;
    QSettings store;
    store.setValue(kKeyHistoryEnabled, on);
    emit changed();
}

void Settings::setHistoryLimit(int limit) {
    const int clamped = qBound(kMinHistoryLimit, limit, kMaxHistoryLimit);
    if (clamped == historyLimit_) return;
    historyLimit_ = clamped;
    QSettings store;
    store.setValue(kKeyHistoryLimit, clamped);
    // Deliberately no signal-driven eviction here: the store trims on the next add and
    // on the next load, so a user who lowers the limit sees the excess go away without
    // their files being deleted the moment they click OK -- and Cancel stays a real
    // cancel.
    emit changed();
}

void Settings::setHistoryNotifyOnCopy(bool on) {
    if (on == historyNotifyOnCopy_) return;
    historyNotifyOnCopy_ = on;
    QSettings store;
    store.setValue(kKeyHistoryNotify, on);
    emit changed();
}

ToolSettings Settings::toolSettings(AnnotationType type) const {
    // The fallback is the per-tool default, not a bare ToolSettings: with "remember tool
    // settings" turned off the highlighter must still come back translucent yellow
    // rather than the shared orange.
    if (!rememberToolSettings_) return defaultToolSettings(type);
    return toolSettings_.value(type, defaultToolSettings(type));
}

void Settings::setToolSettings(AnnotationType type, const ToolSettings& settings) {
    if (type == AnnotationType::None) return;
    if (!rememberToolSettings_) return;

    toolSettings_.insert(type, settings);

    const QString prefix = QStringLiteral("annotations/") + toolKey(type);
    QSettings store;
    // HexArgb keeps the highlighter's alpha; `name()` alone would store it as opaque.
    store.setValue(prefix + "/color", settings.color.name(QColor::HexArgb));
    store.setValue(prefix + "/lineWidth", settings.lineWidth);
    store.setValue(prefix + "/mosaicSize", settings.mosaicSize);
    store.setValue(prefix + "/fontSize", settings.fontSize);
    store.setValue(prefix + "/badgeDiameter", settings.badgeDiameter);
}

void Settings::restoreDefaults() {
    QSettings store;
    store.clear();
    store.sync();
    load();
    emit languageChanged();
    emit hotkeyChanged();
    emit changed();
}

} // namespace qshot
