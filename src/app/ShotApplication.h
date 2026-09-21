#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QList>
#include <QPointer>
#include <memory>

#include "core/IGlobalHotkey.h"
#include "core/IAutoStart.h"

class QAction;
class QTimer;

namespace qshot {

class SnapOverlay;
class SettingsDialog;

class ShotApplication : public QObject {
    Q_OBJECT
public:
    explicit ShotApplication(QObject* parent = nullptr);
    ~ShotApplication() override;

private slots:
    void onCaptureTriggered();
    void onSettingsTriggered();
    void onQuitTriggered();
    void onRetryHotkey();

private:
    void initTrayIcon();
    void registerGlobalHotkeys(); // 预留全局快捷键注册接口
    void updateTrayStatus(bool registered);
    /// Re-apply every user-visible string. Called once at startup and again
    /// whenever the language changes.
    void retranslate();
    /// Make the login item match the stored preference.
    void applyAutoStart();

    QSystemTrayIcon* trayIcon_;
    QMenu* trayMenu_;
    QAction* captureAction_;
    QAction* retryAction_;
    QAction* settingsAction_;
    QAction* quitAction_;
    
    QTimer* hotkeyRetryTimer_;
    
    IGlobalHotkey* globalHotkey_;
    bool hotkeyRegistered_ = false;
    std::unique_ptr<IAutoStart> autoStart_;
    QPointer<SettingsDialog> settingsDialog_;
    QList<QPointer<SnapOverlay>> currentOverlays_;
};

} // namespace qshot
