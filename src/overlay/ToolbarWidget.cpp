#include "ToolbarWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>
#include <QDebug>

namespace qshot {

// Define the colors
static const QVector<QColor> kColors = {
    QColor("#1E88E5"), // Blue
    QColor("#43A047"), // Green
    QColor("#FB8C00"), // Orange
    QColor("#000000"), // Black
    QColor("#FFFFFF"), // White
    QColor("#FDD835")  // Yellow
};

static const QVector<int> kLineWeights = { 2, 4, 6 }; // Thin, Medium, Thick
static const QVector<int> kMosaicSizes = { 8, 16, 24 };
static const QVector<int> kFontSizes = { 14, 18, 24 };

struct ButtonDef {
    AnnotationType type;
    QString action; // If empty, it's a tool. If type is None and action is not empty, it's an action.
    QString iconName;
    bool isSeparator = false;
};

static const QVector<ButtonDef> kButtons = {
    { AnnotationType::Rectangle, "", "Rect" },
    { AnnotationType::Ellipse, "", "Ellp" },
    { AnnotationType::Arrow, "", "Arrw" },
    { AnnotationType::Pen, "", "Pen" },
    { AnnotationType::Mosaic, "", "Mosc" },
    { AnnotationType::Text, "", "Text" },
    { AnnotationType::None, "", "", true }, // Separator
    { AnnotationType::None, "Undo", "Undo" },
    { AnnotationType::None, "Copy", "Copy" },
    { AnnotationType::None, "Save", "Save" },
    { AnnotationType::None, "Cancel", "Cncl" }
};

// ----------------------------------------------------------------------------
// SubPanelWidget (Color & Width Picker)
// ----------------------------------------------------------------------------
class ToolbarWidget::SubPanelWidget : public QWidget {
public:
    SubPanelWidget(ToolbarWidget* parentToolbar) 
        : QWidget(nullptr), parentToolbar_(parentToolbar) 
    {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        resize(240, 36);
    }

    void updateSelection(AnnotationType tool, QColor c, int s) {
        currentTool_ = tool;
        selectedColor_ = c;
        selectedSize_ = s;
        update();
    }

    QVector<int> currentSizes() const {
        if (currentTool_ == AnnotationType::Mosaic) return kMosaicSizes;
        if (currentTool_ == AnnotationType::Text) return kFontSizes;
        return kLineWeights;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Draw background
        QPainterPath path;
        path.addRoundedRect(rect(), 6, 6);
        p.fillPath(path, QColor(43, 43, 43, 230));

        // Draw sizes (left side)
        int xOffset = 12;
        int yCenter = height() / 2;
        
        auto sizes = currentSizes();
        for (int i = 0; i < sizes.size(); ++i) {
            int val = sizes[i];
            int displayR = 4 + i * 4; // visual size 4, 8, 12
            
            QRectF dotRect(xOffset, yCenter - displayR / 2.0, displayR, displayR);
            
            if (val == selectedSize_) {
                p.setBrush(QColor(255, 255, 255, 60)); // Highlight background
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(QRectF(xOffset - 4, yCenter - 14, displayR + 8, 28), 4, 4);
            }
            
            if (currentTool_ == AnnotationType::Text) {
                p.setPen(QColor(220, 220, 220));
                QFont f = p.font();
                f.setPixelSize(10 + i * 2);
                p.setFont(f);
                p.drawText(QRectF(xOffset - 4, yCenter - 14, displayR + 8, 28), Qt::AlignCenter, "T");
            } else if (currentTool_ == AnnotationType::Mosaic) {
                p.setBrush(QColor(220, 220, 220));
                p.setPen(Qt::NoPen);
                p.drawRect(dotRect); // Draw square for mosaic
            } else {
                p.setBrush(QColor(220, 220, 220));
                p.setPen(Qt::NoPen);
                p.drawEllipse(dotRect); // Draw circle for line
            }
            
            xOffset += displayR + 12;
        }

        // Separator
        p.setPen(QColor(100, 100, 100));
        p.drawLine(xOffset, 8, xOffset, height() - 8);
        xOffset += 12;

        // Draw colors (right side)
        for (int i = 0; i < kColors.size(); ++i) {
            QColor c = kColors[i];
            QRectF colorRect(xOffset, yCenter - 8, 16, 16);
            
            if (c == selectedColor_) {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(Qt::white, 2));
                p.drawEllipse(colorRect.adjusted(-3, -3, 3, 3));
            }
            
            p.setBrush(c);
            p.setPen(QPen(QColor(200, 200, 200), 1));
            p.drawEllipse(colorRect);
            
            xOffset += 24;
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        int xOffset = 12;
        int yCenter = height() / 2;
        
        // Check sizes
        auto sizes = currentSizes();
        for (int i = 0; i < sizes.size(); ++i) {
            int displayR = 4 + i * 4;
            QRect hitRect(xOffset - 4, yCenter - 14, displayR + 8, 28);
            if (hitRect.contains(e->pos())) {
                selectedSize_ = sizes[i];
                update();
                emit parentToolbar_->sizeSettingChanged(selectedSize_);
                return;
            }
            xOffset += displayR + 12;
        }
        
        xOffset += 12; // separator

        // Check colors
        for (int i = 0; i < kColors.size(); ++i) {
            QRect hitRect(xOffset - 4, yCenter - 12, 24, 24);
            if (hitRect.contains(e->pos())) {
                selectedColor_ = kColors[i];
                update();
                emit parentToolbar_->colorChanged(selectedColor_);
                return;
            }
            xOffset += 24;
        }
    }

private:
    ToolbarWidget* parentToolbar_;
    AnnotationType currentTool_ = AnnotationType::None;
    QColor selectedColor_ = kColors[2];
    int selectedSize_ = kLineWeights[1];
};

// ----------------------------------------------------------------------------
// ToolbarWidget
// ----------------------------------------------------------------------------

ToolbarWidget::ToolbarWidget(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    int width = padding_ * 2;
    for (const auto& b : kButtons) {
        if (b.isSeparator) {
            width += spacing_ * 2; // extra space for separator
        } else {
            width += buttonSize_ + spacing_;
        }
    }
    width -= spacing_; // remove last spacing
    
    resize(width, 40);

    // Initialize tool settings
    toolSettings_[AnnotationType::Rectangle] = ToolSettings();
    toolSettings_[AnnotationType::Ellipse] = ToolSettings();
    toolSettings_[AnnotationType::Arrow] = ToolSettings();
    toolSettings_[AnnotationType::Pen] = ToolSettings();
    toolSettings_[AnnotationType::Mosaic] = ToolSettings();
    toolSettings_[AnnotationType::Text] = ToolSettings();
    
    subPanel_ = new SubPanelWidget(this);
    connect(this, &ToolbarWidget::colorChanged, this, [this](QColor c) {
        if (currentTool_ != AnnotationType::None) {
            toolSettings_[currentTool_].color = c;
            update();
        }
    });
    connect(this, &ToolbarWidget::sizeSettingChanged, this, [this](int s) {
        if (currentTool_ != AnnotationType::None) {
            if (currentTool_ == AnnotationType::Mosaic) {
                toolSettings_[currentTool_].mosaicSize = s;
            } else if (currentTool_ == AnnotationType::Text) {
                toolSettings_[currentTool_].fontSize = s;
            } else {
                toolSettings_[currentTool_].lineWidth = s;
            }
            update();
        }
    });
}

ToolbarWidget::~ToolbarWidget() {
    if (subPanel_) {
        delete subPanel_;
    }
}

ToolSettings ToolbarWidget::currentSettings() const {
    if (toolSettings_.contains(currentTool_)) {
        return toolSettings_[currentTool_];
    }
    return ToolSettings();
}

void ToolbarWidget::updatePosition(const QRect& selectionRect, const QRect& screenRect) {
    int targetX = selectionRect.center().x() - width() / 2;
    int targetY = selectionRect.bottom() + 8;

    // Check bottom boundary
    if (targetY + height() > screenRect.bottom()) {
        targetY = selectionRect.top() - height() - 8;
        if (targetY < screenRect.top()) {
            targetY = selectionRect.bottom() - height(); // Inside selection if really no space
        }
    }

    // Check horizontal boundaries
    if (targetX < screenRect.left()) targetX = screenRect.left() + 8;
    if (targetX + width() > screenRect.right()) targetX = screenRect.right() - width() - 8;

    move(targetX, targetY);

    if (currentTool_ != AnnotationType::None) {
        showSubPanel();
    }
}

void ToolbarWidget::showSubPanel() {
    if (!subPanel_) return;
    
    // Position subpanel above toolbar
    int targetX = x() + width() / 2 - subPanel_->width() / 2;
    int targetY = y() - subPanel_->height() - 8;
    
    // Quick boundary check
    if (targetY < 0) {
        targetY = y() + height() + 8; // move below if no space
    }
    
    subPanel_->move(targetX, targetY);
    int currentSize = currentSettings().lineWidth;
    if (currentTool_ == AnnotationType::Mosaic) currentSize = currentSettings().mosaicSize;
    if (currentTool_ == AnnotationType::Text) currentSize = currentSettings().fontSize;
    subPanel_->updateSelection(currentTool_, currentSettings().color, currentSize);
    subPanel_->show();
}

void ToolbarWidget::hideSubPanel() {
    if (subPanel_) {
        subPanel_->hide();
    }
}

void ToolbarWidget::setUndoEnabled(bool enabled) {
    if (undoEnabled_ != enabled) {
        undoEnabled_ = enabled;
        update();
    }
}

void ToolbarWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect(), 6, 6);
    p.fillPath(path, QColor(43, 43, 43, 230));

    int currentX = padding_;
    
    for (const auto& b : kButtons) {
        if (b.isSeparator) {
            currentX += spacing_;
            p.setPen(QColor(100, 100, 100));
            p.drawLine(currentX, 10, currentX, height() - 10);
            currentX += spacing_;
            continue;
        }
        
        QRect btnRect(currentX, (height() - buttonSize_) / 2, buttonSize_, buttonSize_);
        bool isHovered = btnRect.contains(hoverPos_);
        bool isSelected = (b.type != AnnotationType::None && b.type == currentTool_);
        
        // Disabled everything except Rect, Pen, Ellipse, Arrow, Mosaic, Text, Undo, Copy, Save, Cancel
        bool isDisabled = false;
        
        if (b.action == "Undo" && !undoEnabled_) {
            isDisabled = true;
        }
        
        drawButton(p, btnRect, b.type, !b.action.isEmpty(), b.iconName, isHovered, isSelected, isDisabled);
        
        currentX += buttonSize_ + spacing_;
    }
}

void ToolbarWidget::drawButton(QPainter& p, const QRect& rect, AnnotationType toolType, bool isAction, const QString& iconName, bool isHovered, bool isSelected, bool isDisabled) {
    if (isSelected) {
        p.setBrush(QColor(255, 255, 255, 40));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect, 4, 4);
    } else if (isHovered && !isDisabled) {
        p.setBrush(QColor(255, 255, 255, 20));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect, 4, 4);
    }

    QColor iconColor = isDisabled ? QColor(150, 150, 150) : Qt::white;
    if (toolType != AnnotationType::None && isSelected && toolSettings_.contains(toolType)) {
        iconColor = toolSettings_[toolType].color; // Icon assumes current color when selected
    }

    p.setPen(QPen(iconColor, 2));
    p.setBrush(Qt::NoBrush);

    QRect inner = rect.adjusted(6, 6, -6, -6);

    // Geometry placeholders
    if (iconName == "Rect") {
        p.drawRect(inner);
    } else if (iconName == "Ellp") {
        p.drawEllipse(inner);
    } else if (iconName == "Arrw") {
        p.drawLine(inner.bottomLeft(), inner.topRight());
        p.drawLine(inner.topRight(), inner.topRight() + QPoint(-4, 0));
        p.drawLine(inner.topRight(), inner.topRight() + QPoint(0, 4));
    } else if (iconName == "Pen") {
        QPainterPath pth;
        pth.moveTo(inner.bottomLeft());
        pth.quadTo(inner.center(), inner.topRight());
        p.drawPath(pth);
    } else if (iconName == "Mosc") {
        // Just draw a checkerboard-like icon
        p.drawRect(inner.x(), inner.y(), inner.width()/2, inner.height()/2);
        p.drawRect(inner.center().x(), inner.center().y(), inner.width()/2, inner.height()/2);
    } else if (iconName == "Text") {
        p.drawText(rect, Qt::AlignCenter, "T");
    } else {
        // Actions (Undo, Copy, Save, Cncl)
        p.setFont(QFont("Arial", 8));
        p.drawText(rect, Qt::AlignCenter, iconName);
    }
}

void ToolbarWidget::mouseMoveEvent(QMouseEvent* event) {
    hoverPos_ = event->pos();
    update();
}

void ToolbarWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    
    int currentX = padding_;
    for (const auto& b : kButtons) {
        if (b.isSeparator) {
            currentX += spacing_ * 2;
            continue;
        }
        
        QRect btnRect(currentX, (height() - buttonSize_) / 2, buttonSize_, buttonSize_);
        if (btnRect.contains(event->pos())) {
            if (b.action == "Undo" && !undoEnabled_) {
                return;
            }
            
            if (!b.action.isEmpty()) {
                handleActionClick(b.action);
            } else {
                handleToolClick(b.type);
            }
            return;
        }
        
        currentX += buttonSize_ + spacing_;
    }
}

void ToolbarWidget::mouseReleaseEvent(QMouseEvent* /*event*/) {
    // nothing
}

void ToolbarWidget::handleToolClick(AnnotationType type) {
    if (currentTool_ == type) {
        // Toggle off
        currentTool_ = AnnotationType::None;
        hideSubPanel();
    } else {
        currentTool_ = type;
        if (currentTool_ != AnnotationType::None) {
            showSubPanel();
        } else {
            hideSubPanel();
        }
    }
    update();
    emit toolSelected(currentTool_);
}

void ToolbarWidget::handleActionClick(const QString& action) {
    if (action == "Undo") emit undoRequested();
    else if (action == "Copy") emit copyRequested();
    else if (action == "Save") emit saveRequested();
    else if (action == "Cancel") emit cancelRequested();
}

} // namespace qshot
