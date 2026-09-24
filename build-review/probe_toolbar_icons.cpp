// Assertions for the toolbar icon set (src/overlay/ToolbarIcons.cpp).
//
// Why this exists rather than "look at the render": the defect it was written for was not a
// glyph being ugly, it was two glyphs being *the same*. The old pen icon was a single
// diagonal stroke, which is exactly what the arrow is once its head is removed, so the
// toolbar offered two buttons that read as one tool. That is invisible in a per-icon review
// and obvious in a pairwise comparison, so the central section below compares every glyph
// against every other one and reports the closest pair.
//
// The rest checks the properties that make eight separate drawings a *set*: one stroke
// weight, one optical box, nothing spilling out of the button, and the two filled glyphs
// keeping the fill semantics they are supposed to have.
//
// Rendering here is deliberately not the toolbar's: it draws the glyphs directly into a
// 32x32 image with a known box and a known colour, so a failure points at the glyph rather
// than at button chrome, hover state or the layout pass.

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QVector>
#include <QPointF>
#include <cstdio>
#include <cmath>

#include "overlay/ToolbarIcons.h"

using namespace qshot;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void report(int got, int want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL", what, got, want);
}

// 32px is the real button size; the glyph box is the button inset by 6 on each side, which is
// what ToolbarWidget passes. Reproduced here so the probe measures the shipped geometry.
static constexpr int kButton = 32;
static constexpr int kInset = 6;
static constexpr int kBox = kButton - 2 * kInset;

static const AnnotationType kTools[] = {
    AnnotationType::Rectangle, AnnotationType::Ellipse, AnnotationType::Arrow,
    AnnotationType::Pen, AnnotationType::Mosaic, AnnotationType::Text,
    AnnotationType::Number, AnnotationType::Highlight
};
static const char* kNames[] = {
    "rectangle", "ellipse", "arrow", "pen", "mosaic", "text", "number", "highlight"
};
static constexpr int kToolCount = 8;

static QRect glyphBox() { return QRect(kInset, kInset, kBox, kBox); }

static QImage renderGlyph(AnnotationType type, qreal dpr = 1.0, const QColor& color = Qt::white) {
    QImage img(int(kButton * dpr), int(kButton * dpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    toolbaricons::paintTool(p, type, glyphBox(), color);
    p.end();
    return img;
}

// Alpha above a floor, so antialiased fringes do not count as ink. The floor matters: a
// fringe pixel at alpha 4 would otherwise make "is anything drawn here" true for an empty
// cell of the mosaic.
static int inkCount(const QImage& img, int threshold = 32) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y).alpha() > threshold) ++n;
    return n;
}

static int maxAlpha(const QImage& img) {
    int m = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            m = qMax(m, img.pixelColor(x, y).alpha());
    return m;
}

// Pixels at full strength. Used to check that an outline is actually drawn rather than to
// rely on a single peak pixel, which a stray antialiased artefact could also provide.
static int opaqueCount(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y).alpha() == 255) ++n;
    return n;
}

// Pixels that differ at all, including in alpha. Both images are built by renderGlyph, so
// they share a storage format and the comparison is meaningful without conversion.
static int diffCount(const QImage& a, const QImage& b) {
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y)) ++n;
    return n;
}

// Mean y of the ink in one column, or a negative sentinel when the column is empty.
static double columnMeanY(const QImage& img, int x) {
    double sum = 0;
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        if (img.pixelColor(x, y).alpha() > 32) { sum += y; ++n; }
    }
    return n ? sum / n : -1.0;
}

// How many times the glyph's vertical centreline reverses direction as it travels left to
// right. A straight stroke gives 0; a wave gives 2 or more.
static int directionReversals(const QImage& img) {
    QVector<double> means;
    for (int x = 0; x < img.width(); ++x) {
        const double m = columnMeanY(img, x);
        if (m >= 0) means.append(m);
    }
    if (means.size() < 4) return 0;

    int reversals = 0;
    int lastSign = 0;
    for (int i = 1; i < means.size(); ++i) {
        const double d = means[i] - means[i - 1];
        // A deadband, so antialiasing wobble on a genuinely straight stroke does not read as
        // a curve. One device pixel is the smallest change that means anything.
        if (std::fabs(d) < 0.6) continue;
        const int sign = d > 0 ? 1 : -1;
        if (lastSign != 0 && sign != lastSign) ++reversals;
        lastSign = sign;
    }
    return reversals;
}

// Thickness of the stroke passing through `(x, y)`, measured along `dx, dy`: the whole
// contiguous run, counted in both directions from the seed.
//
// Both directions matters. A 2px stroke centred on the integer 16 covers pixels 15 and 16,
// so a run that only walks forward from the seed reports 1 whenever the seed happens to be
// the second of the two -- which is exactly the false failure this probe produced first.
static int runLength(const QImage& img, int x, int y, int dx, int dy) {
    const auto inked = [&](int px, int py) {
        return px >= 0 && py >= 0 && px < img.width() && py < img.height()
               && img.pixelColor(px, py).alpha() > 32;
    };
    if (!inked(x, y)) return 0;

    int n = 1;
    for (int dir = -1; dir <= 1; dir += 2) {
        int cx = x + dir * dx, cy = y + dir * dy;
        while (inked(cx, cy)) { ++n; cx += dir * dx; cy += dir * dy; }
    }
    return n;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    QVector<QImage> glyphs;
    for (int i = 0; i < kToolCount; ++i) glyphs.append(renderGlyph(kTools[i]));

    printf("[1] every tool has a glyph, and None does not\n");
    for (int i = 0; i < kToolCount; ++i) {
        char label[96];
        snprintf(label, sizeof(label), "%s draws something", kNames[i]);
        const int ink = inkCount(glyphs[i]);
        check(ink > 0, label);
        printf("       %-9s ink %d px\n", kNames[i], ink);
    }
    {
        QImage none(kButton, kButton, QImage::Format_ARGB32_Premultiplied);
        none.fill(Qt::transparent);
        QPainter p(&none);
        const bool drew = toolbaricons::paintTool(p, AnnotationType::None, glyphBox(), Qt::white);
        p.end();
        check(!drew, "None reports that it has no glyph");
        report(inkCount(none), 0, "and paints nothing");
    }

    printf("\n[2] nothing spills out of the glyph box\n");
    // The box is the button inset by 6, so there are 6px of clearance on every side. The
    // arrow's round caps legitimately overshoot its corner by half a stroke, so the
    // assertion allows 2px and still catches a glyph that is drawn at the wrong scale or
    // from the wrong origin -- the two mistakes that actually happen.
    {
        int worst = 0;
        for (int i = 0; i < kToolCount; ++i) {
            const QImage& img = glyphs[i];
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    if (img.pixelColor(x, y).alpha() <= 32) continue;
                    const int over = qMax(qMax(kInset - 2 - x, x - (kInset + kBox + 1)),
                                          qMax(kInset - 2 - y, y - (kInset + kBox + 1)));
                    if (over > worst) worst = over;
                }
            }
        }
        report(worst, 0, "no glyph reaches more than 2px past the box");
    }

    printf("\n[3] no two glyphs read alike\n");
    // This is the section that would have caught the original pen (a bare diagonal) being
    // indistinguishable from the arrow. The threshold is set from the measurement, not
    // guessed: see the printed closest pair.
    {
        int closest = 1 << 30;
        int closestA = -1, closestB = -1;
        for (int i = 0; i < kToolCount; ++i) {
            for (int j = i + 1; j < kToolCount; ++j) {
                const int d = diffCount(glyphs[i], glyphs[j]);
                if (d < closest) { closest = d; closestA = i; closestB = j; }
            }
        }
        printf("       closest pair: %s vs %s, %d of %d pixels differ\n",
               kNames[closestA], kNames[closestB], closest, kButton * kButton);

        // 24, and the number is derived rather than picked: the smallest feature that can
        // carry meaning at 32px is one stroke of the set, 2px by ~6px, which is 12px of ink.
        // Requiring twice that means a glyph cannot be distinguished from its neighbour by
        // antialiasing or by a single stray dot.
        //
        // The pair that actually comes closest is the ellipse and the number badge, which is
        // expected and fine -- a badge *is* a ring with a numeral in it, and the numeral is
        // what tells them apart. What the check is guarding against is that numeral being
        // degraded to a dot, or the badge's ring drifting onto the ellipse's geometry.
        char label[160];
        snprintf(label, sizeof(label),
                 "even the most similar pair differs by at least 24 px (closest is %d)",
                 closest);
        check(closest >= 24, label);
    }

    printf("\n[4] the pen is a curve, not a line\n");
    {
        const int pen = directionReversals(glyphs[3]);
        const int arrow = directionReversals(glyphs[2]);
        printf("       direction reversals: pen %d, arrow %d\n", pen, arrow);
        check(pen >= 2, "the pen's centreline reverses at least twice");
        check(arrow <= 1, "and the arrow's does not, so it still reads as a straight shot");
    }

    printf("\n[5] the arrow has a head, not just a shaft\n");
    {
        // A bare diagonal drawn with the same pen in the same box, as the control.
        QImage bare(kButton, kButton, QImage::Format_ARGB32_Premultiplied);
        bare.fill(Qt::transparent);
        {
            QPainter p(&bare);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(kInset, kInset + kBox), QPointF(kInset + kBox, kInset));
            p.end();
        }
        const int extra = inkCount(glyphs[2]) - inkCount(bare);
        printf("       arrow ink %d, bare shaft %d, difference %d\n",
               inkCount(glyphs[2]), inkCount(bare), extra);
        check(extra >= 20, "the head adds at least 20 px over a bare shaft");
    }

    printf("\n[6] the mosaic is a 3x3 checkerboard\n");
    {
        // Cell boundaries are (i * width) / 3 in integers, so cells are 6, 7 and 7 wide
        // rather than 6.67 each. Sampling the centre of each cell checks the pattern without
        // depending on how the remainder was distributed.
        const QImage& img = glyphs[4];
        int filled = 0, empty = 0, wrong = 0;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const int x0 = kInset + (col * kBox) / 3;
                const int x1 = kInset + ((col + 1) * kBox) / 3;
                const int y0 = kInset + (row * kBox) / 3;
                const int y1 = kInset + ((row + 1) * kBox) / 3;
                const bool hasInk = img.pixelColor((x0 + x1) / 2, (y0 + y1) / 2).alpha() > 32;
                const bool shouldHave = ((row + col) % 2 == 0);
                if (shouldHave) ++filled; else ++empty;
                if (hasInk != shouldHave) ++wrong;
            }
        }
        report(filled, 5, "five cells are filled");
        report(empty, 4, "four are not");
        report(wrong, 0, "and every cell is on the right side of the checkerboard");
    }

    printf("\n[7] the set uses one stroke weight\n");
    {
        // Three strokes measured through their own midlines, in three different orientations.
        // If these disagree, the icons stop looking like a family -- which is exactly what
        // the default square-cap pen produced before the set was unified.
        const int rectEdge = runLength(glyphs[0], kButton / 2, kInset, 0, 1);
        const int textStem = runLength(glyphs[5], kButton / 2, kInset + 3, 1, 0);
        // Perpendicular to the shaft, not along it. The shaft runs (+1,-1); scanning it in
        // its own direction measures its *length* -- the first version of this line reported
        // 19px and called it a thickness.
        const int arrowShaft = runLength(glyphs[2], kInset + 2, kInset + kBox - 2, 1, 1);
        printf("       rect edge %d px, text stem %d px, arrow shaft run %d px\n",
               rectEdge, textStem, arrowShaft);

        // Measured across, the diagonal is the same 2px as the others: a run along the
        // perpendicular axis crosses a 45-degree stroke in the same number of pixels as it
        // crosses a horizontal one, because the perpendicular is the perpendicular. (An
        // earlier version of this line scanned the shaft along its own direction and
        // measured its length, 19px, then failed on its own arithmetic.)
        //
        // Asserted as agreement between the three rather than as three independent ranges:
        // "one stroke weight" is a claim about the set, and agreement is that claim.
        const int lo = qMin(qMin(rectEdge, textStem), arrowShaft);
        const int hi = qMax(qMax(rectEdge, textStem), arrowShaft);
        check(lo >= 2, "every measured stroke is at least 2px thick");
        check(hi - lo <= 1, "and no two of the three differ by more than a pixel");
    }

    printf("\n[8] the filled glyphs keep their fill semantics\n");
    {
        const int bandAlpha = maxAlpha(glyphs[7]);
        const int mosaicAlpha = maxAlpha(glyphs[4]);
        const int rectAlpha = maxAlpha(glyphs[0]);
        const int bandCentre = glyphs[7].pixelColor(kButton / 2, kButton / 2).alpha();
        const int bandOpaque = opaqueCount(glyphs[7]);
        printf("       highlighter: peak alpha %d, centre alpha %d, %d opaque px\n",
               bandAlpha, bandCentre, bandOpaque);
        printf("       mosaic peak alpha %d, rectangle peak alpha %d\n", mosaicAlpha, rectAlpha);

        // The mosaic is a redaction and must be opaque. Swapping this would be a silent
        // behaviour change in a tool whose entire job is to hide something.
        report(mosaicAlpha, 255, "the mosaic is fully opaque");

        // The highlighter is the opposite: its fill has to let the text underneath through.
        check(bandCentre > 100 && bandCentre < 200,
              "the highlighter's fill is translucent, sampled at its centre");

        // ...but only its *fill*. It also carries the set's ordinary 2px opaque outline, so
        // its peak alpha matches the other glyphs and it does not sit dimmer than its
        // neighbours when unselected. Measured: without the outline the band peaked at 140
        // and read as a grey slash beside seven crisp white glyphs.
        report(bandAlpha, rectAlpha, "its outline is as opaque as the rest of the set");
        check(bandOpaque >= 40, "and that outline is a real 2px edge, not a stray pixel");
    }

    printf("\n[9] the glyphs scale with the device, they are not device-pixel art\n");
    {
        // Same logical box, twice the device resolution. A glyph that hardcoded pixel
        // offsets would come out with the same thickness on both; one drawn in logical units
        // doubles it. The toolbar runs at dpr 1.5 on this machine, so this is the live path.
        const QImage at2 = renderGlyph(AnnotationType::Rectangle, 2.0);
        report(at2.width(), kButton * 2, "a dpr-2 render is twice as wide in device pixels");
        const int edge = runLength(at2, kButton, kInset * 2, 0, 1);
        printf("       rectangle edge at dpr 2: %d device px\n", edge);
        check(edge >= 4 && edge <= 6, "the 2px stroke became 4 device pixels");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
