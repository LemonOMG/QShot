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

    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    QString clsName = QString::fromLocal8Bit(className);
    if (clsName == "Progman" || clsName == "WorkerW" || clsName == "Shell_TrayWnd") {
        return TRUE;
    }

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
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
    } edata;
    edata.name = targetScreen->name();
    edata.found = false;
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
    
    POINT pt;
    if (edata.found) {
        qreal dpr = targetScreen->devicePixelRatio();
        pt.x = edata.mi.rcMonitor.left + qRound((logicalPos.x() - targetScreen->geometry().x()) * dpr);
        pt.y = edata.mi.rcMonitor.top + qRound((logicalPos.y() - targetScreen->geometry().y()) * dpr);
    } else {
        GetCursorPos(&pt); // fallback
    }

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
