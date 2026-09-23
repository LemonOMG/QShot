#include "PinWindow.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>

#include "../core/ImageExport.h"
#include "../core/Strings.h"

namespace qshot {

namespace pin {

qreal clampedScale(qreal scale) {
    return qBound(kMinScale, scale, kMaxScale);
}

QPoint anchoredTopLeft(const QPoint& contentTopLeft, const QPoint& anchor,
                       qreal oldScale, qreal newScale) {
    // Expressed as a displacement from the current origin rather than as an absolute
    // position. Two reasons:
    //   1. a displacement is invariant under translating the whole scene, so the pin
    //      anchors identically on a screen whose origin is not (0,0) -- with absolute
    //      rounding the same gesture lands 1px differently on a screen above the
    //      primary one, because qRound() breaks .5 ties towards +infinity for
    //      positive values and away from zero for negative ones;
    //   2. rounding a displacement keeps the error relative to the pin instead of
    //      snapping the whole window to an absolute grid.
    const qreal ratio = newScale / oldScale;
    const qreal dx = (anchor.x() - contentTopLeft.x()) * (1.0 - ratio);
    const qreal dy = (anchor.y() - contentTopLeft.y()) * (1.0 - ratio);
    return contentTopLeft + QPoint(qRound(dx), qRound(dy));
}

} // namespace pin

namespace {
// The frame is drawn dimmer than the accent until the cursor is over the pin, so a
// row of pins does not turn into a wall of coloured outlines. It also has to stay
// visible over an arbitrary screenshot, which is why it is a dark alpha rather than a
// light one.
const QColor kIdleBorder(0, 0, 0, 120);
const QColor kHoverBorder(0x4C, 0xAF, 0x50);

// Alpha of the hover shadow at the very edge of the image, before the falloff. 150 is
// about as dark as this can go before the dimmed band starts competing with the
// content underneath it for attention.
constexpr int kShadowAlpha = 150;

// The close disc, at rest and under the cursor. Both are dark enough to sit over a
// white document; the hovered one is darker so the button reads as clickable.
constexpr int kCloseDiscAlpha = 140;
constexpr int kCloseDiscHoverAlpha = 205;
} // namespace

PinWindow::PinWindow(const QImage& image, QWidget* parent)
    : QWidget(parent)
    , image_(image)
{
    // A pin floats over other applications: no frame, always on top, and not in the
    // taskbar (Qt::Tool is what maps to WS_EX_TOOLWINDOW there). Unlike the overlay's
    // panels this one *does* accept focus, because Esc / Ctrl+C have to reach it.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // Nothing owns a pin -- ShotApplication deliberately does not track them, so that a
    // pin outlives the overlay that made it -- which makes this the only thing that can
    // reclaim one. Without it every close leaks the window and its QImage, and a
    // full-screen capture is ~16MB. It is a constructor invariant rather than something
    // the caller remembers, because close() is now reachable from the close button, the
    // context menu, double-click and Esc, and only one of those is easy to remember.
    //
    // Consequence for callers: never close a stack-allocated pin -- the deferred delete
    // would run against the stack. Pins are heap-allocated (or test-only and never
    // closed).
    setAttribute(Qt::WA_DeleteOnClose);

    setCursor(Qt::OpenHandCursor);
    setMouseTracking(true);
    resize(widgetSize());
}

PinWindow::~PinWindow() = default;

QSize PinWindow::widgetSize() const {
    const qreal dpr = image_.devicePixelRatio();
    const QSizeF natural = QSizeF(image_.size()) / (dpr > 0 ? dpr : 1.0);
    return QSize(qRound(natural.width()  * scale_) + pin::kBorderWidth * 2,
                 qRound(natural.height() * scale_) + pin::kBorderWidth * 2);
}

void PinWindow::showAt(const QPoint& globalTopLeft) {
    move(globalTopLeft);
    show();
    // Take the keyboard so Esc closes the pin immediately instead of the user having
    // to click it first. The overlay that created this pin is closing at the same
    // time, so nothing else is competing for focus.
    activateWindow();
}

void PinWindow::showCentredOnCursorScreen() {
    QPoint at(0, 0);

    // screenAt() rather than a scan: it is the same question asked the same way, and on
    // a single-screen machine it degrades to the primary screen on its own.
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();

    if (screen) {
        // availableGeometry(), not geometry(): the taskbar is not part of where a
        // reference image should go.
        const QRect area = screen->availableGeometry();
        at = area.center() - QPoint(width() / 2, height() / 2);

        // Clamping the top-left keeps the frame and the close button reachable when the
        // capture is larger than the screen. Centring alone would push both off the top
        // and left edges, leaving no way to close the pin except the keyboard.
        at.setX(qMax(area.left(), at.x()));
        at.setY(qMax(area.top(), at.y()));
    }

    showAt(at);
}

void PinWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    const int b = pin::kBorderWidth;
    const QRect content = contentRect();

    // Smooth when shrinking, nearest-neighbour when magnifying. A screenshot enlarged
    // is usually enlarged to read something, and interpolating it produces a blurry
    // result that hides the very pixels being inspected. At 1:1 the hint is
    // irrelevant and the image maps onto the widget pixel for pixel.
    p.setRenderHint(QPainter::SmoothPixmapTransform, scale_ < 1.0);
    p.drawImage(content, image_);

    // The frame is four filled strips rather than a stroked rectangle. A 1px pen
    // straddles the path it is given, so at a device pixel ratio of 2 half of the
    // stroke lands inside the image and tints the last row and column -- measured: the
    // final column came back as #FF0000A5 (255 x (1 - 90/255) = 165) instead of pure
    // blue. Filled strips land on whole device pixels at every ratio kBorderWidth is
    // a multiple of.
    const QColor frame = hovered_ ? kHoverBorder : kIdleBorder;
    p.fillRect(QRect(0, 0, width(), b), frame);
    p.fillRect(QRect(0, height() - b, width(), b), frame);
    p.fillRect(QRect(0, b, b, height() - 2 * b), frame);
    p.fillRect(QRect(width() - b, b, b, height() - 2 * b), frame);

    // Order matters: the shadow dims the image's outer band, and the close button sits
    // inside that band, so the button has to be painted last or it would be dimmed too.
    if (chromeVisible()) {
        drawInnerShadow(p);
        drawCloseButton(p);
    }
}

QRect PinWindow::contentRect() const {
    const int b = pin::kBorderWidth;
    return rect().adjusted(b, b, -b, -b);
}

QRect PinWindow::closeButtonRect() const {
    const QRect content = contentRect();

    // The button has to fit whole. A clipped disc with a squashed X reads as a
    // rendering bug rather than as a control, and on a pin that small the frame colour
    // already carries the "this is a floating object" message. Returning an empty rect
    // is also what makes a click there fall through to dragging.
    const int needed = pin::kCloseButtonSize + 2 * pin::kCloseButtonMargin;
    if (content.width() < needed || content.height() < needed) return QRect();

    return QRect(content.right() - pin::kCloseButtonMargin - pin::kCloseButtonSize + 1,
                 content.top() + pin::kCloseButtonMargin,
                 pin::kCloseButtonSize, pin::kCloseButtonSize);
}

void PinWindow::drawInnerShadow(QPainter& p) const {
    const QRect content = contentRect();
    if (content.isEmpty()) return;

    // Capped at a third of the shorter side so a pin zoomed out to 25% is not entirely
    // shadow -- at that size the shadow would be the only thing left to see.
    const int depth = qMin(pin::kShadowDepth,
                           qMin(content.width(), content.height()) / 3);
    if (depth <= 0) return;

    // Concentric one-pixel *bands* with a quadratic falloff, not one solid inset
    // rectangle. A single inset rectangle reads as a second, fat border; a falloff
    // reads as a shadow, which is the point -- a pin showing a maximised window at 1:1
    // is otherwise indistinguishable from the desktop behind it.
    //
    // The bands are nested frames that never overlap (the vertical ones skip the rows
    // the horizontal ones already took), because two translucent fills compositing over
    // the same pixel would darken the corners twice as fast as the edges.
    for (int i = 0; i < depth; ++i) {
        const qreal t = 1.0 - static_cast<qreal>(i) / depth;   // 1 at the edge, -> 0 inside
        const int alpha = qRound(kShadowAlpha * t * t);
        if (alpha <= 0) continue;
        const QColor shade(0, 0, 0, alpha);

        const int w = content.width() - 2 * i;
        const int h = content.height() - 2 * i;
        // Both are positive for every i < depth, since depth is capped at a third of the
        // shorter side -- but the vertical strips below use w, so guard them together
        // rather than leaving a dependency that only holds by virtue of the cap.
        if (w <= 0 || h <= 0) continue;

        p.fillRect(QRect(content.x() + i, content.y() + i, w, 1), shade);
        p.fillRect(QRect(content.x() + i, content.y() + i + h - 1, w, 1), shade);
        // Skipping the first and last row is what keeps the corners from being composited
        // twice, once by the horizontal strips and once by the vertical ones.
        if (h - 2 > 0) {
            p.fillRect(QRect(content.x() + i, content.y() + i + 1, 1, h - 2), shade);
            p.fillRect(QRect(content.x() + i + w - 1, content.y() + i + 1, 1, h - 2), shade);
        }
    }
}

void PinWindow::drawCloseButton(QPainter& p) const {
    const QRect r = closeButtonRect();
    if (r.isEmpty()) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // A dark disc with a light glyph, rather than a bare glyph. The pin can be showing
    // anything at all, and a light X over a white document -- the common case -- would
    // disappear; the disc is what guarantees contrast, and the glyph only has to read
    // against the disc.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, closeHovered_ ? kCloseDiscHoverAlpha : kCloseDiscAlpha));
    p.drawEllipse(r);

    // Inset by a quarter rather than drawn corner to corner: a circle's inscribed square
    // is only ~70% of its width, so a full-width X looks like it is spilling over the
    // disc's edge.
    const int inset = qMax(1, r.width() / 4);
    const QRect x = r.adjusted(inset, inset, -inset, -inset);
    QPen pen(QColor(255, 255, 255, closeHovered_ ? 255 : 225));
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(x.topLeft(), x.bottomRight());
    p.drawLine(x.topRight(), x.bottomLeft());

    p.restore();
}

void PinWindow::applyScale(qreal newScale, const QPoint& anchor) {
    const qreal clamped = pin::clampedScale(newScale);
    if (qFuzzyCompare(clamped, scale_)) return; // already at a limit

    // pos() of a top-level window is global, which is also the space `anchor` is in.
    const QPoint contentTopLeft = pos() + QPoint(pin::kBorderWidth, pin::kBorderWidth);
    const QPoint newContentTopLeft =
        pin::anchoredTopLeft(contentTopLeft, anchor, scale_, clamped);

    scale_ = clamped;
    resize(widgetSize());
    move(newContentTopLeft - QPoint(pin::kBorderWidth, pin::kBorderWidth));
    update();
}

void PinWindow::setOpacityBy(qreal delta) {
    const qreal next = qBound(pin::kMinOpacity, windowOpacity() + delta, pin::kMaxOpacity);
    setWindowOpacity(next);
}

void PinWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    // The close button wins over dragging. Without this branch a press on the button
    // would start a drag, and the button would only ever be reachable by the keyboard.
    const QRect button = closeButtonRect();
    if (!button.isEmpty() && button.contains(event->position().toPoint())) {
        close();
        event->accept();
        return;
    }

    dragging_ = true;
    // Where inside the window the cursor grabbed it, in window coordinates, so the pin
    // does not jump so its top-left lands under the cursor.
    dragOffset_ = event->position().toPoint();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PinWindow::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        // globalPosition() and move() are both global desktop coordinates.
        move(event->globalPosition().toPoint() - dragOffset_);
        event->accept();
        return;
    }

    // Tracking the button separately from the hover state: the cursor is over the pin
    // the whole time it is inside the window, but only lights the button up while it is
    // actually on the button.
    const QRect button = closeButtonRect();
    const bool overButton =
        !button.isEmpty() && button.contains(event->position().toPoint());
    if (overButton != closeHovered_) {
        closeHovered_ = overButton;
        setCursor(overButton ? Qt::PointingHandCursor : Qt::OpenHandCursor);
        update(button);
    }
    event->accept();
}

void PinWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !dragging_) return;
    dragging_ = false;

    // A drag can end with the cursor over the button (the user moved the pin under its
    // own button), so the cursor shape has to be re-derived rather than blindly reset
    // to the open hand.
    const QRect button = closeButtonRect();
    closeHovered_ = !button.isEmpty() && button.contains(event->position().toPoint());
    setCursor(closeHovered_ ? Qt::PointingHandCursor : Qt::OpenHandCursor);
    update();
    event->accept();
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    close();
    event->accept();
}

void PinWindow::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) return;

    if (event->modifiers() & Qt::AltModifier) {
        setOpacityBy(delta > 0 ? pin::kOpacityStep : -pin::kOpacityStep);
    } else {
        const qreal factor = delta > 0 ? pin::kScaleStep : 1.0 / pin::kScaleStep;
        applyScale(scale_ * factor, event->globalPosition().toPoint());
    }
    event->accept();
}

void PinWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_C) { copyImage(); return; }
        if (event->key() == Qt::Key_S) { saveImage(); return; }
    }
    QWidget::keyPressEvent(event);
}

void PinWindow::enterEvent(QEnterEvent* /*event*/) {
    hovered_ = true;
    update();
}

void PinWindow::leaveEvent(QEvent* /*event*/) {
    hovered_ = false;
    // The button state is reset too: leaving the pin while on the button and re-entering
    // somewhere else would otherwise show the button still lit until the first move.
    closeHovered_ = false;
    update();
}

void PinWindow::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.addAction(text(Str::PinMenuCopy), this, &PinWindow::copyImage);
    menu.addAction(text(Str::PinMenuSaveAs), this, &PinWindow::saveImage);
    menu.addSeparator();
    menu.addAction(text(Str::PinMenuClose), this, &PinWindow::close);
    menu.exec(event->globalPos());
}

void PinWindow::copyImage() {
    copyImageToClipboard(image_);
}

void PinWindow::saveImage() {
    // Parented to the pin: it is always-on-top, so an unowned dialog could end up
    // behind it.
    saveImageWithDialog(this, image_);
}

} // namespace qshot
