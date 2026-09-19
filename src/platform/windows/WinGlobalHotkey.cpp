#include "WinGlobalHotkey.h"
#include <QCoreApplication>
#include <QKeySequence>
#include <windows.h>

namespace qshot {

WinGlobalHotkey::WinGlobalHotkey(QObject* parent) 
    : IGlobalHotkey(parent), hotkeyId_(0) 
{
    // Register as a native event filter to intercept WM_HOTKEY
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

WinGlobalHotkey::~WinGlobalHotkey() {
    unregisterHotkey();
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

bool WinGlobalHotkey::registerHotkey(const QString& key, Qt::KeyboardModifiers modifiers) {
    unregisterHotkey(); // Unregister any existing hotkey

    UINT fsModifiers = 0;
    if (modifiers & Qt::ShiftModifier) fsModifiers |= MOD_SHIFT;
    if (modifiers & Qt::ControlModifier) fsModifiers |= MOD_CONTROL;
    if (modifiers & Qt::AltModifier) fsModifiers |= MOD_ALT;
    if (modifiers & Qt::MetaModifier) fsModifiers |= MOD_WIN;

    // The MOD_NOREPEAT flag prevents the hotkey from firing repeatedly if held down.
    fsModifiers |= 0x4000; // MOD_NOREPEAT

    // Convert string to Qt::Key
    QKeySequence seq(key);
    if (seq.isEmpty()) return false;
    
    // Get the key code
    int qtKey = seq[0].key();
    
    // For basic A-Z, 0-9, Qt::Key value matches the Windows virtual key code.
    UINT vk = qtKey;

    hotkeyId_ = 1001; // Arbitrary ID
    
    // HWND is NULL, which means WM_HOTKEY is posted to the thread's message queue.
    if (RegisterHotKey(NULL, hotkeyId_, fsModifiers, vk)) {
        return true;
    }
    
    hotkeyId_ = 0;
    return false;
}

void WinGlobalHotkey::unregisterHotkey() {
    if (hotkeyId_ != 0) {
        UnregisterHotKey(NULL, hotkeyId_);
        hotkeyId_ = 0;
    }
}

bool WinGlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(result);
    // In Qt 6, eventType is typically "windows_generic_MSG" or "windows_dispatcher_MSG"
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY) {
            if (msg->wParam == static_cast<WPARAM>(hotkeyId_)) {
                emit hotkeyPressed();
                return true; // Stop propagation
            }
        }
    }
    return false;
}

} // namespace qshot
