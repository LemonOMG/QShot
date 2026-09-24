// M2 (focus routing) -- mechanism-level verification.
//
// Symptom: clicking the toolbar / colour-size panel makes Esc, Enter and Ctrl+Z stop
// working. Those panels are top-level windows, and activating one clears
// QGuiApplication::focusWindow(), leaving the overlay keyboard-dead.
//
// Focus behaviour itself is NOT verifiable from a terminal-launched process (Windows
// refuses to hand foreground to it, so focusWindow() is already null before the test
// starts -- see docs/ROADMAP.md section 7). What IS verifiable is the *mechanism* the
// fix relies on: Qt::WindowDoesNotAcceptFocus must reach the native window as
// WS_EX_NOACTIVATE. With that style set Windows will not activate the window on click
// or on ShowWindow, so the panel is structurally incapable of stealing activation --
// whatever Qt's own focus bookkeeping does.
//
// The invariant checked here is deliberately stated over *all* windows rather than a
// hand-picked list, so a newly added panel is covered automatically:
//
//   every top-level window is display-only (WS_EX_NOACTIVATE set)
//   EXCEPT the classes the user actually types into: SnapOverlay, TextInputWidget.
//
// Classification is by metaObject()->className(), which only works for classes that
// carry Q_OBJECT. ToolbarWidget's private nested SubPanelWidget does not, so it
// reports "QWidget" and lands in the display-only bucket -- which is the correct
// verdict for it, but it is a coincidence worth knowing about.
//
// Run: probe_noactivate.exe        (needs a real platform plugin, not offscreen)

#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QString>
#include <QWidget>
#include <QThread>
#include <cstdio>

#include "overlay/SnapOverlay.h"
#include "overlay/ToolbarWidget.h"
#include "overlay/TextInputWidget.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    fflush(stdout);
}

static void settle(int ms = 200) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

// Extended style actually present on the native window: the ground truth. Qt's
// windowFlags() is only a request and the platform plugin may ignore it. winId() is
// enough to force HWND creation, so nothing has to be shown (no fullscreen flash).
static unsigned long nativeExStyle(QWidget* w) {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    return static_cast<unsigned long>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
#else
    (void)w;
    return 0;
#endif
}

// Classes the user has to be able to give keyboard input to.
static bool acceptsKeyboard(const QString& className) {
    return className.endsWith(QStringLiteral("SnapOverlay"))
        || className.endsWith(QStringLiteral("TextInputWidget"));
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Build the real window stack. SnapOverlay brings its own ToolbarWidget and
    // TextInputWidget with it, so the sweep below covers those too; the extra
    // standalone pair covers the paths where they are constructed on their own.
    QPixmap background(400, 300);
    background.fill(Qt::darkGray);
    qshot::SnapOverlay* overlay =
        new qshot::SnapOverlay(background, QGuiApplication::primaryScreen()->geometry());
    overlay->winId();

    qshot::ToolbarWidget* toolbar = new qshot::ToolbarWidget(nullptr);
    toolbar->show();

    qshot::TextInputWidget* editor = new qshot::TextInputWidget(nullptr);
    editor->startInput(QPoint(120, 160), QColor("#FB8C00"), 18);

    settle();

    printf("\n%-28s %-12s %-10s %s\n", "class", "GWL_EXSTYLE", "NOACTIVATE", "verdict");
    printf("%-28s %-12s %-10s %s\n", "-----", "-----------", "----------", "-------");

    int interactiveSeen = 0;
    int displayOnlySeen = 0;

    for (QWidget* w : QApplication::allWidgets()) {
        if (!w->isWindow()) continue;

        const QString cls = QString::fromLatin1(w->metaObject()->className());
        const unsigned long style = nativeExStyle(w);
        const bool noActivate = (style & WS_EX_NOACTIVATE) != 0;
        const bool interactive = acceptsKeyboard(cls);

        if (interactive) ++interactiveSeen; else ++displayOnlySeen;

        // The policy: display-only windows must not be activatable; the ones the user
        // types into must be.
        const bool ok = interactive ? !noActivate : noActivate;
        if (!ok) ++failures;
        ++checks;

        printf("%-28s 0x%08lX   %-10s %s\n",
               cls.toUtf8().constData(), style, noActivate ? "yes" : "no",
               ok ? "ok" : "FAIL");
    }
    fflush(stdout);

    // Guard against a vacuous sweep: if the windows were never created, or the
    // classification stopped matching, the loop above would pass trivially.
    printf("\n");
    check(interactiveSeen >= 2, "found >= 2 windows that must accept keyboard input");
    check(displayOnlySeen >= 2, "found >= 2 display-only windows (sweep is not vacuous)");

    printf("\n%d checks, %d failures\n", checks, failures);
    fflush(stdout);

    // One-shot inspection: no event loop. The windows are intentionally leaked;
    // ~SnapOverlay would run teardown we do not care about and the process is exiting.
    return failures == 0 ? 0 : 1;
}
