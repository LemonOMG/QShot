// Render verification for PinWindow and the toolbar's new Pin button.
//
// What is being checked, and why it needs a picture:
//
//  1. The pin must show a high-DPI capture at its true physical resolution. The source
//     image is a 1px-wide red/blue stripe pattern: if the render resamples, the stripes
//     blend into intermediate colours, so counting the distinct colours in a content row
//     is an objective test of "no blur" -- much better than eyeballing it.
//  2. The frame has to be outside the image, not on top of it, or the pin silently eats
//     a line of the screenshot.
//  3. A wheel notch has to go through the real event path and resize the window.
//  4. The toolbar now has a fifth action button; its caption has to still fit inside a
//     32px button, in both languages. This is the constraint that already forced the
//     English "Cncl", and it is easy to break with a longer translation.
//
// DPR 1.5 is included because it is this machine's actual ratio, and it is the awkward
// one: the frame offset has to land on a whole device pixel there too, or the whole
// image is shifted half a pixel and quietly resampled.
//
// Run: render_pin.exe       (needs a real platform plugin, not offscreen -- see
//                            docs/ROADMAP.md section 7: offscreen renders no glyphs)

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QThread>
#include <QWheelEvent>
#include <cstdio>

#include "core/Settings.h"
#include "core/Strings.h"
#include "overlay/ToolbarWidget.h"
#include "pin/PinWindow.h"

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

static void settle(int ms = 120) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

// 1px-wide vertical stripes: any resampling shows up as colours that are neither.
static QImage makeStriped(int physicalW, int physicalH, qreal dpr) {
    QImage img(physicalW, physicalH, QImage::Format_ARGB32);
    for (int y = 0; y < physicalH; ++y) {
        for (int x = 0; x < physicalW; ++x) {
            img.setPixelColor(x, y, (x % 2 == 0) ? QColor(255, 0, 0) : QColor(0, 0, 255));
        }
    }
    img.setDevicePixelRatio(dpr);
    return img;
}

// Renders the widget at a chosen device pixel ratio. QPainter on a QImage whose DPR is
// `dpr` scales the widget's logical geometry by exactly that factor.
static QImage renderAt(QWidget* w, qreal dpr) {
    QImage img(w->size() * dpr, QImage::Format_ARGB32);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    w->render(&p);
    p.end();
    return img;
}

static QSet<QRgb> rowColors(const QImage& img, int y, int x0, int x1) {
    QSet<QRgb> out;
    for (int x = x0; x <= x1; ++x) out.insert(img.pixel(x, y));
    return out;
}

static void reportRow(const QImage& img, int y, int x0, int x1, const char* label) {
    const QSet<QRgb> colors = rowColors(img, y, x0, x1);
    printf("       %s: %lld distinct colours", label, static_cast<long long>(colors.size()));
    int shown = 0;
    for (QRgb c : colors) {
        if (shown++ >= 4) { printf(" ..."); break; }
        printf(" #%08X", c);
    }
    printf("\n");
}

// Says exactly which column is wrong and what it should have been, so a failure points
// at the pixel rather than at "the row".
static void explainOddColumns(const QImage& img, int y, int x0, int x1, int firstSourceColumn) {
    for (int x = x0; x <= x1; ++x) {
        const QRgb got = img.pixel(x, y);
        if (got == QColor(255, 0, 0).rgb() || got == QColor(0, 0, 255).rgb()) continue;
        const int src = x - x0 + firstSourceColumn;
        printf("       odd column: device x=%d -> #%08X (source column %d is %s)\n",
               x, got, src, (src % 2 == 0) ? "red" : "blue");
    }
}

static void sendWheel(QWidget* w, const QPoint& globalPos, int notches, Qt::KeyboardModifiers mods) {
    const QPoint local = w->mapFromGlobal(globalPos);
    QWheelEvent ev(QPointF(local), QPointF(globalPos), QPoint(0, 0),
                   QPoint(0, notches * 120), Qt::NoButton, mods,
                   Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &ev);
}

// One case per device pixel ratio, asserting the image survives 1:1.
static void checkExactAtDpr(qreal dpr, int physW, int physH, const char* tag) {
    printf("\n[%s] source %dx%d at DPR %g (logical %dx%d)\n",
           tag, physW, physH, dpr, qRound(physW / dpr), qRound(physH / dpr));

    PinWindow pin(makeStriped(physW, physH, dpr));

    const int logicalW = qRound(physW / dpr);
    const int logicalH = qRound(physH / dpr);
    const int frame = pin::kBorderWidth;

    checkEqInt(pin.width(), logicalW + 2 * frame, "widget width = logical image + frame");
    checkEqInt(pin.height(), logicalH + 2 * frame, "widget height = logical image + frame");

    const QImage out = renderAt(&pin, dpr);
    checkEqInt(out.width(), qRound((logicalW + 2 * frame) * dpr), "rendered physical width");
    checkEqInt(out.height(), qRound((logicalH + 2 * frame) * dpr), "rendered physical height");

    // The frame offset in device pixels has to be a whole number, or the image content
    // is shifted by a fraction of a pixel and every column is resampled.
    const qreal contentOffset = frame * dpr;
    char label[160];
    snprintf(label, sizeof(label), "frame offset %.3f device px is a whole number", contentOffset);
    check(qFuzzyCompare(contentOffset, qRound(contentOffset)), label);

    const int x0 = qRound(contentOffset);
    const int y0 = qRound(contentOffset);
    const int x1 = x0 + physW - 1;

    reportRow(out, y0, x0, x1, "content row");
    checkEqInt(rowColors(out, y0, x0, x1).size(), 2,
               "content row has exactly 2 colours -> image is 1:1");
    check(out.pixel(x0, y0) == QColor(255, 0, 0).rgb(), "first content pixel is red");
    check(out.pixel(x0 + 1, y0) == QColor(0, 0, 255).rgb(), "second content pixel is blue");
    check(out.pixel(x1, y0) == (((physW - 1) % 2 == 0) ? QColor(255, 0, 0).rgb()
                                                      : QColor(0, 0, 255).rgb()),
          "last content pixel is the last source pixel, not a blend");
    explainOddColumns(out, y0, x0, x1, 0);

    // The frame must be its own ring, not painted over the image.
    const QRgb corner = out.pixel(0, 0);
    check(corner != QColor(255, 0, 0).rgb() && corner != QColor(0, 0, 255).rgb(),
          "corner is frame, not image content");
    printf("       frame colour = #%08X\n", corner);

    out.save(QStringLiteral("pin_dpr%1.png").arg(dpr));
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Isolate QSettings before anything reads it: Settings is a singleton, and the
    // language is switched below, which writes. Without this the probe would rewrite
    // the developer's real QShot\QShot key in HKCU.
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("QShotProbe"));

    checkExactAtDpr(1.0, 200, 100, "1");
    checkExactAtDpr(1.5, 300, 150, "2");   // this machine's real ratio
    checkExactAtDpr(2.0, 400, 200, "3");

    // ------------------------------------------------------------- wheel events ---
    printf("\n[4] wheel zoom goes through the real event path\n");
    {
        PinWindow pin(makeStriped(200, 100, 1.0));
        pin.showAt(QPoint(300, 300));
        settle();

        const int frame = pin::kBorderWidth;
        check(qFuzzyCompare(pin.scale(), 1.0), "starts at 1.0");

        // Zoom in one notch, anchored at the widget centre.
        const QPoint centre = pin.mapToGlobal(QPoint(pin.width() / 2, pin.height() / 2));
        sendWheel(&pin, centre, 1, Qt::NoModifier);
        settle(30);
        check(qAbs(pin.scale() - pin::kScaleStep) < 1e-6, "one notch up multiplies the scale");
        checkEqInt(pin.width(), qRound(200 * pin::kScaleStep) + 2 * frame,
                   "widget grew with the scale");

        // Zoom out two notches: back below 1.0, and the size follows.
        sendWheel(&pin, centre, -1, Qt::NoModifier);
        sendWheel(&pin, centre, -1, Qt::NoModifier);
        settle(30);
        check(qAbs(pin.scale() - 1.0 / pin::kScaleStep) < 1e-6, "two notches down");
        checkEqInt(pin.width(), qRound(200 / pin::kScaleStep) + 2 * frame,
                   "widget shrank with the scale");

        // Clamping: 60 notches up must stop at the ceiling, not overflow it.
        for (int i = 0; i < 60; ++i) sendWheel(&pin, centre, 1, Qt::NoModifier);
        check(qFuzzyCompare(pin.scale(), pin::kMaxScale), "clamped at the maximum");
        for (int i = 0; i < 120; ++i) sendWheel(&pin, centre, -1, Qt::NoModifier);
        check(qFuzzyCompare(pin.scale(), pin::kMinScale), "clamped at the minimum");

        // Alt+wheel changes opacity instead of the scale.
        const qreal beforeScale = pin.scale();
        sendWheel(&pin, centre, 1, Qt::AltModifier);
        check(qFuzzyCompare(pin.scale(), beforeScale), "Alt+wheel does not zoom");
        check(pin.windowOpacity() > pin::kMinOpacity, "Alt+wheel raised the opacity");

        const QImage out = renderAt(&pin, 1.0);
        out.save(QStringLiteral("pin_scaled.png"));
        printf("       after clamping: scale=%.3f size=%dx%d opacity=%.2f\n",
               pin.scale(), pin.width(), pin.height(), pin.windowOpacity());
    }

    // ------------------------------------------------------- toolbar caption fit ---
    printf("\n[5] toolbar action captions still fit a 32px button, in both languages\n");
    {
        ToolbarWidget toolbar;
        toolbar.show();
        settle();

        // Same font the toolbar uses for captions: derived from the widget font rather
        // than a hardcoded family, so CJK captions do not depend on glyph fallback.
        QFont captionFont = toolbar.font();
        captionFont.setPointSize(8);
        const QFontMetrics fm(captionFont);

        struct Entry { Str id; const char* name; };
        const Entry entries[] = {
            { Str::ToolbarUndo,   "Undo" },
            { Str::ToolbarCopy,   "Copy" },
            { Str::ToolbarSave,   "Save" },
            { Str::ToolbarPin,    "Pin" },
            { Str::ToolbarCancel, "Cancel" },
        };

        const Language original = Settings::instance().language();
        for (Language lang : { Language::Chinese, Language::English }) {
            Settings::instance().setLanguage(lang);
            printf("       --- %s ---\n", lang == Language::Chinese ? "zh" : "en");
            for (const Entry& e : entries) {
                const QString caption = text(e.id);
                const int advance = fm.horizontalAdvance(caption);
                char label[160];
                snprintf(label, sizeof(label), "%s \"%s\" fits in 32px (needs %d)",
                         e.name, caption.toUtf8().constData(), advance);
                check(advance <= 30, label);
            }
        }
        Settings::instance().setLanguage(original);

        printf("       toolbar size = %dx%d\n", toolbar.width(), toolbar.height());
        const QImage out = renderAt(&toolbar, 1.0);
        out.save(QStringLiteral("toolbar_with_pin.png"));
        check(toolbar.width() > 0, "toolbar has a width");
    }

    // ------------------------------------------------------- realistic content ---
    // The striped cases above prove the geometry. This one is for the eye: a real
    // rendered UI (CJK text, 1px strokes) pinned at 1:1 and at 2x, so blur and edge
    // artifacts would actually be visible.
    printf("\n[6] realistic content, for visual inspection\n");
    {
        QImage sample(QStringLiteral("panel_zh_text_toolbar.png"));
        if (sample.isNull()) {
            printf("       (skipped: panel_zh_text_toolbar.png not found)\n");
        } else {
            PinWindow pin(sample);
            const QImage out = renderAt(&pin, 1.0);
            out.save(QStringLiteral("pin_real_1to1.png"));
            printf("       pinned %dx%d -> widget %dx%d, saved pin_real_1to1.png\n",
                   sample.width(), sample.height(), pin.width(), pin.height());

            // Zoomed to 2x through the real wheel path, so the magnification branch of
            // the render hint is exercised too.
            const QPoint centre = pin.mapToGlobal(QPoint(pin.width() / 2, pin.height() / 2));
            while (pin.scale() < 2.0) sendWheel(&pin, centre, 1, Qt::NoModifier);
            const QImage zoomed = renderAt(&pin, 1.0);
            zoomed.save(QStringLiteral("pin_real_2x.png"));
            printf("       at scale %.3f -> widget %dx%d, saved pin_real_2x.png\n",
                   pin.scale(), pin.width(), pin.height());
            check(pin.scale() >= 2.0, "reached at least 2x through wheel events");
        }
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    fflush(stdout);
    return failures == 0 ? 0 : 1;
}
