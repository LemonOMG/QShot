// Verifies the multi-monitor coordinate handling.
//
// The defect: the overlay's own coordinate space starts at (0,0) on whichever
// screen it covers, but the toolbar and the text editor are separate top-level
// windows, and move() on a top-level window takes *global desktop* coordinates.
// On a single monitor whose origin is (0,0) the two spaces coincide, so every
// mistake of this kind is invisible. On any other monitor the floating windows are
// placed on the wrong screen, and a clicked-on window resolves to a selection that
// does not intersect the screen image, producing an empty crop.
//
// This machine has one monitor, so the second screen is synthesised. That is
// sufficient here because the defect is pure arithmetic on the screen origin -- no
// real second display is involved in getting it right.
//
// What this probe CANNOT establish: whether the window manager accepts these
// positions on a real multi-monitor desktop, and whether focus routing behaves.
// Those need a human on real hardware.
//
// Build (from the repository root), then run with $QT/bin on PATH:
//
//   QT=D:/Qt/6.11.2/mingw_64
//   AUTOGEN=build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug/qshot_autogen
//   g++ -std=c++17 -Wall -Wextra -Wshadow -Wunused -Isrc -I$QT/include
//       -I$QT/include/QtCore -I$QT/include/QtGui -I$QT/include/QtWidgets
//       build-review/probe_multiscreen_coords.cpp
//       src/overlay/ToolbarWidget.cpp src/core/Settings.cpp src/core/Strings.cpp
//       $AUTOGEN/ONP5DLDO2K/moc_ToolbarWidget.cpp $AUTOGEN/PRMOGMWJPH/moc_Settings.cpp
//       -L$QT/lib -lQt6Core -lQt6Gui -lQt6Widgets -o <output.exe>
//
// Exit code 0 means every assertion held.

#include "overlay/ToolbarWidget.h"

#include <QApplication>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <cstdio>

using namespace qshot;

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

/// A monitor to the right of the primary one: the origin's x is non-zero, which is
/// the whole point.
const QRect kRightScreen(1920, 0, 1920, 1080);

/// A monitor stacked above the primary one, so the origin's y is non-zero and the
/// "is there room above?" test has to compare against something other than 0.
const QRect kAboveScreen(0, -1080, 1920, 1080);

/// Stands in for the overlay window: identical flags and geometry, so it reports
/// its origin through the same mechanism SnapOverlay::globalOrigin() uses.
///
/// Deliberately never shown. move()/mapToGlobal() work from the widget's geometry,
/// so showing it would only flash a full-screen window at whoever runs the probe.
class OverlayStandIn : public QWidget {
public:
    explicit OverlayStandIn(const QRect& screen)
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                       | Qt::WindowStaysOnTopHint | Qt::Tool);
        setGeometry(screen);
    }

    /// Mirrors SnapOverlay::globalOrigin().
    QPoint globalOrigin() const { return mapToGlobal(QPoint(0, 0)); }
};

// ---------------------------------------------------------------------------
// 1. The overlay's origin, and the conversion that depends on it
// ---------------------------------------------------------------------------

void testGlobalOrigin()
{
    OverlayStandIn overlay(kRightScreen);

    // The assumption the whole conversion rests on: a frameless full-screen tool
    // window reports its own top-left as the screen origin. Measured, not assumed.
    check(overlay.globalOrigin() == kRightScreen.topLeft(),
          "origin: the overlay's (0,0) is the screen's top-left");

    // On the primary monitor the conversion is the identity, which is exactly why
    // a missing conversion went unnoticed for so long.
    OverlayStandIn primary(QRect(0, 0, 1707, 1067));
    check(primary.globalOrigin() == QPoint(0, 0),
          "origin: on a screen at the origin the conversion is a no-op");

    // mapFromGlobal must be the exact inverse, since the text editor uses it to
    // turn its own global position back into an annotation offset.
    check(overlay.mapFromGlobal(overlay.mapToGlobal(QPoint(37, 91))) == QPoint(37, 91),
          "origin: the local/global round trip is lossless");
}

void testHoverNormalisation()
{
    OverlayStandIn overlay(kRightScreen);
    const QPoint origin = overlay.globalOrigin();
    const QRect localSpace(QPoint(0, 0), kRightScreen.size());

    // A window on the secondary monitor, as the detector reports it: global.
    const QRect globalWindow(2000, 100, 300, 200);
    const QRect local = globalWindow.translated(-origin);

    checkRect(local, QRect(80, 100, 300, 200),
              "hover: the global rect is translated into the overlay's space");
    check(localSpace.contains(local),
          "hover: the translated rect lands inside the overlay");

    // Without the translation the rect is outside the overlay entirely: it would be
    // painted off-widget, and taking it as the selection would crop to nothing
    // because it does not intersect the screen image.
    check(!localSpace.contains(globalWindow),
          "hover: the untranslated rect is outside the overlay (this was the defect)");
    check(globalWindow.intersected(localSpace).isEmpty(),
          "hover: the untranslated rect would crop to an empty image");

    // A monitor above the primary one: only the y origin is non-zero, so a fix that
    // handled only x would still be wrong here.
    OverlayStandIn above(kAboveScreen);
    check(above.globalOrigin() == QPoint(0, -1080),
          "hover: a screen above the primary has a negative y origin");
    checkRect(QRect(300, -900, 200, 150).translated(-above.globalOrigin()),
              QRect(300, 180, 200, 150),
              "hover: the y axis is translated for a screen above the primary");
}

// ---------------------------------------------------------------------------
// 2. Toolbar placement
// ---------------------------------------------------------------------------

/// What showToolbar() used to do: pass the selection in overlay-local coordinates
/// and the overlay's own rect as the screen bounds.
QRect placeWithOldCaller(ToolbarWidget& toolbar, const QRect& localSelection)
{
    toolbar.updatePosition(localSelection, QRect(QPoint(0, 0), kRightScreen.size()));
    return toolbar.geometry();
}

/// What showToolbar() does now: both rectangles in global coordinates, the
/// selection translated by the overlay's origin.
QRect placeWithNewCaller(ToolbarWidget& toolbar, const QRect& localSelection,
                         const QPoint& origin, const QRect& screen)
{
    toolbar.updatePosition(localSelection.translated(origin), screen);
    return toolbar.geometry();
}

void testToolbarPlacement()
{
    OverlayStandIn overlay(kRightScreen);
    const QPoint origin = overlay.globalOrigin();
    const QRect localSelection(100, 200, 300, 150);

    // Old behaviour, on the second screen: the toolbar is placed at the selection's
    // *local* position, which on this screen is far to the left of where the user
    // is actually working -- on the wrong monitor.
    ToolbarWidget oldToolbar;
    const QRect oldGeometry = placeWithOldCaller(oldToolbar, localSelection);
    check(!kRightScreen.contains(oldGeometry),
          "toolbar: the old local-coordinate call places it off the second screen");

    // New behaviour: under the selection, on the correct screen.
    ToolbarWidget toolbar;
    const QRect geometry = placeWithNewCaller(toolbar, localSelection, origin, kRightScreen);
    check(kRightScreen.contains(geometry),
          "toolbar: the global-coordinate call keeps it on the second screen");
    check(geometry.top() > localSelection.translated(origin).bottom(),
          "toolbar: it sits below the selection");

    // A selection at the bottom of the screen must flip above it.
    const QRect bottomSelection(400, kRightScreen.height() - 60, 300, 50);
    const QRect flipped = placeWithNewCaller(toolbar, bottomSelection, origin, kRightScreen);
    check(kRightScreen.contains(flipped),
          "toolbar: flips above a bottom-edge selection and stays on screen");
    check(flipped.bottom() < bottomSelection.translated(origin).top(),
          "toolbar: the flipped position really is above the selection");

    // The monitor above the primary one: the y origin is negative, so every
    // comparison against 0 in the old code was against the wrong edge.
    OverlayStandIn aboveOverlay(kAboveScreen);
    const QRect onAboveScreen(100, 10, 300, 100);
    const QRect aboveGeometry =
        placeWithNewCaller(toolbar, onAboveScreen, aboveOverlay.globalOrigin(), kAboveScreen);
    check(kAboveScreen.contains(aboveGeometry),
          "toolbar: stays on a screen whose top edge is above the primary one");

    ToolbarWidget oldToolbar2;
    const QRect oldOnAbove = placeWithOldCaller(oldToolbar2, onAboveScreen);
    check(!kAboveScreen.contains(oldOnAbove),
          "toolbar: the old call misplaces it on the monitor above the primary one");
}

// ---------------------------------------------------------------------------
// 3. Text editor placement
// ---------------------------------------------------------------------------

void testTextEditorPlacement()
{
    OverlayStandIn overlay(kRightScreen);

    // A click at local (100, 200) on the second screen. startInput() moves a
    // top-level window, so it needs the global point.
    const QPoint localClick(100, 200);
    const QPoint globalClick = overlay.mapToGlobal(localClick);

    check(globalClick == QPoint(2020, 200),
          "editor: a local click maps to a point on the second screen");
    check(kRightScreen.contains(globalClick),
          "editor: the mapped point is on the screen the user clicked");

    // The old code handed the local point straight to move(), which puts the box on
    // the primary monitor -- visible as the editor appearing on the wrong screen.
    check(!kRightScreen.contains(localClick),
          "editor: the unmapped point is off the second screen (this was the defect)");

    // The annotation offset is computed by mapping the editor's global position
    // back, so that round trip has to survive.
    const QPoint offset = overlay.mapFromGlobal(globalClick) - QPoint(50, 60);
    check(offset == QPoint(50, 140),
          "editor: the global position maps back to the annotation-relative offset");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    // Isolate the registry keys ToolbarWidget reads through Settings, so the probe
    // cannot disturb the real application's stored preferences.
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("QShotProbe"));

    testGlobalOrigin();
    testHoverNormalisation();
    testToolbarPlacement();
    testTextEditorPlacement();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
