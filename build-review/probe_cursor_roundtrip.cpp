// Separates three concerns that the earlier cursor probe conflated:
//
//   1. Baseline noise: do two consecutive grabs of the same screen differ?
//      (If yes, pixel-diffing two grabs proves nothing.)
//   2. Round trip: does QPixmap -> QImage -> setDevicePixelRatio(1) ->
//      QPixmap::fromImage lose or shift any pixel? This is the path
//      captureScreen() takes when includeCursor is on, so a lossy round trip
//      would silently degrade every cursor-enabled screenshot.
//   3. Cursor placement: where does the hotspot actually land relative to the
//      position reported by GetCursorInfo?

#include <QApplication>
#include <QImage>
#include <QScreen>
#include <cstdio>

#include <windows.h>

#include "platform/windows/WinScreenCapture.h"

using namespace qshot;

namespace {

struct Diff {
    int changed = 0;
    int maxDelta = 0;
    int maxAlphaDelta = 0;
};

Diff compare(const QImage& a, const QImage& b) {
    Diff d;
    if (a.size() != b.size()) {
        std::printf("   size mismatch %dx%d vs %dx%d\n",
                    a.width(), a.height(), b.width(), b.height());
        return d;
    }
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb pa = a.pixel(x, y);
            const QRgb pb = b.pixel(x, y);
            if (pa != pb) ++d.changed;
            d.maxDelta = qMax(d.maxDelta, qAbs(qRed(pa) - qRed(pb)));
            d.maxDelta = qMax(d.maxDelta, qAbs(qGreen(pa) - qGreen(pb)));
            d.maxDelta = qMax(d.maxDelta, qAbs(qBlue(pa) - qBlue(pb)));
            d.maxAlphaDelta = qMax(d.maxAlphaDelta, qAbs(qAlpha(pa) - qAlpha(pb)));
        }
    }
    return d;
}

/// The exact transform captureScreen() applies when compositing the cursor.
QPixmap roundTrip(const QPixmap& source) {
    const qreal dpr = source.devicePixelRatio();
    QImage canvas = source.toImage();
    canvas.setDevicePixelRatio(1.0);
    canvas.setDevicePixelRatio(dpr);
    return QPixmap::fromImage(canvas);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        std::printf("no primary screen\n");
        return 1;
    }

    WinScreenCapture capture;

    // --- 1. baseline: two consecutive plain grabs ---------------------------
    const QImage grab1 = capture.captureScreen(screen, false).toImage();
    const QImage grab2 = capture.captureScreen(screen, false).toImage();
    std::printf("1) two plain grabs, format=%d\n", static_cast<int>(grab1.format()));
    const Diff noise = compare(grab1, grab2);
    std::printf("   changed=%d/%d maxDelta=%d maxAlphaDelta=%d\n",
                noise.changed, grab1.width() * grab1.height(),
                noise.maxDelta, noise.maxAlphaDelta);

    // --- 2. round trip on the very same source (deterministic) --------------
    const QImage before = grab1;
    const QImage after = roundTrip(capture.captureScreen(screen, false)).toImage();
    std::printf("2) round trip through toImage/fromImage, format=%d -> %d\n",
                static_cast<int>(before.format()), static_cast<int>(after.format()));
    const Diff rt = compare(before, after);
    std::printf("   changed=%d/%d maxDelta=%d maxAlphaDelta=%d\n",
                rt.changed, before.width() * before.height(),
                rt.maxDelta, rt.maxAlphaDelta);

    // --- 2b. round trip with no screen involved at all ----------------------
    QPixmap synthetic(300, 200);
    synthetic.setDevicePixelRatio(1.5);
    synthetic.fill(QColor(17, 200, 90));
    const Diff syn = compare(synthetic.toImage(), roundTrip(synthetic).toImage());
    std::printf("2b) synthetic round trip: changed=%d maxDelta=%d maxAlphaDelta=%d\n",
                syn.changed, syn.maxDelta, syn.maxAlphaDelta);

    // --- 3. alpha statistics of a plain grab --------------------------------
    int zeroAlpha = 0;
    int partialAlpha = 0;
    for (int y = 0; y < before.height(); ++y) {
        for (int x = 0; x < before.width(); ++x) {
            const int a = qAlpha(before.pixel(x, y));
            if (a == 0) ++zeroAlpha;
            else if (a != 255) ++partialAlpha;
        }
    }
    std::printf("3) plain grab alpha: zero=%d partial=%d of %d\n",
                zeroAlpha, partialAlpha, before.width() * before.height());

    std::fflush(stdout);
    return 0;
}
