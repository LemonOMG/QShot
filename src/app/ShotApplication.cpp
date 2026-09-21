#include "ShotApplication.h"
#include <QIcon>
#include <QAction>
#include <QDebug>
#include <QApplication>
#include <QPixmap>

#include "core/PlatformFactory.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "overlay/SnapOverlay.h"
#include "ui/SettingsDialog.h"
#include <QScreen>

#include <QTimer>

namespace qshot {

ShotApplication::ShotApplication(QObject* parent)
    : QObject(parent)
    , trayIcon_(nullptr)
    , trayMenu_(nullptr)
    , captureAction_(nullptr)
    , retryAction_(nullptr)
    , settingsAction_(nullptr)
    , quitAction_(nullptr)
    , hotkeyRetryTimer_(new QTimer(this))
    , globalHotkey_(nullptr) 
{
    // Qt's own catalogues (QMessageBox buttons, the line-edit context menu) have
    // to follow our language selection too.
    installQtTranslations();

    connect(hotkeyRetryTimer_, &QTimer::timeout, this, &ShotApplication::onRetryHotkey);

    // The settings dialog only writes Settings. The side effects that need the
    // rest of the application are wired up here, so the dialog stays free of any
    // knowledge about hotkey registration or autostart.
    Settings& settings = Settings::instance();
    connect(&settings, &Settings::hotkeyChanged, this, &ShotApplication::registerGlobalHotkeys);
    connect(&settings, &Settings::changed, this, &ShotApplication::applyAutoStart);
    connect(&settings, &Settings::languageChanged, this, [this]() {
        installQtTranslations();
        retranslate();
    });

    autoStart_ = PlatformFactory::createAutoStart();

    initTrayIcon();
    registerGlobalHotkeys();
    applyAutoStart();
}

ShotApplication::~ShotApplication() {
    if (globalHotkey_) {
        delete globalHotkey_;
        globalHotkey_ = nullptr;
    }
    if (trayIcon_) {
        trayIcon_->hide();
    }
}

void ShotApplication::initTrayIcon() {
    trayIcon_ = new QSystemTrayIcon(this);
    
    // Create a temporary icon until resources are added
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::blue);
    trayIcon_->setIcon(QIcon(pixmap));

    trayMenu_ = new QMenu();

    captureAction_ = new QAction(this);
    connect(captureAction_, &QAction::triggered, this, &ShotApplication::onCaptureTriggered);
    trayMenu_->addAction(captureAction_);

    retryAction_ = new QAction(this);
    connect(retryAction_, &QAction::triggered, this, &ShotApplication::onRetryHotkey);
    trayMenu_->addAction(retryAction_);

    settingsAction_ = new QAction(this);
    connect(settingsAction_, &QAction::triggered, this, &ShotApplication::onSettingsTriggered);
    trayMenu_->addAction(settingsAction_);

    trayMenu_->addSeparator();

    quitAction_ = new QAction(this);
    connect(quitAction_, &QAction::triggered, this, &ShotApplication::onQuitTriggered);
    trayMenu_->addAction(quitAction_);

    retranslate();

    trayIcon_->setContextMenu(trayMenu_);
    trayIcon_->show();
}

void ShotApplication::retranslate() {
    if (trayIcon_) {
        trayIcon_->setToolTip(text(Str::AppName));
    }
    if (retryAction_)    retryAction_->setText(text(Str::TrayRetryHotkey));
    if (settingsAction_) settingsAction_->setText(text(Str::TraySettings));
    if (quitAction_)     quitAction_->setText(text(Str::TrayQuit));

    // The capture entry embeds the current hotkey, so let updateTrayStatus()
    // rebuild it rather than duplicating that logic here.
    updateTrayStatus(hotkeyRegistered_);
}

void ShotApplication::updateTrayStatus(bool registered) {
    hotkeyRegistered_ = registered;
    if (!trayIcon_ || !captureAction_ || !retryAction_) return;

    const QString appName = text(Str::AppName);
    const QString hotkey =
        Settings::instance().hotkey().toString(QKeySequence::NativeText);

    if (registered) {
        trayIcon_->setToolTip(text(Str::TrayTooltipReady).arg(appName));
        retryAction_->setVisible(false);
        captureAction_->setText(text(Str::TrayCapture).arg(hotkey));
        captureAction_->setEnabled(true);
    } else {
        trayIcon_->setToolTip(text(Str::TrayTooltipHotkeyTaken).arg(appName));
        retryAction_->setVisible(true);
        captureAction_->setText(text(Str::TrayCaptureHotkeyTaken));
        captureAction_->setEnabled(false); // We could leave it enabled for manual click
    }
}

void ShotApplication::registerGlobalHotkeys() {
    if (!globalHotkey_) {
        globalHotkey_ = PlatformFactory::createGlobalHotkey(this);

        // The factory returns nullptr on platforms without an implementation.
        // Without this guard the very next call would dereference it.
        if (!globalHotkey_) {
            qWarning() << "No global hotkey implementation for this platform.";
            updateTrayStatus(false);
            return;
        }

        connect(globalHotkey_, &IGlobalHotkey::hotkeyPressed,
                this, &ShotApplication::onCaptureTriggered);
    }

    const QKeySequence hotkey = Settings::instance().hotkey();
    const bool success = globalHotkey_->registerHotkey(hotkey);
    updateTrayStatus(success);
    
    if (!success) {
        qWarning() << "Failed to register global hotkey" << hotkey.toString()
                   << "- retrying in 5 seconds.";
        // Only show the message the first time it fails; a retry loop would
        // otherwise pop a balloon every 5 seconds.
        if (trayIcon_ && !hotkeyRetryTimer_->isActive()) {
            trayIcon_->showMessage(
                text(Str::HotkeyConflictTitle),
                text(Str::HotkeyConflictBody)
                    .arg(hotkey.toString(QKeySequence::NativeText)),
                QSystemTrayIcon::Warning,
                5000);
        }
        hotkeyRetryTimer_->start(5000); // Retry every 5 seconds
    } else {
        qDebug() << "Global hotkey registered successfully:" << hotkey.toString();
        hotkeyRetryTimer_->stop();
    }
}

void ShotApplication::onRetryHotkey() {
    qDebug() << "Manually retrying hotkey registration...";
    registerGlobalHotkeys();
}

void ShotApplication::onCaptureTriggered() {
    qDebug() << "Capture triggered!";
    
    // 如果已经有处于激活状态的截图叠加层，再次按下快捷键则退出当前截图（类似于 Toggle 机制）
    if (!currentOverlays_.isEmpty()) {
        qDebug() << "Overlays already active, closing them.";
        for (auto& overlay : currentOverlays_) {
            if (overlay) overlay->close();
        }
        currentOverlays_.clear();
        return;
    }
    
    auto capture = PlatformFactory::createScreenCapture();
    if (!capture) {
        qWarning() << "No screen capture implementation for this platform.";
        return;
    }

    for (QScreen* screen : QGuiApplication::screens()) {
        QPixmap screenPixmap = capture->captureScreen(screen, Settings::instance().includeCursor());
        if (screenPixmap.isNull()) continue;
        
        SnapOverlay* overlay = new SnapOverlay(screenPixmap, screen->geometry());
        currentOverlays_.append(overlay);
        
        connect(overlay, &SnapOverlay::closed, this, [this, overlay]() {
            // Close all others
            for (auto& other : currentOverlays_) {
                if (other && other != overlay) {
                    other->close();
                }
            }
            currentOverlays_.clear();
        });
        
        // Geometry is already applied by the SnapOverlay constructor.
        overlay->show();
        // Showing a top-level window does not guarantee it becomes the active one,
        // and an inactive overlay never receives Esc / Enter / Ctrl+Z. With several
        // screens the last one shown ends up active (see the multi-screen notes in
        // docs/CODE_REVIEW_ROUND3.md).
        overlay->activateWindow();
    }
}

void ShotApplication::onSettingsTriggered() {
    if (!settingsDialog_) {
        settingsDialog_ = new SettingsDialog(nullptr);
        // The dialog is the only long-lived window of a tray application; owning
        // its lifetime by "delete on close" avoids tracking it manually.
        settingsDialog_->setAttribute(Qt::WA_DeleteOnClose);
    }
    settingsDialog_->show();
    // A tray application has no other window to inherit activation from, so the
    // dialog has to claim it explicitly.
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
}

void ShotApplication::applyAutoStart() {
    if (!autoStart_) return;

    const bool wanted = Settings::instance().autoStart();
    if (wanted) {
        // Unconditional rewrite: it also refreshes the stored path when the
        // executable has moved since the entry was created.
        autoStart_->setEnabled(true);
    } else if (autoStart_->isEnabled()) {
        autoStart_->setEnabled(false);
    }
}

void ShotApplication::onQuitTriggered() {
    qDebug() << "Quit triggered!";
    QApplication::quit();
}

} // namespace qshot
