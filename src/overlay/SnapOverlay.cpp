#include "SnapOverlay.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QPainterPath>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QtMath>

#include "core/PlatformFactory.h"
#include "core/Settings.h"
#include "core/Strings.h"

namespace qshot {

namespace {
// Magnifier / mouse-info panel tuning. Shared by paintEvent() and magnifierLayout()
// so that the drawn panel and the repainted region can never disagree.
constexpr int kMagMinSize       = 140; // minimum magnifier edge length, logical px
constexpr int kMagZoom          = 4;   // magnification factor
constexpr int kMagPadding       = 8;
constexpr int kMagLineSpacing   = 4;
constexpr int kMagColorBlockSize = 12;
constexpr int kMagFontPixelSize = 12;
constexpr int kMagCursorGap     = 12;  // gap between cursor and panel
constexpr int kMagRowCount      = 3;   // RGB / HEX / POS

// "click to type" badge shown next to the cursor while the text tool is armed
constexpr int kHintFontPixelSize = 12;
constexpr int kHintPadding       = 6;

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
        QRect newHover = detector_->windowRectAt(lastHoverPos_);
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
        if (saveToFile()) {
            close();
            emit closed();
        }
    });
    
    textInput_ = new TextInputWidget(this);
    textInput_->hide();
    connect(textInput_, &TextInputWidget::editingFinished, this, [this](const QString& text) {
        if (!text.isEmpty()) {
            Annotation a;
            a.type = AnnotationType::Text;
            a.color = toolbar_->currentSettings().color;
            a.fontSize = toolbar_->currentSettings().fontSize;
            a.points.append(textInput_->pos() - selectionRect_.normalized().topLeft());
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
    toolbar_->updatePosition(selectionRect_.normalized(), rect());
    toolbar_->show();
    reclaimKeyboardFocus();
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
            // Reuse the selection computed at the top of paintEvent().
            if (!currentSelection.isEmpty()) {
                relPos = currentMousePos_ - currentSelection.topLeft();
            }
        }
        
        // 2. Sample pixel color
        qreal dpr = dpr_;
        QPoint scaledPos(qRound(currentMousePos_.x() * dpr), qRound(currentMousePos_.y() * dpr));
        
        QColor pixelColor = Qt::black;
        if (backgroundImage_.valid(scaledPos)) {
            pixelColor = backgroundImage_.pixelColor(scaledPos);
        }
        
        // 3. Prepare text and panel layout
        const int padding = kMagPadding;
        const int lineSpacing = kMagLineSpacing;
        const int colorBlockSize = kMagColorBlockSize;
        
        QString rgbText = QString("RGB: (%1, %2, %3)").arg(pixelColor.red()).arg(pixelColor.green()).arg(pixelColor.blue());
        QString hexText = QString("HEX: %1").arg(pixelColor.name().toUpper());
        QString coordText = QString("POS: %1, %2").arg(relPos.x()).arg(relPos.y());
        
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPixelSize(kMagFontPixelSize);
        QFontMetrics fm(font);
        
        // Panel geometry is shared with mouseMoveEvent() so that partial repaints
        // always cover exactly what is drawn here.
        const MagnifierLayout layout = magnifierLayout(currentMousePos_);
        const int boxX = layout.panel.x();
        const int boxY = layout.panel.y();
        const int boxWidth = layout.panel.width();
        const int boxHeight = layout.panel.height();
        const int finalMagSize = layout.magHeight;
        const int srcPhysicalW = layout.srcPhysicalW;
        const int srcPhysicalH = layout.srcPhysicalH;
        const int magPhysicalW = srcPhysicalW * kMagZoom;
        const int magPhysicalH = srcPhysicalH * kMagZoom;
        
        // 4. Draw background with rounded corners and no outer border
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
        
        // 5. Draw magnifier
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
        
        // 6. Draw contents (Text area)
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

    // "Click to type" badge: with the text tool armed, clicking inside the selection
    // is the only way to start typing, and that is not discoverable on its own.
    if (shouldShowTextHint()) {
        const QRect hint = textHintRect(currentMousePos_);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPainterPath hintPath;
        hintPath.addRoundedRect(hint, 4, 4);
        painter.fillPath(hintPath, QColor(26, 173, 25, 235));

        QFont hintFont = painter.font();
        hintFont.setPixelSize(kHintFontPixelSize);
        painter.setFont(hintFont);
        painter.setPen(Qt::white);
        painter.drawText(hint, Qt::AlignCenter, textHintString());
        painter.restore();
    }
}

SnapOverlay::MagnifierLayout SnapOverlay::magnifierLayout(const QPoint& mousePos) const {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(kMagFontPixelSize);
    const QFontMetrics fm(font);

    // Worst-case text widths: three RGB components and five-digit coordinates.
    // Using a fixed budget (instead of the currently sampled values) keeps the panel
    // from resizing as the cursor moves, which is both steadier to look at and what
    // makes the rect usable as a repaint region.
    const int rgbWidth  = fm.horizontalAdvance(QStringLiteral("RGB: (255, 255, 255)"));
    const int hexWidth  = kMagColorBlockSize + 4 + fm.horizontalAdvance(QStringLiteral("HEX: #FFFFFF"));
    const int posWidth  = fm.horizontalAdvance(QStringLiteral("POS: -99999, -99999"));
    const int textWidth = qMax(qMax(rgbWidth, hexWidth), posWidth);

    const int initialBoxWidth = qMax(kMagMinSize, textWidth + kMagPadding * 2);

    // To avoid subpixel alignment issues we crop the physical image, scale it with
    // nearest neighbour, and force the centre physical pixel onto the crosshair.
    int srcPhysicalW = qRound(initialBoxWidth * dpr_ / kMagZoom);
    if (srcPhysicalW % 2 == 0) ++srcPhysicalW; // odd width => true centre pixel
    int srcPhysicalH = qRound(kMagMinSize * dpr_ / kMagZoom);
    if (srcPhysicalH % 2 == 0) ++srcPhysicalH; // odd height => true centre pixel

    MagnifierLayout layout;
    layout.srcPhysicalW = srcPhysicalW;
    layout.srcPhysicalH = srcPhysicalH;
    layout.magHeight = qCeil(srcPhysicalH * kMagZoom / dpr_);
    const int boxWidth = qCeil(srcPhysicalW * kMagZoom / dpr_);
    const int boxHeight = layout.magHeight + kMagPadding * 2
                        + fm.height() * kMagRowCount + kMagLineSpacing * (kMagRowCount - 1);

    int boxX = mousePos.x() + kMagCursorGap;
    int boxY = mousePos.y() + kMagCursorGap;
    if (boxX + boxWidth > rect().right()) {
        boxX = mousePos.x() - kMagCursorGap - boxWidth;
    }
    if (boxY + boxHeight > rect().bottom()) {
        boxY = mousePos.y() - kMagCursorGap - boxHeight;
    }

    layout.panel = QRect(boxX, boxY, boxWidth, boxHeight);
    return layout;
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
    QFont font = QApplication::font();
    font.setPixelSize(kHintFontPixelSize);
    const QFontMetrics fm(font);
    const int w = fm.horizontalAdvance(textHintString()) + kHintPadding * 2;
    const int h = fm.height() + kHintPadding;

    // Same flip logic as the magnifier panel, so the badge never leaves the screen.
    int x = mousePos.x() + kMagCursorGap;
    int y = mousePos.y() + kMagCursorGap;
    if (x + w > rect().right()) {
        x = mousePos.x() - kMagCursorGap - w;
    }
    if (y + h > rect().bottom()) {
        y = mousePos.y() - kMagCursorGap - h;
    }
    return QRect(x, y, w, h);
}

Handle SnapOverlay::hitTestHandle(const QPoint& pos) const {
    if (selectionRect_.isEmpty()) return Handle::None;

    const int m = 8;
    const QRect r = selectionRect_;

    // Every handle is a 2m x 2m square centred on its anchor point. Keep it that way
    // for all eight handles -- off-centre hit areas are invisible but very annoying.
    const auto hits = [&pos, m](const QPoint& anchor) {
        return QRect(anchor - QPoint(m, m), QSize(2 * m, 2 * m)).contains(pos);
    };

    if (hits(r.topLeft()))     return Handle::TopLeft;
    if (hits(r.topRight()))    return Handle::TopRight;
    if (hits(r.bottomLeft()))  return Handle::BottomLeft;
    if (hits(r.bottomRight())) return Handle::BottomRight;
    if (hits(QPoint(r.center().x(), r.top())))    return Handle::Top;
    if (hits(QPoint(r.center().x(), r.bottom()))) return Handle::Bottom;
    if (hits(QPoint(r.left(),  r.center().y())))  return Handle::Left;
    if (hits(QPoint(r.right(), r.center().y())))  return Handle::Right;
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
            QRect dirty = magnifierLayout(oldMousePos).panel
                              .united(magnifierLayout(currentMousePos_).panel);
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
            if ((event->pos() - startPos_).manhattanLength() > 5) {
                // Was a drag
                if (selectionRect_.width() >= 4 && selectionRect_.height() >= 4) {
                    state_ = OverlayState::Selected;
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

QRect SnapOverlay::physicalSelectionRect() const {
    const QRect currentSelection = selectionRect_.normalized();
    if (currentSelection.isEmpty()) return QRect();

    QRect physicalRect(
        qRound(currentSelection.x() * dpr_),
        qRound(currentSelection.y() * dpr_),
        qRound(currentSelection.width() * dpr_),
        qRound(currentSelection.height() * dpr_)
    );
    return physicalRect.intersected(backgroundImage_.rect());
}

void SnapOverlay::copyToClipboard() {
    const QRect physicalRect = physicalSelectionRect();
    if (physicalRect.isEmpty()) return;

    const QImage basePhysical = backgroundImage_.copy(physicalRect);
    const QImage finalImage = annotationLayer_.renderToImage(basePhysical);
    // QPixmap::fromImage() carries the device pixel ratio over from finalImage
    // (verified with a probe), so an extra setDevicePixelRatio() is not needed.
    QPixmap selectedPixmap = QPixmap::fromImage(finalImage);
    QApplication::clipboard()->setPixmap(selectedPixmap);
}

bool SnapOverlay::saveToFile() {
    const QRect physicalRect = physicalSelectionRect();
    if (physicalRect.isEmpty()) return false;

    const Settings& settings = Settings::instance();
    const bool preferJpeg = settings.saveFormat() == SaveFormat::Jpeg;
    const QString pngFilter = text(Str::SaveFilterPng);
    const QString jpegFilter = text(Str::SaveFilterJpeg);

    QString defaultPath = settings.saveDirectory();
    if (defaultPath.isEmpty()) {
        defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    if (defaultPath.isEmpty()) defaultPath = QDir::homePath();
    defaultPath += preferJpeg ? QStringLiteral("/screenshot.jpg")
                              : QStringLiteral("/screenshot.png");

    // The configured format is listed first so the dialog preselects it.
    const QString filters = preferJpeg ? (jpegFilter + QStringLiteral(";;") + pngFilter)
                                       : (pngFilter + QStringLiteral(";;") + jpegFilter);

    // Parented to this overlay on purpose: the overlay is an always-on-top
    // fullscreen tool window, so an unowned dialog could end up behind it.
    QString selectedFilter;
    QString filePath = QFileDialog::getSaveFileName(
        this,
        text(Str::SaveDialogTitle),
        defaultPath,
        filters,
        &selectedFilter);
    if (filePath.isEmpty()) return false; // cancelled

    // Non-native dialogs do not append an extension for us.
    if (QFileInfo(filePath).suffix().isEmpty()) {
        // Honour the filter the user actually picked; the configured format is
        // only the preselected default.
        bool asJpeg = preferJpeg;
        if (selectedFilter == pngFilter)  asJpeg = false;
        if (selectedFilter == jpegFilter) asJpeg = true;
        filePath += asJpeg ? QStringLiteral(".jpg") : QStringLiteral(".png");
    }

    const QImage basePhysical = backgroundImage_.copy(physicalRect);
    const QImage finalImage = annotationLayer_.renderToImage(basePhysical);

    // Derive the encoder from the final extension, not from the setting: the user
    // may have typed a different one in the dialog.
    const bool saveAsJpeg = filePath.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
                            || filePath.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive);

    if (!finalImage.save(filePath,
                         saveAsJpeg ? "JPEG" : "PNG",
                         saveAsJpeg ? settings.jpegQuality() : -1)) {
        QMessageBox::warning(this, text(Str::SaveFailedTitle),
                             text(Str::SaveFailedBody).arg(filePath));
        return false; // keep the overlay so the user does not lose their work
    }
    return true;
}

} // namespace qshot
