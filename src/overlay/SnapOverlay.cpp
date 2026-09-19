#include "SnapOverlay.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QPainterPath>
#include <QDebug>

#ifdef Q_OS_WIN
#include "platform/windows/WinWindowDetector.h"
#endif

namespace qshot {

SnapOverlay::SnapOverlay(const QPixmap& background, const QRect& virtualGeometry, QWidget* parent)
    : QWidget(parent)
    , backgroundPixmap_(background)
    , backgroundImage_(background.toImage())
    , currentMousePos_(-1, -1)
    , isMouseValid_(false)
{
    // Make window frameless, stay on top, tool window (no taskbar)
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    
    setGeometry(virtualGeometry);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true); // Required for hover detection

#ifdef Q_OS_WIN
    detector_ = std::make_unique<WinWindowDetector>();
#endif

    hoverTimer_.setSingleShot(true);
    hoverTimer_.setInterval(30);
    connect(&hoverTimer_, &QTimer::timeout, this, [this]() {
        if (state_ != OverlayState::Idle) return;
        if (!detector_) return;
        QRect newHover = detector_->windowRectAt(lastHoverPos_);
        if (newHover != hoverWindowRect_) {
            hoverWindowRect_ = newHover;
            update();
        }
    });

    toolbar_ = new ToolbarWidget(this);
    toolbar_->hide();
    connect(toolbar_, &ToolbarWidget::toolSelected, this, &SnapOverlay::handleToolSelection);
    connect(toolbar_, &ToolbarWidget::undoRequested, this, [this]() {
        annotationLayer_.undo();
        update();
    });
    connect(toolbar_, &ToolbarWidget::cancelRequested, this, [this]() {
        close();
        emit closed();
    });
    connect(toolbar_, &ToolbarWidget::copyRequested, this, [this]() {
        copyToClipboard();
        close();
        emit closed();
    });
    
    textInput_ = new TextInputWidget(this);
    textInput_->hide();
    connect(textInput_, &TextInputWidget::editingFinished, this, [this](const QString& text) {
        if (text.isEmpty()) return;
        Annotation a;
        a.type = AnnotationType::Text;
        a.color = toolbar_->currentSettings().color;
        a.fontSize = toolbar_->currentSettings().fontSize;
        a.points.append(textInput_->pos() - selectionRect_.normalized().topLeft());
        a.text = text;
        annotationLayer_.add(a);
        toolbar_->setUndoEnabled(true);
        update();
    });
}

SnapOverlay::~SnapOverlay() = default;

void SnapOverlay::showToolbar() {
    if (selectionRect_.isEmpty()) return;
    toolbar_->updatePosition(selectionRect_.normalized(), rect());
    toolbar_->show();
}

void SnapOverlay::hideToolbar() {
    if (toolbar_) toolbar_->hide();
}

void SnapOverlay::handleToolSelection(AnnotationType /*type*/) {
    updateCursorForPos(currentMousePos_);
    update();
}

void SnapOverlay::handleUndo() {
    annotationLayer_.undo();
    toolbar_->setUndoEnabled(!annotationLayer_.isEmpty());
    update();
}

void SnapOverlay::finishAnnotation() {
    if (state_ == OverlayState::Annotating) {
        annotationLayer_.add(activeAnnotation_);
        toolbar_->setUndoEnabled(true);
        activeAnnotation_.points.clear();
        state_ = OverlayState::Selected;
        update();
    }
}

void SnapOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    
    // Draw original background
    painter.drawPixmap(0, 0, backgroundPixmap_);
    
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
            int cpSize = 6;
            painter.setBrush(QColor(26, 173, 25));
            painter.setPen(Qt::NoPen);
            
            int xs[3] = { currentSelection.left(), currentSelection.center().x(), currentSelection.right() - 1 };
            int ys[3] = { currentSelection.top(), currentSelection.center().y(), currentSelection.bottom() - 1 };
            
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    if (i == 1 && j == 1) continue; // Skip center
                    painter.drawRect(xs[i] - cpSize / 2, ys[j] - cpSize / 2, cpSize, cpSize);
                }
            }
        }
        
        // Draw dimensions
        QString dimensionsText = QString("%1 x %2").arg(currentSelection.width()).arg(currentSelection.height());
        
        // Calculate text background rect
        QFontMetrics fm = painter.fontMetrics();
        QRect textRect = fm.boundingRect(dimensionsText);
        textRect.adjust(-4, -2, 4, 2);
        
        // Position the text box above the selection if possible, otherwise inside or below
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
    OverlayState state = state_;
    if (isMouseValid_ && rect().contains(currentMousePos_) && (state == OverlayState::Idle || state == OverlayState::Dragging)) {
        // 1. Calculate relative coordinates
        QPoint relPos = currentMousePos_;
        if (state == OverlayState::Dragging) {
            QRect currentSelection = selectionRect_.normalized();
            if (!currentSelection.isEmpty()) {
                relPos = currentMousePos_ - currentSelection.topLeft();
            }
        }
        
        // 2. Sample pixel color
        qreal dpr = backgroundPixmap_.devicePixelRatio();
        QPoint scaledPos(qRound(currentMousePos_.x() * dpr), qRound(currentMousePos_.y() * dpr));
        
        QColor pixelColor = Qt::black;
        if (backgroundImage_.valid(scaledPos)) {
            pixelColor = backgroundImage_.pixelColor(scaledPos);
        }
        
        // 3. Prepare dimensions and text
        const int magSize = 140; 
        const int padding = 8;
        const int lineSpacing = 4;
        const int colorBlockSize = 12;
        
        QString rgbText = QString("RGB: (%1, %2, %3)").arg(pixelColor.red()).arg(pixelColor.green()).arg(pixelColor.blue());
        QString hexText = QString("HEX: %1").arg(pixelColor.name().toUpper());
        QString coordText = QString("POS: %1, %2").arg(relPos.x()).arg(relPos.y());
        
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPixelSize(12);
        QFontMetrics fm(font);
        
        int w1 = fm.horizontalAdvance(rgbText);
        int w2 = colorBlockSize + 4 + fm.horizontalAdvance(hexText);
        int w3 = fm.horizontalAdvance(coordText);
        int maxTextWidth = qMax(qMax(w1, w2), w3);
        
        int initialBoxWidth = qMax(magSize, maxTextWidth + padding * 2);
        
        // To avoid subpixel alignment issues, we crop the physical image, scale it using nearest neighbor,
        // and force the center physical pixel to be exactly at the crosshair.
        const int zoom = 4; // 4x zoom
        int srcPhysicalW = qRound(initialBoxWidth * dpr / zoom);
        if (srcPhysicalW % 2 == 0) srcPhysicalW++; // Force odd width to have a true center pixel
        
        int srcPhysicalH = qRound(magSize * dpr / zoom);
        if (srcPhysicalH % 2 == 0) srcPhysicalH++; // Force odd height
        
        int magPhysicalW = srcPhysicalW * zoom;
        int magPhysicalH = srcPhysicalH * zoom;
        
        qreal magLogicalW = magPhysicalW / dpr;
        qreal magLogicalH = magPhysicalH / dpr;
        
        int boxWidth = qCeil(magLogicalW);
        int finalMagSize = qCeil(magLogicalH);
        
        int textHeight = fm.height() * 3 + lineSpacing * 2;
        int boxHeight = finalMagSize + padding * 2 + textHeight;
        
        // 4. Calculate position with screen boundary constraints
        int offsetX = 12;
        int offsetY = 12;
        int boxX = currentMousePos_.x() + offsetX;
        int boxY = currentMousePos_.y() + offsetY;
        
        if (boxX + boxWidth > rect().right()) {
            boxX = currentMousePos_.x() - offsetX - boxWidth;
        }
        if (boxY + boxHeight > rect().bottom()) {
            boxY = currentMousePos_.y() - offsetY - boxHeight;
        }
        
        // 5. Draw background with rounded corners and no outer border
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        QPainterPath clipPath;
        clipPath.addRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, 8);
        painter.setClipPath(clipPath);
        
        painter.setPen(Qt::NoPen);
        
        // Draw the full box first
        painter.setBrush(Qt::black);
        painter.drawRect(boxX, boxY, boxWidth, boxHeight);
        
        // Fill text area with white
        QRectF textBgRect(boxX, boxY + finalMagSize, boxWidth, boxHeight - finalMagSize);
        painter.setBrush(Qt::white);
        painter.drawRect(textBgRect);
        
        // 6. Draw magnifier
        int srcX = scaledPos.x() - srcPhysicalW / 2;
        int srcY = scaledPos.y() - srcPhysicalH / 2;
        QRect physicalSrcRect(srcX, srcY, srcPhysicalW, srcPhysicalH);
        
        QImage srcImage(physicalSrcRect.size(), QImage::Format_ARGB32);
        srcImage.fill(Qt::black);
        srcImage.setDevicePixelRatio(1.0); // CRITICAL: Avoid Qt auto-scaling when drawing onto this
        
        QRect intersect = physicalSrcRect.intersected(backgroundImage_.rect());
        if (!intersect.isEmpty()) {
            QImage cropped = backgroundImage_.copy(intersect);
            cropped.setDevicePixelRatio(1.0); // CRITICAL: Ensure 1:1 pixel copy
            QPainter p(&srcImage);
            p.drawImage(intersect.topLeft() - physicalSrcRect.topLeft(), cropped);
        }
        
        QImage magnifiedImage = srcImage.scaled(magPhysicalW, magPhysicalH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        painter.drawImage(QRectF(boxX, boxY, boxWidth, finalMagSize), magnifiedImage);
        
        // Magnifier crosshair (十字型坐标系)
        // Since the physical source has odd dimensions, the exact center maps precisely to 50% of the box
        qreal crosshairX = boxX + boxWidth / 2.0;
        qreal crosshairY = boxY + finalMagSize / 2.0;
        
        painter.setPen(QPen(QColor(0, 255, 0, 150), 2));
        painter.drawLine(QPointF(crosshairX, boxY), QPointF(crosshairX, boxY + finalMagSize));
        painter.drawLine(QPointF(boxX, crosshairY), QPointF(boxX + boxWidth, crosshairY));
        
        // Remove the border separating magnifier and text to make it cleaner
        
        // 7. Draw contents (Text area)
        painter.setFont(font);
        
        int currentY = boxY + finalMagSize + padding + fm.ascent();
        
        // Row 1: RGB
        painter.setPen(Qt::black);
        painter.drawText(boxX + padding, currentY, rgbText);
        
        // Row 2: Color block + Hex
        currentY += fm.height() + lineSpacing;
        int blockY = currentY - fm.ascent() + (fm.height() - colorBlockSize) / 2;
        
        painter.setPen(QPen(Qt::black, 1));
        painter.setBrush(pixelColor);
        painter.drawRect(boxX + padding, blockY, colorBlockSize, colorBlockSize);
        
        painter.setPen(Qt::black);
        painter.drawText(boxX + padding + colorBlockSize + 4, currentY, hexText);
        
        // Row 3: Coordinates
        currentY += fm.height() + lineSpacing;
        painter.setPen(Qt::black);
        painter.drawText(boxX + padding, currentY, coordText);
        
        painter.restore();
    }
}

Handle SnapOverlay::hitTestHandle(const QPoint& pos) const {
    if (selectionRect_.isEmpty()) return Handle::None;
    const int m = 8;
    QRect r = selectionRect_;
    if (QRect(r.topLeft()    - QPoint(m,m), QSize(2*m,2*m)).contains(pos)) return Handle::TopLeft;
    if (QRect(r.topRight()   - QPoint(m,0), QSize(2*m,2*m)).contains(pos)) return Handle::TopRight;
    if (QRect(r.bottomLeft() - QPoint(0,m), QSize(2*m,2*m)).contains(pos)) return Handle::BottomLeft;
    if (QRect(r.bottomRight(),              QSize(2*m,2*m)).contains(pos)) return Handle::BottomRight;
    if (QRect(QPoint(r.center().x()-m, r.top()-m),    QSize(2*m,2*m)).contains(pos)) return Handle::Top;
    if (QRect(QPoint(r.center().x()-m, r.bottom()-m), QSize(2*m,2*m)).contains(pos)) return Handle::Bottom;
    if (QRect(QPoint(r.left()-m,  r.center().y()-m),  QSize(2*m,2*m)).contains(pos)) return Handle::Left;
    if (QRect(QPoint(r.right()-m, r.center().y()-m),  QSize(2*m,2*m)).contains(pos)) return Handle::Right;
    return Handle::None;
}

void SnapOverlay::updateCursorForPos(const QPoint& pos) {
    if (state_ == OverlayState::Selected) {
        Handle h = hitTestHandle(pos);
        switch (h) {
            case Handle::TopLeft:
            case Handle::BottomRight: setCursor(Qt::SizeFDiagCursor); return;
            case Handle::TopRight:
            case Handle::BottomLeft:  setCursor(Qt::SizeBDiagCursor); return;
            case Handle::Top:
            case Handle::Bottom:      setCursor(Qt::SizeVerCursor);   return;
            case Handle::Left:
            case Handle::Right:       setCursor(Qt::SizeHorCursor);   return;
            default: break;
        }
        if (selectionRect_.contains(pos)) {
            if (toolbar_ && toolbar_->currentTool() != AnnotationType::None) {
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
    qDebug() << "PRESS state:" << int(state_) << "pos:" << event->pos()
             << "sel:" << selectionRect_ << "hover:" << hoverWindowRect_;

    if (event->button() == Qt::LeftButton) {
        if (state_ == OverlayState::Selected) {
            bool isLocked = !annotationLayer_.isEmpty() || (textInput_ && textInput_->isVisible());
            
            Handle h = isLocked ? Handle::None : hitTestHandle(event->pos());
            if (h != Handle::None) {
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
                        textInput_->startInput(event->pos(), toolbar_->currentSettings().color, toolbar_->currentSettings().fontSize);
                        return; // stay in Selected state, let TextInputWidget handle input
                    }
                    
                    state_ = OverlayState::Annotating;
                    activeAnnotation_.type = toolbar_->currentTool();
                    activeAnnotation_.color = toolbar_->currentSettings().color;
                    activeAnnotation_.lineWidth = toolbar_->currentSettings().lineWidth;
                    activeAnnotation_.mosaicSize = toolbar_->currentSettings().mosaicSize;
                    activeAnnotation_.points.clear();
                    activeAnnotation_.points.append(event->pos() - selectionRect_.normalized().topLeft());
                    
                    if (activeAnnotation_.type == AnnotationType::Mosaic) {
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
        state_ = OverlayState::Idle;
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
        } else if (activeAnnotation_.type == AnnotationType::Pen || activeAnnotation_.type == AnnotationType::Mosaic) {
            activeAnnotation_.points.append(relPos);
            if (activeAnnotation_.type == AnnotationType::Mosaic) {
                annotationLayer_.updateMosaic(activeAnnotation_);
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
        QRect r = startRect_;
        QPoint p = event->pos();
        switch (activeHandle_) {
            case Handle::TopLeft:     r.setTopLeft(p);     break;
            case Handle::Top:         r.setTop(p.y());     break;
            case Handle::TopRight:    r.setTopRight(p);    break;
            case Handle::Right:       r.setRight(p.x());   break;
            case Handle::BottomRight: r.setBottomRight(p); break;
            case Handle::Bottom:      r.setBottom(p.y());  break;
            case Handle::BottomLeft:  r.setBottomLeft(p);  break;
            case Handle::Left:        r.setLeft(p.x());    break;
            default: break;
        }
        selectionRect_ = r.normalized();
        update();
        return;
    }
    case OverlayState::Dragging: {
        if ((event->pos() - startPos_).manhattanLength() > 5) {
            selectionRect_ = QRect(startPos_, event->pos()).normalized();
        }
        update();
        return;
    }
    case OverlayState::Selected: {
        updateCursorForPos(event->pos());
        return;
    }
    case OverlayState::Idle: {
        lastHoverPos_ = event->globalPosition().toPoint();
        if (!hoverTimer_.isActive()) hoverTimer_.start();
        updateCursorForPos(event->pos());
        update();
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
            if ((event->pos() - startPos_).manhattanLength() <= 5) {
                // Clicked inside existing selection without moving
                copyToClipboard();
                close();
                emit closed();
            } else {
                state_ = OverlayState::Selected;
                annotationLayer_.setBaseImage(backgroundImage_, selectionRect_.normalized(), backgroundPixmap_.devicePixelRatio());
                showToolbar();
            }
            return;
        }
        if (state_ == OverlayState::Resizing) {
            state_ = OverlayState::Selected;
            annotationLayer_.setBaseImage(backgroundImage_, selectionRect_.normalized(), backgroundPixmap_.devicePixelRatio());
            showToolbar();
            return;
        }
        if (state_ == OverlayState::Dragging) {
            if ((event->pos() - startPos_).manhattanLength() > 5) {
                // Was a drag
                if (selectionRect_.width() >= 4 && selectionRect_.height() >= 4) {
                    state_ = OverlayState::Selected;
                    annotationLayer_.setBaseImage(backgroundImage_, selectionRect_.normalized(), backgroundPixmap_.devicePixelRatio());
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
                    annotationLayer_.setBaseImage(backgroundImage_, selectionRect_.normalized(), backgroundPixmap_.devicePixelRatio());
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
            toolbar_->setUndoEnabled(false);
            update();
        } else {
            close();
            emit closed();
        }
    } else if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Z) {
        handleUndo();
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

void SnapOverlay::copyToClipboard() {
    QRect currentSelection = selectionRect_.normalized();
    if (!currentSelection.isEmpty()) {
        qreal dpr = backgroundPixmap_.devicePixelRatio();
        QRect physicalRect(
            qRound(currentSelection.x() * dpr),
            qRound(currentSelection.y() * dpr),
            qRound(currentSelection.width() * dpr),
            qRound(currentSelection.height() * dpr)
        );
        physicalRect = physicalRect.intersected(backgroundPixmap_.rect());
        QPixmap selectedPixmap = backgroundPixmap_.copy(physicalRect);
        selectedPixmap.setDevicePixelRatio(dpr);
        QApplication::clipboard()->setPixmap(selectedPixmap);
    }
}

} // namespace qshot
