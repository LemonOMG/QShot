// Deterministic check of captureScreen(..., includeCursor = true).
//
// The cursor is pinned to a known physical position with SetCursorPos first, so
// the crop can be centred on it exactly. If the DPR / hotspot arithmetic is
// right, the cursor's hotspot must land on the centre of the saved crop; any
// offset is a real bug rather than mouse movement between two reads.

#include <QApplication>
#include <QCursor>
#include <QImage>
#include <QScreen>
#include <cstdio>

#include <windows.h>

#include "platform/windows/WinScreenCapture.h"

using namespace qshot;

namespace {

/// Largest per-channel difference between two equally sized images.
int maxChannelDelta(const QImage& a, const QImage& b, int* changedPixels) {
    int worst = 0;
    int changed = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb pa = a.pixel(x, y);
            const QRgb pb = b.pixel(x, y);
            if (pa == pb) continue;
            ++changed;
            worst = qMax(worst, qAbs(qRed(pa) - qRed(pb)));
            worst = qMax(worst, qAbs(qGreen(pa) - qGreen(pb)));
            worst = qMax(worst, qAbs(qBlue(pa) - qBlue(pb)));
        }
    }
    *changedPixels = changed;
    return worst;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        std::printf("no primary screen\n");
        return 1;
    }

    // Pin the cursor somewhere with plenty of room around it.
    const int pinX = 1600;
    const int pinY = 1000;
    SetCursorPos(pinX, pinY);
    Sleep(120); // let the shell redraw the cursor at the new position

    WinScreenCapture capture;
    const QPixmap withCursor = capture.captureScreen(screen, true);
    const QPixmap withoutCursor = capture.captureScreen(screen, false);

    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    const bool havePos = GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) != 0;
    if (havePos) {
        std::printf("pinned to (%d,%d); GetCursorInfo now reports (%ld,%ld)\n",
                    pinX, pinY,
                    static_cast<long>(ci.ptScreenPos.x),
                    static_cast<long>(ci.ptScreenPos.y));
    } else {
        std::printf("pinned to (%d,%d); GetCursorInfo unavailable\n", pinX, pinY);
    }

    std::printf("screen logical=%dx%d  shot=%dx%d dpr=%.2f\n",
                screen->geometry().width(), screen->geometry().height(),
                withCursor.width(), withCursor.height(),
                withCursor.devicePixelRatio());

    const QImage a = withCursor.toImage();
    const QImage b = withoutCursor.toImage();

    int changed = 0;
    const int delta = maxChannelDelta(a, b, &changed);
    std::printf("full-image diff: changedPixels=%d of %d, maxChannelDelta=%d\n",
                changed, a.width() * a.height(), delta);

    // Crop centred on the pinned position: the hotspot should sit in the middle.
    const int half = 60;
    QRect crop(pinX - half, pinY - half, half * 2, half * 2);
    crop &= QRect(0, 0, a.width(), a.height());

    QImage zoom = a.copy(crop);
    zoom.setDevicePixelRatio(1.0);
    zoom.scaled(zoom.width() * 4, zoom.height() * 4, Qt::IgnoreAspectRatio,
                Qt::FastTransformation)
        .save(QStringLiteral("cursor_pinned.png"));

    // Where is the topmost opaque pixel of the cursor inside the crop? For the
    // standard arrow that is the tip, i.e. the hotspot.
    int tipX = -1;
    int tipY = -1;
    for (int y = crop.top(); y <= crop.bottom() && tipY < 0; ++y) {
        for (int x = crop.left(); x <= crop.right(); ++x) {
            if (qAlpha(a.pixel(x, y)) != 0) {
                tipX = x;
                tipY = y;
                break;
            }
        }
    }
    std::printf("crop=(%d,%d %dx%d)  first opaque pixel=(%d,%d)  centre=(%d,%d)\n",
                crop.x(), crop.y(), crop.width(), crop.height(),
                tipX, tipY, crop.center().x(), crop.center().y());
    std::printf("saved cursor_pinned.png\n");
    std::fflush(stdout);

    return 0;
}
