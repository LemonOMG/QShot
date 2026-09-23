#pragma once

#include <QImage>
#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QList>
#include <QPointer>
#include <QRect>
#include <memory>

#include "core/IGlobalHotkey.h"
#include "core/IAutoStart.h"

class QAction;
class QTimer;

namespace qshot {

class SnapOverlay;
class SettingsDialog;
class HistoryMenu;

/// How many times a failed hotkey registration is retried automatically before giving up
/// (12 x 5s = about a minute). Bounded because the usual cause -- another process holding
/// the hotkey, most often a second copy of QShot -- does not resolve itself, and retrying
/// forever only fills the log. The tray menu keeps a manual retry for when it does.
constexpr int kMaxHotkeyRetries = 12;

class ShotApplication : public QObject {
    Q_OBJECT
public:
    explicit ShotApplication(QObject* parent = nullptr);
    ~ShotApplication() override;

private slots:
    void onCaptureTriggered();
    void onSettingsTriggered();
    void onQuitTriggered();
    /// The automatic retry timer fired. Separate from the tray's retry entry on purpose:
    /// only this path spends the attempt budget, so pressing "retry" always gets a fresh
    /// set of tries.
    void onHotkeyRetryTimeout();
    /// Pin a finished capture on screen. Owned by nobody: the window deletes itself
    /// when closed, and it deliberately outlives the overlay that produced it.
    void onPinRequested(const QImage& image, const QRect& globalRect);
    /// Pin a capture picked from the history menu, which has no position to inherit.
    void onHistoryPinRequested(const QImage& image);
    /// Confirm a copy on the clipboard, if the user asked to be told. `savedPath` is
    /// non-empty when the copy also wrote a file, which is worth saying in the same
    /// balloon rather than in a second one.
    void onCaptureCopied(const QString& savedPath);
    /// Confirm a save that happened without a file dialog -- there the balloon is the
    /// only evidence, so it is shown whether or not notifications are enabled.
    void onCaptureSaved(const QString& path);

private:
    void initTrayIcon();
    /// Register the global hotkey, resetting the automatic-retry budget. Startup, a hotkey
    /// change and the tray's retry entry all land here.
    void registerGlobalHotkeys(); // 预留全局快捷键注册接口
    /// One attempt. Decrements the budget and stops the timer when it runs out.
    void attemptHotkeyRegistration();
    void updateTrayStatus(bool registered);
    /// Re-apply every user-visible string. Called once at startup and again
    /// whenever the language changes.
    void retranslate();
    /// Make the login item match the stored preference.
    void applyAutoStart();

    QSystemTrayIcon* trayIcon_;
    /// Owned here rather than by a parent: QMenu wants a QWidget parent and `this` is a
    /// QObject, so the alternatives were a raw pointer that leaked or this.
    std::unique_ptr<QMenu> trayMenu_;
    QAction* captureAction_;
    QAction* retryAction_;
    QAction* settingsAction_;
    QAction* quitAction_;
    HistoryMenu* historyMenu_;
    
    QTimer* hotkeyRetryTimer_;
    int hotkeyRetriesLeft_ = kMaxHotkeyRetries;
    
    /// Owned by the pointer, not by a QObject parent: the factory used to be handed `this`,
    /// and the destructor then deleted it by hand as well. Only one of those can be the
    /// ownership, so the pointer is it -- the same shape as autoStart_ below.
    std::unique_ptr<IGlobalHotkey> globalHotkey_;
    bool hotkeyRegistered_ = false;
    std::unique_ptr<IAutoStart> autoStart_;
    QPointer<SettingsDialog> settingsDialog_;
    QList<QPointer<SnapOverlay>> currentOverlays_;
};

} // namespace qshot
