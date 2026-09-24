// Verification for the mosaic dirty rectangle.
//
// The change: updateMosaic() used to derive its work area from the *whole* path every time
// it was called. A mosaic annotation appends one point per mouse move, so the path's
// bounding box grows with the drag -- by the end of a stroke across the screen, every
// mouse move was rescanning the screen. It now takes the rectangle the caller just added
// ink to, which keeps the work proportional to the new ink.
//
// Two things have to be true, and they pull in opposite directions, which is why this
// needs a probe rather than a look:
//
//  1. The result must not change. The whole path is still drawn into the stroke mask; the
//     mask is merely sized to the dirty rectangle, so it clips. That is only sound because
//     antialiasing is off in there -- coverage is a binary inside/outside test, so clipping
//     cannot flip a pixel that both runs would have touched. If someone ever turns
//     antialiasing on in updateMosaic(), the two paths will diverge at the clip edges and
//     this probe is what will say so.
//
//  2. It must actually be cheaper. The measurement is an A/B on the same function: one
//     layer fed the way the old caller did (an empty rectangle, meaning "the whole
//     annotation"), one fed the way the new caller does (the segment between the last two
//     points). Same code, same stroke, so the difference is the change and nothing else.
//
// The first run of this probe found the two feeds differing by 314 whole mosaic blocks --
// not edge pixels, whole blocks, 144 device pixels each. That is the signature of a mask
// placed at the wrong origin rather than of a coverage disagreement: updateMosaic()
// translated the painter by the *logical* rectangle while the mask image had been clipped
// to the image and therefore started somewhere else. Section [5] pins that down.
//
// Run: probe_mosaic_incremental.exe      (no widgets, no windows, nothing flashes)

#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <cstdio>

#include "annotation/AnnotationLayer.h"

using namespace qshot;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void checkInt(int got, int want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL", what, got, want);
}

// --- the fixture ----------------------------------------------------------------------
//
// These come before every helper that reads them. Twice now a helper has been inserted
// above this block and failed to compile with "'kMosaicSize' was not declared"; if a new
// helper needs them, put it below this line.

constexpr qreal kDpr = 1.5;
constexpr int kLogicalW = 400;
constexpr int kLogicalH = 300;
constexpr int kPhysicalW = int(kLogicalW * kDpr);   // 600
constexpr int kPhysicalH = int(kLogicalH * kDpr);   // 450
constexpr int kMosaicSize = 8;                      // logical -> 12 physical
constexpr int kStrokePoints = 240;

/// QImage::operator== compares the format as well as the pixels, so every comparison here
/// goes through the same normalisation. See the qt-gui-verification skill for how that
/// detail makes one check fail and its neighbour pass for the wrong reason.
static int differingPixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return -1;
    const QImage na = a.convertToFormat(QImage::Format_ARGB32);
    const QImage nb = b.convertToFormat(QImage::Format_ARGB32);
    int n = 0;
    for (int y = 0; y < na.height(); ++y) {
        for (int x = 0; x < na.width(); ++x) {
            if (na.pixel(x, y) != nb.pixel(x, y)) ++n;
        }
    }
    return n;
}

/// The bounding box of everything that differs, plus which mosaic blocks it lands in.
/// "45216 pixels differ" says nothing about whether the difference is a systematic offset,
/// a handful of edge blocks, or the whole stroke.
static void diffReport(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) {
        printf("       sizes differ: %dx%d vs %dx%d\n", a.width(), a.height(), b.width(),
               b.height());
        return;
    }
    const QImage na = a.convertToFormat(QImage::Format_ARGB32);
    const QImage nb = b.convertToFormat(QImage::Format_ARGB32);

    const int block = int(kMosaicSize * kDpr); // one mosaic block, in physical pixels
    int minX = na.width(), minY = na.height(), maxX = -1, maxY = -1;
    QVector<QPoint> cells;
    for (int y = 0; y < na.height(); ++y) {
        for (int x = 0; x < na.width(); ++x) {
            if (na.pixel(x, y) == nb.pixel(x, y)) continue;
            minX = qMin(minX, x); minY = qMin(minY, y);
            maxX = qMax(maxX, x); maxY = qMax(maxY, y);
            const QPoint cell(x / block, y / block);
            if (!cells.contains(cell)) cells.append(cell);
        }
    }
    if (maxX < 0) {
        printf("       no differences\n");
        return;
    }
    printf("       differing bbox (%d,%d)-(%d,%d), %d mosaic blocks\n", minX, minY, maxX, maxY,
           int(cells.size()));
    printf("       first blocks:");
    for (int i = 0; i < qMin(8, int(cells.size())); ++i) {
        printf(" (%d,%d)", cells[i].x(), cells[i].y());
    }
    printf("\n");
}

/// Classifies each differing block: was it mosaicked in each image? A mosaic block is
/// uniform and different from the background. "Mosaicked in one and not the other" and
/// "mosaicked in both with different values" are different bugs, and the pixel count alone
/// cannot tell them apart.
static void classifyBlocks(const QImage& bg, const QImage& a, const QImage& b) {
    const QImage na = a.convertToFormat(QImage::Format_ARGB32);
    const QImage nb = b.convertToFormat(QImage::Format_ARGB32);
    const QImage nbg = bg.convertToFormat(QImage::Format_ARGB32);
    const int block = int(kMosaicSize * kDpr);

    const auto isMosaic = [&](const QImage& img, int bx, int by) {
        const QRgb first = img.pixel(bx * block, by * block);
        if (first == nbg.pixel(bx * block, by * block)) return false;
        for (int y = by * block; y < (by + 1) * block && y < img.height(); ++y) {
            for (int x = bx * block; x < (bx + 1) * block && x < img.width(); ++x) {
                if (img.pixel(x, y) != first) return false;
            }
        }
        return true;
    };

    int bothMosaic = 0, onlyWhole = 0, onlyIncremental = 0, neither = 0;
    printf("       block  whole  incr   sample(whole) sample(incr)\n");
    int printed = 0;
    for (int by = 0; by < na.height() / block; ++by) {
        for (int bx = 0; bx < na.width() / block; ++bx) {
            const int x = bx * block, y = by * block;
            if (na.pixel(x, y) == nb.pixel(x, y)) continue;
            const bool mw = isMosaic(na, bx, by);
            const bool mi = isMosaic(nb, bx, by);
            if (mw && mi) ++bothMosaic;
            else if (mw) ++onlyWhole;
            else if (mi) ++onlyIncremental;
            else ++neither;
            if (printed < 8) {
                printf("       (%2d,%2d) %-6s %-6s #%08x     #%08x\n", bx, by,
                       mw ? "yes" : "no", mi ? "yes" : "no", na.pixel(x, y), nb.pixel(x, y));
                ++printed;
            }
        }
    }
    printf("       both mosaicked %d, only whole-path %d, only incremental %d, neither %d\n",
           bothMosaic, onlyWhole, onlyIncremental, neither);
}

/// The bounding box of everything that differs from the background, in physical pixels.
/// `QRect()` when nothing differs.
static QRect changedBox(const QImage& img, const QImage& bg) {
    const QImage ni = img.convertToFormat(QImage::Format_ARGB32);
    const QImage nb = bg.convertToFormat(QImage::Format_ARGB32);
    int minX = ni.width(), minY = ni.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < ni.height(); ++y) {
        for (int x = 0; x < ni.width(); ++x) {
            if (ni.pixel(x, y) == nb.pixel(x, y)) continue;
            minX = qMin(minX, x); minY = qMin(minY, y);
            maxX = qMax(maxX, x); maxY = qMax(maxY, y);
        }
    }
    if (maxX < 0) return QRect();
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

/// A background with structure, so a mosaic block is visibly different from what it covers.
static QImage background() {
    QImage img(kPhysicalW, kPhysicalH, QImage::Format_ARGB32);
    for (int y = 0; y < kPhysicalH; ++y) {
        for (int x = 0; x < kPhysicalW; ++x) {
            img.setPixelColor(x, y, QColor((x * 255) / kPhysicalW, (y * 255) / kPhysicalH,
                                           128 + ((x / 7 + y / 7) % 2) * 100));
        }
    }
    return img;
}

/// A zig-zag that sweeps the full width, so the stroke's own bounding box ends up covering
/// the whole image -- which is exactly the case that used to rescan everything.
static QVector<QPoint> stroke() {
    QVector<QPoint> pts;
    pts.reserve(kStrokePoints);
    for (int i = 0; i < kStrokePoints; ++i) {
        const int x = (i * (kLogicalW - 1)) / (kStrokePoints - 1);
        const int y = ((i / 30) % 2 == 0) ? 60 : 240;
        pts.append(QPoint(x, y));
    }
    return pts;
}

static Annotation mosaicAnnotation() {
    Annotation a;
    a.type = AnnotationType::Mosaic;
    a.mosaicSize = kMosaicSize;
    a.color = Qt::black;
    return a;
}

/// Feed one point at a time, updating after each -- what a mouse drag does. `incremental`
/// selects between the new caller's rectangle and the old one's.
static void feed(AnnotationLayer& layer, Annotation a, const QVector<QPoint>& pts,
                 bool incremental) {
    for (const QPoint& p : pts) {
        const QPoint previous = a.points.isEmpty() ? p : a.points.last();
        a.points.append(p);
        if (incremental) {
            const QRect segment = QRect(previous, p).normalized();
            layer.updateMosaic(a, segment.isEmpty() ? QRect(p, QSize(1, 1)) : segment);
        } else {
            // The empty rectangle means "the whole annotation" -- the previous behaviour.
            layer.updateMosaic(a);
        }
    }
}

int main(int, char**) {
    const QImage bg = background();

    // --- 1 and 2: same result, less work ---------------------------------------------
    printf("[1] feeding the stroke both ways, and timing each\n");
    AnnotationLayer fullLayer;
    fullLayer.setBaseImage(bg, QRect(0, 0, kLogicalW, kLogicalH), kDpr);

    AnnotationLayer incLayer;
    incLayer.setBaseImage(bg, QRect(0, 0, kLogicalW, kLogicalH), kDpr);

    const QVector<QPoint> pts = stroke();

    QElapsedTimer timer;
    timer.start();
    feed(fullLayer, mosaicAnnotation(), pts, /*incremental=*/false);
    const qint64 fullMs = timer.elapsed();

    timer.restart();
    feed(incLayer, mosaicAnnotation(), pts, /*incremental=*/true);
    const qint64 incMs = timer.elapsed();

    printf("       %d points over a %dx%d physical image, mosaic %d logical\n",
           kStrokePoints, kPhysicalW, kPhysicalH, kMosaicSize);
    printf("       whole-path %lld ms, incremental %lld ms\n", fullMs, incMs);

    const QImage fullOut = fullLayer.renderToImage(bg);
    const QImage incOut = incLayer.renderToImage(bg);
    const int diff = differingPixels(fullOut, incOut);
    if (diff != 0) {
        diffReport(fullOut, incOut);
        classifyBlocks(bg, fullOut, incOut);
    }
    checkInt(diff, 0, "the two feeds produce a pixel-identical image");

    printf("\n[2] the incremental feed does less work\n");
    // The baseline has to be slow enough for the ratio to mean anything; if this ever
    // fails, the stroke needs to be longer rather than the threshold looser.
    check(fullMs >= 10, "the whole-path baseline took long enough to compare against");
    check(incMs * 5 < fullMs,
          "the incremental feed is at least 5x faster on a full-width stroke");

    // --- 3: a plain click still leaves a dot ------------------------------------------
    printf("\n[3] a single click still leaves a pen-width dot\n");
    {
        AnnotationLayer layer;
        layer.setBaseImage(bg, QRect(0, 0, kLogicalW, kLogicalH), kDpr);
        Annotation a = mosaicAnnotation();
        a.points.append(QPoint(200, 150));
        // The 1x1 rectangle is how the caller says "just this point"; an empty one would
        // mean "the whole annotation", which happens to be the same thing for a one-point
        // path but is not what the caller means.
        layer.updateMosaic(a, QRect(a.points.first(), QSize(1, 1)));

        const int changed = differingPixels(layer.renderToImage(bg), bg);
        printf("       %d pixels were mosaicked\n", changed);
        check(changed > 0, "the click mosaicked something");
        // One block plus its neighbours: the stroke mask is a square cap of one pen width,
        // and the block walk is grown by the mosaic size on each side. A dot that covered
        // the whole image would mean the dirty rectangle was ignored.
        check(changed < kPhysicalW * kPhysicalH / 4, "and it stayed local, not the whole image");
    }

    // --- 4: the rebuild path still processes the whole annotation ----------------------
    printf("\n[4] rebuilding after an undo matches the incremental result\n");
    {
        // rebuildMosaicCache() calls updateMosaic() with no rectangle, so this is the one
        // place the empty-means-everything default is load-bearing. An undo throws the
        // incremental blocks away and has to reproduce them from the point list.
        AnnotationLayer layer;
        layer.setBaseImage(bg, QRect(0, 0, kLogicalW, kLogicalH), kDpr);
        Annotation a = mosaicAnnotation();
        a.points = pts;
        layer.add(a);
        layer.rebuildMosaicCache();

        const QImage out = layer.renderToImage(bg);
        const int d = differingPixels(out, incOut);
        if (d != 0) diffReport(out, incOut);
        checkInt(d, 0, "a full rebuild reproduces the incremental image");
    }

    // --- 5: the mask is placed at the mask's own origin, not the logical one -----------
    printf("\n[5] a tap at the corner lands in the corner block\n");
    {
        // The bug section [1] caught: physicalBounding is clipped to the image, but the
        // painter was translated by the unclipped logical rectangle. A tap at (2,2) grows
        // its rectangle to (-6,-6)..(11,11), so the clip moves the mask origin by 6 logical
        // pixels in both axes -- and every mosaicked block moved with it.
        AnnotationLayer layer;
        layer.setBaseImage(bg, QRect(0, 0, kLogicalW, kLogicalH), kDpr);
        Annotation a = mosaicAnnotation();
        a.points.append(QPoint(2, 2));
        layer.updateMosaic(a, QRect(a.points.first(), QSize(1, 1)));

        const QImage out = layer.renderToImage(bg);
        const QRect box = changedBox(out, bg);
        const int block = int(kMosaicSize * kDpr);
        printf("       mosaicked bbox (%d,%d)-(%d,%d), one block is %dpx\n", box.left(),
               box.top(), box.right(), box.bottom(), block);
        checkInt(box.left(), 0, "the mosaic starts at the image edge, not a block inward");
        checkInt(box.top(), 0, "in y as well");
        // The ink is clipped to a 3x3 logical corner, so only block (0,0) can be touched.
        // Anything larger means the mask origin drifted and the neighbouring blocks lit up.
        checkInt(box.width(), block, "exactly one block wide");
        checkInt(box.height(), block, "and one block tall");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
