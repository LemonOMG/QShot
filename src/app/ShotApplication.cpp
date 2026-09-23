#include "ShotApplication.h"
#include <QIcon>
#include <QAction>
#include <QDebug>
#include <QApplication>
#include <QPixmap>

#include "core/HistoryStore.h"
#include "core/PlatformFactory.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "overlay/SnapOverlay.h"
#include "pin/PinWindow.h"
#include "ui/HistoryMenu.h"
#include "ui/SettingsDialog.h"
#include <QCursor>
#include <QScreen>

#include <QTimer>

namespace qshot {

namespace {
/// Delay between automatic hotkey-registration attempts. Long enough not to hammer the
/// system while the conflicting process is still starting up.
constexpr int kHotkeyRetryIntervalMs = 5000;
} // namespace

ShotApplication::ShotApplication(QObject* parent)
    : QObject(parent)
    , trayIcon_(nullptr)
    , captureAction_(nullptr)
    , retryAction_(nullptr)
    , settingsAction_(nullptr)
    , quitAction_(nullptr)
    , historyMenu_(nullptr)
    , hotkeyRetryTimer_(new QTimer(this))
{
    // Qt's own catalogues (QMessageBox buttons, the line-edit context menu) have
    // to follow our language selection too.
    installQtTranslations();

    connect(hotkeyRetryTimer_, &QTimer::timeout, this, &ShotApplication::onHotkeyRetryTimeout);

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
    // globalHotkey_ deletes itself: it is a unique_ptr member with no QObject parent, so it
    // is released right after this body and before ~QObject. Deleting it here as well used
    // to be the second half of a redundant pair -- the factory was handed `this` as a
    // parent, so the pointer was owned in two places at once.
    if (trayIcon_) {
        trayIcon_->hide();
        // Detach before `trayMenu_` is destroyed. QSystemTrayIcon::setContextMenu() does
        // not take ownership, and the tray icon outlives every member: it is a QObject
        // child of `this`, so it is deleted by ~QObject *after* the members are gone.
        // Leaving the pointer in place would have the icon's destructor looking at freed
        // memory.
        trayIcon_->setContextMenu(nullptr);
    }
    // trayMenu_ is a unique_ptr, so it is released here rather than leaked. It used to be a
    // bare `new QMenu()` with no parent -- QMenu takes a QWidget parent, not a QObject one,
    // so there was nowhere to hand it to.
}

void ShotApplication::initTrayIcon() {
    trayIcon_ = new QSystemTrayIcon(this);

    // From the embedded resource, not a file on disk. The .ico carries eight sizes and
    // Windows picks the right one per context -- the tray wants 16px, alt-tab 32, Explorer
    // 256 -- so a single scaled pixmap would look soft in most of them.
    trayIcon_->setIcon(QIcon(QStringLiteral(":/icons/qshot.ico")));

    // Owned by the unique_ptr member, not by a parent: QMenu takes a QWidget parent and
    // there is no widget to give it -- `this` is a QObject. A bare `new QMenu()` leaked.
    trayMenu_ = std::make_unique<QMenu>();

    captureAction_ = new QAction(this);
    connect(captureAction_, &QAction::triggered, this, &ShotApplication::onCaptureTriggered);
    trayMenu_->addAction(captureAction_);

    retryAction_ = new QAction(this);
    // The tray's retry is a user action, so it goes through registerGlobalHotkeys() rather
    // than the timer's attempt path: only the automatic loop spends the attempt budget.
    connect(retryAction_, &QAction::triggered, this, &ShotApplication::registerGlobalHotkeys);
    trayMenu_->addAction(retryAction_);

    settingsAction_ = new QAction(this);
    connect(settingsAction_, &QAction::triggered, this, &ShotApplication::onSettingsTriggered);
    trayMenu_->addAction(settingsAction_);

    // Above the settings entry and below capture: it is a way of getting a capture back,
    // so it belongs with the capture actions rather than with the configuration.
    historyMenu_ = new HistoryMenu(HistoryStore::instance(), trayMenu_.get());
    connect(historyMenu_, &HistoryMenu::pinRequested,
            this, &ShotApplication::onHistoryPinRequested);
    // History copies carry no saved path on purpose: the capture was written when it was
    // first taken, so "also save when copying" must not drop a second identical file every
    // time the user re-copies an old capture from this menu.
    connect(historyMenu_, &HistoryMenu::copied, this,
            [this]() { onCaptureCopied(QString()); });
    trayMenu_->addMenu(historyMenu_);

    trayMenu_->addSeparator();

    quitAction_ = new QAction(this);
    connect(quitAction_, &QAction::triggered, this, &ShotApplication::onQuitTriggered);
    trayMenu_->addAction(quitAction_);

    retranslate();

    trayIcon_->setContextMenu(trayMenu_.get());
    trayIcon_->show();
}

void ShotApplication::retranslate() {
    if (trayIcon_) {
        trayIcon_->setToolTip(text(Str::AppName));
    }
    if (retryAction_)    retryAction_->setText(text(Str::TrayRetryHotkey));
    if (settingsAction_) settingsAction_->setText(text(Str::TraySettings));
    if (quitAction_)     quitAction_->setText(text(Str::TrayQuit));
    // Only the submenu's own title: its entries are generated in rebuild(), which reads
    // the current language every time the menu opens, so they cannot go stale.
    if (historyMenu_)    historyMenu_->setTitle(text(Str::TrayHistory));

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
    // Every caller of this one is a deliberate act -- startup, a hotkey change, or the
    // tray's retry entry -- so the attempt budget starts over. Only the timer's own path
    // (onHotkeyRetryTimeout) spends it.
    hotkeyRetriesLeft_ = kMaxHotkeyRetries;
    attemptHotkeyRegistration();
}

void ShotApplication::attemptHotkeyRegistration() {
    if (!globalHotkey_) {
        // No parent: the unique_ptr owns it (see the header).
        globalHotkey_.reset(PlatformFactory::createGlobalHotkey());

        // The factory returns nullptr on platforms without an implementation.
        // Without this guard the very next call would dereference it.
        if (!globalHotkey_) {
            qWarning() << "No global hotkey implementation for this platform.";
            updateTrayStatus(false);
            return;
        }

        connect(globalHotkey_.get(), &IGlobalHotkey::hotkeyPressed,
                this, &ShotApplication::onCaptureTriggered);
    }

    const QKeySequence hotkey = Settings::instance().hotkey();
    const bool success = globalHotkey_->registerHotkey(hotkey);
    updateTrayStatus(success);

    if (success) {
        hotkeyRetryTimer_->stop();
        return;
    }

    if (hotkeyRetriesLeft_ <= 0) {
        // Out of automatic attempts. The tray menu already offers a manual retry and the
        // tooltip already says the hotkey is not registered, so the only thing left is to
        // stop -- the previous behaviour was to keep trying every 5 seconds forever, which
        // for the common cause of this failure (another process holding the hotkey) means
        // an endless stream of log lines and timer wakeups for a condition that cannot
        // resolve itself.
        qWarning() << "Failed to register global hotkey" << hotkey.toString()
                   << "- giving up after" << kMaxHotkeyRetries
                   << "automatic attempts; the tray menu can retry.";
        return;
    }

    --hotkeyRetriesLeft_;
    qWarning() << "Failed to register global hotkey" << hotkey.toString()
               << "- retrying in" << (kHotkeyRetryIntervalMs / 1000) << "seconds ("
               << hotkeyRetriesLeft_ << "attempts left )";

    // Only show the balloon on the first failure; the loop would otherwise pop one every
    // 5 seconds.
    if (trayIcon_ && !hotkeyRetryTimer_->isActive()) {
        trayIcon_->showMessage(
            text(Str::HotkeyConflictTitle),
            text(Str::HotkeyConflictBody)
                .arg(hotkey.toString(QKeySequence::NativeText)),
            QSystemTrayIcon::Warning,
            5000);
    }
    hotkeyRetryTimer_->start(kHotkeyRetryIntervalMs);
}

void ShotApplication::onHotkeyRetryTimeout() {
    attemptHotkeyRegistration();
}

void ShotApplication::onCaptureTriggered() {
    // 如果已经有处于激活状态的截图叠加层，再次按下快捷键则退出当前截图（类似于 Toggle 机制）
    if (!currentOverlays_.isEmpty()) {
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

    // The overlay covering the screen the cursor is on is the one the user is looking
    // at, and activation is the only thing that decides where key events go (Esc is
    // already global: it closes this overlay, and the `closed` handler above takes the
    // rest down with it). Picking whichever overlay the loop happened to visit last
    // would be arbitrary -- on a multi-screen setup that is simply the last screen.
    const QPoint cursorPos = QCursor::pos();
    SnapOverlay* onCursorScreen = nullptr;
    SnapOverlay* firstOverlay = nullptr;

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
        
        connect(overlay, &SnapOverlay::pinRequested, this, &ShotApplication::onPinRequested);
        connect(overlay, &SnapOverlay::captureCopied, this, &ShotApplication::onCaptureCopied);
        connect(overlay, &SnapOverlay::captureSaved, this, &ShotApplication::onCaptureSaved);
        
        // Geometry is already applied by the SnapOverlay constructor.
        overlay->show();

        if (!firstOverlay) firstOverlay = overlay;
        if (screen->geometry().contains(cursorPos)) onCursorScreen = overlay;
    }

    // Showing a top-level window does not guarantee it becomes the active one, and an
    // inactive overlay never receives Esc / Enter / Ctrl+Z. Prefer the screen the
    // cursor is on; fall back to the first one so something is always activated.
    SnapOverlay* const focusTarget = onCursorScreen ? onCursorScreen : firstOverlay;
    if (focusTarget) {
        focusTarget->activateWindow();
    }
}

void ShotApplication::onPinRequested(const QImage& image, const QRect& globalRect) {
    if (image.isNull()) return;

    // Parentless on purpose: the pin has to outlive the overlay, which is closing
    // right now. It is created with WA_DeleteOnClose, so there is nothing to track
    // here -- and setQuitOnLastWindowClosed(false) in main.cpp means having no
    // windows at all is already a normal state for this tray application.
    PinWindow* pin = new PinWindow(image);
    pin->showAt(globalRect.topLeft());
}

void ShotApplication::onHistoryPinRequested(const QImage& image) {
    if (image.isNull()) return;

    // Same ownership as onPinRequested(): parentless, self-deleting.
    PinWindow* pin = new PinWindow(image);
    // No originating selection here, so the pin has to find its own place.
    pin->showCentredOnCursorScreen();
}

void ShotApplication::onCaptureCopied(const QString& savedPath) {
    if (!trayIcon_) return;

    // Copying closes the overlay and shows nothing else, so this is the only feedback the
    // user gets. Deliberately not shown after a *dialog* save: the dialog already confirmed
    // that, and a balloon on top of it would be noise. A silent save is the opposite case
    // -- the balloon *is* the confirmation -- so it is shown regardless of the copy
    // notification setting, and it says both things rather than popping two balloons.
    if (!savedPath.isEmpty()) {
        trayIcon_->showMessage(text(Str::HistoryCopiedTitle),
                               text(Str::HistoryCopiedSavedBody).arg(savedPath),
                               QSystemTrayIcon::Information, 2500);
        return;
    }

    if (!Settings::instance().historyNotifyOnCopy()) return;
    trayIcon_->showMessage(text(Str::HistoryCopiedTitle), text(Str::HistoryCopiedBody),
                           QSystemTrayIcon::Information, 2500);
}

void ShotApplication::onCaptureSaved(const QString& path) {
    if (!trayIcon_ || path.isEmpty()) return;

    // Unconditional, unlike the copy notification: with the file dialog switched off this
    // balloon is the only evidence the save happened at all.
    trayIcon_->showMessage(text(Str::SavedTitle), text(Str::SavedBody).arg(path),
                           QSystemTrayIcon::Information, 2500);
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
    QApplication::quit();
}

} // namespace qshot
