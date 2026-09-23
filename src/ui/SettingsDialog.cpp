#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "../core/Settings.h"
#include "../core/Strings.h"

namespace qshot {

namespace {

constexpr int kDialogMinWidth = 460;

/// A modifier-free letter would swallow that key in every application, so a
/// modifier is required - except for the function keys, which are the
/// conventional modifier-free choice for a global hotkey.
bool isAcceptableHotkey(const QKeySequence& seq) {
    if (seq.isEmpty()) return false;

    const QKeyCombination combo = seq[0];
    if (combo.keyboardModifiers() != Qt::NoModifier) return true;

    const int key = combo.key();
    return key >= Qt::Key_F1 && key <= Qt::Key_F24;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(text(Str::SettingsTitle));
    setMinimumWidth(kDialogMinWidth);

    auto* root = new QVBoxLayout(this);

    // --- hotkey -------------------------------------------------------------
    hotkeyGroup_ = new QGroupBox(this);
    auto* hotkeyForm = new QFormLayout(hotkeyGroup_);

    hotkeyEdit_ = new QKeySequenceEdit(hotkeyGroup_);
    // A single chord: the Windows hotkey API registers exactly one key plus
    // modifiers, so allowing "Ctrl+K, Ctrl+C" style sequences would be a lie.
    hotkeyEdit_->setMaximumSequenceLength(1);

    hotkeyLabel_ = new QLabel(hotkeyGroup_);
    hotkeyForm->addRow(hotkeyLabel_, hotkeyEdit_);

    hotkeyHint_ = new QLabel(hotkeyGroup_);
    hotkeyHint_->setWordWrap(true);
    QFont hintFont = hotkeyHint_->font();
    hintFont.setPointSizeF(qMax(7.0, hintFont.pointSizeF() - 1.0));
    hotkeyHint_->setFont(hintFont);
    hotkeyHint_->setEnabled(false); // greyed out
    hotkeyForm->addRow(QString(), hotkeyHint_);

    root->addWidget(hotkeyGroup_);

    // --- general ------------------------------------------------------------
    generalGroup_ = new QGroupBox(this);
    auto* generalForm = new QFormLayout(generalGroup_);

    languageCombo_ = new QComboBox(generalGroup_);
    // Language names are shown in their own language on purpose: someone who
    // cannot read the current UI language still has to find their own.
    languageCombo_->addItem(text(Str::LanguageChinese), static_cast<int>(Language::Chinese));
    languageCombo_->addItem(text(Str::LanguageEnglish), static_cast<int>(Language::English));

    languageLabel_ = new QLabel(generalGroup_);
    generalForm->addRow(languageLabel_, languageCombo_);

    autoStartCheck_ = new QCheckBox(generalGroup_);
    generalForm->addRow(autoStartCheck_);

    root->addWidget(generalGroup_);

    // --- saving -------------------------------------------------------------
    saveGroup_ = new QGroupBox(this);
    auto* saveForm = new QFormLayout(saveGroup_);

    saveDirEdit_ = new QLineEdit(saveGroup_);
    browseButton_ = new QPushButton(saveGroup_);
    connect(browseButton_, &QPushButton::clicked, this, &SettingsDialog::onBrowse);

    auto* dirRow = new QWidget(saveGroup_);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->addWidget(saveDirEdit_, 1);
    dirLayout->addWidget(browseButton_);

    saveDirLabel_ = new QLabel(saveGroup_);
    saveForm->addRow(saveDirLabel_, dirRow);

    // Right under the folder it applies to: this toggle is about *that* folder, so putting
    // it anywhere else in the group would make the reader connect the two themselves.
    quietSaveCheck_ = new QCheckBox(saveGroup_);
    saveForm->addRow(quietSaveCheck_);

    formatCombo_ = new QComboBox(saveGroup_);
    formatCombo_->addItem(text(Str::FormatPng), static_cast<int>(SaveFormat::Png));
    formatCombo_->addItem(text(Str::FormatJpeg), static_cast<int>(SaveFormat::Jpeg));
    connect(formatCombo_, &QComboBox::currentIndexChanged,
            this, &SettingsDialog::onFormatChanged);

    saveFormatLabel_ = new QLabel(saveGroup_);
    saveForm->addRow(saveFormatLabel_, formatCombo_);

    qualitySpin_ = new QSpinBox(saveGroup_);
    qualitySpin_->setRange(1, 100);
    jpegQualityLabel_ = new QLabel(saveGroup_);
    saveForm->addRow(jpegQualityLabel_, qualitySpin_);

    // Last in the group: it is about the copy action rather than about the folder or the
    // format, and it is the only row here that writes a file the user did not ask for by
    // name -- so it reads best after everything that describes what the file looks like.
    saveOnCopyCheck_ = new QCheckBox(saveGroup_);
    saveForm->addRow(saveOnCopyCheck_);

    root->addWidget(saveGroup_);

    // --- capture ------------------------------------------------------------
    captureGroup_ = new QGroupBox(this);
    auto* captureLayout = new QVBoxLayout(captureGroup_);
    includeCursorCheck_ = new QCheckBox(captureGroup_);
    captureLayout->addWidget(includeCursorCheck_);
    root->addWidget(captureGroup_);

    // --- annotations --------------------------------------------------------
    annotationGroup_ = new QGroupBox(this);
    auto* annotationLayout = new QVBoxLayout(annotationGroup_);
    rememberToolsCheck_ = new QCheckBox(annotationGroup_);
    annotationLayout->addWidget(rememberToolsCheck_);
    root->addWidget(annotationGroup_);

    // --- history ------------------------------------------------------------
    historyGroup_ = new QGroupBox(this);
    auto* historyForm = new QFormLayout(historyGroup_);

    historyEnabledCheck_ = new QCheckBox(historyGroup_);
    connect(historyEnabledCheck_, &QCheckBox::toggled, this, &SettingsDialog::onHistoryToggled);
    historyForm->addRow(historyEnabledCheck_);

    historyLimitSpin_ = new QSpinBox(historyGroup_);
    historyLimitSpin_->setRange(kMinHistoryLimit, kMaxHistoryLimit);
    historyLimitLabel_ = new QLabel(historyGroup_);
    historyForm->addRow(historyLimitLabel_, historyLimitSpin_);

    historyNotifyCheck_ = new QCheckBox(historyGroup_);
    historyForm->addRow(historyNotifyCheck_);

    root->addWidget(historyGroup_);

    // --- buttons ------------------------------------------------------------
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    restoreButton_ = buttons->addButton(QString(), QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(restoreButton_, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaults);
    root->addWidget(buttons);

    retranslate();
    loadFromSettings();
}

void SettingsDialog::retranslate() {
    setWindowTitle(text(Str::SettingsTitle));

    hotkeyGroup_->setTitle(text(Str::GroupHotkey));
    hotkeyLabel_->setText(text(Str::HotkeyLabel));
    hotkeyHint_->setText(text(Str::HotkeyHint));

    generalGroup_->setTitle(text(Str::GroupGeneral));
    languageLabel_->setText(text(Str::LanguageLabel));
    // Keep the current selection: the ids are what matter, not the labels.
    languageCombo_->setItemText(0, text(Str::LanguageChinese));
    languageCombo_->setItemText(1, text(Str::LanguageEnglish));
    autoStartCheck_->setText(text(Str::AutoStartLabel));

    saveGroup_->setTitle(text(Str::GroupSave));
    saveDirLabel_->setText(text(Str::SaveDirLabel));
    browseButton_->setText(text(Str::BrowseButton));
    quietSaveCheck_->setText(text(Str::SaveQuietLabel));
    saveFormatLabel_->setText(text(Str::SaveFormatLabel));
    formatCombo_->setItemText(0, text(Str::FormatPng));
    formatCombo_->setItemText(1, text(Str::FormatJpeg));
    jpegQualityLabel_->setText(text(Str::JpegQualityLabel));
    saveOnCopyCheck_->setText(text(Str::SaveOnCopyLabel));

    captureGroup_->setTitle(text(Str::GroupCapture));
    includeCursorCheck_->setText(text(Str::IncludeCursorLabel));

    annotationGroup_->setTitle(text(Str::GroupAnnotations));
    rememberToolsCheck_->setText(text(Str::RememberToolsLabel));

    historyGroup_->setTitle(text(Str::GroupHistory));
    historyEnabledCheck_->setText(text(Str::HistoryEnabledLabel));
    historyLimitLabel_->setText(text(Str::HistoryLimitLabel));
    historyNotifyCheck_->setText(text(Str::HistoryNotifyLabel));

    restoreButton_->setText(text(Str::RestoreDefaultsButton));
}

void SettingsDialog::loadFromSettings() {
    const Settings& s = Settings::instance();

    hotkeyEdit_->setKeySequence(s.hotkey());
    languageCombo_->setCurrentIndex(s.language() == Language::English ? 1 : 0);
    autoStartCheck_->setChecked(s.autoStart());
    saveDirEdit_->setText(s.saveDirectory());
    formatCombo_->setCurrentIndex(s.saveFormat() == SaveFormat::Jpeg ? 1 : 0);
    qualitySpin_->setValue(s.jpegQuality());
    quietSaveCheck_->setChecked(s.quietSave());
    saveOnCopyCheck_->setChecked(s.saveOnCopy());
    includeCursorCheck_->setChecked(s.includeCursor());
    rememberToolsCheck_->setChecked(s.rememberToolSettings());
    historyEnabledCheck_->setChecked(s.historyEnabled());
    historyLimitSpin_->setValue(s.historyLimit());
    historyNotifyCheck_->setChecked(s.historyNotifyOnCopy());

    onFormatChanged();
    onHistoryToggled();
}

void SettingsDialog::onFormatChanged() {
    // JPEG quality is meaningless for PNG, so hide rather than disable it -
    // a greyed-out row invites the user to hunt for a way to enable it.
    const bool jpeg = formatCombo_->currentData().toInt() == static_cast<int>(SaveFormat::Jpeg);
    jpegQualityLabel_->setVisible(jpeg);
    qualitySpin_->setVisible(jpeg);
}

void SettingsDialog::onHistoryToggled() {
    // Disabled rather than hidden here, unlike the JPEG quality row: hiding it would
    // make the group jump to a different height every time the box is ticked, and the
    // limit is still meaningful information ("what will it be if I turn this back on").
    const bool on = historyEnabledCheck_->isChecked();
    historyLimitLabel_->setEnabled(on);
    historyLimitSpin_->setEnabled(on);
    historyNotifyCheck_->setEnabled(on);
}

void SettingsDialog::onBrowse() {
    const QString dir = QFileDialog::getExistingDirectory(this, text(Str::SaveDirLabel),
                                                          saveDirEdit_->text());
    if (!dir.isEmpty()) {
        saveDirEdit_->setText(dir);
    }
}

void SettingsDialog::onRestoreDefaults() {
    Settings::instance().restoreDefaults();
    // The language may have just changed, so refresh the texts before reloading.
    retranslate();
    loadFromSettings();
}

void SettingsDialog::onAccept() {
    if (!isAcceptableHotkey(hotkeyEdit_->keySequence())) {
        QMessageBox::warning(this, text(Str::HotkeyInvalidTitle), text(Str::HotkeyNeedsModifier));
        hotkeyEdit_->setFocus();
        return;
    }

    Settings& s = Settings::instance();

    // Hotkey first: it is the only value that can be rejected by the system, and
    // ShotApplication reacts to hotkeyChanged() by re-registering.
    s.setHotkey(hotkeyEdit_->keySequence());
    s.setLanguage(languageCombo_->currentData().toInt() == static_cast<int>(Language::English)
                      ? Language::English
                      : Language::Chinese);
    s.setAutoStart(autoStartCheck_->isChecked());

    if (!saveDirEdit_->text().isEmpty()) {
        s.setSaveDirectory(saveDirEdit_->text());
    }
    s.setSaveFormat(formatCombo_->currentData().toInt() == static_cast<int>(SaveFormat::Jpeg)
                        ? SaveFormat::Jpeg
                        : SaveFormat::Png);
    s.setJpegQuality(qualitySpin_->value());
    s.setQuietSave(quietSaveCheck_->isChecked());
    s.setSaveOnCopy(saveOnCopyCheck_->isChecked());
    s.setIncludeCursor(includeCursorCheck_->isChecked());
    s.setRememberToolSettings(rememberToolsCheck_->isChecked());
    s.setHistoryEnabled(historyEnabledCheck_->isChecked());
    s.setHistoryLimit(historyLimitSpin_->value());
    s.setHistoryNotifyOnCopy(historyNotifyCheck_->isChecked());

    accept();
}

} // namespace qshot
