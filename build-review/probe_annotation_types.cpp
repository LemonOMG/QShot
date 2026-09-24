// Are the sequence badge and the highlighter actually drawn the way they are meant to be?
//
// Both are new in M5 and both have properties that "it looks about right" cannot confirm:
//
//   - The highlighter must be *translucent*, not just yellow. The whole point is that the
//     text underneath stays readable, and an opaque marker would look almost identical in a
//     screenshot while being useless. Its alpha can be recovered exactly by inverting the
//     blend against a known source colour, which turns "is it see-through" into a number.
//   - The badge must be a *disc*, not a square, must be legible (a white digit with a dark
//     halo, so it survives both a white and a black background), and must actually show the
//     number it was given rather than a hardcoded 1.
//
// Everything here goes through AnnotationLayer::renderToImage(), the same call the product
// uses to bake annotations into the exported image, so a bug that only affects export is
// caught here rather than in a saved file.

#include <QGuiApplication>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QVector>
#include <cmath>
#include <cstdio>

#include "annotation/AnnotationLayer.h"
#include "annotation/Annotation.h"

using namespace qshot;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void checkNear(int got, int want, int tol, const char* what) {
    ++checks;
    const bool ok = std::abs(got - want) <= tol;
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d+-%d)\n", ok ? "ok" : "FAIL", what, got, want, tol);
}

static QString rgbText(QRgb c) {
    return QStringLiteral("#%1%2%3")
        .arg(qRed(c), 2, 16, QLatin1Char('0'))
        .arg(qGreen(c), 2, 16, QLatin1Char('0'))
        .arg(qBlue(c), 2, 16, QLatin1Char('0'))
        .toUpper();
}

// A solid source image. The highlighter's alpha is recovered by inverting the blend
// against it, which needs the source to be a single known colour.
static QImage solidImage(int w, int h, const QColor& c) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(c);
    img.setDevicePixelRatio(1.0);
    return img;
}

static bool sameColour(QRgb a, const QColor& b, int tol = 1) {
    return std::abs(qRed(a) - b.red()) <= tol
        && std::abs(qGreen(a) - b.green()) <= tol
        && std::abs(qBlue(a) - b.blue()) <= tol;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    const QColor source(0, 0, 255);           // pure blue
    const QImage base = solidImage(200, 200, source);

    // ---------------------------------------------------------------- highlighter
    printf("[1] the highlighter is translucent, not opaque\n");
    {
        AnnotationLayer layer;
        Annotation a;
        a.type = AnnotationType::Highlight;
        a.color = QColor(253, 216, 53, 110); // the shipped default
        a.lineWidth = 18;
        a.points << QPoint(20, 100) << QPoint(180, 100);
        layer.add(a);

        const QImage out = layer.renderToImage(base);
        const QRgb mid = out.pixel(100, 100);

        printf("       source %s -> stroke %s\n",
               qPrintable(rgbText(base.pixel(100, 100))), qPrintable(rgbText(mid)));

        check(!sameColour(mid, a.color),
              "the stroke is not simply the overlay colour (i.e. it is not opaque)");
        check(!sameColour(mid, source), "the stroke is not simply the source colour either");

        // Recover alpha per channel: observed = source*(1-a) + overlay*a, so
        // a = (observed - source) / (overlay - source). Two channels must agree; if they
        // do not, the colour is not a straight alpha blend of these two colours.
        const double aRed = double(qRed(mid) - source.red()) / double(a.color.red() - source.red());
        const double aBlue = double(qBlue(mid) - source.blue()) / double(a.color.blue() - source.blue());
        const int wantAlpha = a.color.alpha();
        printf("       recovered alpha: red channel %.4f, blue channel %.4f (nominal %.4f)\n",
               aRed, aBlue, wantAlpha / 255.0);

        checkNear(qRound(aRed * 255.0), wantAlpha, 3,
                  "the red channel recovers the nominal alpha");
        checkNear(qRound(aBlue * 255.0), wantAlpha, 3,
                  "the blue channel recovers the nominal alpha");
        check(std::abs(aRed - aBlue) < 0.02,
              "the two channels agree, so it is a straight alpha blend");
    }

    printf("\n[2] the highlighter covers exactly where it was dragged\n");
    {
        AnnotationLayer layer;
        Annotation a;
        a.type = AnnotationType::Highlight;
        a.color = QColor(253, 216, 53, 110);
        a.lineWidth = 18; // half-width 9
        a.points << QPoint(20, 100) << QPoint(180, 100);
        layer.add(a);

        const QImage out = layer.renderToImage(base);

        check(!sameColour(out.pixel(100, 100), source), "inside the stroke is covered");
        check(sameColour(out.pixel(100, 100 - 12), source),
              "12px above the centre is outside an 18px stroke");
        check(sameColour(out.pixel(100, 100 + 12), source),
              "12px below the centre is outside an 18px stroke");
        check(!sameColour(out.pixel(100, 100 + 7), source),
              "7px below the centre is still inside it");

        // Flat caps: the stroke spans exactly the dragged segment and stops there.
        //
        // This was a real defect rather than a preference. With Qt::SquareCap the stroke
        // overshoots by half the pen width at each end -- 9px at this width -- so dragging
        // along one line of text painted 9px of highlight into the margin on both sides,
        // which defeats the point of the gesture. Sampling 5px outside each end
        // distinguishes the two cap styles unambiguously.
        check(!sameColour(out.pixel(20, 100), source), "the start endpoint is covered");
        check(sameColour(out.pixel(20 - 5, 100), source),
              "and nothing is drawn 5px before it (flat cap, not square)");
        check(sameColour(out.pixel(180 + 5, 100), source),
              "nor 5px past the far endpoint");
        // A segment from x=20 to x=180 covers continuous x in [20,180], and pixel 180 spans
        // [180,181], so it has zero overlap. The stroke therefore ends one pixel short of
        // the endpoint index -- exact, and the reason a flat cap is worth having.
        check(!sameColour(out.pixel(179, 100), source),
              "the last pixel inside the segment is covered");
        check(sameColour(out.pixel(180, 100), source),
              "and the stroke does not bleed into the pixel past the endpoint");

        // A horizontal drag should not paint a diagonal band.
        check(sameColour(out.pixel(100, 100 - 20), source),
              "20px above is untouched, so the band is not tilted");
    }

    printf("\n[3] a single tap still leaves a mark\n");
    {
        AnnotationLayer layer;
        Annotation a;
        a.type = AnnotationType::Highlight;
        a.color = QColor(253, 216, 53, 110);
        a.lineWidth = 18; // so the dot's radius is 9
        a.points << QPoint(100, 100); // one point: a click, not a drag
        layer.add(a);

        const QImage out = layer.renderToImage(base);
        // This is the case a flat cap would silently drop: a zero-length line has no area,
        // so without the explicit dot a tap would paint nothing at all.
        check(!sameColour(out.pixel(100, 100), source),
              "a one-point highlight is still drawn");
        check(!sameColour(out.pixel(100, 100 + 5), source),
              "the dot has the stroke's radius");
        check(sameColour(out.pixel(100, 100 + 12), source),
              "and stops at it (radius 9, not a full-width blob)");
        check(sameColour(out.pixel(100, 100 - 12), source),
              "symmetric above the tap");
    }

    // ---------------------------------------------------------------- sequence badge
    const QColor badge(229, 57, 53);
    printf("\n[4] the badge is a filled disc with a legible digit\n");
    {
        AnnotationLayer layer;
        Annotation a;
        a.type = AnnotationType::Number;
        a.color = badge;
        a.badgeDiameter = 28; // radius 14
        a.number = 1;
        a.points << QPoint(100, 100);
        layer.add(a);

        const QImage out = layer.renderToImage(base);
        const QPoint centre(100, 100);

        printf("       centre %s, +7px %s, +20px %s\n",
               qPrintable(rgbText(out.pixel(centre.x(), centre.y()))),
               qPrintable(rgbText(out.pixel(centre.x() + 7, centre.y()))),
               qPrintable(rgbText(out.pixel(centre.x() + 20, centre.y()))));

        check(sameColour(out.pixel(centre.x() + 7, centre.y()), badge, 2),
              "inside the disc carries the badge colour");

        // Round, not square: the corner of the bounding box is outside the circle, so it
        // must still be background. A 28px square would fail this.
        check(sameColour(out.pixel(centre.x() + 11, centre.y() + 11), source),
              "the corner of the bounding box is untouched, so it is a disc not a square");
        check(!sameColour(out.pixel(centre.x() + 11, centre.y()), source),
              "but the same distance along the axis is inside the disc");

        // The digit: some pixel in the disc must be near-white. Scanning rather than
        // probing one coordinate, because where the glyph lands depends on the font.
        int whiteish = 0;
        for (int y = centre.y() - 12; y <= centre.y() + 12; ++y) {
            for (int x = centre.x() - 12; x <= centre.x() + 12; ++x) {
                const QRgb c = out.pixel(x, y);
                if (qRed(c) > 220 && qGreen(c) > 220 && qBlue(c) > 220) ++whiteish;
            }
        }
        printf("       near-white pixels inside the disc: %d\n", whiteish);
        check(whiteish > 5, "the digit is drawn in white inside the disc");

        // The halo: the glyph must be surrounded by dark pixels so it stays readable on a
        // light background. Counting near-black pixels is the cheap proxy for that.
        int darkish = 0;
        for (int y = centre.y() - 12; y <= centre.y() + 12; ++y) {
            for (int x = centre.x() - 12; x <= centre.x() + 12; ++x) {
                const QRgb c = out.pixel(x, y);
                if (qRed(c) < 120 && qGreen(c) < 120 && qBlue(c) < 120) ++darkish;
            }
        }
        printf("       dark pixels inside the disc (badge fill + halo): %d\n", darkish);
        check(darkish > whiteish, "the digit is surrounded by dark pixels, not bare white");
    }

    printf("\n[5] the badge shows the number it was given\n");
    {
        // Two badges with different numbers must not render identically. This is the
        // assertion that would fail if `number` were ignored in favour of a constant.
        AnnotationLayer one;
        Annotation a1;
        a1.type = AnnotationType::Number;
        a1.color = badge;
        a1.badgeDiameter = 28;
        a1.number = 1;
        a1.points << QPoint(100, 100);
        one.add(a1);

        AnnotationLayer two;
        Annotation a2 = a1;
        a2.number = 2;
        two.add(a2);

        const QImage imgOne = one.renderToImage(base);
        const QImage imgTwo = two.renderToImage(base);

        int differing = 0;
        for (int y = 80; y < 120; ++y) {
            for (int x = 80; x < 120; ++x) {
                if (imgOne.pixel(x, y) != imgTwo.pixel(x, y)) ++differing;
            }
        }
        printf("       pixels differing between badge 1 and badge 2: %d\n", differing);
        check(differing > 10, "badge 2 is visibly different from badge 1");
    }

    printf("\n[6] the badge scales with badgeDiameter\n");
    {
        auto extent = [&](int diameter) {
            AnnotationLayer layer;
            Annotation a;
            a.type = AnnotationType::Number;
            a.color = badge;
            a.badgeDiameter = diameter;
            a.number = 1;
            a.points << QPoint(100, 100);
            layer.add(a);
            const QImage out = layer.renderToImage(base);
            int widest = 0;
            for (int y = 100; y == 100; ++y) {
                int n = 0;
                for (int x = 50; x < 150; ++x) {
                    if (!sameColour(out.pixel(x, y), source, 3)) ++n;
                }
                widest = n;
            }
            return widest;
        };

        const int small = extent(20);
        const int large = extent(38);
        printf("       20px badge spans %d px, 38px badge spans %d px\n", small, large);
        check(small < large, "the larger setting really does draw larger");
        check(small >= 18 && small <= 26, "the 20px badge spans about 20px");
        check(large >= 36 && large <= 44, "the 38px badge spans about 38px");
    }

    printf("\n[7] a badge is not clipped by the selection edge it sits on\n");
    {
        // Placed at the very corner of the image, the disc extends past the edge. That is
        // expected to be clipped rather than to crash or wrap around, and nothing may
        // appear on the opposite side.
        AnnotationLayer layer;
        Annotation a;
        a.type = AnnotationType::Number;
        a.color = badge;
        a.badgeDiameter = 28;
        a.number = 1;
        a.points << QPoint(0, 0);
        layer.add(a);

        const QImage out = layer.renderToImage(base);
        check(!sameColour(out.pixel(0, 0), source), "the visible quarter is drawn");
        check(sameColour(out.pixel(199, 199), source),
              "and nothing wraps around to the opposite corner");
        check(out.size() == base.size(), "the output keeps the source size");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
