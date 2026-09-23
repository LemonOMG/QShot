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
