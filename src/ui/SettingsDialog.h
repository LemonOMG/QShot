#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace qshot {

/**
 * The settings window, opened from the tray menu.
 *
 * Everything is applied when the user presses OK, so Cancel is a real cancel.
 * The one visible consequence is that switching the language does not retranslate
 * the window while it is open; it takes effect when the dialog is reopened.
 *
 * The dialog only reads and writes Settings. It deliberately knows nothing about
 * how a hotkey gets registered or how autostart is implemented - those side
 * effects belong to ShotApplication, which reacts to the Settings signals.
 */
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onBrowse();
    void onFormatChanged();
    void onHistoryToggled();
    void onRestoreDefaults();
    void onAccept();

private:
    /// Push the current Settings values into the widgets.
    void loadFromSettings();
    /// Re-apply every visible string (used after the language may have changed).
    void retranslate();

    QGroupBox* hotkeyGroup_;
    QGroupBox* generalGroup_;
    QGroupBox* saveGroup_;
    QGroupBox* captureGroup_;
    QGroupBox* annotationGroup_;
    QGroupBox* historyGroup_;

    QLabel* hotkeyLabel_;
    QLabel* hotkeyHint_;
    QLabel* languageLabel_;
    QLabel* saveDirLabel_;
    QLabel* saveFormatLabel_;
    QLabel* jpegQualityLabel_;
    QLabel* historyLimitLabel_;

    QKeySequenceEdit* hotkeyEdit_;
    QComboBox* languageCombo_;
    QCheckBox* autoStartCheck_;
    QLineEdit* saveDirEdit_;
    QPushButton* browseButton_;
    QComboBox* formatCombo_;
    QSpinBox* qualitySpin_;
    QCheckBox* quietSaveCheck_;
    QCheckBox* saveOnCopyCheck_;
    QCheckBox* includeCursorCheck_;
    QCheckBox* rememberToolsCheck_;
    QCheckBox* historyEnabledCheck_;
    QSpinBox* historyLimitSpin_;
    QCheckBox* historyNotifyCheck_;
    QPushButton* restoreButton_;
};

} // namespace qshot
