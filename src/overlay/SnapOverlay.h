#pragma once

#include <QWidget>
#include <QPixmap>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <memory>
#include "core/IWindowDetector.h"
#include "ToolbarWidget.h"
#include "TextInputWidget.h"
#include "SelectionGeometry.h"
#include "../annotation/AnnotationLayer.h"

namespace qshot {

enum class OverlayState {
    Idle,
    Dragging,
    Selected,
    Moving,
    Resizing,
    Annotating
};

class SnapOverlay : public QWidget {
    Q_OBJECT
public:
    explicit SnapOverlay(const QPixmap& background, const QRect& screenGeometry, QWidget* parent = nullptr);
    ~SnapOverlay() override;

signals:
    void closed();
    // The selection composed with its annotations, plus where it came from in global
    // desktop coordinates so the pin can appear exactly where the user selected.
    //
    // Emitted rather than acted on here: a pinned image outlives this overlay, so it
    // is the application's window, not the overlay's child.
    void pinRequested(const QImage& image, const QRect& globalRect);

    // A capture was put on the clipboard.
    //
    // Emitted so the application can confirm it: copying closes the overlay and shows
    // nothing else, so without this the user has no way to tell whether it worked. The
    // overlay does not show the confirmation itself -- the tray icon, and therefore any
    // notification, belongs to the application.
    //
    // `savedPath` is the file the copy also wrote, or empty if it wrote none -- either
    // because the setting is off or because the write failed. Empty is the ordinary case,
    // not an error: a failed write has already been reported by the warning box, and the
    // copy itself succeeded regardless.
    void captureCopied(const QString& savedPath);

    // A capture was written to a file without the user choosing a name.
    //
    // Only the silent-save path emits this. With a file dialog there is nothing to report
    // -- the dialog was the confirmation -- but a silent save has no dialog left, so this
    // signal is the only thing standing between the user and "did that work?".
    void captureSaved(const QString& path);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // With the text tool armed the only way to start typing is to click inside the
    // selection, which is not obvious. A small badge follows the cursor to say so.
    bool textToolArmed() const;
    bool shouldShowTextHint() const;
    QRect textHintRect(const QPoint& mousePos) const;
    /// textHintRect() at the positions where the badge is actually drawn, empty
    /// otherwise. The mouse handler needs the *before* position as well as the current
    /// one, which is why the predicate takes a position rather than reading
    /// currentMousePos_ the way shouldShowTextHint() does.
    QRect textHintRectIfShown(const QPoint& mousePos) const;

    // --- dirty regions ---------------------------------------------------------
    // This widget covers an entire screen, so a bare update() repaints a whole
    // screen -- 4.1M pixels here, and a drag issues one per mouse move. Each helper
    // below answers "which part of the screen can this one interaction have
    // changed", and the mouse handlers pass the union of the answer taken *before*
    // and *after* the change to update(). They are the only thing standing between
    // a drag and a full-screen repaint per move, which is why every one of them
    // deliberately errs on the side of being too large: a region one pixel short
    // leaves a trail of stale ink behind the cursor, and that is far worse than
    // painting a few extra pixels.

    /// The "1234 x 567" badge for a selection, in this widget's space.
    ///
    /// Shared with paintEvent() rather than computed twice: a badge whose repainted
    /// rectangle and drawn rectangle disagree would leave one of its corners on
    /// screen. Same reason textHintRect() exists.
    QRect dimensionBadgeRect(const QRect& selection) const;
    /// Everything that depends on the selection's geometry: the dim mask's hole, the
    /// 2px border, the eight grips that straddle its edges, and the badge.
    QRect selectionChromeRect(const QRect& selection) const;
    /// The chrome for whatever currently forms the hole in the dim mask: the
    /// selection when there is one, otherwise the hovered window's outline.
    ///
    /// Taking the union of this before and after a change is what covers the frame
    /// where a drag crosses the threshold and the hover outline is replaced by a
    /// selection -- the outline's old position has to be repainted away.
    QRect activeChromeRect() const;
    /// The magnifier panel for a cursor position, or an empty rectangle at the
    /// positions where paintEvent() does not draw it.
    QRect magnifierRect(const QPoint& mousePos) const;
    /// The area the in-progress annotation's ink occupies, in this widget's space.
    ///
    /// `onlyLastSegment` restricts it to the tail the most recent mouse move added.
    /// That is the whole difference for the three stroke tools -- the pen, the highlighter
    /// and the mosaic append one segment per move and repaint the rest of the path
    /// identically -- and using the whole path for them would let a stroke across the
    /// screen grow its own dirty region back to the whole screen. The other five tools are
    /// redrawn from their two points, so for them every move does change the whole shape
    /// and the flag makes no difference.
    QRect annotationInkRect(const Annotation& a, bool onlyLastSegment = false) const;

    void updateCursorForPos(const QPoint& pos);
    void copyToClipboard();
    /// Returns the path the image was written to, empty when the user cancelled.
    QString saveToFile();
    void pinSelection();
    // The selection composed with its annotations, in physical pixels and carrying the
    // capture's device pixel ratio. The one place that produces the final image:
    // copy, save and pin all go through it.
    QImage renderSelectionImage() const;
    // Current selection mapped to physical pixels of backgroundImage_, clipped to it.
    QRect physicalSelectionRect() const;

    // This widget's origin in global desktop coordinates.
    //
    // Everything inside this widget is screen-local, starting at (0,0) on whichever
    // screen it covers, while move() on a top-level window takes global desktop
    // coordinates. Every value crossing that boundary has to go through this --
    // which is what the toolbar and the text editor are, since both are top-level
    // windows rather than children.
    //
    // Taken from the widget itself rather than from screenGeometry_ so that it
    // stays correct if the window manager ever places the overlay somewhere other
    // than where it was asked to go.
    QPoint globalOrigin() const;

    QImage backgroundImage_;
    qreal dpr_ = 1.0;
    // The screen this overlay covers, in global desktop coordinates.
    //
    // The overlay's own coordinate space starts at (0,0) on whichever screen it is
    // showing, so this is the translation between the two. Every value that crosses
    // the boundary to a separate top-level window (the toolbar, the text editor) has
    // to be converted, because move() on a top-level window takes global
    // coordinates while everything inside this widget is screen-local. Without it
    // the code happens to work on a single monitor whose origin is (0,0) and
    // misplaces every floating window on any other monitor.
    QRect screenGeometry_;
    QPoint startPos_;
    QRect startRect_;
    QRect selectionRect_;
    QRect hoverWindowRect_;
    
    QPoint currentMousePos_;
    bool isMouseValid_;
    
    OverlayState state_ = OverlayState::Idle;
    selection::Handle activeHandle_ = selection::Handle::None;
    
    std::unique_ptr<IWindowDetector> detector_;
    QTimer hoverTimer_;
    QPoint lastHoverPos_;
    
    // Annotation Phase A & B
    ToolbarWidget* toolbar_ = nullptr;
    TextInputWidget* textInput_ = nullptr;
    AnnotationLayer annotationLayer_;
    Annotation activeAnnotation_;

    // Next sequence number to hand out. Reset whenever a *new* selection is drawn, so a
    // fresh capture always starts at 1; deliberately not reset when an existing selection
    // is moved or resized, because its annotations survive that and their numbers have to
    // stay consistent with them.
    int nextNumber_ = 1;
    
    void showToolbar();
    void hideToolbar();
    // The toolbar, the colour/size panel and the text editor are all separate
    // top-level windows. Showing or hiding one of them leaves the process with no
    // focus window at all (measured: QGuiApplication::focusWindow() becomes null),
    // which silently kills Esc / Enter / Ctrl+Z. Call this after every such
    // show/hide to hand the keyboard back to the overlay.
    void reclaimKeyboardFocus();
    void handleToolSelection(AnnotationType type);
    void finishAnnotation();
    void handleUndo();
};

} // namespace qshot
