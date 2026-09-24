// Unit assertions for the pure arithmetic behind PinWindow.
//
// Why this gets a probe of its own: the wheel-zoom anchoring is the one part of the
// pin that is impossible to eyeball. A pin that creeps a few pixels per zoom step
// looks fine in a screenshot and wrong in the hand, and the drift only shows up after
// a dozen alternating zoom in/out gestures. It is pure arithmetic with no widget
// involved, so it can be pinned down exactly.
//
// Run: probe_pin_geometry.exe

#include <cstdio>

#include "pin/PinWindow.h"

using namespace qshot;

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void checkEqPoint(const QPoint& got, const QPoint& want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d,%d want %d,%d)\n", ok ? "ok" : "FAIL", what,
           got.x(), got.y(), want.x(), want.y());
}

static void checkNear(qreal got, qreal want, qreal tol, const char* what) {
    ++checks;
    const bool ok = (qAbs(got - want) <= tol);
    if (!ok) ++failures;
    printf("  %-4s %s (got %.6f, want %.6f)\n", ok ? "ok" : "FAIL", what, got, want);
}

int main() {
    printf("\n[1] clampedScale\n");
    checkNear(pin::clampedScale(1.0), 1.0, 0.0, "1.0 is unchanged");
    checkNear(pin::clampedScale(0.25), 0.25, 0.0, "the lower limit itself is kept");
    checkNear(pin::clampedScale(4.0), 4.0, 0.0, "the upper limit itself is kept");
    checkNear(pin::clampedScale(0.1), pin::kMinScale, 0.0, "below the floor clamps up");
    checkNear(pin::clampedScale(10.0), pin::kMaxScale, 0.0, "above the ceiling clamps down");
    checkNear(pin::clampedScale(0.0), pin::kMinScale, 0.0, "zero clamps to the floor");

    printf("\n[2] anchoredTopLeft: the anchored point stays put\n");
    {
        // Cursor exactly on the content origin: the origin cannot move.
        const QPoint tl(100, 100);
        checkEqPoint(pin::anchoredTopLeft(tl, QPoint(100, 100), 1.0, 2.0), tl,
                     "anchor at the origin, zoom 1.0 -> 2.0");
        checkEqPoint(pin::anchoredTopLeft(tl, QPoint(100, 100), 1.0, 0.5), tl,
                     "anchor at the origin, zoom 1.0 -> 0.5");
    }
    {
        // 100px into the image, doubling: that pixel must end up 200px into it.
        checkEqPoint(pin::anchoredTopLeft(QPoint(100, 100), QPoint(200, 200), 1.0, 2.0),
                     QPoint(0, 0), "anchor 100px in, zoom 1.0 -> 2.0");
        checkEqPoint(pin::anchoredTopLeft(QPoint(100, 100), QPoint(200, 200), 1.0, 0.5),
                     QPoint(150, 150), "anchor 100px in, zoom 1.0 -> 0.5");
        checkEqPoint(pin::anchoredTopLeft(QPoint(100, 100), QPoint(150, 150), 0.5, 1.0),
                     QPoint(50, 50), "anchor 50px in at 0.5x (=100 image px), zoom back to 1.0");
    }

    printf("\n[3] anchoring verified from the image's point of view\n");
    {
        // Independently recompute: the image coordinate under the anchor must be the
        // same before and after. This checks the intent, not just the formula.
        struct Case { QPoint tl; QPoint anchor; qreal from; qreal to; };
        const Case cases[] = {
            { QPoint(100, 100), QPoint(250, 175), 1.0, 2.0 },
            { QPoint(100, 100), QPoint(250, 175), 1.0, 0.5 },
            { QPoint(-40, 900), QPoint(310, 1000), 0.75, 1.5 },
            { QPoint(1900, 50), QPoint(2100, 120), 1.0, 1.1 },   // right-hand screen
            { QPoint(300, -1000), QPoint(500, -900), 1.0, 4.0 }, // screen above
        };
        for (const Case& c : cases) {
            const QPoint moved = pin::anchoredTopLeft(c.tl, c.anchor, c.from, c.to);
            const qreal beforeX = (c.anchor.x() - c.tl.x()) / c.from;
            const qreal beforeY = (c.anchor.y() - c.tl.y()) / c.from;
            const qreal afterX = (c.anchor.x() - moved.x()) / c.to;
            const qreal afterY = (c.anchor.y() - moved.y()) / c.to;
            char label[128];
            snprintf(label, sizeof(label),
                     "image coord under anchor preserved (%.2f->%.2f at %g->%g)",
                     c.from, c.to, c.from, c.to);
            checkNear(beforeX, afterX, 0.5 / c.to, label);
            snprintf(label, sizeof(label),
                     "  same, y axis (%.2f->%.2f at %g->%g)", c.from, c.to, c.from, c.to);
            checkNear(beforeY, afterY, 0.5 / c.to, label);
        }
    }

    printf("\n[4] translation invariance (nothing assumes an origin of 0,0)\n");
    {
        const QPoint tl(100, 100);
        const QPoint anchor(250, 175);
        const QPoint base = pin::anchoredTopLeft(tl, anchor, 1.0, 1.5);
        for (const QPoint& shift : { QPoint(1920, 0), QPoint(0, -1080), QPoint(-800, 600) }) {
            const QPoint moved = pin::anchoredTopLeft(tl + shift, anchor + shift, 1.0, 1.5);
            char label[128];
            snprintf(label, sizeof(label), "shifting the whole scene by (%d,%d) shifts the result",
                     shift.x(), shift.y());
            checkEqPoint(moved, base + shift, label);
        }
    }

    printf("\n[5] round trip: zoom in then out with the same anchor returns home\n");
    {
        const QPoint tl(320, 240);
        const QPoint anchor(500, 380);
        QPoint cur = tl;
        qreal scale = 1.0;
        for (int i = 0; i < 12; ++i) {
            const qreal next = pin::clampedScale(scale * pin::kScaleStep);
            cur = pin::anchoredTopLeft(cur, anchor, scale, next);
            scale = next;
        }
        for (int i = 0; i < 12; ++i) {
            const qreal next = pin::clampedScale(scale / pin::kScaleStep);
            cur = pin::anchoredTopLeft(cur, anchor, scale, next);
            scale = next;
        }
        checkNear(scale, 1.0, 1e-6, "scale returns to 1.0 after 12 in and 12 out");
        // Integer rounding is allowed to cost a fraction of a pixel per step, but a
        // truncating implementation would drift steadily in one direction.
        checkEqPoint(cur, tl, "top-left returns exactly home (rounding, not truncation)");
    }

    printf("\n[6] clamping happens before anchoring, so hitting a limit does not move the pin\n");
    {
        // At the ceiling a further zoom-in request is a no-op; the caller checks for
        // this with qFuzzyCompare, and the value it compares against must be the
        // clamped one or the pin would keep sliding while the image stays the same.
        const qreal atCeiling = pin::clampedScale(pin::kMaxScale * pin::kScaleStep);
        checkNear(atCeiling, pin::kMaxScale, 0.0, "past the ceiling clamps to the ceiling");
        check(atCeiling != pin::kMaxScale * pin::kScaleStep,
              "the clamped value differs from the raw request, so the caller can detect it");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
