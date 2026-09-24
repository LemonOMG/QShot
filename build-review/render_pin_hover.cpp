// Hover chrome verification for PinWindow: the inner shadow and the close button.
//
// The complaint this answers: pin a maximised window at 1:1 and the pin is
// pixel-identical to the desktop behind it, so nothing on screen says "this is an
// image" -- let alone how to get rid of it. The fix is chrome that appears on hover.
// Which raises the question this probe exists to answer: does the chrome stay out of
// the way of the picture?
//
// Three properties, in order of how badly they would hurt if wrong:
//
//  1. The chrome must not touch the image. A shadow creeping into the middle of a
//     screenshot is worse than no shadow at all, and an off-by-one in the band loop is
//     invisible to the eye. Asserted exhaustively rather than by spot check: every
//     content pixel outside the close button must be byte-identical between the idle
//     and the hovered render.
//  2. The shadow has to be a falloff, not a second fat border. Measured by inverting
//     the blend -- the source is a known solid colour, so the shadow's alpha can be
//     recovered from the rendered pixel band by band.
//  3. The close button must appear only on hover, must brighten under the cursor, and
//     must actually close the window when pressed, while a press a few pixels away
//     still starts a drag.
//
// The widget is never shown for the rendering sections; QWidget::render() paints
// synchronously, so no window flashes. The interaction section does show pins, because
// close() is defined in terms of visibility.
//
// Run: render_pin_hover.exe   (needs a real platform plugin, not offscreen -- see
//                              docs/ROADMAP.md section 7: offscreen renders no glyphs)

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>   // also brings in QEnterEvent, both live in QtGui/qevent.h
#include <QPainter>
#include <QPointer>
#include <QThread>
#include <QtMath>
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

static void checkEqInt(int got, int want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL", what, got, want);
}

static void checkNear(double got, double want, double tol, const char* what) {
    ++checks;
    const bool ok = (qAbs(got - want) <= tol);
    if (!ok) ++failures;
    printf("  %-4s %s (got %.2f, want %.2f +/- %.2f)\n", ok ? "ok" : "FAIL", what, got, want, tol);
}

static void settle(int ms = 100) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

// A flat mid-light grey: the worst case for the shadow (a dark screenshot would hide
// it), and a known constant, which is what lets the alpha be recovered exactly.
static constexpr int kSolid = 240;

static QImage makeSolid(int w, int h, qreal dpr) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(QColor(kSolid, kSolid, kSolid));
    img.setDevicePixelRatio(dpr);
    return img;
}

// Every pixel encodes its own coordinate, so a misplacement anywhere is identifiable
// from a single pixel rather than from "the picture looks wrong".
static QImage makeCoordinateMap(int w, int h, qreal dpr) {
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.setPixelColor(x, y, QColor((x * 3) & 0xFF, (y * 7) & 0xFF, (x ^ y) & 0xFF));
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

// Drives the real event path rather than poking private state: QWidget::event() routes
// these to enterEvent / leaveEvent / mouseMoveEvent exactly as the platform would.
static void sendEnter(QWidget* w, const QPoint& local = QPoint(20, 20)) {
    QEnterEvent ev(QPointF(local), QPointF(local), QPointF(w->mapToGlobal(local)));
    QCoreApplication::sendEvent(w, &ev);
}

static void sendLeave(QWidget* w) {
    QEvent ev(QEvent::Leave);
    QCoreApplication::sendEvent(w, &ev);
}

static void sendMouse(QWidget* w, QEvent::Type type, const QPoint& local,
                      Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPoint global = w->mapToGlobal(local);
    QMouseEvent ev(type, QPointF(local), QPointF(global), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

// The shadow is a black overlay, so rendered = source * (1 - alpha/255); with a known
// source the alpha comes back out of any one channel.
static double alphaAt(const QImage& rendered, const QPoint& pt) {
    const int got = qGreen(rendered.pixel(pt));
    return 255.0 * (1.0 - double(got) / kSolid);
}

// ---------------------------------------------------------------------------- 1 ---
static void checkChromeGeometry() {
    printf("\n[1] chrome geometry\n");
    {
        PinWindow pin(makeCoordinateMap(240, 160, 1.0));
        const int b = pin::kBorderWidth;
        const QRect content = pin.contentRect();
        const QRect button = pin.closeButtonRect();

        checkEqInt(content.left(), b, "content starts inside the frame");
        checkEqInt(content.width(), 240, "content width is the image's logical width");
        checkEqInt(content.height(), 160, "content height is the image's logical height");

        check(!button.isEmpty(), "a 240x160 pin has a close button");
        checkEqInt(button.width(), pin::kCloseButtonSize, "button is square (width)");
        checkEqInt(button.height(), pin::kCloseButtonSize, "button is square (height)");
        check(content.contains(button), "button sits wholly inside the image");
        checkEqInt(button.right(), content.right() - pin::kCloseButtonMargin,
                   "button is inset from the right edge by the margin");
        checkEqInt(button.top(), content.top() + pin::kCloseButtonMargin,
                   "button is inset from the top edge by the margin");
    }

    // Too small to hold the button: the rect has to be empty, because an empty rect is
    // what makes the click fall through to dragging instead of hitting a clipped disc.
    {
        PinWindow pin(makeCoordinateMap(20, 20, 1.0));
        check(pin.closeButtonRect().isEmpty(), "a 20x20 pin has no close button");
        check(!pin.contentRect().isEmpty(), "but it still has content");
    }

    // At 1:1 a full-screen capture is the case the user complained about, so make sure
    // the button lands somewhere sensible there too.
    {
        PinWindow pin(makeCoordinateMap(1707, 1067, 1.0));
        const QRect button = pin.closeButtonRect();
        printf("       full-screen pin: widget %dx%d, button at (%d,%d) %dx%d\n",
               pin.width(), pin.height(), button.x(), button.y(),
               button.width(), button.height());
        check(!button.isEmpty(), "a full-screen pin has a close button");
    }
}

// ---------------------------------------------------------------------------- 2 ---
// Compares two renders of the same widget over the region the shadow must not reach,
// and returns how many pixels differ.
//
// Everything is done in *device* pixels. QImage::pixel() indexes device pixels while
// the widget's geometry is logical, so mixing the two silently samples the wrong
// region at any ratio other than 1. That is not hypothetical: the first version of this
// check compared logical coordinates against a DPR 1.5 image, sampled the shadow band
// itself, and reported 2059 "differing" pixels that were the shadow doing its job.
static int countInteriorDiffs(const QImage& a, const QImage& b, const QRect& logicalContent,
                              const QRect& logicalButton, qreal dpr, int* compared) {
    // The band is kShadowDepth logical pixels deep, so it reaches qCeil(depth * dpr) in
    // from the content edge in device pixels.
    const int m = qCeil(pin::kShadowDepth * dpr);
    const QRect interior(qRound(logicalContent.left() * dpr) + m,
                         qRound(logicalContent.top() * dpr) + m,
                         qRound(logicalContent.width() * dpr) - 2 * m,
                         qRound(logicalContent.height() * dpr) - 2 * m);

    // The exclusion zone is padded by two device pixels. The disc is antialiased, so its
    // coverage can spill a fraction of a pixel past its logical rect at a fractional
    // ratio, and a false positive here would read as "the chrome moved the image".
    const QRect button(qRound(logicalButton.left() * dpr) - 2,
                       qRound(logicalButton.top() * dpr) - 2,
                       qRound(logicalButton.width() * dpr) + 4,
                       qRound(logicalButton.height() * dpr) + 4);

    int differing = 0;
    int n = 0;
    for (int y = interior.top(); y <= interior.bottom(); ++y) {
        for (int x = interior.left(); x <= interior.right(); ++x) {
            if (button.contains(x, y)) continue;
            ++n;
            if (a.pixel(x, y) != b.pixel(x, y)) ++differing;
        }
    }
    if (compared) *compared = n;
    return differing;
}

static void checkChromeStaysOffTheImage() {
    printf("\n[2] the chrome touches nothing but the outer band\n");

    const QImage source = makeCoordinateMap(240, 160, 1.0);
    {
        PinWindow pin(source);
        sendLeave(&pin);
        const QImage idle = renderAt(&pin, 1.0);
        sendEnter(&pin);
        const QImage hovered = renderAt(&pin, 1.0);

        // The frame is the one thing that is *supposed* to change everywhere. Its alpha
        // composites against the widget's own background rather than against the desktop
        // -- the pin is an ordinary opaque window, not a translucent one -- so the
        // "dark alpha" frame reads as a mid grey instead of letting the screenshot
        // behind the window show through.
        check(idle.pixel(0, 0) != hovered.pixel(0, 0), "the frame changes colour on hover");
        checkEqInt(qAlpha(idle.pixel(0, 0)), 255,
                   "the idle pin is opaque, so the frame blends with the palette");
        printf("       idle frame #%08X, hovered frame #%08X\n",
               idle.pixel(0, 0), hovered.pixel(0, 0));
        check(hovered.pixel(0, 0) == QColor(0x4C, 0xAF, 0x50).rgb(), "hovered frame is the accent");

        // Exhaustive, not a spot check: this is the assertion that would catch a band
        // loop running one iteration too far, which is otherwise invisible to the eye.
        int compared = 0;
        const int differing = countInteriorDiffs(idle, hovered, pin.contentRect(),
                                                pin.closeButtonRect(), 1.0, &compared);
        printf("       compared %d interior pixels, %d differ\n", compared, differing);
        checkEqInt(differing, 0, "no interior pixel changes between idle and hovered");
        check(compared > 20000, "the exhaustive sweep actually covered the interior");

        // And the interior really is the untouched source, not a pair of coincidentally
        // equal renders.
        const QPoint interior = pin.contentRect().center();
        check(hovered.pixel(interior)
                  == source.pixel(interior - QPoint(pin::kBorderWidth, pin::kBorderWidth)),
              "an interior pixel is still exactly the source pixel");

        idle.save(QStringLiteral("pin_chrome_idle.png"));
        hovered.save(QStringLiteral("pin_chrome_hover.png"));
    }

    // The same claim at this machine's real ratio, where one logical pixel is 1.5 device
    // pixels and the bands get blended rather than landing on whole pixels.
    {
        PinWindow pin(makeCoordinateMap(300, 150, 1.5));   // 200x100 logical
        sendLeave(&pin);
        const QImage idle = renderAt(&pin, 1.5);
        sendEnter(&pin);
        const QImage hovered = renderAt(&pin, 1.5);

        int compared = 0;
        const int differing = countInteriorDiffs(idle, hovered, pin.contentRect(),
                                                pin.closeButtonRect(), 1.5, &compared);
        printf("       DPR 1.5: compared %d device pixels, %d differ\n", compared, differing);
        checkEqInt(differing, 0, "same at DPR 1.5");
        check(compared > 20000, "the DPR 1.5 sweep covered the interior too");
        hovered.save(QStringLiteral("pin_chrome_hover_dpr15.png"));
    }
}

// ---------------------------------------------------------------------------- 3 ---
static void checkShadowFalloff() {
    printf("\n[3] the shadow is a falloff, and it only ever darkens\n");
    PinWindow pin(makeSolid(240, 160, 1.0));
    sendEnter(&pin);
    const QImage hovered = renderAt(&pin, 1.0);

    const QRect content = pin.contentRect();
    const int depth = pin::kShadowDepth;

    // Walk in from the left edge along a row that is well clear of the button, reading
    // the shadow's alpha back out of the rendered pixel.
    const int row = content.top() + 80;
    double previous = 1e9;
    int nonIncreasing = 0;
    for (int i = 0; i < depth; ++i) {
        const double a = alphaAt(hovered, QPoint(content.left() + i, row));
        if (a <= previous + 0.6) ++nonIncreasing;
        if (i < 4 || i >= depth - 3) printf("       band %2d: alpha %.1f\n", i, a);
        previous = a;
    }
    checkEqInt(nonIncreasing, depth, "alpha never increases going inward");

    checkNear(alphaAt(hovered, QPoint(content.left(), row)), 150.0, 4.0,
              "shadow starts at its full strength at the edge");
    check(alphaAt(hovered, QPoint(content.left() + depth - 1, row)) < 12.0,
          "and has faded out by the last band");
    checkNear(alphaAt(hovered, QPoint(content.left() + depth, row)), 0.0, 0.1,
              "the pixel just inside the band is untouched");

    // Bands must not overlap, or the four corners would be twice as dark as the four
    // edges -- the classic tell of a hand-rolled inner shadow.
    const QPoint leftEdge(content.left(), row);
    const QPoint topEdge(content.left() + 80, content.top());
    const QPoint corner(content.left(), content.top());
    printf("       left edge %.1f, top edge %.1f, corner %.1f\n",
           alphaAt(hovered, leftEdge), alphaAt(hovered, topEdge), alphaAt(hovered, corner));
    check(hovered.pixel(leftEdge) == hovered.pixel(topEdge),
          "left edge and top edge are equally dark");
    check(hovered.pixel(corner) == hovered.pixel(leftEdge),
          "the corner is not double-composited");

    // Structural invariant: outside the button, every hover pixel is darker than or
    // equal to the source. Anything brighter would be a stray light artifact -- and it
    // would also catch a shadow accidentally drawn with a light colour.
    const QRect button = pin.closeButtonRect();
    int brighter = 0;
    for (int y = content.top(); y <= content.bottom(); ++y) {
        for (int x = content.left(); x <= content.right(); ++x) {
            if (button.contains(x, y)) continue;
            if (qGreen(hovered.pixel(x, y)) > kSolid) ++brighter;
        }
    }
    checkEqInt(brighter, 0, "no pixel outside the button is lighter than the source");
}

// ---------------------------------------------------------------------------- 4 ---
static void checkCloseButton() {
    printf("\n[4] the close button appears on hover and reacts to the cursor\n");
    PinWindow pin(makeSolid(240, 160, 1.0));
    const QRect button = pin.closeButtonRect();

    // Sampled near the bottom of the disc: inside the circle, clear of the X strokes,
    // and far enough from the top edge that the shadow band does not reach it -- so
    // anything other than the source colour there is the button.
    const QPoint disc(button.center().x(), button.bottom() - 3);
    printf("       button (%d,%d) %dx%d, disc sample at (%d,%d)\n",
           button.x(), button.y(), button.width(), button.height(), disc.x(), disc.y());

    sendLeave(&pin);
    const QImage idle = renderAt(&pin, 1.0);
    checkEqInt(qGreen(idle.pixel(disc)), kSolid,
               "with no hover there is no button at all, just the image");

    sendEnter(&pin, QPoint(20, 20));   // hovered, but the cursor is nowhere near the button
    const QImage hovered = renderAt(&pin, 1.0);
    const double restAlpha = alphaAt(hovered, disc);
    check(restAlpha > 100.0, "the disc is drawn while the pin is merely hovered");

    // Now put the cursor on the button through the real move-event path.
    sendMouse(&pin, QEvent::MouseMove, button.center(), Qt::NoButton, Qt::NoButton);
    const QImage lit = renderAt(&pin, 1.0);
    const double litAlpha = alphaAt(lit, disc);
    printf("       disc alpha: idle=n/a rest=%.1f under cursor=%.1f\n", restAlpha, litAlpha);
    check(litAlpha > restAlpha + 20.0, "the disc darkens when the cursor is on it");
    check(pin.cursor().shape() == Qt::PointingHandCursor,
          "and the cursor becomes a pointing hand");

    // Moving off the button, still inside the pin, has to put both back.
    sendMouse(&pin, QEvent::MouseMove, QPoint(40, 40), Qt::NoButton, Qt::NoButton);
    check(pin.cursor().shape() == Qt::OpenHandCursor, "moving off restores the open hand");
    const QImage back = renderAt(&pin, 1.0);
    check(back.pixel(disc) == hovered.pixel(disc), "and the disc returns to its rest state");

    hovered.save(QStringLiteral("pin_chrome_button_rest.png"));
    lit.save(QStringLiteral("pin_chrome_button_lit.png"));
}

// ---------------------------------------------------------------------------- 5 ---
static void checkInteraction() {
    printf("\n[5] pressing the button closes the pin; pressing elsewhere drags it\n");
    {
        QPointer<PinWindow> pin = new PinWindow(makeSolid(240, 160, 1.0));
        pin->showAt(QPoint(220, 220));
        settle();
        sendEnter(pin);

        sendMouse(pin, QEvent::MouseButtonPress, pin->closeButtonRect().center(),
                  Qt::LeftButton, Qt::LeftButton);
        check(!pin->isVisible(), "a left press on the close button closes the pin");

        // WA_DeleteOnClose is what stops every pin leaking its image; if it were ever
        // dropped, the window would just hide and this would fail.
        settle();
        check(pin.isNull(), "and the window was reclaimed, not merely hidden");
    }
    {
        QPointer<PinWindow> pin = new PinWindow(makeSolid(240, 160, 1.0));
        pin->showAt(QPoint(220, 220));
        settle();
        sendEnter(pin);

        const QPoint start = pin->pos();
        const QPoint middle(pin->width() / 2, pin->height() / 2);
        sendMouse(pin, QEvent::MouseButtonPress, middle, Qt::LeftButton, Qt::LeftButton);
        check(pin->isVisible(), "a press in the middle does not close the pin");

        const QPoint target = middle + QPoint(37, 23);
        sendMouse(pin, QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
        sendMouse(pin, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
        check(pin->pos() == start + QPoint(37, 23), "and it drags the pin by exactly the delta");
        printf("       dragged %d,%d -> %d,%d\n", start.x(), start.y(),
               pin->pos().x(), pin->pos().y());

        pin->close();
    }
    {
        // A pin too small for a button: the top-right corner must drag, not close.
        QPointer<PinWindow> pin = new PinWindow(makeSolid(20, 20, 1.0));
        pin->showAt(QPoint(400, 400));
        settle();
        sendEnter(pin);

        check(pin->closeButtonRect().isEmpty(), "no button on a 20x20 pin");
        sendMouse(pin, QEvent::MouseButtonPress, QPoint(5, 5), Qt::LeftButton, Qt::LeftButton);
        check(pin->isVisible(), "a press in that corner drags instead of closing");
        sendMouse(pin, QEvent::MouseButtonRelease, QPoint(5, 5), Qt::LeftButton, Qt::NoButton);
        pin->close();
    }
}

// ---------------------------------------------------------------------------- 6 ---
// The user's actual complaint, rendered: a light "maximised window" pinned at 1:1 on a
// light desktop. Numbers cannot answer "can you tell it is an image?" -- this section
// exists to be looked at. Two files land next to the probe: ..._idle.png and
// ..._hover.png.
static QImage makeFakeMaximisedWindow(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(QColor(0xFA, 0xFA, 0xFA));
    QPainter p(&img);

    // A title bar, a couple of paragraphs and an accent button -- enough structure that
    // "the pin blends into the window behind it" is obvious or not.
    p.fillRect(QRect(0, 0, w, 34), QColor(0xEC, 0xEC, 0xEC));
    p.setPen(QColor(0x33, 0x33, 0x33));
    QFont f = QApplication::font();
    f.setPointSize(11);
    p.setFont(f);
    p.drawText(QPoint(16, 23), QStringLiteral("未命名文档 - 记事本"));
    p.drawText(QRect(16, 70, w - 32, h - 100), Qt::TextWordWrap,
               QStringLiteral(
                   "这一行是模拟的窗口正文，用来检验贴图在浅色内容上是否还能被认出来。\n"
                   "贴一张最大化的窗口截图时，贴图与底下的窗口长得一模一样，\n"
                   "如果不做任何提示，用户根本不知道屏幕上多了一张图片。\n"
                   "\n"
                   "悬停时出现的四边内阴影和右上角关闭按钮就是那个提示。\n"
                   "关键约束是：这些提示不能盖住画面本身，否则用户为了看内容\n"
                   "还得先把鼠标移开。"));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4C, 0xAF, 0x50));
    p.drawRoundedRect(QRect(w - 130, h - 56, 114, 32), 4, 4);
    p.setPen(Qt::white);
    p.drawText(QRect(w - 130, h - 56, 114, 32), Qt::AlignCenter, QStringLiteral("确定"));
    p.end();
    return img;
}

static void renderForTheEye() {
    printf("\n[6] what it looks like: the same pin, idle vs hovered\n");

    const QImage window = makeFakeMaximisedWindow(640, 420);

    PinWindow pin(window);
    sendLeave(&pin);
    const QImage idle = renderAt(&pin, 1.0);
    sendEnter(&pin);
    sendMouse(&pin, QEvent::MouseMove, pin.closeButtonRect().center(),
              Qt::NoButton, Qt::NoButton);
    const QImage hover = renderAt(&pin, 1.0);

    // Side by side rather than composited over a desktop: the case that prompted this
    // is a pin that covers the whole screen, so there is no "behind it" to compare
    // against -- the only thing that can tell the user it is an image is the chrome.
    const int gap = 40;
    const int label = 34;
    QImage canvas(2 * idle.width() + gap, idle.height() + label + 16, QImage::Format_ARGB32);
    canvas.fill(QColor(0x3A, 0x3A, 0x3A));
    {
        QPainter p(&canvas);
        QFont f = QApplication::font();
        f.setPointSize(11);
        p.setFont(f);
        p.setPen(QColor(0xE0, 0xE0, 0xE0));
        p.drawText(QRect(0, 4, idle.width(), 24), Qt::AlignCenter,
                   QStringLiteral("鼠标未移入：与底下那个窗口一模一样"));
        p.drawText(QRect(idle.width() + gap, 4, idle.width(), 24), Qt::AlignCenter,
                   QStringLiteral("鼠标移入：一眼看出这是一张图片"));
        p.drawImage(QPoint(0, label), idle);
        p.drawImage(QPoint(idle.width() + gap, label), hover);
    }
    canvas.save(QStringLiteral("pin_chrome_before_after.png"));

    printf("       pinned a 640x420 window at 1:1 -> pin %dx%d\n", pin.width(), pin.height());
    printf("       saved pin_chrome_before_after.png (idle left, hovered right)\n");
    check(true, "the comparison image was written");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("QShotProbe"));

    checkChromeGeometry();
    checkChromeStaysOffTheImage();
    checkShadowFalloff();
    checkCloseButton();
    checkInteraction();
    renderForTheEye();

    printf("\n%d checks, %d failures\n", checks, failures);
    fflush(stdout);
    return failures == 0 ? 0 : 1;
}
