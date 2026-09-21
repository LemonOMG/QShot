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
constexpr auto kKeyIncludeCursor = "capture/includeCursor";
constexpr auto kKeyRememberTools = "annotations/remember";

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
    includeCursor_ = store.value(kKeyIncludeCursor, false).toBool();
    rememberToolSettings_ = store.value(kKeyRememberTools, true).toBool();

    const AnnotationType tools[] = {
        AnnotationType::Rectangle, AnnotationType::Ellipse, AnnotationType::Arrow,
        AnnotationType::Pen, AnnotationType::Mosaic, AnnotationType::Text
    };
    toolSettings_.clear();
    for (AnnotationType type : tools) {
        const QString prefix = QStringLiteral("annotations/") + toolKey(type);

        // Start from the shipped defaults and only override what is stored, so a
        // partially written config (or a new field added later) still yields a
        // complete ToolSettings.
        ToolSettings s;
        s.color = QColor::fromString(
            store.value(prefix + "/color", s.color.name()).toString());
        if (!s.color.isValid()) s.color = ToolSettings().color;
        s.lineWidth = store.value(prefix + "/lineWidth", s.lineWidth).toInt();
        s.mosaicSize = store.value(prefix + "/mosaicSize", s.mosaicSize).toInt();
        s.fontSize = store.value(prefix + "/fontSize", s.fontSize).toInt();
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

ToolSettings Settings::toolSettings(AnnotationType type) const {
    if (!rememberToolSettings_) return ToolSettings();
    return toolSettings_.value(type, ToolSettings());
}

void Settings::setToolSettings(AnnotationType type, const ToolSettings& settings) {
    if (type == AnnotationType::None) return;
    if (!rememberToolSettings_) return;

    toolSettings_.insert(type, settings);

    const QString prefix = QStringLiteral("annotations/") + toolKey(type);
    QSettings store;
    store.setValue(prefix + "/color", settings.color.name());
    store.setValue(prefix + "/lineWidth", settings.lineWidth);
    store.setValue(prefix + "/mosaicSize", settings.mosaicSize);
    store.setValue(prefix + "/fontSize", settings.fontSize);
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
