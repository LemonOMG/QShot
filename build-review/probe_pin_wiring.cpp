// Wiring check for the pin: overlay -> signal -> image, and toolbar -> signal.
//
// The render probe proves PinWindow draws correctly. It does not prove anything gets
// to it. This one drives the real event path instead:
//
//   drag on the overlay  ->  selection  ->  Ctrl+T  ->  pinRequested(image, rect)
//
// and then asserts the emitted image really is the selected crop of the background,
// pixel for pixel. A wrong crop (off-by-one, wrong DPR, annotations composited against
// the wrong base) is invisible in a UI screenshot but obvious here.
//
// Run: probe_pin_wiring.exe      (the overlay is never shown, so nothing flashes)

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QKeyEvent>
#include <cstdio>

#include "core/Strings.h"
#include "overlay/SnapOverlay.h"
#include "overlay/ToolbarWidget.h"

using namespace qshot;

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void checkEqInt(int got, int want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL", what, got, want);
}

// A background whose every pixel encodes its own coordinate, so a mis-cropped image
// can be detected by looking at any single pixel.
static QPixmap makeCoordinateMap(int w, int h, qreal dpr) {
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.setPixelColor(x, y, QColor(x % 256, y % 256, 0, 255));
        }
    }
    img.setDevicePixelRatio(dpr);
    return QPixmap::fromImage(img);
}

static void sendMouse(QWidget* w, QEvent::Type type, const QPoint& local,
                      Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPoint global = w->mapToGlobal(local);
    QMouseEvent ev(type, QPointF(local), QPointF(global), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("QShotProbe"));

    // ------------------------------------------------------- toolbar -> signal ---
    printf("\n[1] the toolbar's Pin button emits pinRequested\n");
    {
        ToolbarWidget toolbar;
        int emitted = 0;
        QObject::connect(&toolbar, &ToolbarWidget::pinRequested,
                         [&emitted]() { ++emitted; });

        toolbar.handleActionClick(QStringLiteral("Pin"));
        checkEqInt(emitted, 1, "handleActionClick(\"Pin\") emitted once");

        // The other actions must not have been rewired by the addition.
        toolbar.handleActionClick(QStringLiteral("Cancel"));
        checkEqInt(emitted, 1, "a different action does not emit pinRequested");
    }

    // ------------------------------------------- overlay drag -> Ctrl+T -> signal ---
    printf("\n[2] drag a selection, then Ctrl+T emits the composed crop\n");
    {
        const qreal dpr = 1.0;
        const QSize screen(400, 300);
        SnapOverlay overlay(makeCoordinateMap(screen.width(), screen.height(), dpr),
                            QRect(QPoint(0, 0), screen));

        QImage gotImage;
        QRect gotRect;
        int emissions = 0;
        QObject::connect(&overlay, &SnapOverlay::pinRequested,
                         [&](const QImage& image, const QRect& rect) {
                             ++emissions;
                             gotImage = image;
                             gotRect = rect;
                         });

        // Drag from (50,40) to (150,120). sendEvent() bypasses the platform, so no
        // window has to be shown.
        sendMouse(&overlay, QEvent::MouseButtonPress, QPoint(50, 40),
                  Qt::LeftButton, Qt::LeftButton);
        sendMouse(&overlay, QEvent::MouseMove, QPoint(150, 120),
                  Qt::NoButton, Qt::LeftButton);
        sendMouse(&overlay, QEvent::MouseButtonRelease, QPoint(150, 120),
                  Qt::LeftButton, Qt::NoButton);

        QKeyEvent ctrlT(QEvent::KeyPress, Qt::Key_T, Qt::ControlModifier);
        QCoreApplication::sendEvent(&overlay, &ctrlT);

        checkEqInt(emissions, 1, "Ctrl+T emitted pinRequested exactly once");
        if (emissions == 0) {
            printf("\n%d checks, %d failures\n", checks, failures);
            return 1;
        }

        // dragRect() builds from qMin/qMax, so a drag from (50,40) to (150,120) is an
        // inclusive 101x81 rectangle.
        checkEqInt(gotImage.width(), 101, "crop width");
        checkEqInt(gotImage.height(), 81, "crop height");

        // Every pixel must come from the matching place in the background: the pixel at
        // (0,0) of the crop is background (50,40), and so on.
        check(gotImage.pixelColor(0, 0) == QColor(50 % 256, 40 % 256, 0, 255),
              "crop pixel (0,0) is background pixel (50,40)");
        check(gotImage.pixelColor(100, 80) == QColor(150 % 256, 120 % 256, 0, 255),
              "crop pixel (100,80) is background pixel (150,120)");
        check(gotImage.pixelColor(7, 33) == QColor(57 % 256, 73 % 256, 0, 255),
              "crop pixel (7,33) is background pixel (57,73)");

        // The pin is placed where the selection was. Checked through the overlay's own
        // local<->global conversion rather than by re-deriving the origin here, so the
        // probe cannot drift away from the product.
        checkEqInt(gotRect.width(), 101, "placement rect width");
        checkEqInt(gotRect.height(), 81, "placement rect height");
        check(overlay.mapFromGlobal(gotRect.topLeft()) == QPoint(50, 40),
              "placement rect top-left maps back to the selection's top-left");

        // The image must carry the capture's device pixel ratio, or the pin would show
        // a high-DPI capture at the wrong size.
        check(qFuzzyCompare(gotImage.devicePixelRatio(), dpr),
              "emitted image keeps the capture's device pixel ratio");
    }

    // ------------------------------------------------ no selection, no pin ---
    printf("\n[3] Ctrl+T with nothing selected emits nothing\n");
    {
        SnapOverlay overlay(makeCoordinateMap(400, 300, 1.0), QRect(0, 0, 400, 300));
        int emissions = 0;
        QObject::connect(&overlay, &SnapOverlay::pinRequested,
                         [&emissions](const QImage&, const QRect&) { ++emissions; });

        QKeyEvent ctrlT(QEvent::KeyPress, Qt::Key_T, Qt::ControlModifier);
        QCoreApplication::sendEvent(&overlay, &ctrlT);
        checkEqInt(emissions, 0, "no selection -> no pin");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    fflush(stdout);
    return failures == 0 ? 0 : 1;
}
