// R3-6 -- every overlay state but Idle repainted the entire screen on every mouse move.
//
// The fix has two halves, and each can be wrong in the same silent way:
//
//   1. paintEvent() honours the region Qt asks for (it used to ignore the paint event
//      and draw the whole surface, including a screen-sized QPainterPath boolean for
//      the dim mask);
//   2. each mouse handler passes a dirty rectangle to update() instead of calling the
//      bare update().
//
// Both look correct while the cursor is still, and both leave a trail of stale ink
// behind it the moment it moves. So the assertion here is deliberately *not* "the
// dirty rectangle equals what I computed by hand" -- that would only restate the
// formula in the test and pass even if the formula were wrong. It is the property the
// formula exists to guarantee:
//
//   repainting the frame with only the region the widget asked for must produce the
//   same image, for every pixel of the widget, as repainting all of it.
//
// A region one pixel short fails that; a region that is too large passes it. The other
// half of the objective -- that the regions are actually small -- is asserted
// separately, by requiring that the far corner of the screen is not repainted for a
// gesture confined to the near one, and by measuring what the two renders cost.
//
// How the widget is driven:
//   * it must be show()n -- QWidget::update() is a no-op on a widget that is not
//     visible, so nothing would ever be painted and every comparison would pass
//     vacuously;
//   * the platform is forced to "offscreen" first, because this widget is a
//     frameless full-screen window and showing it for real would flash a window over
//     whatever the person running the probe is doing;
//   * paint events are delivered by pumping the event loop, and the region is read out
//     of the paint event by a subclass. QWidget::update() is not virtual, so the
//     paint event is the only place the requested region is observable.
//
// What this probe cannot establish: the hover outline (it comes from the window
// detector, which needs a real window under the cursor) and the on-screen result of a
// real drag on real hardware. See the note at the Idle step.
//
// Exit code 0 means every assertion held.

#include "overlay/SnapOverlay.h"
#include "overlay/ToolbarWidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegion>
#include <QStringList>
#include <QThread>

#include <cstdio>
#include <functional>

using namespace qshot;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what)
{
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
}

void checkEqInt(long actual, long expected, const char* what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  %s\n      expected %ld, got %ld\n", what, expected, actual);
    } else {
        std::printf("  ok   %s (%ld)\n", what, actual);
    }
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

/// Records the region of every paint event it is given.
///
/// The regions are accumulated rather than overwritten: one update() can end up
/// delivered as more than one paint event, and keeping only the last would under-report
/// the region and turn a correct implementation into a failure.
class Recorder : public SnapOverlay {
public:
    using SnapOverlay::SnapOverlay;

    QRegion takeRegion()
    {
        const QRegion region = accumulated_;
        accumulated_ = QRegion();
        return region;
    }

    int paints = 0;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        accumulated_ += event->region();
        ++paints;
        SnapOverlay::paintEvent(event);
    }

private:
    QRegion accumulated_;
};

/// Pumps the event loop until the widget has actually repainted, then returns the
/// region it was asked for.
///
/// Deliberately does *not* sleep between passes. The overlay runs a 30ms single-shot
/// hover timer, started by a mouse move in Idle, whose update() changes the dim mask
/// across most of the screen -- so any step that waits longer than that can have the
/// state change underneath it and measure a frame that is not the one it compared
/// against. Measured the hard way: waiting 10ms per pass let that timer fire in the
/// middle of a step, and the failures it produced looked exactly like a broken dirty
/// rectangle (a difference box of 672x512 against a 264x204 region). No sleep, and the
/// timer is flushed explicitly at the one place it is unavoidable.
///
/// The wait still matters in the other direction: a widget that was never exposed never
/// receives a paint event, and an empty QRegion means "the whole widget" to render() --
/// so a step that recorded nothing would sail through every comparison below. Callers
/// assert the region is not empty for exactly that reason.
QRegion drain(Recorder* overlay)
{
    overlay->takeRegion(); // discard whatever the setup painted
    const int before = overlay->paints;
    for (int i = 0; i < 50 && overlay->paints == before; ++i) {
        QCoreApplication::processEvents();
    }
    return overlay->takeRegion();
}

/// Runs the event loop for long enough that the overlay's hover timer has certainly
/// fired, and throws the result away.
///
/// Used at the points where the timer is about to be flushed rather than measured --
/// see the comment on drain().
void settle(Recorder* overlay)
{
    QThread::msleep(90);
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
    }
    overlay->takeRegion();
}

/// A full repaint of the widget, in the same way render() paints a child: the window
/// background is left out because paintEvent() draws an opaque screen image over all of
/// it, and including it would only add a palette fill whose clipping is a separate
/// question.
QImage fullFrame(Recorder* overlay)
{
    QImage image(overlay->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    overlay->render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
    return image;
}

/// Repaints only `region` of the widget, in place, over whatever `target` already holds.
///
/// The targetOffset is not decoration. Measured on this build:
/// QWidget::render(target, targetOffset, sourceRegion) draws the *source region's*
/// content at targetOffset, not the widget's origin -- a region at (100,50) rendered with
/// a zero offset lands in the target's top-left corner, while the widget's (0,0) stays
/// where it was. Passing the region's own top-left is what puts it back where it belongs.
///
/// This is exactly the kind of thing that turns a working dirty rectangle into a
/// spectacular failure: the first version of this probe used a zero offset and reported
/// 100k+ differing pixels on every step, with a difference box the size of the dirty
/// region sitting at the origin. checkRenderSemantics() below pins the behaviour down and
/// proves the assertion can fail, so the harness cannot silently depend on a reading of
/// render() that this Qt build does not implement.
void renderRegion(Recorder* overlay, QImage& target, const QRegion& region)
{
    overlay->render(&target, region.boundingRect().topLeft(), region, QWidget::DrawChildren);
}


/// Counts differing pixels, optionally restricted to a region.
///
/// A count rather than a boolean, so that "identical" and "wrong size" cannot be
/// confused with each other.
long countDiff(const QImage& a, const QImage& b, const QRegion& region = QRegion())
{
    if (a.size() != b.size() || a.format() != b.format()) return -1;

    const QRect bounds = region.isEmpty() ? a.rect() : region.boundingRect();
    long differing = 0;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            if (!region.isEmpty() && !region.contains(QPoint(x, y))) continue;
            if (a.pixel(x, y) != b.pixel(x, y)) ++differing;
        }
    }
    return differing;
}

/// Pins down the render() semantics the whole file rests on.
void checkRenderSemantics(Recorder* overlay, const QImage& frame, const QRect& region)
{
    QImage correct = frame;
    renderRegion(overlay, correct, QRegion(region));
    checkEqInt(countDiff(correct, frame), 0,
               "repainting a region that is already correct leaves the frame unchanged");

    QImage shifted = frame;
    overlay->render(&shifted, QPoint(0, 0), QRegion(region), QWidget::DrawChildren);
    check(countDiff(shifted, frame) > 0,
          "control: a zero targetOffset moves the region's content (that is the trap)");
}

/// The bounding box of the differing pixels, or a null rectangle when there are none.
///
/// The single most useful number when a comparison fails: a box that fits inside the
/// dirty region means the region was right and the painting inside it is wrong, while a
/// box that spills outside means something changed that the region never claimed.
QRect diffBounds(const QImage& a, const QImage& b)
{
    QRect box;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) box = box.united(QRect(x, y, 1, 1));
        }
    }
    return box;
}

/// Writes the three frames and a diff mask next to the sources, for the cases where a
/// number is not enough to say what went wrong.
void dumpFrames(const char* tag, const QImage& prev, const QImage& expected,
                const QImage& incremental)
{
    const QString base = QStringLiteral("partial_%1").arg(QString::fromLatin1(tag));
    prev.save(base + QStringLiteral("_prev.png"));
    expected.save(base + QStringLiteral("_expected.png"));
    incremental.save(base + QStringLiteral("_incremental.png"));

    QImage mask(expected.size(), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::black);
    for (int y = 0; y < expected.height(); ++y) {
        for (int x = 0; x < expected.width(); ++x) {
            if (expected.pixel(x, y) != incremental.pixel(x, y)) mask.setPixel(x, y, qRgb(255, 0, 0));
        }
    }
    mask.save(base + QStringLiteral("_diffmask.png"));
    std::printf("  wrote %s_*.png\n", base.toUtf8().constData());
}

void sendMouse(Recorder* overlay, QEvent::Type type, const QPoint& pos,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPointF local(pos);
    const QPointF global(overlay->mapToGlobal(pos));
    QMouseEvent event(type, local, local, global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(overlay, &event);
}

void sendPress(Recorder* overlay, const QPoint& pos)
{
    sendMouse(overlay, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
}

void sendMove(Recorder* overlay, const QPoint& pos)
{
    sendMouse(overlay, QEvent::MouseMove, pos, Qt::NoButton, Qt::LeftButton);
}

void sendRelease(Recorder* overlay, const QPoint& pos)
{
    sendMouse(overlay, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
}

void sendRightClick(Recorder* overlay, const QPoint& pos)
{
    sendMouse(overlay, QEvent::MouseButtonPress, pos, Qt::RightButton, Qt::RightButton);
}

void sendKey(Recorder* overlay, int key)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(overlay, &event);
}

/// Puts the overlay back into Idle with nothing selected.
///
/// The right-click path is the only way to get there that also clears the hover
/// rectangle, which is what makes the steps after it measurable: a stale hover
/// rectangle is included in the dirty region by design (the outline has to be repainted
/// away), and it is the size of whatever real window the window detector found.
void clearToIdle(Recorder* overlay)
{
    sendRightClick(overlay, QPoint(500, 400));
    drain(overlay);
}

struct StepResult {
    QImage prev;      // the frame before the change
    QImage expected;  // a full repaint after it: the ground truth
    QRegion dirty;    // the region the widget asked to have repainted
};

/// Drives one interaction step and checks the incremental repaint against a full one.
///
/// `farCorner` is a point that this gesture cannot possibly affect. It is asserted to
/// be outside the dirty region, which is what turns "the repaint is correct" into "the
/// repaint is correct *and* local" -- a bare update() would satisfy the first and fail
/// the second.
StepResult runStep(Recorder* overlay, const char* name, const std::function<void()>& mutate,
                   const QPoint& farCorner)
{
    const QImage prev = fullFrame(overlay);

    mutate();

    const QRegion dirty = drain(overlay);
    const QImage expected = fullFrame(overlay);

    const QRect bounds = dirty.boundingRect();
    const double fraction = 100.0 * double(bounds.width()) * double(bounds.height())
                          / double(overlay->width()) / double(overlay->height());
    std::printf("\n[%s]\n  dirty %d,%d %dx%d  (%d rect(s), %.2f%% of the widget)\n",
                name, bounds.x(), bounds.y(), bounds.width(), bounds.height(),
                dirty.rectCount(), fraction);

    // Guard against the vacuous case: no paint event at all leaves an empty region,
    // which render() would read as "repaint everything".
    check(!dirty.isEmpty(), "a paint event was delivered with a non-empty region");
    check(overlay->size() == QSize(900, 700), "the widget is the size the probe set up");
    check(!dirty.contains(farCorner), "the far corner of the screen is not repainted");

    QImage incremental = prev;
    renderRegion(overlay, incremental, dirty);
    const long differing = countDiff(incremental, expected);
    if (differing != 0) {
        const QRect box = diffBounds(incremental, expected);
        std::printf("      differences span %d,%d %dx%d  (dirty region was %d,%d %dx%d)\n",
                    box.x(), box.y(), box.width(), box.height(),
                    bounds.x(), bounds.y(), bounds.width(), bounds.height());
        static int stepIndex = 0;
        dumpFrames(QString::number(stepIndex++).toUtf8().constData(), prev, expected, incremental);
    }
    checkEqInt(differing, 0, "partial repaint of the requested region matches a full repaint");

    return { prev, expected, dirty };
}

/// The same comparison for a region the probe chooses.
///
/// This isolates the other half of the fix: whatever region is asked for, painting only
/// that region must agree with a full repaint *inside it*. It is the property that lets
/// the handlers pick their own regions at all.
void checkArbitraryRegions(Recorder* overlay, const StepResult& step)
{
    const QRect selection(100, 100, 201, 121);
    const QRegion candidates[3] = {
        QRegion(QRect(0, 0, 30, 30)),                                   // far from the change
        QRegion(QRect(selection.left() - 20, selection.top() - 20, 60, 60)), // straddling a corner
        QRegion(QRect(selection.right() - 5, selection.bottom() - 5, 40, 40)) // straddling the far corner
    };
    const char* names[3] = {
        "an unrelated corner",
        "a band straddling the selection's top-left corner",
        "a band straddling the selection's bottom-right corner"
    };

    for (int i = 0; i < 3; ++i) {
        QImage incremental = step.prev;
        renderRegion(overlay, incremental, candidates[i]);
        char what[160];
        std::snprintf(what, sizeof(what),
                      "painting only %s agrees with a full repaint inside it", names[i]);
        checkEqInt(countDiff(incremental, step.expected, candidates[i]), 0, what);
    }
}

} // namespace

int main(int argc, char** argv)
{
    // Before QApplication: the overlay is a frameless full-screen window, and it has to
    // be shown for update() to do anything at all.
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    // Isolate the registry keys ToolbarWidget reads through Settings, so the probe
    // cannot disturb the real application's stored preferences.
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("QShotProbe"));

    std::printf("platform: %s\n", QGuiApplication::platformName().toUtf8().constData());
    check(QGuiApplication::platformName() == QStringLiteral("offscreen"),
          "running on the offscreen platform (no window is put on screen)");

    // 900x700 rather than this machine's 1707x1067: the assertions are about regions,
    // not about scale, and every full render is compared pixel by pixel.
    const QRect screen(0, 0, 900, 700);

    QPixmap background(screen.size());
    background.fill(QColor("#204060"));
    {
        // A pattern, so that a region left unpainted shows up as a difference in
        // content rather than as two identical flat colours.
        QPainter p(&background);
        for (int y = 0; y < screen.height(); y += 40) {
            for (int x = 0; x < screen.width(); x += 40) {
                p.fillRect(x, y, 20, 20, QColor("#E0E0E0"));
            }
        }
    }

    Recorder* overlay = new Recorder(background, screen);
    overlay->show();
    drain(overlay);

    // -----------------------------------------------------------------------
    // Idle: the magnifier follows the cursor
    // -----------------------------------------------------------------------
    // Deliberately the first event the overlay ever sees. oldMousePos is still (-1,-1)
    // at that point, and that is the one case the region helper has to get right
    // without a panel ever having been drawn.
    runStep(overlay, "Idle move (magnifier follows the cursor; the session's first move)",
            [&] { sendMove(overlay, QPoint(400, 300)); },
            QPoint(880, 680));

    // That move started the overlay's hover timer, whose update() repaints the hovered
    // window's outline. It has to be flushed here, before any later step takes a
    // "before" frame, or it lands in the middle of one and changes the dim mask under
    // the measurement.
    //
    // This is also where the probe's coverage stops. The hover rectangle comes from the
    // window detector, which answers about a real window under a real cursor -- so it is
    // whatever happens to be on the desktop when this runs. The branch that repaints a
    // *changed* hover outline is therefore not exercised here. The dragging step below
    // does cover the union that exists for the case that matters, the outline being
    // replaced by a selection mid-drag, because activeChromeRect() is included on both
    // sides of that change.
    settle(overlay);

    // Warm-up drag, unmeasured on purpose. Its only job is to leave the overlay in
    // Selected with an empty hover rectangle, which is what makes every later step's
    // region depend on the application's own logic and nothing else. The drag release is
    // the only thing besides a right-click that clears the hover rectangle.
    sendPress(overlay, QPoint(100, 100));
    drain(overlay);
    sendMove(overlay, QPoint(300, 220));
    drain(overlay);
    sendRelease(overlay, QPoint(300, 220));
    drain(overlay);
    clearToIdle(overlay);

    // -----------------------------------------------------------------------
    // Dragging: the selection being drawn out
    // -----------------------------------------------------------------------
    sendPress(overlay, QPoint(100, 100));
    drain(overlay); // the press itself is a full update by design
    const StepResult dragging =
        runStep(overlay, "Dragging move (selection grows)",
                [&] { sendMove(overlay, QPoint(300, 220)); },
                QPoint(880, 680));

    checkRenderSemantics(overlay, dragging.expected, QRect(96, 79, 476, 353));
    checkArbitraryRegions(overlay, dragging);

    // Negative control. The comparison above is only evidence if it is capable of
    // failing: repaint a region that is deliberately far too small and require the
    // difference to be reported. Same state, so the only variable is the region.
    {
        QImage tooSmall = dragging.prev;
        renderRegion(overlay, tooSmall, QRegion(QRect(0, 0, 4, 4)));
        check(countDiff(tooSmall, dragging.expected) > 0,
              "control: a deliberately too-small region does report a difference");
    }

    // What the partial repaint actually saves. Printed as well as asserted, because the
    // number is the point of the exercise.
    {
        constexpr int kRounds = 30;
        QElapsedTimer timer;

        timer.start();
        for (int i = 0; i < kRounds; ++i) {
            QImage frame = fullFrame(overlay);
        }
        const qint64 fullNs = timer.nsecsElapsed() / kRounds;

        QImage frame = fullFrame(overlay);
        timer.start();
        for (int i = 0; i < kRounds; ++i) {
            renderRegion(overlay, frame, dragging.dirty);
        }
        const qint64 partialNs = timer.nsecsElapsed() / kRounds;

        std::printf("\n  full repaint   %6.2f ms\n  dirty repaint  %6.2f ms   (%.1fx less)\n",
                    fullNs / 1e6, partialNs / 1e6,
                    double(fullNs) / double(qMax<qint64>(1, partialNs)));
        check(partialNs < fullNs,
              "the dirty repaint costs less than a full one");
    }

    sendRelease(overlay, QPoint(300, 220));
    drain(overlay); // establishes the selection; also a full update

    // -----------------------------------------------------------------------
    // Selected: the "click to type" badge
    // -----------------------------------------------------------------------
    ToolbarWidget* toolbar = overlay->findChild<ToolbarWidget*>();
    check(toolbar != nullptr, "the overlay exposes its toolbar");
    if (!toolbar) {
        std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
        return 1;
    }

    toolbar->handleToolClick(AnnotationType::Text);
    drain(overlay); // arming a tool repaints in full
    runStep(overlay, "Selected move (click-to-type badge follows the cursor)",
            [&] { sendMove(overlay, QPoint(200, 160)); },
            QPoint(880, 680));

    // -----------------------------------------------------------------------
    // Annotating: a rectangle being dragged out
    // -----------------------------------------------------------------------
    toolbar->handleToolClick(AnnotationType::Rectangle);
    drain(overlay);
    sendPress(overlay, QPoint(150, 150));
    drain(overlay);
    runStep(overlay, "Annotating move (rectangle follows the cursor)",
            [&] { sendMove(overlay, QPoint(260, 200)); },
            QPoint(880, 680));
    sendRelease(overlay, QPoint(260, 200));
    drain(overlay);

    // -----------------------------------------------------------------------
    // Moving: the whole selection dragged
    // -----------------------------------------------------------------------
    // Escape clears the annotations, which is what unlocks the selection: a layer with
    // content in it refuses to enter Moving, and so does an armed tool.
    sendKey(overlay, Qt::Key_Escape);
    drain(overlay);
    toolbar->handleToolClick(AnnotationType::Rectangle); // toggle the tool off
    drain(overlay);

    sendPress(overlay, QPoint(200, 160));
    drain(overlay);
    runStep(overlay, "Moving move (selection translated)",
            [&] { sendMove(overlay, QPoint(240, 190)); },
            QPoint(880, 680));
    sendRelease(overlay, QPoint(240, 190));
    drain(overlay);

    // -----------------------------------------------------------------------
    // Resizing: a grip dragged
    // -----------------------------------------------------------------------
    // The selection is now at (140,130,201,121), so this is its bottom-right grip.
    const QPoint grip(340, 250);
    sendPress(overlay, grip);
    drain(overlay);
    runStep(overlay, "Resizing move (bottom-right grip dragged)",
            [&] { sendMove(overlay, QPoint(420, 320)); },
            QPoint(20, 20));
    sendRelease(overlay, QPoint(420, 320));
    drain(overlay);

    // -----------------------------------------------------------------------
    // Annotating: a pen stroke, five segments long
    // -----------------------------------------------------------------------
    // The three stroke tools append one segment per move and repaint the rest of the path
    // identically, so their dirty region is the newest segment rather than the whole path.
    // This step is what that claim rests on, and it is deliberately a long diagonal: with
    // the whole path as the region it would cover 57% of the widget, with the segment it
    // covers a couple of percent. The area assertion below is what tells the two apart --
    // the pixel comparison cannot, because a region that is too *large* still passes it.
    toolbar->handleToolClick(AnnotationType::Pen);
    drain(overlay);
    sendPress(overlay, QPoint(150, 150));
    drain(overlay);
    // Ten segments rather than three or four: the region is the union of the newest segment
    // with the one before it, so shorter segments are what make the region small -- and the
    // point of the assertion is that it stays proportional to the *segment*.
    StepResult penStep;
    for (int i = 1; i <= 10; ++i) {
        const QPoint at(150 + (710 * i) / 10, 150 + (510 * i) / 10);
        char name[96];
        std::snprintf(name, sizeof(name), "Annotating move (pen stroke, segment %d of 10)", i);
        penStep = runStep(overlay, name, [&] { sendMove(overlay, at); }, QPoint(20, 660));
    }
    sendRelease(overlay, QPoint(860, 660));
    drain(overlay);

    const auto areaPercent = [overlay](const QRegion& region) {
        const QRect box = region.boundingRect();
        return 100.0 * double(box.width()) * double(box.height())
             / double(overlay->width()) / double(overlay->height());
    };
    check(areaPercent(penStep.dirty) < 10.0,
          "a pen stroke's dirty region is the newest segment, not the whole path");

    // -----------------------------------------------------------------------
    // Annotating: a mosaic stroke, on a selection big enough to hold one
    // -----------------------------------------------------------------------
    // Same incremental rule, but the ink lands in a separate layer whose blocks are
    // snapped to a grid aligned to the image origin -- so a block can begin up to one
    // whole block *before* the segment that triggered it, and the region has to allow for
    // that. The selection is re-drawn large here so that a stroke which stays inside it
    // (and therefore actually produces blocks) is still long enough for the area
    // assertion to mean something.
    clearToIdle(overlay);
    sendPress(overlay, QPoint(60, 60));
    drain(overlay);
    sendMove(overlay, QPoint(840, 640));
    drain(overlay);
    sendRelease(overlay, QPoint(840, 640));
    drain(overlay);

    toolbar->handleToolClick(AnnotationType::Mosaic);
    drain(overlay);
    sendPress(overlay, QPoint(100, 100));
    drain(overlay);
    StepResult mosaicStep;
    for (int i = 1; i <= 10; ++i) {
        const QPoint at(100 + (700 * i) / 10, 100 + (500 * i) / 10);
        char name[96];
        std::snprintf(name, sizeof(name), "Annotating move (mosaic stroke, segment %d of 10)", i);
        mosaicStep = runStep(overlay, name, [&] { sendMove(overlay, at); }, QPoint(20, 660));
    }
    check(areaPercent(mosaicStep.dirty) < 10.0,
          "a mosaic stroke's dirty region is the newest segment, not the whole path");
    sendRelease(overlay, QPoint(800, 600));
    drain(overlay);

    // -----------------------------------------------------------------------
    // Idle again, with the hover timer left pending on purpose
    // -----------------------------------------------------------------------
    // Last, because this move starts the hover timer again and nothing that follows
    // measures anything -- so there is nothing left for it to disturb.
    clearToIdle(overlay);
    runStep(overlay, "Idle move (magnifier, after the selection was cleared)",
            [&] { sendMove(overlay, QPoint(420, 330)); },
            QPoint(880, 680));

    settle(overlay);

    // A clean run must not leave diff dumps behind. dumpFrames() writes them into the
    // source directory, where they outlive the run -- and a diff mask sitting next to
    // the sources reads as "there is still a difference", which is the opposite of what
    // a green run says. The dumps are evidence for a failure, so a pass discards them.
    if (g_failures == 0) {
        QDir dir(QStringLiteral("."));
        const QStringList stale = dir.entryList({QStringLiteral("partial_*.png")}, QDir::Files);
        for (const QString& name : stale) {
            dir.remove(name);
        }
        if (!stale.isEmpty()) {
            std::printf("(removed %d stale diff dump(s) from a previous failing run)\n",
                        int(stale.size()));
        }
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    std::fflush(stdout);

    // The overlay is intentionally leaked: ~SnapOverlay runs teardown that is not under
    // test here, and the process is about to exit.
    return g_failures == 0 ? 0 : 1;
}
