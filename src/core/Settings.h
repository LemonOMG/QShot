#pragma once

#include <QObject>
#include <QKeySequence>
#include <QString>
#include <QMap>

#include "../annotation/AnnotationType.h"
#include "../annotation/ToolSettings.h"

namespace qshot {

enum class Language {
    Chinese,
    English
};

enum class SaveFormat {
    Png,
    Jpeg
};

/**
 * Application settings, persisted with QSettings.
 *
 * QSettings is used directly (rather than a JSON file of our own) because it is
 * the native mechanism on every target platform: the registry on Windows,
 * plists on macOS, INI on Linux. No extra dependency, no file-format code.
 *
 * All values are cached in members and loaded once in the constructor. Reading
 * QSettings on every access would be a registry hit per call, and text() is
 * called from paintEvent paths where that is not acceptable.
 *
 * The class is a Meyers singleton so that the string table, the settings dialog
 * and the widgets all observe the same instance. It is not thread-safe; QShot is
 * single-threaded.
 */
class Settings : public QObject {
    Q_OBJECT
public:
    static Settings& instance();

    // --- capture hotkey -----------------------------------------------------
    QKeySequence hotkey() const { return hotkey_; }
    void setHotkey(const QKeySequence& seq);

    // --- language -----------------------------------------------------------
    Language language() const { return language_; }
    void setLanguage(Language lang);

    // --- start at login -----------------------------------------------------
    bool autoStart() const { return autoStart_; }
    void setAutoStart(bool enabled);

    // --- saving -------------------------------------------------------------
    QString saveDirectory() const { return saveDirectory_; }
    void setSaveDirectory(const QString& dir);

    SaveFormat saveFormat() const { return saveFormat_; }
    void setSaveFormat(SaveFormat format);

    /// JPEG quality, 1-100.
    int jpegQuality() const { return jpegQuality_; }
    void setJpegQuality(int quality);

    // --- capture ------------------------------------------------------------
    bool includeCursor() const { return includeCursor_; }
    void setIncludeCursor(bool on);

    // --- annotations --------------------------------------------------------
    /// When on, the colour / thickness picked in the toolbar survives a restart.
    bool rememberToolSettings() const { return rememberToolSettings_; }
    void setRememberToolSettings(bool on);

    /// Never returns a value outside the shipped defaults, even if the stored
    /// entry is missing or the setting above is off.
    ToolSettings toolSettings(AnnotationType type) const;
    void setToolSettings(AnnotationType type, const ToolSettings& settings);

    /// Wipes the stored configuration and reloads every cached value.
    void restoreDefaults();

signals:
    /// The active language changed; every visible string must be refreshed.
    void languageChanged();
    /// The capture hotkey changed; the global hotkey has to be re-registered.
    void hotkeyChanged();
    /// Anything else changed (saving, capture, annotations).
    void changed();

private:
    explicit Settings(QObject* parent = nullptr);

    QString toolKey(AnnotationType type) const;
    void load();

    QKeySequence hotkey_;
    Language language_ = Language::Chinese;
    bool autoStart_ = false;
    QString saveDirectory_;
    SaveFormat saveFormat_ = SaveFormat::Png;
    int jpegQuality_ = 92;
    bool includeCursor_ = false;
    bool rememberToolSettings_ = true;
    QMap<AnnotationType, ToolSettings> toolSettings_;
};

} // namespace qshot
