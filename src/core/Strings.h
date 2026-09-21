#pragma once

#include <QString>

namespace qshot {

/**
 * Every user-visible string in the application.
 *
 * Why a hand-written table instead of Qt Linguist (.ts/.qm):
 *   - the whole UI is ~40 strings in 2 languages;
 *   - Linguist would add lrelease + CMake plumbing *and* still require a
 *     changeEvent()/retranslateUi() override in every widget, because runtime
 *     switching is the caller's job either way;
 *   - a table has no build-tooling dependency and reads like a translation
 *     sheet, which makes missing entries obvious in review.
 * Migrating this file to .ts later is mechanical if the string count grows.
 *
 * Qt's *own* strings (QMessageBox buttons, the line-edit context menu) are not
 * covered here; they come from Qt's bundled catalogues, see
 * installQtTranslations().
 *
 * Add new strings by appending before Count - the static_assert in Strings.cpp
 * will fail to compile if the table and the enum drift apart.
 */
enum class Str {
    // --- application / tray ------------------------------------------------
    AppName,
    TrayCapture,
    TrayCaptureHotkeyTaken,
    TrayRetryHotkey,
    TraySettings,
    TrayQuit,
    TrayTooltipReady,
    TrayTooltipHotkeyTaken,
    HotkeyConflictTitle,
    HotkeyConflictBody,

    // --- overlay / text input ---------------------------------------------
    TextInputPlaceholder,
    TextHintClickToType,

    // --- annotation toolbar -----------------------------------------------
    ToolbarUndo,
    ToolbarCopy,
    ToolbarSave,
    ToolbarCancel,

    // --- saving ------------------------------------------------------------
    SaveDialogTitle,
    SaveFilterPng,
    SaveFilterJpeg,
    SaveFailedTitle,
    SaveFailedBody,

    // --- settings dialog ---------------------------------------------------
    SettingsTitle,
    GroupHotkey,
    HotkeyLabel,
    HotkeyHint,
    HotkeyInvalidTitle,
    HotkeyNeedsModifier,
    GroupGeneral,
    LanguageLabel,
    LanguageChinese,
    LanguageEnglish,
    AutoStartLabel,
    GroupSave,
    SaveDirLabel,
    BrowseButton,
    SaveFormatLabel,
    FormatPng,
    FormatJpeg,
    JpegQualityLabel,
    GroupCapture,
    IncludeCursorLabel,
    GroupAnnotations,
    RememberToolsLabel,
    RestoreDefaultsButton,

    Count
};

/// Look up a string in the language currently selected in Settings.
QString text(Str id);

/**
 * Loads (or removes) Qt's bundled translation catalogue so that standard widget
 * texts - QMessageBox buttons, the QLineEdit context menu - follow the selected
 * language. Safe to call repeatedly; call it once at startup and again whenever
 * the language changes.
 */
void installQtTranslations();

} // namespace qshot
