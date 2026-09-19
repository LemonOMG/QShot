#include "ShotApplication.h"
#include <QIcon>
#include <QAction>
#include <QDebug>
#include <QApplication>
#include <QPixmap>

#include "platform/windows/WinScreenCapture.h"
#include "overlay/SnapOverlay.h"
#include "platform/windows/WinGlobalHotkey.h"
#include "core/IGlobalHotkey.h"
#include <QScreen>

#include <QTimer>

namespace qshot {

ShotApplication::ShotApplication(QObject* parent)
    : QObject(parent)
    , trayIcon_(nullptr)
    , trayMenu_(nullptr)
    , captureAction_(nullptr)
    , retryAction_(nullptr)
    , hotkeyRetryTimer_(new QTimer(this))
    , globalHotkey_(nullptr) 
{
    connect(hotkeyRetryTimer_, &QTimer::timeout, this, &ShotApplication::onRetryHotkey);
    
    initTrayIcon();
    registerGlobalHotkeys();
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
    trayIcon_->setToolTip(QStringLiteral("QShot (快截)"));

    trayMenu_ = new QMenu();

    captureAction_ = new QAction(QStringLiteral("截图 (Alt+A)"), this);
    connect(captureAction_, &QAction::triggered, this, &ShotApplication::onCaptureTriggered);
    trayMenu_->addAction(captureAction_);

    retryAction_ = new QAction(QStringLiteral("重新注册热键"), this);
    connect(retryAction_, &QAction::triggered, this, &ShotApplication::onRetryHotkey);
    trayMenu_->addAction(retryAction_);

    QAction* settingsAction = new QAction(QStringLiteral("设置"), this);
    connect(settingsAction, &QAction::triggered, this, &ShotApplication::onSettingsTriggered);
    trayMenu_->addAction(settingsAction);

    trayMenu_->addSeparator();

    QAction* quitAction = new QAction(QStringLiteral("退出"), this);
    connect(quitAction, &QAction::triggered, this, &ShotApplication::onQuitTriggered);
    trayMenu_->addAction(quitAction);

    trayIcon_->setContextMenu(trayMenu_);
    trayIcon_->show();
}

void ShotApplication::updateTrayStatus(bool registered) {
    if (!trayIcon_) return;
    
    if (registered) {
        trayIcon_->setToolTip(QStringLiteral("QShot (快截) - 热键已注册"));
        retryAction_->setVisible(false);
        captureAction_->setText(QStringLiteral("截图 (Alt+A)"));
        captureAction_->setEnabled(true);
    } else {
        trayIcon_->setToolTip(QStringLiteral("QShot (快截) - 热键被占用"));
        retryAction_->setVisible(true);
        captureAction_->setText(QStringLiteral("截图 (热键被占用)"));
        captureAction_->setEnabled(false); // We could leave it enabled for manual click
    }
}

void ShotApplication::registerGlobalHotkeys() {
    if (!globalHotkey_) {
        globalHotkey_ = new WinGlobalHotkey(this);
        
        // IGlobalHotkey inherits QObject and has hotkeyPressed() signal
        auto hotkeyObj = dynamic_cast<QObject*>(globalHotkey_);
        if (hotkeyObj) {
            connect(hotkeyObj, SIGNAL(hotkeyPressed()), this, SLOT(onCaptureTriggered()));
        }
    }
    
    bool success = globalHotkey_->registerHotkey(QStringLiteral("A"), Qt::AltModifier);
    updateTrayStatus(success);
    
    if (!success) {
        qWarning() << "Failed to register global hotkey (Alt+A). Retrying in 5 seconds...";
        if (trayIcon_ && !hotkeyRetryTimer_->isActive()) { // Only show message first time it fails
            trayIcon_->showMessage(QStringLiteral("快捷键冲突"), 
                                   QStringLiteral("快捷键(Alt+A)已被占用，将自动重试。"), 
                                   QSystemTrayIcon::Warning, 
                                   5000);
        }
        hotkeyRetryTimer_->start(5000); // Retry every 5 seconds
    } else {
        qDebug() << "Global hotkey Alt+A registered successfully.";
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
    if (currentOverlay_) {
        qDebug() << "Overlay already active, closing it.";
        currentOverlay_->close();
        return;
    }
    
    WinScreenCapture capture;
    QPixmap fullScreen = capture.captureEntireScreen();
    
    if (fullScreen.isNull()) {
        qWarning() << "Failed to capture screens.";
        return;
    }
    
    QRect virtualGeometry;
    for (QScreen* screen : QGuiApplication::screens()) {
        virtualGeometry = virtualGeometry.united(screen->geometry());
    }
    
    SnapOverlay* overlay = new SnapOverlay(fullScreen, virtualGeometry);
    currentOverlay_ = overlay;
    overlay->show();
}

void ShotApplication::onSettingsTriggered() {
    qDebug() << "Settings triggered!";
}

void ShotApplication::onQuitTriggered() {
    qDebug() << "Quit triggered!";
    QApplication::quit();
}

} // namespace qshot
