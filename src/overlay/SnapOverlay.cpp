#include "SnapOverlay.h"
#include "SelectionGeometry.h"
#include "FloatingPanel.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainterPath>

#include "core/HistoryStore.h"
#include "core/ImageExport.h"
#include "core/PlatformFactory.h"
#include "core/Settings.h"
#include "core/Strings.h"

namespace qshot {

namespace {
// "click to type" badge shown next to the cursor while the text tool is armed.
// Its font comes from panel::uiFontMetrics() so that the rectangle measured in
// textHintRect() and the text drawn in paintEvent() can never disagree.
constexpr int kHintPadding   = 6;
constexpr int kHintCursorGap = 12; // matches the magnifier panel's own gap

QString textHintString() {
    // Deliberately *not* cached in a function-local static: that would freeze the
    // language at whatever was selected the first time this ran.
    return text(Str::TextHintClickToType);
}
} // namespace

SnapOverlay::SnapOverlay(const QPixmap& background, const QRect& screenGeometry, QWidget* parent)
    : QWidget(parent)
    , backgroundImage_(background.toImage())
    , dpr_(background.devicePixelRatio())
    , screenGeometry_(screenGeometry)
    , currentMousePos_(-1, -1)
    , isMouseValid_(false)
{
    // Make window frameless, stay on top, tool window (no taskbar)
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    
    setGeometry(screenGeometry);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true); // Required for hover detection

    detector_ = PlatformFactory::createWindowDetector();

    hoverTimer_.setSingleShot(true);
    hoverTimer_.setInterval(30);
    connect(&hoverTimer_, &QTimer::timeout, this, [this]() {
        if (state_ != OverlayState::Idle) return;
        if (!detector_) return;
        // The detector answers in global desktop coordinates, because that is what
        // "which window is under this desktop point" means. Everything below --
        // painting, the selection, the dirty rectangles -- is in this widget's own
        // space, so the conversion happens here, once, at the single point where the
        // value enters.
        const QRect newHover = detector_->windowRectAt(lastHoverPos_)
                                   .translated(-globalOrigin());
        if (newHover != hoverWindowRect_) {
            // Only the hover outline and the dimmed "hole" change, so repaint just
            // the union of the old and new rectangles (expanded for the 2px border).
            QRect dirty = hoverWindowRect_.united(newHover).adjusted(-2, -2, 2, 2);
            hoverWindowRect_ = newHover;
            update(dirty);
        }
    });

    toolbar_ = new ToolbarWidget(this);
    toolbar_->hide();
    connect(toolbar_, &ToolbarWidget::toolSelected, this, &SnapOverlay::handleToolSelection);
    connect(toolbar_, &ToolbarWidget::undoRequested, this, &SnapOverlay::handleUndo);
    connect(toolbar_, &ToolbarWidget::cancelRequested, this, [this]() {
        close();
        emit closed();
    });
    connect(toolbar_, &ToolbarWidget::copyRequested, this, [this]() {
        copyToClipboard();
        close();
        emit closed();
    });
    connect(toolbar_, &ToolbarWidget::saveRequested, this, [this]() {
        // Keep the overlay alive when saving fails or is cancelled, so the user
        // does not lose their selection and annotations.
        if (!saveToFile().isEmpty()) {
            close();
            emit closed();
        }
    });
    connect(toolbar_, &ToolbarWidget::pinRequested, this, &SnapOverlay::pinSelection);
    
    textInput_ = new TextInputWidget(this);
    textInput_->hide();
    connect(textInput_, &TextInputWidget::editingFinished, this, [this](const QString& text) {
        if (!text.isEmpty()) {
            Annotation a;
            a.type = AnnotationType::Text;
            a.color = toolbar_->currentSettings().color;
            a.fontSize = toolbar_->currentSettings().fontSize;
            // The editor is a separate top-level window, so its pos() is in global
            // desktop coordinates. Annotation points are relative to the selection's
            // top-left in *this* widget's space, hence the mapFromGlobal().
            a.points.append(mapFromGlobal(textInput_->pos())
                            - selectionRect_.normalized().topLeft());
            a.text = text;
            annotationLayer_.add(a);
            toolbar_->setUndoEnabled(true);
            update();
        }
        // The editor window just hid itself; without this the overlay would be
        // left with no keyboard focus.
        reclaimKeyboardFocus();
    });
    connect(textInput_, &TextInputWidget::canceled, this, [this]() {
        reclaimKeyboardFocus();
    });

    // Clicking the toolbar or the colour/size panel must not cost the overlay its
    // keyboard focus either.
    connect(toolbar_, &ToolbarWidget::colorChanged, this, [this]() { reclaimKeyboardFocus(); });
    connect(toolbar_, &ToolbarWidget::sizeSettingChanged, this, [this]() { reclaimKeyboardFocus(); });
}

SnapOverlay::~SnapOverlay() = default;

void SnapOverlay::showToolbar() {
    if (selectionRect_.isEmpty()) return;
    // The toolbar is its own top-level window, so it has to be told where to go in
    // global coordinates, and it does its boundary arithmetic against the screen it
    // must stay inside. Both are therefore global.
    toolbar_->updatePosition(selectionRect_.normalized().translated(globalOrigin()),
                             screenGeometry_);
    toolbar_->show();
    reclaimKeyboardFocus();
}

QPoint SnapOverlay::globalOrigin() const {
    return mapToGlobal(QPoint(0, 0));
}

void SnapOverlay::hideToolbar() {
    if (toolbar_) {
        toolbar_->hide();
        toolbar_->hideSubPanel();
    }
    reclaimKeyboardFocus();
}

void SnapOverlay::reclaimKeyboardFocus() {
    // No raise() on purpose: the toolbar / panel are owned by this window, so the
    // window manager keeps them above it anyway (verified by inspecting the Win32
    // z-order after activateWindow()).
    activateWindow();
}

void SnapOverlay::handleToolSelection(AnnotationType /*type*/) {
    updateCursorForPos(currentMousePos_);
    update();
    reclaimKeyboardFocus();
}

void SnapOverlay::handleUndo() {
    const AnnotationType removed = annotationLayer_.undo();
    // Give the number back. Undoing badge 3 and placing another should produce 3, not 4 --
    // otherwise undo leaves a permanent gap in the sequence the user cannot close.
    if (removed == AnnotationType::Number && nextNumber_ > 1) {
        --nextNumber_;
    }
    toolbar_->setUndoEnabled(!annotationLayer_.isEmpty());
    update();
}

void SnapOverlay::finishAnnotation() {
    if (state_ == OverlayState::Annotating) {
        annotationLayer_.add(activeAnnotation_);
        // The number was claimed at press time so the badge could show it while being
        // placed; committing is what consumes it. A cancelled placement therefore returns
        // the number to the pool rather than leaving a gap in the sequence.
        if (activeAnnotation_.type == AnnotationType::Number) {
            ++nextNumber_;
        }
        toolbar_->setUndoEnabled(true);
        activeAnnotation_.points.clear();
        state_ = OverlayState::Selected;
        update();
    }
}

void SnapOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    
    // Draw original background.
    // backgroundImage_ already carries dpr_ (QPixmap::toImage() preserves the device
    // pixel ratio) and drawImage() honours it, so it must be drawn as-is. Copying it
    // into a local just to call setDevicePixelRatio() would detach and memcpy the
    // whole screen bitmap on every repaint.
    painter.drawImage(0, 0, backgroundImage_);
    
    // Determine the active region (the "hole")
    QRect currentSelection = selectionRect_.normalized();
    QRect activeRect;
    if (!currentSelection.isEmpty()) {
        activeRect = currentSelection;
    } else if (!hoverWindowRect_.isEmpty()) {
        activeRect = hoverWindowRect_;
    }
    
    // Draw dark overlay with a hole
    QPainterPath fullPath;
    fullPath.addRect(rect());
    
    if (!activeRect.isEmpty()) {
        QPainterPath holePath;
        holePath.addRect(activeRect);
        painter.fillPath(fullPath.subtracted(holePath), QColor(0, 0, 0, 100));
    } else {
        painter.fillRect(rect(), QColor(0, 0, 0, 100));
    }
    
    // Draw selection or hover border
    if (!currentSelection.isEmpty()) {
        
        // Draw border
        painter.setPen(QPen(QColor(26, 173, 25), 2)); // Green-ish border
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(currentSelection.adjusted(0, 0, -1, -1));
        
        // Draw annotations
        annotationLayer_.paint(painter, currentSelection);
        if (state_ == OverlayState::Annotating) {
            painter.save();
            painter.translate(currentSelection.topLeft());
            painter.setRenderHint(QPainter::Antialiasing, true);
            AnnotationLayer::paintAnnotation(painter, activeAnnotation_);
            painter.restore();
        }
        
        // Draw 8 control points (ONLY if unlocked)
        if (annotationLayer_.isEmpty() && (!textInput_ || !textInput_->isVisible())) {
            constexpr int kControlPointSize = 6;
            painter.setBrush(QColor(26, 173, 25));
            painter.setPen(Qt::NoPen);
            for (const QRect& grip : selection::controlPointRects(currentSelection, kControlPointSize)) {
                painter.drawRect(grip);
            }
        }
        
        // Draw dimensions
        QString dimensionsText = QString("%1 x %2").arg(currentSelection.width()).arg(currentSelection.height());
        
        // Calculate text background rect
        QFontMetrics fm = painter.fontMetrics();
        QRect textRect = fm.boundingRect(dimensionsText);
        textRect.adjust(-4, -2, 4, 2);
        
        // Position the text box above the selection if possible, otherwise inside or below.
        // 0 is the correct boundary here: this badge is painted into the overlay's own
        // surface, whose origin is the top-left of the screen it covers. Contrast the
        // toolbar and the text editor, which are separate top-level windows and
        // therefore need global coordinates.
        int textX = currentSelection.left();
        int textY = currentSelection.top() - textRect.height() - 5;
        if (textY < 0) {
            textY = currentSelection.top() + 5;
        }
        textRect.moveTo(textX, textY);
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRect(textRect);
        
        painter.setPen(Qt::white);
        painter.drawText(textRect, Qt::AlignCenter, dimensionsText);
    } else if (!hoverWindowRect_.isEmpty()) {
        // Draw dashed hover highlight
        painter.setPen(QPen(QColor(26, 173, 25), 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(hoverWindowRect_.adjusted(0, 0, -1, -1));
    }
    
    // Draw floating mouse info panel (only when Idle or Dragging)
    const OverlayState state = state_;
    if (isMouseValid_ && rect().contains(currentMousePos_)
        && (state == OverlayState::Idle || state == OverlayState::Dragging)) {
        // While dragging the readout shows the coordinate relative to the selection's
        // top-left; while idle it shows the absolute position.
        QPoint relPos = currentMousePos_;
        if (state == OverlayState::Dragging && !currentSelection.isEmpty()) {
            relPos = currentMousePos_ - currentSelection.topLeft();
        }

        panel::paintMagnifier(painter, backgroundImage_, dpr_, currentMousePos_, relPos,
                              panel::computeMagnifierLayout(currentMousePos_, rect(), dpr_));
    }

    // "Click to type" badge: with the text tool armed, clicking inside the selection
    // is the only way to start typing, and that is not discoverable on its own.
    if (shouldShowTextHint()) {
        const QRect hint = textHintRect(currentMousePos_);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPainterPath hintPath;
        hintPath.addRoundedRect(hint, 4, 4);
        painter.fillPath(hintPath, QColor(26, 173, 25, 235));

        painter.setFont(panel::uiFont());
        painter.setPen(Qt::white);
        painter.drawText(hint, Qt::AlignCenter, textHintString());
        painter.restore();
    }
}

bool SnapOverlay::textToolArmed() const {
    return toolbar_
        && toolbar_->currentTool() == AnnotationType::Text
        && (!textInput_ || !textInput_->isVisible());
}

bool SnapOverlay::shouldShowTextHint() const {
    return textToolArmed()
        && state_ == OverlayState::Selected
        && isMouseValid_
        && selectionRect_.contains(currentMousePos_);
}

QRect SnapOverlay::textHintRect(const QPoint& mousePos) const {
    const QFontMetrics& fm = panel::uiFontMetrics();
    const int w = fm.horizontalAdvance(textHintString()) + kHintPadding * 2;
    const int h = fm.height() + kHintPadding;
    return panel::placeNearCursor(QSize(w, h), mousePos, rect(), kHintCursorGap);
}

void SnapOverlay::updateCursorForPos(const QPoint& pos) {
    if (state_ == OverlayState::Selected) {
        const selection::Handle h = selection::hitTestHandle(selectionRect_, pos);
        if (h != selection::Handle::None) {
            setCursor(selection::cursorForHandle(h));
            return;
        }
        if (selectionRect_.contains(pos)) {
            if (toolbar_ && toolbar_->currentTool() == AnnotationType::Text) {
                // A caret cursor is the standard hint that a click here starts typing.
                setCursor(Qt::IBeamCursor);
            } else if (toolbar_ && toolbar_->currentTool() != AnnotationType::None) {
                setCursor(Qt::CrossCursor);
            } else {
                setCursor(Qt::SizeAllCursor);
            }
        } else {
            setCursor(Qt::ForbiddenCursor);
        }
        return;
    }
    if (state_ == OverlayState::Annotating) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (state_ == OverlayState::Idle) {
        setCursor(Qt::CrossCursor);
    }
}

void SnapOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (state_ == OverlayState::Selected) {
            bool isLocked = !annotationLayer_.isEmpty() || (textInput_ && textInput_->isVisible());
            
            const selection::Handle h = isLocked
                ? selection::Handle::None
                : selection::hitTestHandle(selectionRect_, event->pos());
            if (h != selection::Handle::None) {
                state_ = OverlayState::Resizing;
                activeHandle_ = h;
                startPos_ = event->pos();
                startRect_ = selectionRect_;
                hideToolbar();
                return;
            }
            if (selectionRect_.contains(event->pos())) {
                if (toolbar_ && toolbar_->currentTool() != AnnotationType::None) {
                    if (toolbar_->currentTool() == AnnotationType::Text) {
                        // startInput() moves a top-level window, so it needs the
                        // position in global coordinates, not this widget's.
                        textInput_->startInput(mapToGlobal(event->pos()),
                                               toolbar_->currentSettings().color,
                                               toolbar_->currentSettings().fontSize);
                        return; // stay in Selected state, let TextInputWidget handle input
                    }
                    
                    state_ = OverlayState::Annotating;
                    activeAnnotation_.type = toolbar_->currentTool();
                    activeAnnotation_.color = toolbar_->currentSettings().color;
                    activeAnnotation_.lineWidth = toolbar_->currentSettings().lineWidth;
                    activeAnnotation_.mosaicSize = toolbar_->currentSettings().mosaicSize;
                    activeAnnotation_.badgeDiameter = toolbar_->currentSettings().badgeDiameter;
                    // Claimed at press time rather than at commit time: the badge shows its
                    // number while it is being placed, and a cancelled placement (right
                    // click) has to give the number back -- see the RightButton branch.
                    if (activeAnnotation_.type == AnnotationType::Number) {
                        activeAnnotation_.number = nextNumber_;
                    }
                    activeAnnotation_.points.clear();
                    activeAnnotation_.points.append(event->pos() - selectionRect_.normalized().topLeft());
                    
                    if (activeAnnotation_.type == AnnotationType::Mosaic) {
                        // The first point of the stroke, so there is no segment to pass: the
                        // default (empty = the whole annotation) is both correct and free
                        // here, because the path is a single point at this moment.
                        annotationLayer_.updateMosaic(activeAnnotation_);
                    }
                    
                    return;
                } else {
                    if (isLocked) {
                        // If locked and no tool selected, do nothing
                        return;
                    }
                    state_ = OverlayState::Moving;
                    startPos_ = event->pos();
                    startRect_ = selectionRect_;
                    hideToolbar();
                    return;
                }
            }
            return;
        }

        // Idle
        startPos_ = event->pos();
        selectionRect_ = QRect(); // Keep empty so hover rect is still drawn until drag starts
        state_ = OverlayState::Dragging;
        update();
        return;
    }

    if (event->button() == Qt::RightButton) {
        if (state_ == OverlayState::Annotating) {
            // Cancel current annotation
            if (activeAnnotation_.type == AnnotationType::Mosaic) {
                // updateMosaic() writes blocks into the layer while the stroke is
                // being dragged, so cancelling has to rebuild it -- otherwise the
                // "cancelled" mosaic stays on screen and, worse, gets baked into
                // the copied/saved image by renderToImage().
                annotationLayer_.rebuildMosaicCache();
            }
            state_ = OverlayState::Selected;
            activeAnnotation_.points.clear();
            updateCursorForPos(event->pos());
            update();
            return;
        }
        if (state_ == OverlayState::Idle) {
            close();
            emit closed();
            return;
        }
        selectionRect_ = QRect();
        hoverWindowRect_ = QRect();
        annotationLayer_.clear();
        nextNumber_ = 1;
        state_ = OverlayState::Idle;
        // Drop any half-finished text, otherwise the editor would stay on screen
        // over an empty canvas (and keep stealing keystrokes).
        if (textInput_ && textInput_->isVisible()) {
            textInput_->cancelInput();
        }
        hideToolbar();
        if (toolbar_) {
            // Reset tool selection to None
            toolbar_->handleToolClick(toolbar_->currentTool()); // Toggle off
        }
        updateCursorForPos(event->pos());
        update();
        return;
    }
}

void SnapOverlay::mouseMoveEvent(QMouseEvent* event) {
    QPoint oldMousePos = currentMousePos_;
    currentMousePos_ = event->pos();
    isMouseValid_ = true;

    switch (state_) {
    case OverlayState::Annotating: {
        QPoint relPos = event->pos() - selectionRect_.normalized().topLeft();
        if (activeAnnotation_.type == AnnotationType::Rectangle || activeAnnotation_.type == AnnotationType::Ellipse || activeAnnotation_.type == AnnotationType::Arrow) {
            if (activeAnnotation_.points.size() < 2) {
                activeAnnotation_.points.append(relPos);
            } else {
                activeAnnotation_.points[1] = relPos;
            }
        } else if (activeAnnotation_.type == AnnotationType::Number) {
            // The badge follows the cursor while the button is held, so a press places it
            // and a press-and-nudge refines it. Committing happens on release either way,
            // which is what makes a plain click work.
            if (activeAnnotation_.points.isEmpty()) {
                activeAnnotation_.points.append(relPos);
            } else {
                activeAnnotation_.points[0] = relPos;
            }
        } else if (activeAnnotation_.type == AnnotationType::Pen
                   || activeAnnotation_.type == AnnotationType::Mosaic
                   || activeAnnotation_.type == AnnotationType::Highlight) {
            const QPoint previous = activeAnnotation_.points.isEmpty()
                                        ? relPos
                                        : activeAnnotation_.points.last();
            activeAnnotation_.points.append(relPos);
            if (activeAnnotation_.type == AnnotationType::Mosaic) {
                // Only the segment from the previous point to this one is new ink. Handing
                // that to updateMosaic() keeps the cost proportional to what was added --
                // the annotation's own bounding box grows with every point, so mosaicking
                // the full path on every mouse move makes a drag across the screen rescan
                // the screen each time.
                //
                // A zero-length segment would be an empty rectangle, which updateMosaic()
                // reads as "the whole annotation"; a 1x1 rectangle is the honest way to say
                // "just this point", and is what makes a plain click leave a pen-width dot.
                const QRect segment = QRect(previous, relPos).normalized();
                annotationLayer_.updateMosaic(
                    activeAnnotation_, segment.isEmpty() ? QRect(relPos, QSize(1, 1)) : segment);
            }
        }
        update();
        return;
    }
    case OverlayState::Moving: {
        QPoint delta = event->pos() - startPos_;
        selectionRect_ = startRect_.translated(delta);
        update();
        return;
    }
    case OverlayState::Resizing: {
        selectionRect_ = selection::resizedRect(startRect_, activeHandle_, event->pos());
        update();
        return;
    }
    case OverlayState::Dragging: {
        if (selection::isDragGesture(startPos_, event->pos())) {
            selectionRect_ = selection::dragRect(startPos_, event->pos());
        }
        update();
        return;
    }
    case OverlayState::Selected: {
        updateCursorForPos(event->pos());
        // The "click to type" badge follows the cursor, so repaint the union of its
        // old and new positions instead of the whole screen.
        if (textToolArmed()) {
            if (oldMousePos.x() < 0) {
                update();
            } else {
                update(textHintRect(oldMousePos).united(textHintRect(currentMousePos_))
                           .adjusted(-2, -2, 2, 2));
            }
        }
        return;
    }
    case OverlayState::Idle: {
        lastHoverPos_ = event->globalPosition().toPoint();
        if (!hoverTimer_.isActive()) hoverTimer_.start();
        updateCursorForPos(event->pos());
        
        // In Idle the only thing that moves with the cursor is the magnifier panel
        // (the hover outline is repainted by hoverTimer_), so repaint just the union
        // of the old and new panel rectangles instead of the whole screen.
        if (oldMousePos.x() >= 0) {
            QRect dirty = panel::computeMagnifierLayout(oldMousePos, rect(), dpr_).panel
                              .united(panel::computeMagnifierLayout(currentMousePos_, rect(), dpr_).panel);
            update(dirty.adjusted(-2, -2, 2, 2));
        } else {
            update();
        }
        return;
    }
    }
}

void SnapOverlay::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    isMouseValid_ = false;
    update();
}

void SnapOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (state_ == OverlayState::Annotating) {
            finishAnnotation();
            return;
        }
        if (state_ == OverlayState::Moving) {
            // Whether the selection was actually moved or the user merely clicked
            // inside it, the outcome is identical: keep the selection, re-crop the
            // base image and show the toolbar.
            state_ = OverlayState::Selected;
            annotationLayer_.setBaseImage(backgroundImage_, selectionRect_.normalized(), dpr_);
            showToolbar();
            return;
        }
        if (state_ == OverlayState::Resizing) {
            state_ = OverlayState::Selected;
            annotationLayer_.setBaseImage(backgroundImage_, selectionRect_.normalized(), dpr_);
            showToolbar();
            return;
        }
        if (state_ == OverlayState::Dragging) {
            if (selection::isDragGesture(startPos_, event->pos())) {
                // Was a drag
                if (selectionRect_.width() >= selection::kMinimumSelectionEdge
                    && selectionRect_.height() >= selection::kMinimumSelectionEdge) {
                    state_ = OverlayState::Selected;
                    // A newly established selection starts its own sequence, so its first
                    // badge is always 1. Deliberately not done for Moving/Resizing above:
                    // those keep their annotations, whose numbers must stay consistent.
                    nextNumber_ = 1;
                    annotationLayer_.setBaseImage(backgroundImage_, selectionRect_.normalized(), dpr_);
                    showToolbar();
                } else {
                    selectionRect_ = QRect();
                    state_ = OverlayState::Idle;
                    hideToolbar();
                }
            } else {
                // Was a click
                if (hoverWindowRect_.isValid()) {
                    selectionRect_ = hoverWindowRect_;
                    state_ = OverlayState::Selected;
                    // A newly established selection starts its own sequence, so its first
                    // badge is always 1. Deliberately not done for Moving/Resizing above:
                    // those keep their annotations, whose numbers must stay consistent.
                    nextNumber_ = 1;
                    annotationLayer_.setBaseImage(backgroundImage_, selectionRect_.normalized(), dpr_);
                    showToolbar();
                } else {
                    selectionRect_ = QRect();
                    state_ = OverlayState::Idle;
                    hideToolbar();
                }
            }
            hoverWindowRect_ = QRect(); // Clear hover once decision is made
            update();
            return;
        }
    }
}

void SnapOverlay::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (state_ == OverlayState::Selected && selectionRect_.contains(event->pos())) {
            copyToClipboard();
            close();
            emit closed();
        }
    }
}

void SnapOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (!annotationLayer_.isEmpty()) {
            annotationLayer_.clear();
            nextNumber_ = 1;
            toolbar_->setUndoEnabled(false);
            update();
        } else {
            close();
            emit closed();
        }
    } else if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Z) {
        handleUndo();
    } else if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_T) {
        // Ctrl+T rather than a bare letter: every unmodified key is potentially text
        // input once the text tool is armed.
        if (state_ == OverlayState::Selected && !selectionRect_.isEmpty()) {
            pinSelection();
        }
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (state_ == OverlayState::Selected && !selectionRect_.isEmpty()) {
            copyToClipboard();
            close();
            emit closed();
        }
    } else {
        QWidget::keyPressEvent(event);
    }
}

QRect SnapOverlay::physicalSelectionRect() const {
    return selection::toPhysicalRect(selectionRect_.normalized(), dpr_, backgroundImage_.rect());
}

QImage SnapOverlay::renderSelectionImage() const {
    const QRect physicalRect = physicalSelectionRect();
    if (physicalRect.isEmpty()) return QImage();
    return annotationLayer_.renderToImage(backgroundImage_.copy(physicalRect));
}

void SnapOverlay::copyToClipboard() {
    const QImage image = renderSelectionImage();
    copyImageToClipboard(image);

    // The optional file write happens *before* the history add so the store can adopt the
    // file instead of encoding the same image a second time. It is deliberately tied to
    // taking a capture and not to re-copying an old one from the history menu: that file
    // was already written when the capture was first made, and writing another copy of it
    // would fill the save folder with duplicates of the same screenshot.
    const QString saved = Settings::instance().saveOnCopy()
                              ? saveImageQuietly(image, this)
                              : QString();

    // Recorded here rather than in the application because this is the only place the
    // composed image exists; the history store is a core service, not an application
    // concern, so reaching it directly is the shorter path.
    HistoryStore::instance().add(image, saved);
    emit captureCopied(saved);
}

QString SnapOverlay::saveToFile() {
    const QImage image = renderSelectionImage();

    // One decision point for both ways of saving. The silent path is opt-in, and it reports
    // itself through captureSaved() because the dialog that used to be the confirmation is
    // no longer there.
    QString path;
    if (Settings::instance().quietSave()) {
        path = saveImageQuietly(image, this);
        if (!path.isEmpty()) emit captureSaved(path);
    } else {
        path = saveImageWithDialog(this, image);
    }
    if (path.isEmpty()) return path;

    // Hand the store the file that was just written instead of encoding the same image
    // a second time; it copies it in, so the entry survives the user later tidying up
    // wherever they saved it to.
    HistoryStore::instance().add(image, path);
    return path;
}

void SnapOverlay::pinSelection() {
    const QImage image = renderSelectionImage();
    if (image.isNull()) return;

    // The pin is placed where the selection was, so the screenshot appears to stay
    // put while the overlay disappears from under it.
    emit pinRequested(image, selectionRect_.normalized().translated(globalOrigin()));

    close();
    emit closed();
}

} // namespace qshot
