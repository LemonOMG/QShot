#include "WinWindowDetector.h"
#include <QGuiApplication>
#include <QScreen>
#include <QString>
#include <windows.h>
#include <dwmapi.h>

namespace qshot {

struct WindowSearchData {
    POINT pt;
    HWND result = nullptr;
    DWORD currentPid = 0;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowSearchData* data = reinterpret_cast<WindowSearchData*>(lParam);

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE;

    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->currentPid) return TRUE;

    // The W variants, not the A ones: the A calls decode with the *system* code page, so a class
    // name containing anything outside it comes back as mojibake and silently stops matching.
    // Likewise GetWindowLongPtr rather than GetWindowLong -- the latter is the 32-bit form and
    // truncates a LONG_PTR on 64-bit. GWL_EXSTYLE happens to fit today, but this is the variant
    // that cannot quietly lose bits.
    wchar_t className[256];
    GetClassNameW(hwnd, className, 256);
    QString clsName = QString::fromWCharArray(className);
    if (clsName == "Progman" || clsName == "WorkerW" || clsName == "Shell_TrayWnd") {
        return TRUE;
    }

    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        if (GetWindowTextLength(hwnd) == 0) return TRUE;
    }

    RECT rect;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
    if (FAILED(hr)) {
        GetWindowRect(hwnd, &rect);
    }

    if (PtInRect(&rect, data->pt)) {
        data->result = hwnd;
        return FALSE; // Stop enumerating
    }
    return TRUE;
}

QRect WinWindowDetector::windowRectAt(const QPoint& logicalPos) const {
    QScreen* targetScreen = QGuiApplication::screenAt(logicalPos);
    if (!targetScreen) targetScreen = QGuiApplication::primaryScreen();
    
    struct EnumData {
        QString name;
        MONITORINFOEXW mi;
        bool found;
    } edata{}; // value-initialised: `mi` is left untouched unless a monitor actually matches
    edata.name = targetScreen->name();
    edata.mi.cbSize = sizeof(MONITORINFOEXW);
    
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
        EnumData* d = reinterpret_cast<EnumData*>(lParam);
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(MONITORINFOEXW);
        if (GetMonitorInfoW(hMon, (LPMONITORINFO)&mi)) {
            if (QString::fromWCharArray(mi.szDevice) == d->name) {
                d->mi = mi;
                d->found = true;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&edata));
    
    if (!edata.found) {
        // No HMONITOR matched this screen's name, so there is no rcMonitor to map against -- and
        // the mapping genuinely needs one, because EnumWindowsProc below compares against
        // *physical* desktop coordinates (DwmGetWindowAttribute and GetWindowRect both are).
        // Multiplying by the device pixel ratio instead would only be correct for a screen whose
        // physical origin is (0,0), i.e. the primary one; on any other screen it lands somewhere
        // else entirely and would highlight a window the user is not pointing at. An empty
        // rectangle is already this interface's documented answer for "no window found" (see
        // IWindowDetector.h) and the caller turns that into "no hover outline", so degrading to
        // it is strictly better than answering wrongly.
        // This used to call GetCursorPos(), which ignored the argument it was handed -- the
        // P1-2 defect. It was never a correct answer, only an often-close-enough one.
        return QRect();
    }

    POINT pt{};
    const qreal inputDpr = targetScreen->devicePixelRatio();
    pt.x = edata.mi.rcMonitor.left + qRound((logicalPos.x() - targetScreen->geometry().x()) * inputDpr);
    pt.y = edata.mi.rcMonitor.top + qRound((logicalPos.y() - targetScreen->geometry().y()) * inputDpr);

    WindowSearchData data;
    data.pt = pt;
    data.currentPid = GetCurrentProcessId();

    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));

    HWND hwnd = data.result;
    if (!hwnd) return QRect();

    RECT rect;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
    if (FAILED(hr)) {
        GetWindowRect(hwnd, &rect);
    }

    HMONITOR hMon = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(hMon, (LPMONITORINFO)&mi)) {
        return QRect();
    }

    targetScreen = nullptr;
    QString monName = QString::fromWCharArray(mi.szDevice);
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == monName) {
            targetScreen = screen;
            break;
        }
    }

    if (!targetScreen) {
        targetScreen = QGuiApplication::screenAt(logicalPos);
        if (!targetScreen) {
            targetScreen = QGuiApplication::primaryScreen();
        }
    }

    qreal dpr = targetScreen->devicePixelRatio();
    QRect logicalRect;
    logicalRect.setLeft(targetScreen->geometry().x() + qRound((rect.left - mi.rcMonitor.left) / dpr));
    logicalRect.setTop(targetScreen->geometry().y() + qRound((rect.top - mi.rcMonitor.top) / dpr));
    logicalRect.setWidth(qRound((rect.right - rect.left) / dpr));
    logicalRect.setHeight(qRound((rect.bottom - rect.top) / dpr));

    return logicalRect;
}

} // namespace qshot
