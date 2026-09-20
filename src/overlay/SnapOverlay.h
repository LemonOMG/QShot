#pragma once

#include <QWidget>
#include <QPixmap>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QFontDatabase>
#include <memory>
#include "core/IWindowDetector.h"
#include "ToolbarWidget.h"
#include "TextInputWidget.h"
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

enum class Handle {
    None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left
};

class SnapOverlay : public QWidget {
    Q_OBJECT
public:
    explicit SnapOverlay(const QPixmap& background, const QRect& screenGeometry, QWidget* parent = nullptr);
    ~SnapOverlay() override;

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Layout of the floating magnifier / mouse-info panel.
    // Width is derived from worst-case text widths (RGB triplets, 5-digit coordinates)
    // so the result is stable while the mouse moves over different pixels. That makes
    // it safe to use as a superset when computing partial repaint regions.
    struct MagnifierLayout {
        QRect panel;              // full panel, logical widget coordinates
        int magHeight = 0;        // height of the magnifier part (upper section of panel)
        int srcPhysicalW = 0;     // physical size of the magnified source crop (always odd)
        int srcPhysicalH = 0;
    };
    MagnifierLayout magnifierLayout(const QPoint& mousePos) const;

    Handle hitTestHandle(const QPoint& pos) const;
    void updateCursorForPos(const QPoint& pos);
    void copyToClipboard();
    bool saveToFile();
    // Current selection mapped to physical pixels of backgroundImage_, clipped to it.
    QRect physicalSelectionRect() const;
    
    QImage backgroundImage_;
    qreal dpr_ = 1.0;
    QPoint startPos_;
    QRect startRect_;
    QRect selectionRect_;
    QRect hoverWindowRect_;
    
    QPoint currentMousePos_;
    bool isMouseValid_;
    
    OverlayState state_ = OverlayState::Idle;
    Handle activeHandle_ = Handle::None;
    
    std::unique_ptr<IWindowDetector> detector_;
    QTimer hoverTimer_;
    QPoint lastHoverPos_;
    
    // Annotation Phase A & B
    ToolbarWidget* toolbar_ = nullptr;
    TextInputWidget* textInput_ = nullptr;
    AnnotationLayer annotationLayer_;
    Annotation activeAnnotation_;
    
    void showToolbar();
    void hideToolbar();
    void handleToolSelection(AnnotationType type);
    void finishAnnotation();
    void handleUndo();
};

} // namespace qshot
