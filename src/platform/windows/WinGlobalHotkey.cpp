#include "WinGlobalHotkey.h"
#include <QCoreApplication>
#include <QKeySequence>
#include <windows.h>

namespace qshot {

namespace {

/**
 * Map a Qt key to a Windows virtual-key code.
 *
 * Returns 0 for anything unmapped. The previous implementation fell back to
 * `vk = qtKey`, which is wrong for every named key (Qt::Key_Print is 0x01000009,
 * VK_SNAPSHOT is 0x2C) - RegisterHotKey then failed with an opaque error, or
 * worse, registered an unrelated key. Returning 0 lets the caller report an
 * honest failure instead.
 */
UINT virtualKeyFor(Qt::Key key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<UINT>('A' + (static_cast<int>(key) - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<UINT>('0' + (static_cast<int>(key) - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return static_cast<UINT>(VK_F1 + (static_cast<int>(key) - Qt::Key_F1));
    }

    switch (key) {
        case Qt::Key_Escape:     return VK_ESCAPE;
        case Qt::Key_Tab:        return VK_TAB;
        case Qt::Key_Backspace:  return VK_BACK;
        case Qt::Key_Return:
        case Qt::Key_Enter:      return VK_RETURN;
        case Qt::Key_Space:      return VK_SPACE;
        case Qt::Key_Insert:     return VK_INSERT;
        case Qt::Key_Delete:     return VK_DELETE;
        case Qt::Key_Home:       return VK_HOME;
        case Qt::Key_End:        return VK_END;
        case Qt::Key_PageUp:     return VK_PRIOR;
        case Qt::Key_PageDown:   return VK_NEXT;
        case Qt::Key_Left:       return VK_LEFT;
        case Qt::Key_Up:         return VK_UP;
        case Qt::Key_Right:      return VK_RIGHT;
        case Qt::Key_Down:       return VK_DOWN;
        case Qt::Key_Print:      return VK_SNAPSHOT;
        case Qt::Key_Pause:      return VK_PAUSE;
        case Qt::Key_CapsLock:   return VK_CAPITAL;
        case Qt::Key_NumLock:    return VK_NUMLOCK;
        case Qt::Key_ScrollLock: return VK_SCROLL;
        default:                 break;
    }
    return 0;
}

} // namespace

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

bool WinGlobalHotkey::registerHotkey(const QKeySequence& sequence) {
    unregisterHotkey(); // Unregister any existing hotkey

    if (sequence.isEmpty()) return false;
    const QKeyCombination combo = sequence[0];

    const UINT vk = virtualKeyFor(combo.key());
    if (vk == 0) return false;

    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    UINT fsModifiers = 0;
    if (mods & Qt::ShiftModifier)   fsModifiers |= MOD_SHIFT;
    if (mods & Qt::ControlModifier) fsModifiers |= MOD_CONTROL;
    if (mods & Qt::AltModifier)     fsModifiers |= MOD_ALT;
    if (mods & Qt::MetaModifier)    fsModifiers |= MOD_WIN;

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
    // The MOD_NOREPEAT flag prevents the hotkey from firing repeatedly if held down.
    fsModifiers |= MOD_NOREPEAT;

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
