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
    { Str::ToolbarPin,               QStringLiteral("贴图"),                QStringLiteral("Pin") },
    { Str::ToolbarCancel,            QStringLiteral("取消"),                QStringLiteral("Cncl") },

    // --- pinned image window ----------------------------------------------
    { Str::PinMenuCopy,              QStringLiteral("复制"),                QStringLiteral("Copy") },
    { Str::PinMenuSaveAs,            QStringLiteral("另存为…"),             QStringLiteral("Save as…") },
    { Str::PinMenuClose,             QStringLiteral("关闭"),                QStringLiteral("Close") },

    // --- capture history ---------------------------------------------------
    { Str::TrayHistory,              QStringLiteral("历史记录"),            QStringLiteral("History") },
    { Str::TrayHistoryEmpty,         QStringLiteral("（暂无记录）"),        QStringLiteral("(no captures yet)") },
    { Str::TrayHistoryClear,         QStringLiteral("清空历史记录…"),       QStringLiteral("Clear history…") },
    { Str::HistoryCopy,              QStringLiteral("复制到剪贴板"),        QStringLiteral("Copy to clipboard") },
    { Str::HistorySaveAs,            QStringLiteral("另存为…"),             QStringLiteral("Save as…") },
    { Str::HistoryPin,               QStringLiteral("贴到屏幕上"),          QStringLiteral("Pin to screen") },
    { Str::HistoryDelete,            QStringLiteral("删除"),                QStringLiteral("Delete") },
    { Str::HistoryClearTitle,        QStringLiteral("清空历史记录"),        QStringLiteral("Clear history") },
    { Str::HistoryClearBody,         QStringLiteral("将删除全部 %1 张截图，此操作无法撤销。"),
                                     QStringLiteral("This deletes all %1 captures. It cannot be undone.") },
    { Str::HistoryCopiedTitle,       QStringLiteral("已复制"),              QStringLiteral("Copied") },
    { Str::HistoryCopiedBody,        QStringLiteral("截图已复制到剪贴板。"),
                                     QStringLiteral("The capture is on the clipboard.") },
    { Str::GroupHistory,             QStringLiteral("历史记录"),            QStringLiteral("History") },
    { Str::HistoryEnabledLabel,      QStringLiteral("保留最近的截图"),      QStringLiteral("Keep recent captures") },
    { Str::HistoryLimitLabel,        QStringLiteral("最多保留"),            QStringLiteral("Keep at most") },
    { Str::HistoryNotifyLabel,       QStringLiteral("复制后显示托盘提示"),  QStringLiteral("Notify after copying") },

    // --- saving ------------------------------------------------------------
    { Str::SaveDialogTitle,          QStringLiteral("保存截图"),            QStringLiteral("Save screenshot") },
    { Str::SaveFilterPng,            QStringLiteral("PNG 图片 (*.png)"),    QStringLiteral("PNG image (*.png)") },
    { Str::SaveFilterJpeg,           QStringLiteral("JPEG 图片 (*.jpg)"),   QStringLiteral("JPEG image (*.jpg)") },
    { Str::SaveFailedTitle,          QStringLiteral("保存失败"),            QStringLiteral("Save failed") },
    { Str::SaveFailedBody,           QStringLiteral("无法写入文件：\n%1"),
                                     QStringLiteral("Could not write to:\n%1") },
    { Str::SaveQuietLabel,           QStringLiteral("保存时不再询问，直接存到默认目录"),
                                     QStringLiteral("Save without asking, straight into the default folder") },
    { Str::SaveOnCopyLabel,          QStringLiteral("复制时同时在默认目录存一份"),
                                     QStringLiteral("Also write a file into the default folder when copying") },
    { Str::SavedTitle,               QStringLiteral("已保存"),              QStringLiteral("Saved") },
    { Str::SavedBody,                QStringLiteral("已保存到：\n%1"),
                                     QStringLiteral("Saved to:\n%1") },
    // Used instead of HistoryCopiedBody when the copy also wrote a file: one balloon
    // saying both beats two saying one each.
    { Str::HistoryCopiedSavedBody,   QStringLiteral("已复制，并保存到：\n%1"),
                                     QStringLiteral("Copied, and saved to:\n%1") },

    // --- startup -----------------------------------------------------------
    { Str::AlreadyRunningTitle,      QStringLiteral("快截已在运行"),
                                     QStringLiteral("QShot is already running") },
    // Shown by the second instance before it exits. It has to say what to do next,
    // because from the user's point of view "nothing happened" is indistinguishable
    // from "the program is broken".
    { Str::AlreadyRunningBody,       QStringLiteral("QShot 已经在运行，正驻留在托盘里等待快捷键。\n\n"
                                                    "看不到托盘图标的话，请检查是否被收进了「隐藏的图标」。"),
                                     QStringLiteral("QShot is already running and waiting in the tray for its hotkey.\n\n"
                                                    "If you cannot see the tray icon, check the hidden icons area.") },

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
