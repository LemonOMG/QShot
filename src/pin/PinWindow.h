#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

// Only ever passed by reference from the private drawing helpers, so a forward
// declaration is enough and the header does not drag in the whole painter.
class QPainter;

namespace qshot {

namespace pin {

/// Zoom limits, as a multiple of the image's natural (device-independent) size.
constexpr qreal kMinScale = 0.25;
constexpr qreal kMaxScale = 4.0;

/// One wheel notch multiplies the scale by this.
constexpr qreal kScaleStep = 1.1;

/// Alt+wheel changes opacity by this much per notch.
constexpr qreal kOpacityStep = 0.1;
constexpr qreal kMinOpacity = 0.2;
constexpr qreal kMaxOpacity = 1.0;

/// The frame drawn around the image, in logical pixels. Two rather than one so that
/// at device pixel ratios of 1, 1.5 and 2 the image content starts on a whole device
/// pixel: a 1px frame puts the content at device offset 1.5 on a 150% display, which
/// shifts the whole image half a pixel and resamples it.
constexpr int kBorderWidth = 2;

/// How far the hover shadow reaches in from the edge, in logical pixels. Capped at a
/// third of the shorter side when the pin is small, so a zoomed-out pin does not end
/// up entirely shadow.
constexpr int kShadowDepth = 14;

/// Close affordance in the top-right corner, drawn only while the cursor is over the
/// pin. Without it a pin that happens to match what is behind it -- a maximised window
/// pinned at 1:1, which is the common case -- is indistinguishable from the desktop,
/// and the user has no way to tell it is an image, let alone how to get rid of it.
constexpr int kCloseButtonSize = 18;
constexpr int kCloseButtonMargin = 3;

/// `scale` limited to [kMinScale, kMaxScale].
qreal clampedScale(qreal scale);

/**
 * Where the image's top-left corner has to move so that the image pixel under
 * `anchor` stays under `anchor` while the image scales from `oldScale` to `newScale`.
 *
 * `contentTopLeft` is the top-left of the *image*, not of the window -- the 1px frame
 * sits outside it. Both points are in global desktop coordinates, which is the space
 * QMouseEvent and move() use for a top-level window.
 *
 * Pure arithmetic with no widget involved, so it is unit-testable without a screen --
 * and this is exactly the part that is easy to get subtly wrong and impossible to
 * eyeball.
 */
QPoint anchoredTopLeft(const QPoint& contentTopLeft, const QPoint& anchor,
                       qreal oldScale, qreal newScale);

} // namespace pin

/**
 * A screenshot pinned on top of everything else, for use as a reference while you
 * work in another window.
 *
 * Interaction: drag to move, wheel to zoom (anchored at the cursor), Alt+wheel to
 * change opacity, double-click or Esc to close, right-click for copy / save / close.
 * Hovering reveals an inner shadow and a close button, so the pin announces itself as
 * a floating object rather than blending into whatever is behind it.
 *
 * The image keeps its own device pixel ratio, so a capture taken on a scaled display
 * is shown at its true physical resolution instead of being resampled through a
 * logical-pixel detour.
 *
 * Lifetime: created parentless and self-deleting, so a pin outlives the overlay that
 * made it. The application does not track them -- `setQuitOnLastWindowClosed(false)`
 * in main.cpp already keeps the process alive with no windows at all.
 */
class PinWindow : public QWidget {
    Q_OBJECT
public:
    explicit PinWindow(const QImage& image, QWidget* parent = nullptr);
    ~PinWindow() override;

    /// Place the pin's top-left at `globalTopLeft`, in global desktop coordinates.
    void showAt(const QPoint& globalTopLeft);

    /// Show the pin centred on the screen the cursor is on.
    ///
    /// For the path that has no originating selection to inherit a position from (the
    /// history menu): the cursor is the only hint about which screen the user means, and
    /// a capture pinned at the cursor's top-left corner would hang off the bottom-right
    /// of the screen in the common case of a full-screen capture.
    void showCentredOnCursorScreen();

    /// Current zoom factor, as a multiple of the natural size. Exposed for tests.
    qreal scale() const { return scale_; }

    /// Where the close affordance sits, in widget coordinates. Empty when the pin is
    /// too small to hold one -- a click there then falls through to dragging.
    QRect closeButtonRect() const;

    /// The part of the widget showing the image, i.e. inside the frame.
    QRect contentRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    /// Resize around `anchor` (global coordinates) so the pixel under it stays put.
    void applyScale(qreal newScale, const QPoint& anchor);
    void setOpacityBy(qreal delta);
    /// Widget size for the current scale, frame included.
    QSize widgetSize() const;
    /// Whether the hover chrome (shadow + close button) should be showing.
    bool chromeVisible() const { return hovered_; }
    void drawInnerShadow(QPainter& painter) const;
    void drawCloseButton(QPainter& painter) const;
    void copyImage();
    void saveImage();

    QImage image_;
    qreal scale_ = 1.0;
    /// Cursor position inside the window while dragging, in window coordinates.
    QPoint dragOffset_;
    bool dragging_ = false;
    bool hovered_ = false;
    /// Separate from hovered_: the cursor can be anywhere over the pin, but only the
    /// close button lights up when it is on the button.
    bool closeHovered_ = false;
};

} // namespace qshot
