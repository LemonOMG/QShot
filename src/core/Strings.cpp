#include "Strings.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QTranslator>

#include <iterator>

#include "Settings.h"

namespace qshot {

namespace {

struct Entry {
    Str id;
    QString zh;
    QString en;
};

// The single place where user-visible text lives. Keep rows in the same order as
// the Str enum so the two can be eyeballed against each other.
const Entry kTable[] = {
    // --- application / tray ------------------------------------------------
    { Str::AppName,                  QStringLiteral("QShot (快截)"),        QStringLiteral("QShot") },
    { Str::TrayCapture,              QStringLiteral("截图 (%1)"),           QStringLiteral("Capture (%1)") },
    { Str::TrayCaptureHotkeyTaken,   QStringLiteral("截图 (热键被占用)"),   QStringLiteral("Capture (hotkey unavailable)") },
    { Str::TrayRetryHotkey,          QStringLiteral("重新注册热键"),        QStringLiteral("Re-register hotkey") },
    { Str::TraySettings,             QStringLiteral("设置…"),               QStringLiteral("Settings…") },
    { Str::TrayQuit,                 QStringLiteral("退出"),                QStringLiteral("Quit") },
    { Str::TrayTooltipReady,         QStringLiteral("%1 - 热键已注册"),     QStringLiteral("%1 - hotkey registered") },
    { Str::TrayTooltipHotkeyTaken,   QStringLiteral("%1 - 热键被占用"),     QStringLiteral("%1 - hotkey unavailable") },
    { Str::HotkeyConflictTitle,      QStringLiteral("快捷键冲突"),          QStringLiteral("Hotkey conflict") },
    { Str::HotkeyConflictBody,       QStringLiteral("快捷键 %1 已被占用，将自动重试。"),
                                     QStringLiteral("Hotkey %1 is already in use. Retrying automatically.") },

    // --- overlay / text input ---------------------------------------------
    { Str::TextInputPlaceholder,     QStringLiteral("输入文字…"),           QStringLiteral("Type here…") },
    { Str::TextHintClickToType,      QStringLiteral("点击输入文字"),        QStringLiteral("Click to type") },

    // --- annotation toolbar -----------------------------------------------
    // Kept short on purpose: these are drawn inside a 32px button.
    { Str::ToolbarUndo,              QStringLiteral("撤销"),                QStringLiteral("Undo") },
    { Str::ToolbarCopy,              QStringLiteral("复制"),                QStringLiteral("Copy") },
    { Str::ToolbarSave,              QStringLiteral("保存"),                QStringLiteral("Save") },
    { Str::ToolbarCancel,            QStringLiteral("取消"),                QStringLiteral("Cncl") },

    // --- saving ------------------------------------------------------------
    { Str::SaveDialogTitle,          QStringLiteral("保存截图"),            QStringLiteral("Save screenshot") },
    { Str::SaveFilterPng,            QStringLiteral("PNG 图片 (*.png)"),    QStringLiteral("PNG image (*.png)") },
    { Str::SaveFilterJpeg,           QStringLiteral("JPEG 图片 (*.jpg)"),   QStringLiteral("JPEG image (*.jpg)") },
    { Str::SaveFailedTitle,          QStringLiteral("保存失败"),            QStringLiteral("Save failed") },
    { Str::SaveFailedBody,           QStringLiteral("无法写入文件：\n%1"),
                                     QStringLiteral("Could not write to:\n%1") },

    // --- settings dialog ---------------------------------------------------
    { Str::SettingsTitle,            QStringLiteral("QShot 设置"),          QStringLiteral("QShot Settings") },
    { Str::GroupHotkey,              QStringLiteral("快捷键"),              QStringLiteral("Hotkey") },
    { Str::HotkeyLabel,              QStringLiteral("截图快捷键"),          QStringLiteral("Capture hotkey") },
    { Str::HotkeyHint,               QStringLiteral("需包含 Ctrl / Alt / Shift / Win，或使用 F1–F24"),
                                     QStringLiteral("Must include Ctrl / Alt / Shift / Win, or be one of F1–F24") },
    { Str::HotkeyInvalidTitle,       QStringLiteral("快捷键无效"),          QStringLiteral("Invalid hotkey") },
    { Str::HotkeyNeedsModifier,      QStringLiteral("快捷键必须包含 Ctrl / Alt / Shift / Win 修饰键，或使用 F1–F24 功能键。"),
                                     QStringLiteral("A hotkey must include Ctrl / Alt / Shift / Win, or be one of the F1–F24 keys.") },
    { Str::GroupGeneral,             QStringLiteral("通用"),                QStringLiteral("General") },
    { Str::LanguageLabel,            QStringLiteral("界面语言"),            QStringLiteral("Language") },
    { Str::LanguageChinese,          QStringLiteral("简体中文"),            QStringLiteral("简体中文") },
    { Str::LanguageEnglish,          QStringLiteral("English"),             QStringLiteral("English") },
    { Str::AutoStartLabel,           QStringLiteral("开机时自动启动"),      QStringLiteral("Start automatically when I log in") },
    { Str::GroupSave,                QStringLiteral("保存"),                QStringLiteral("Saving") },
    { Str::SaveDirLabel,             QStringLiteral("默认保存目录"),        QStringLiteral("Default folder") },
    { Str::BrowseButton,             QStringLiteral("浏览…"),               QStringLiteral("Browse…") },
    { Str::SaveFormatLabel,          QStringLiteral("图片格式"),            QStringLiteral("Image format") },
    { Str::FormatPng,                QStringLiteral("PNG（无损）"),         QStringLiteral("PNG (lossless)") },
    { Str::FormatJpeg,               QStringLiteral("JPEG（有损，体积小）"), QStringLiteral("JPEG (lossy, smaller)") },
    { Str::JpegQualityLabel,         QStringLiteral("JPEG 质量"),           QStringLiteral("JPEG quality") },
    { Str::GroupCapture,             QStringLiteral("截图"),                QStringLiteral("Capture") },
    { Str::IncludeCursorLabel,       QStringLiteral("截图中包含鼠标指针"),  QStringLiteral("Include the mouse cursor in screenshots") },
    { Str::GroupAnnotations,         QStringLiteral("标注"),                QStringLiteral("Annotations") },
    { Str::RememberToolsLabel,       QStringLiteral("记住上次使用的颜色和粗细"),
                                     QStringLiteral("Remember the last used colour and thickness") },
    { Str::RestoreDefaultsButton,    QStringLiteral("恢复默认"),            QStringLiteral("Restore defaults") },
};

// Guarantees a row exists for every enum value; a duplicate row would still slip
// through, but text() asserts on lookup misses so that cannot go unnoticed.
static_assert(std::size(kTable) == static_cast<std::size_t>(Str::Count),
              "Strings table and the Str enum are out of sync");

} // namespace

QString text(Str id) {
    const bool english = Settings::instance().language() == Language::English;
    for (const Entry& e : kTable) {
        if (e.id == id) {
            return english ? e.en : e.zh;
        }
    }
    Q_ASSERT_X(false, "qshot::text", "missing string table entry");
    return QStringLiteral("??");
}

void installQtTranslations() {
    // Held so the previous catalogue can be removed before a new one is loaded.
    static QTranslator* qtTranslator = nullptr;

    if (qtTranslator) {
        QCoreApplication::removeTranslator(qtTranslator);
        delete qtTranslator;
        qtTranslator = nullptr;
    }

    // English is Qt's source language, so nothing has to be loaded for it.
    if (Settings::instance().language() != Language::Chinese) return;

    const QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    auto* candidate = new QTranslator;
    if (candidate->load(QStringLiteral("qtbase_zh_CN"), dir)) {
        QCoreApplication::installTranslator(candidate);
        qtTranslator = candidate;
    } else {
        // Not fatal: only standard widget texts stay in English.
        delete candidate;
    }
}

} // namespace qshot
