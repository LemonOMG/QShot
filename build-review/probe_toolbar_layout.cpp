// Does the toolbar still fit once it carries eight tools instead of six?
//
// The roadmap flagged this as a blocking constraint for M5: two more tool buttons plus the
// action buttons were estimated at 12 x 32px ~= 384px, and a small selection was assumed to
// overflow the screen. That estimate predates the Pin button (added in M3), so the real
// count is already higher than it assumed.
//
// The claim is worth measuring rather than trusting, because the toolbar is a *top-level
// window*: it is not confined to the selection at all. updatePosition() clamps it to the
// screen it was told about. So the real question is not "does it fit in the selection" but
// "does it fit on the screen, and does the clamp actually hold at every corner".
//
// This probe therefore asserts the invariant rather than a hardcoded width, so it stays
// meaningful as buttons are added: for any selection, the placed toolbar must lie entirely
// inside the screen rect it was given.

#include <QApplication>
#include <QRect>
#include <QVector>
#include <cstdio>

#include "overlay/ToolbarWidget.h"
#include "core/Settings.h"

using namespace qshot;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void report(const char* what, int got, int want) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL", what, got, want);
}

// Places the toolbar for one selection and reports whether it ended up inside the screen.
// Both rectangles are in global desktop coordinates, which is what updatePosition expects.
static bool placeAndCheck(ToolbarWidget& toolbar, const QRect& selection, const QRect& screen,
                          const char* what) {
    toolbar.updatePosition(selection, screen);
    const QRect placed = toolbar.geometry();
    const bool inside = screen.contains(placed);
    ++checks;
    if (!inside) ++failures;
    printf("  %-4s %s\n", inside ? "ok" : "FAIL", what);
    if (!inside) {
        printf("         placed %d,%d %dx%d vs screen %d,%d %dx%d\n",
               placed.x(), placed.y(), placed.width(), placed.height(),
               screen.x(), screen.y(), screen.width(), screen.height());
    }
    return inside;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QShot"));
    QCoreApplication::setApplicationName(QStringLiteral("QShot"));

    // The developer's real machine: 1707x1067 logical at dpr 1.5.
    const QRect desktop(0, 0, 1707, 1067);
    // A plausible small laptop, to see how much headroom is left on a worse screen.
    const QRect small(0, 0, 1366, 768);
    // A second monitor to the right, so the clamp has to use a non-zero origin.
    const QRect right(1920, 0, 1920, 1080);

    ToolbarWidget toolbar;
    toolbar.show(); // needed for geometry() to be meaningful on some platforms

    printf("[1] actual size\n");
    printf("       toolbar is %dx%d\n", toolbar.width(), toolbar.height());
    // 40px tall is the fixed design height; the width is whatever the button list needs.
    report("height is the fixed 40px", toolbar.height(), 40);

    const int width = toolbar.width();
    // Report the headroom rather than asserting a magic number, so this stays useful when
    // buttons are added or the button size changes.
    printf("       headroom on %dx%d: %d px\n", desktop.width(), desktop.height(),
           desktop.width() - width);
    printf("       headroom on %dx%d: %d px\n", small.width(), small.height(),
           small.width() - width);
    check(width < desktop.width(), "fits on the developer's screen");
    check(width < small.width(), "fits on a 1366px screen");

    printf("\n[2] a 1x1 selection at each corner stays on screen\n");
    // The worst case for the clamp: a selection so small the toolbar cannot be centred on
    // it, pushed into each corner in turn.
    const QPoint corners[4] = {
        { 0, 0 },
        { desktop.right(), 0 },
        { 0, desktop.bottom() },
        { desktop.right(), desktop.bottom() },
    };
    const char* names[4] = { "top-left", "top-right", "bottom-left", "bottom-right" };
    for (int i = 0; i < 4; ++i) {
        const QRect sel(corners[i], QSize(1, 1));
        char label[128];
        snprintf(label, sizeof(label), "1x1 selection at the %s corner", names[i]);
        placeAndCheck(toolbar, sel, desktop, label);
    }

    printf("\n[3] the clamp holds on a second monitor with a non-zero origin\n");
    // A screen at x=1920 is where "clamp against 0 instead of the screen edge" breaks.
    const QRect selOnRight(QPoint(3839, 1079), QSize(1, 1));
    placeAndCheck(toolbar, selOnRight, right, "1x1 selection at the far corner of screen 2");

    const QRect selLeftOfRight(QPoint(1920, 0), QSize(1, 1));
    placeAndCheck(toolbar, selLeftOfRight, right, "1x1 selection at the near corner of screen 2");

    printf("\n[4] the toolbar prefers to sit below the selection\n");
    // Not a correctness requirement, but a regression here would be visible: the toolbar
    // should be under the selection when there is room, and flip above only when there
    // is not.
    const QRect midSelection(QPoint(700, 400), QSize(200, 150));
    toolbar.updatePosition(midSelection, desktop);
    report("below a mid-screen selection", toolbar.geometry().top(),
           midSelection.bottom() + 8);

    // Pushed against the bottom edge, there is no room below, so it flips above.
    const QRect lowSelection(QPoint(700, 1000), QSize(200, 60));
    toolbar.updatePosition(lowSelection, desktop);
    check(toolbar.geometry().bottom() < lowSelection.top(),
          "flips above a selection with no room below");

    printf("\n[5] the toolbar is a top-level window, not a child of the selection\n");
    // This is what makes the whole width question benign: the toolbar is allowed to be
    // wider than the selection it belongs to.
    check(toolbar.isWindow(), "the toolbar is its own window");
    const QRect narrow(QPoint(800, 400), QSize(30, 30));
    toolbar.updatePosition(narrow, desktop);
    check(toolbar.width() > narrow.width(),
          "and is legitimately wider than a narrow selection");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
