#pragma once

#include <QWidget>
#include <QColor>
#include <QMap>
#include "../annotation/AnnotationType.h"

namespace qshot {

struct ToolSettings {
    QColor color = QColor(251, 140, 0); // Orange default #FB8C00
    int lineWidth = 4; // Medium default
    int mosaicSize = 16;
    int fontSize = 18;
};

class ToolbarWidget : public QWidget {
    Q_OBJECT
public:
    explicit ToolbarWidget(QWidget* parent = nullptr);
    ~ToolbarWidget() override;

    // Get current settings
    AnnotationType currentTool() const { return currentTool_; }
    ToolSettings currentSettings() const;
    
    void setUndoEnabled(bool enabled);

    // To position the toolbar relative to the selection rect
    void updatePosition(const QRect& selectionRect, const QRect& screenRect);

    void handleToolClick(AnnotationType type);
    void handleActionClick(const QString& action);

signals:
    void toolSelected(AnnotationType type);
    void colorChanged(QColor color);
    void sizeSettingChanged(int size); // width, mosaic size, or font size
    
    void undoRequested();
    void copyRequested();
    void saveRequested();
    void cancelRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void drawButton(QPainter& p, const QRect& rect, AnnotationType toolType, bool isAction, const QString& iconName, bool isHovered, bool isSelected, bool isDisabled = false);
    AnnotationType currentTool_ = AnnotationType::None; // Default to None
    QMap<AnnotationType, ToolSettings> toolSettings_;
    
    // UI Layout
    int buttonSize_ = 32;
    int spacing_ = 4;
    int padding_ = 6;
    
    // Interaction
    QPoint hoverPos_ = QPoint(-1, -1);
    bool undoEnabled_ = false;
    
    // Sub-panels (Color/Width picker)
    class SubPanelWidget;
    SubPanelWidget* subPanel_ = nullptr;
    
    void showSubPanel();
    void hideSubPanel();
};

} // namespace qshot
