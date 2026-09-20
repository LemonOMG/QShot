#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include "core/IGlobalHotkey.h"
#include <QList>
#include <QPointer>

namespace qshot {

class SnapOverlay;

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

    QSystemTrayIcon* trayIcon_;
    QMenu* trayMenu_;
    QAction* captureAction_;
    QAction* retryAction_;
    
    QTimer* hotkeyRetryTimer_;
    
    IGlobalHotkey* globalHotkey_;
    QList<QPointer<SnapOverlay>> currentOverlays_;
};

} // namespace qshot
