#pragma once

#include <QWidget>
#include <QColor>
#include <QMap>
#include "../annotation/AnnotationType.h"
#include "../annotation/ToolSettings.h"

namespace qshot {

class ToolbarWidget : public QWidget {
    Q_OBJECT
public:
    explicit ToolbarWidget(QWidget* parent = nullptr);
    ~ToolbarWidget() override;

    // Get current settings
    AnnotationType currentTool() const { return currentTool_; }
    ToolSettings currentSettings() const;
    
    void setUndoEnabled(bool enabled);

    // Positions the toolbar under the selection, or above it when there is no room
    // below.
    //
    // Both rectangles are in *global desktop* coordinates. The toolbar is a
    // top-level window, so move() takes global coordinates, and the boundary test
    // has to be made against the screen the selection is actually on. Passing the
    // parent overlay's local coordinates works only while the screen origin is
    // (0,0), which is why it went unnoticed.
    void updatePosition(const QRect& globalSelectionRect, const QRect& screenRect);

    void hideSubPanel(); // Public for SnapOverlay to cascade hide

    void handleToolClick(AnnotationType type);
    void handleActionClick(const QString& action);

signals:
    void toolSelected(AnnotationType type);
    void colorChanged(QColor color);
    void sizeSettingChanged(int size); // width, mosaic size, or font size
    
    void undoRequested();
    void copyRequested();
    void saveRequested();
    void pinRequested();
    void cancelRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // The tool glyph comes from toolbaricons::paintTool, keyed by `toolType`. `label` is the
    // caption for action buttons (already translated by the caller); it is empty for the
    // icon-only tool buttons.
    void drawButton(QPainter& p, const QRect& rect, AnnotationType toolType,
                    const QString& label,
                    bool isHovered, bool isSelected, bool isDisabled = false);
    AnnotationType currentTool_ = AnnotationType::None; // Default to None
    QMap<AnnotationType, ToolSettings> toolSettings_;
    
    // UI Layout
    int buttonSize_ = 32;
    int spacing_ = 4;
    int padding_ = 6;
    
    // Interaction
    QPoint hoverPos_ = QPoint(-1, -1);
    bool undoEnabled_ = false;

    // The screen the toolbar was last placed on, in global coordinates. Remembered
    // so showSubPanel() can keep the panel inside it without being told again.
    QRect screenRect_;
    
    // Sub-panels (Color/Width picker)
    class SubPanelWidget;
    SubPanelWidget* subPanel_ = nullptr;
    
    void showSubPanel();

};

} // namespace qshot
