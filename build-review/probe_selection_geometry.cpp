// Unit assertions for the pure selection geometry.
//
// This is the one part of the overlay that can be verified deterministically in
// this environment: no widget, no window, no focus, no screen. Everything here
// is a total function of its arguments, so a plain assertion is real evidence.
//
// Build (from the repository root), then run with $QT/bin on PATH:
//
//   QT=D:/Qt/6.11.2/mingw_64
//   g++ -std=c++17 -Wall -Wextra -Wshadow -Wunused -Isrc -I$QT/include -I$QT/include/QtCore
//       build-review/probe_selection_geometry.cpp src/overlay/SelectionGeometry.cpp
//       -L$QT/lib -lQt6Core -o <output.exe>
//
// Exit code 0 means every assertion held.
//
// QRect note: x()/right() and y()/bottom() are *inclusive* endpoints, so a rect
// of width w starting at x ends at x+w-1, and center() floors. Several expected
// values below look "off by one" until that is kept in mind.

#include "overlay/SelectionGeometry.h"

#include <QPoint>
#include <QRect>
#include <QVector>

#include <cstdio>

using namespace qshot::selection;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL  %s\n", what);
    }
}

void checkRect(const QRect& actual, const QRect& expected, const char* what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  %s\n      expected (%d,%d %dx%d), got (%d,%d %dx%d)\n",
                    what,
                    expected.x(), expected.y(), expected.width(), expected.height(),
                    actual.x(), actual.y(), actual.width(), actual.height());
    }
}

void checkHandle(Handle actual, Handle expected, const char* what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  %s\n      expected handle %d, got %d\n",
                    what, static_cast<int>(expected), static_cast<int>(actual));
    }
}

// --- hit testing ----------------------------------------------------------

void testHitTestHandles()
{
    // right() and bottom() are the last addressable pixel, so this rect's grips
    // sit on x = 100 / 199 / 299 and y = 100 / 174 / 249.
    const QRect sel(100, 100, 200, 150);

    checkHandle(hitTestHandle(sel, QPoint(100, 100)), Handle::TopLeft,     "grip: top-left corner");
    checkHandle(hitTestHandle(sel, QPoint(299, 100)), Handle::TopRight,    "grip: top-right corner");
    checkHandle(hitTestHandle(sel, QPoint(100, 249)), Handle::BottomLeft,  "grip: bottom-left corner");
    checkHandle(hitTestHandle(sel, QPoint(299, 249)), Handle::BottomRight, "grip: bottom-right corner");

    checkHandle(hitTestHandle(sel, QPoint(199, 100)), Handle::Top,    "grip: top edge midpoint");
    checkHandle(hitTestHandle(sel, QPoint(199, 249)), Handle::Bottom, "grip: bottom edge midpoint");
    checkHandle(hitTestHandle(sel, QPoint(100, 174)), Handle::Left,   "grip: left edge midpoint");
    checkHandle(hitTestHandle(sel, QPoint(299, 174)), Handle::Right,  "grip: right edge midpoint");

    // Interior points away from any grip belong to no handle -- the caller uses
    // that to decide between moving the selection and resizing it.
    checkHandle(hitTestHandle(sel, QPoint(199, 174)), Handle::None, "grip: centre is not a grip");
    checkHandle(hitTestHandle(sel, QPoint(150, 130)), Handle::None, "grip: interior is not a grip");
    checkHandle(hitTestHandle(sel, QPoint(0, 0)),     Handle::None, "grip: far outside is not a grip");

    // An empty selection has no grips at all.
    checkHandle(hitTestHandle(QRect(), QPoint(0, 0)), Handle::None, "grip: empty selection has none");
}

void testHitTestHandleMargin()
{
    const QRect sel(100, 100, 200, 150);

    // The hit box for the top-left anchor is QRect(92, 92, 16, 16), which spans
    // x and y in [92..107]: eight pixels on the near side, seven on the far side,
    // because QRect's far edge is inclusive.
    checkHandle(hitTestHandle(sel, QPoint(107, 107)), Handle::TopLeft, "margin: inside the box");
    checkHandle(hitTestHandle(sel, QPoint(108, 108)), Handle::None,    "margin: one past the box is out");
    checkHandle(hitTestHandle(sel, QPoint(92, 92)),   Handle::TopLeft, "margin: on the near edge");
    checkHandle(hitTestHandle(sel, QPoint(91, 91)),   Handle::None,    "margin: one before the near edge is out");
    checkHandle(hitTestHandle(sel, QPoint(93, 93)),   Handle::TopLeft, "margin: outside the rect but in the box");

    // The margin is a parameter, so the box size is testable without editing it.
    checkHandle(hitTestHandle(sel, QPoint(109, 109), 4),  Handle::None,    "margin: smaller margin shrinks the box");
    checkHandle(hitTestHandle(sel, QPoint(120, 120), 24), Handle::TopLeft, "margin: larger margin grows the box");
}

void testCornerBeatsEdge()
{
    // On a small selection the corner and edge hit boxes overlap. The corner is
    // what the user is aiming at, so it must win regardless of evaluation order.
    const QRect small(0, 0, 10, 10);
    checkHandle(hitTestHandle(small, QPoint(0, 0)), Handle::TopLeft,     "overlap: corner wins at the corner");
    checkHandle(hitTestHandle(small, QPoint(5, 0)), Handle::TopLeft,     "overlap: corner wins mid-edge");
    checkHandle(hitTestHandle(small, QPoint(9, 9)), Handle::BottomRight, "overlap: far corner still wins");
}

// --- resizing -------------------------------------------------------------

void testResizeNonFlipping()
{
    const QRect start(100, 100, 200, 150); // x in [100..299], y in [100..249]

    checkRect(resizedRect(start, Handle::Right, QPoint(400, 200)),
              QRect(100, 100, 301, 150), "resize: right edge follows the cursor");
    checkRect(resizedRect(start, Handle::Left, QPoint(50, 200)),
              QRect(50, 100, 250, 150), "resize: left edge follows the cursor");
    checkRect(resizedRect(start, Handle::Top, QPoint(200, 20)),
              QRect(100, 20, 200, 230), "resize: top edge follows the cursor");
    checkRect(resizedRect(start, Handle::Bottom, QPoint(200, 400)),
              QRect(100, 100, 200, 301), "resize: bottom edge follows the cursor");

    checkRect(resizedRect(start, Handle::TopLeft, QPoint(60, 60)),
              QRect(60, 60, 240, 190), "resize: top-left corner moves both axes");
    checkRect(resizedRect(start, Handle::TopRight, QPoint(350, 60)),
              QRect(100, 60, 251, 190), "resize: top-right corner moves both axes");
    checkRect(resizedRect(start, Handle::BottomLeft, QPoint(60, 300)),
              QRect(60, 100, 240, 201), "resize: bottom-left corner moves both axes");
    checkRect(resizedRect(start, Handle::BottomRight, QPoint(350, 300)),
              QRect(100, 100, 251, 201), "resize: bottom-right corner moves both axes");

    // Edge grips must move only their own axis.
    checkRect(resizedRect(start, Handle::Left, QPoint(50, 999)),
              QRect(50, 100, 250, 150), "resize: left grip ignores the y coordinate");
    checkRect(resizedRect(start, Handle::Top, QPoint(999, 20)),
              QRect(100, 20, 200, 230), "resize: top grip ignores the x coordinate");

    // Handle::None must leave the rectangle untouched.
    checkRect(resizedRect(start, Handle::None, QPoint(999, 999)),
              start, "resize: no handle leaves the rect alone");
}

void testResizeFlipping()
{
    const QRect start(100, 100, 200, 150);

    // Dragging a grip past the opposite edge must produce a valid rectangle
    // rather than a negative-sized one. The exact off-by-one of QRect's
    // normalisation is Qt's business; what matters here is that the result is
    // never inverted, because an inverted rect silently matches nothing and the
    // selection would appear to vanish mid-drag.
    const Handle handles[] = {
        Handle::TopLeft, Handle::Top, Handle::TopRight, Handle::Right,
        Handle::BottomRight, Handle::Bottom, Handle::BottomLeft, Handle::Left
    };

    for (Handle h : handles) {
        const QRect r = resizedRect(start, h, QPoint(5000, 5000));
        ++g_checks;
        if (r.width() < 0 || r.height() < 0) {
            ++g_failures;
            std::printf("FAIL  flip: handle %d produced a negative size (%dx%d) dragged down-right\n",
                        static_cast<int>(h), r.width(), r.height());
        }
    }

    // Same again in the opposite direction, which is the case that catches a
    // missing normalisation on the top/left handles.
    for (Handle h : handles) {
        const QRect r = resizedRect(start, h, QPoint(-5000, -5000));
        ++g_checks;
        if (r.width() < 0 || r.height() < 0) {
            ++g_failures;
            std::printf("FAIL  flip: handle %d produced a negative size (%dx%d) dragged up-left\n",
                        static_cast<int>(h), r.width(), r.height());
        }
    }
}

// --- cursors --------------------------------------------------------------

void testCursorForHandle()
{
    check(cursorForHandle(Handle::TopLeft)     == Qt::SizeFDiagCursor, "cursor: top-left is F-diagonal");
    check(cursorForHandle(Handle::BottomRight) == Qt::SizeFDiagCursor, "cursor: bottom-right is F-diagonal");
    check(cursorForHandle(Handle::TopRight)    == Qt::SizeBDiagCursor, "cursor: top-right is B-diagonal");
    check(cursorForHandle(Handle::BottomLeft)  == Qt::SizeBDiagCursor, "cursor: bottom-left is B-diagonal");
    check(cursorForHandle(Handle::Top)         == Qt::SizeVerCursor,   "cursor: top is vertical");
    check(cursorForHandle(Handle::Bottom)      == Qt::SizeVerCursor,   "cursor: bottom is vertical");
    check(cursorForHandle(Handle::Left)        == Qt::SizeHorCursor,   "cursor: left is horizontal");
    check(cursorForHandle(Handle::Right)       == Qt::SizeHorCursor,   "cursor: right is horizontal");

    // Documented sentinel: callers must test the handle, not this value.
    check(cursorForHandle(Handle::None) == Qt::ArrowCursor, "cursor: none returns the sentinel");
}

// --- control points -------------------------------------------------------

void testControlPointRects()
{
    // For QRect(0,0,100,100): left=0, right=99, center()=49. The drawn grips use
    // right()-1 and bottom()-1, so the anchors are x,y in {0, 49, 98}.
    const QRect sel(0, 0, 100, 100);
    const QVector<QRect> grips = controlPointRects(sel, 6);

    check(grips.size() == 8, "grips: eight of them");

    // Each grip is a 6px square centred on its anchor.
    check(grips.contains(QRect(-3, -3, 6, 6)), "grips: top-left anchor");
    check(grips.contains(QRect(95, 95, 6, 6)), "grips: bottom-right anchor");
    check(grips.contains(QRect(46, -3, 6, 6)), "grips: top edge midpoint");
    check(grips.contains(QRect(-3, 46, 6, 6)), "grips: left edge midpoint");

    // The centre of the 3x3 anchor grid is deliberately omitted.
    check(!grips.contains(QRect(46, 46, 6, 6)), "grips: centre is omitted");

    // Every drawn grip must also be grabbable, otherwise the user sees a handle
    // they cannot use. This is the assertion that ties the two modules together.
    const QPoint anchors[8] = {
        {0, 0}, {98, 0}, {0, 98}, {98, 98}, {49, 0}, {49, 98}, {0, 49}, {98, 49}
    };
    for (const QPoint& anchor : anchors) {
        check(hitTestHandle(sel, anchor) != Handle::None,
              "grips: every drawn grip is grabbable");
    }
}

// --- drag threshold -------------------------------------------------------

void testDragThreshold()
{
    // Manhattan distance, strictly greater than the threshold.
    check(!isDragGesture(QPoint(0, 0), QPoint(2, 2)), "drag: 4px is still a click");
    check(!isDragGesture(QPoint(0, 0), QPoint(3, 2)), "drag: exactly 5px is still a click");
    check(isDragGesture(QPoint(0, 0), QPoint(3, 3)),  "drag: 6px is a drag");
    check(isDragGesture(QPoint(0, 0), QPoint(4, 2)),  "drag: 6px off-axis is a drag");
    check(!isDragGesture(QPoint(10, 10), QPoint(10, 10)), "drag: no movement is a click");

    // The threshold is a parameter, not a constant buried in the function.
    check(isDragGesture(QPoint(0, 0), QPoint(1, 1), 1),  "drag: custom threshold is honoured");
    check(!isDragGesture(QPoint(0, 0), QPoint(1, 1), 2), "drag: custom threshold is honoured");
}

void testDragRect()
{
    // The whole point of building this from min/max: the same gesture must give
    // the same rectangle in all four directions. QRect(topLeft, bottomRight)
    // followed by normalized() does not have that property -- it normalises one
    // pixel short when the drag runs up-and-left, which is why this is no longer
    // implemented that way.
    const QRect expected(10, 10, 41, 31); // covers x,y in [10..50] and [10..40]

    checkRect(dragRect(QPoint(10, 10), QPoint(50, 40)), expected, "dragRect: down-right");
    checkRect(dragRect(QPoint(50, 40), QPoint(10, 10)), expected, "dragRect: up-left matches down-right");
    checkRect(dragRect(QPoint(50, 10), QPoint(10, 40)), expected, "dragRect: up-right matches down-right");
    checkRect(dragRect(QPoint(10, 40), QPoint(50, 10)), expected, "dragRect: down-left matches down-right");

    // A pure horizontal or vertical drag keeps the other axis at 1px.
    checkRect(dragRect(QPoint(10, 20), QPoint(50, 20)),
              QRect(10, 20, 41, 1), "dragRect: horizontal drag is one pixel tall");
    checkRect(dragRect(QPoint(20, 10), QPoint(20, 50)),
              QRect(20, 10, 1, 41), "dragRect: vertical drag is one pixel wide");

    checkRect(dragRect(QPoint(20, 20), QPoint(20, 20)),
              QRect(20, 20, 1, 1), "dragRect: zero-length drag is a 1px rect");
}

// --- logical to physical --------------------------------------------------

void testToPhysicalRect()
{
    const QRect clamp(0, 0, 2560, 1600); // a 1707x1067 logical screen at dpr 1.5

    checkRect(toPhysicalRect(QRect(10, 10, 100, 50), 1.5, clamp),
              QRect(15, 15, 150, 75), "physical: scaled by dpr");
    checkRect(toPhysicalRect(QRect(10, 10, 100, 50), 1.0, clamp),
              QRect(10, 10, 100, 50), "physical: dpr 1 is the identity");

    // An empty selection has no physical counterpart.
    check(toPhysicalRect(QRect(), 1.5, clamp).isNull(), "physical: empty stays null");
    check(toPhysicalRect(QRect(10, 10, 0, 0), 1.5, clamp).isNull(), "physical: zero-size stays null");

    // A selection hanging off the right edge is clipped, not rejected: the part
    // still on screen has valid source pixels, and dropping them would silently
    // produce a smaller image than the user asked for.
    checkRect(toPhysicalRect(QRect(1700, 0, 100, 100), 1.5, clamp),
              QRect(2550, 0, 10, 150), "physical: clipped to the image edge");

    // Entirely off-screen selections produce nothing.
    check(toPhysicalRect(QRect(5000, 5000, 100, 100), 1.5, clamp).isEmpty(),
          "physical: fully off-screen is empty");

    // Rounding is applied to the origin and the size alike, so the rectangle
    // stays self-consistent: qRound(1.5) == 2 on both.
    checkRect(toPhysicalRect(QRect(1, 1, 1, 1), 1.5, clamp),
              QRect(2, 2, 2, 2), "physical: rounds half away from zero");
}

} // namespace

int main()
{
    testHitTestHandles();
    testHitTestHandleMargin();
    testCornerBeatsEdge();
    testResizeNonFlipping();
    testResizeFlipping();
    testCursorForHandle();
    testControlPointRects();
    testDragThreshold();
    testDragRect();
    testToPhysicalRect();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
