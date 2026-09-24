// Placement check for the composited cursor.
//
// The screen grab on this machine is not bit-stable between two calls, so
// diffing grabs proves nothing. Instead the cursor position is read from Win32
// immediately *before* and *after* the grab: only when the two agree was the
// cursor provably stationary for the whole capture, and only then is the
// measured offset meaningful.
//
// The crop is centred on that position, so a correct hotspot lands in the middle.

#include <QApplication>
#include <QImage>
#include <QScreen>
#include <cstdio>

#include <windows.h>

#include "platform/windows/WinScreenCapture.h"

using namespace qshot;

namespace {

bool cursorPhysical(QPoint* out) {
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) return false;
    if ((ci.flags & CURSOR_SHOWING) == 0) return false;
    *out = QPoint(static_cast<int>(ci.ptScreenPos.x), static_cast<int>(ci.ptScreenPos.y));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        std::printf("no primary screen\n");
        return 1;
    }

    QPoint before;
    if (!cursorPhysical(&before)) {
        std::printf("cursor not available\n");
        return 0;
    }

    WinScreenCapture capture;
    const QPixmap shot = capture.captureScreen(screen, true);

    QPoint after;
    const bool stable = cursorPhysical(&after) && after == before;
    std::printf("cursor before=(%d,%d) after=(%d,%d)  stable=%d\n",
                before.x(), before.y(), after.x(), after.y(), stable ? 1 : 0);
    std::printf("shot=%dx%d dpr=%.2f\n", shot.width(), shot.height(),
                shot.devicePixelRatio());

    const QImage image = shot.toImage();

    // The composited cursor is the only *opaque* addition on top of the screen
    // content, so look for the cursor's outline: the standard arrow is dark.
    // Search a generous window around the reported position.
    const int half = 100;
    QRect crop(before.x() - half, before.y() - half, half * 2, half * 2);
    crop &= QRect(0, 0, image.width(), image.height());

    QImage zoom = image.copy(crop);
    zoom.setDevicePixelRatio(1.0);
    zoom.scaled(zoom.width() * 3, zoom.height() * 3, Qt::IgnoreAspectRatio,
                Qt::FastTransformation)
        .save(QStringLiteral("cursor_placement.png"));

    // Locate the darkest pixel: the arrow outline is near-black while a typical
    // desktop region is not. Reported relative to the crop centre.
    int bestX = -1;
    int bestY = -1;
    int bestLuma = 256;
    for (int y = crop.top(); y <= crop.bottom(); ++y) {
        for (int x = crop.left(); x <= crop.right(); ++x) {
            const QRgb p = image.pixel(x, y);
            const int luma = (qRed(p) * 299 + qGreen(p) * 587 + qBlue(p) * 114) / 1000;
            if (luma < bestLuma) {
                bestLuma = luma;
                bestX = x;
                bestY = y;
            }
        }
    }

    std::printf("crop=(%d,%d %dx%d) centre=(%d,%d)\n",
                crop.x(), crop.y(), crop.width(), crop.height(),
                crop.center().x(), crop.center().y());
    std::printf("darkest pixel=(%d,%d) luma=%d  -> offset from centre=(%d,%d)\n",
                bestX, bestY, bestLuma,
                bestX - crop.center().x(), bestY - crop.center().y());
    std::printf("saved cursor_placement.png\n");
    std::fflush(stdout);

    return 0;
}
