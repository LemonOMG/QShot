#include "WinScreenCapture.h"
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QCursor>
#include <QPoint>
#include <QtMath>

#include <windows.h>

#include <cstring>

namespace qshot {

namespace {

/**
 * Render the current system cursor into an ARGB image.
 *
 * @param hotspot Receives the cursor hotspot, in pixels of the returned image.
 * @return A null image when the cursor is hidden or cannot be queried.
 */
QImage currentCursorImage(QPoint* hotspot) {
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) return QImage();
    if ((ci.flags & CURSOR_SHOWING) == 0 || ci.hCursor == nullptr) return QImage();

    ICONINFO ii{};
    if (!GetIconInfo(ci.hCursor, &ii)) return QImage();
    *hotspot = QPoint(static_cast<int>(ii.xHotspot), static_cast<int>(ii.yHotspot));
    // GetIconInfo hands the caller ownership of both bitmaps.
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);

    const int w = GetSystemMetrics(SM_CXCURSOR);
    const int h = GetSystemMetrics(SM_CYCURSOR);
    if (w <= 0 || h <= 0) return QImage();

    // A top-down 32bpp DIB section: it has a real alpha channel, which
    // CreateCompatibleBitmap() would not, so the transparent parts of the cursor
    // stay transparent instead of turning black.
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // negative height -> top-down rows
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

    if (dib == nullptr || bits == nullptr) {
        if (dib) DeleteObject(dib);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return QImage();
    }

    HGDIOBJ previous = SelectObject(memDc, dib);
    memset(bits, 0, static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    DrawIconEx(memDc, 0, 0, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    SelectObject(memDc, previous);

    const QImage view(reinterpret_cast<const uchar*>(bits), w, h, w * 4,
                      QImage::Format_ARGB32_Premultiplied);
    // QImage does not take ownership of the DIB memory, so copy before freeing.
    const QImage owned = view.copy();

    DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return owned;
}

} // namespace

QPixmap WinScreenCapture::captureScreen(QScreen* screen, bool includeCursor) {
    if (!screen) {
        return QPixmap();
    }

    QPixmap shot = screen->grabWindow(0);
    if (!includeCursor || shot.isNull()) {
        return shot;
    }

    QPoint hotspot;
    const QImage cursor = currentCursorImage(&hotspot);
    if (cursor.isNull()) {
        return shot;
    }

    const qreal dpr = shot.devicePixelRatio();

    // grabWindow() returns physical pixels, and a QPainter on that pixmap would
    // scale every coordinate by its devicePixelRatio - drawing the cursor there
    // would blow it up by 1.5x on this machine. So composite on a DPR-1 copy in
    // physical pixel coordinates and put the ratio back afterwards.
    QImage canvas = shot.toImage();
    canvas.setDevicePixelRatio(1.0);
    {
        // QCursor::pos() and QScreen::geometry() share Qt's logical coordinate
        // space, so their difference is the logical offset inside the screen.
        // (Multi-monitor setups with *different* scale factors are not handled
        // here; see the coordinate-space notes in docs/CODE_REVIEW_ROUND3.md.)
        const QPoint logical = QCursor::pos() - screen->geometry().topLeft();
        const QPoint physical(qRound(logical.x() * dpr), qRound(logical.y() * dpr));
        const QPoint topLeft = physical - hotspot;

        QPainter painter(&canvas);
        painter.drawImage(topLeft, cursor);
    }
    canvas.setDevicePixelRatio(dpr);

    return QPixmap::fromImage(canvas);
}

} // namespace qshot
