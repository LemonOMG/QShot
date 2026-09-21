#include "ToolbarWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>
#include <QDebug>

#include "../core/Settings.h"
#include "../core/Strings.h"

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
    QString iconName; // Geometry to draw; empty for action buttons, which are labelled with text.
    Str label = Str::Count; // Translated caption for action buttons; Count when the button draws an icon.
    bool isSeparator = false;
};

static const QVector<ButtonDef> kButtons = {
    { AnnotationType::Rectangle, "", "Rect" },
    { AnnotationType::Ellipse, "", "Ellp" },
    { AnnotationType::Arrow, "", "Arrw" },
    { AnnotationType::Pen, "", "Pen" },
    { AnnotationType::Mosaic, "", "Mosc" },
    { AnnotationType::Text, "", "Text" },
    { AnnotationType::None, "", "", Str::Count, true }, // Separator
    { AnnotationType::None, "Undo", "", Str::ToolbarUndo },
    { AnnotationType::None, "Copy", "", Str::ToolbarCopy },
    { AnnotationType::None, "Save", "", Str::ToolbarSave },
    { AnnotationType::None, "Cancel", "", Str::ToolbarCancel }
};

// ----------------------------------------------------------------------------
// SubPanelWidget (Size & Colour Picker)
//
// The controls depend on the active tool:
//   - rectangle / ellipse / arrow / pen : stroke width + colour
//   - text                              : font size + colour
//   - mosaic                            : block size only (a mosaic has no colour,
//                                         so the colour row is not shown at all)
// All geometry comes from computeLayout(), shared by paintEvent() and
// mousePressEvent(), so the drawn controls and their hit areas cannot drift apart.
// ----------------------------------------------------------------------------
namespace {
constexpr int kSubPanelH   = 36;
constexpr int kSubPanelPad = 12;
constexpr int kSizeChip    = 28;  // one clickable square per size option
constexpr int kSizeChipGap = 4;
constexpr int kSepGap      = 6;
constexpr int kColorDot    = 16;
constexpr int kColorGap    = 8;
} // namespace

class ToolbarWidget::SubPanelWidget : public QWidget {
public:
    SubPanelWidget(ToolbarWidget* parentToolbar) 
        : QWidget(nullptr), parentToolbar_(parentToolbar) 
    {
        // WindowDoesNotAcceptFocus: the panel is display-only, so it must never take
        // activation away from the overlay (clicking it would otherwise kill
        // Esc / Enter / Ctrl+Z).
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        resize(computeLayout().contentWidth, kSubPanelH);
    }

    void updateSelection(AnnotationType tool, QColor c, int s) {
        const bool toolChanged = (currentTool_ != tool);
        currentTool_ = tool;
        selectedColor_ = c;
        selectedSize_ = s;
        if (toolChanged) {
            // Different tools expose a different number of controls, so the panel
            // has to resize itself (mosaic loses the whole colour row).
            resize(computeLayout().contentWidth, kSubPanelH);
        }
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

        const Layout layout = computeLayout();
        const QVector<int> sizes = currentSizes();

        // Size options (left side)
        for (int i = 0; i < layout.sizeChips.size(); ++i) {
            const QRectF chip = layout.sizeChips[i];
            const bool selected = (sizes[i] == selectedSize_);
            const QColor ink = selected ? QColor(255, 255, 255) : QColor(200, 200, 200);

            if (selected) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 60));
                p.drawRoundedRect(chip, 4, 4);
            }
            p.setPen(ink);
            p.setBrush(ink);
            paintSizeGlyph(p, chip, sizes[i], i);
        }

        if (layout.hasColors) {
            // Separator
            p.setPen(QColor(100, 100, 100));
            p.drawLine(QPointF(layout.separator.x(), 8),
                       QPointF(layout.separator.x(), height() - 8));

            // Colours (right side)
            for (int i = 0; i < layout.colorDots.size(); ++i) {
                const QRectF dot = layout.colorDots[i];
                const QColor c = kColors[i];
                if (c == selectedColor_) {
                    p.setBrush(Qt::NoBrush);
                    p.setPen(QPen(Qt::white, 2));
                    p.drawEllipse(dot.adjusted(-3, -3, 3, 3));
                }
                p.setBrush(c);
                p.setPen(QPen(QColor(200, 200, 200), 1));
                p.drawEllipse(dot);
            }
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        const Layout layout = computeLayout();
        const QVector<int> sizes = currentSizes();

        for (int i = 0; i < layout.sizeChips.size(); ++i) {
            if (layout.sizeChips[i].contains(e->pos())) {
                selectedSize_ = sizes[i];
                update();
                emit parentToolbar_->sizeSettingChanged(selectedSize_);
                return;
            }
        }

        for (int i = 0; i < layout.colorDots.size(); ++i) {
            if (layout.colorDots[i].adjusted(-4, -4, 4, 4).contains(e->pos())) {
                selectedColor_ = kColors[i];
                update();
                emit parentToolbar_->colorChanged(selectedColor_);
                return;
            }
        }
    }

private:
    struct Layout {
        QVector<QRectF> sizeChips;
        QVector<QRectF> colorDots;
        QRectF separator;          // vertical divider between sizes and colours
        bool hasColors = false;
        int contentWidth = 0;
    };

    // A mosaic has no colour, so that tool shows the size row only.
    bool hasColorRow() const { return currentTool_ != AnnotationType::Mosaic; }

    Layout computeLayout() const {
        Layout l;
        const QVector<int> sizes = currentSizes();

        int x = kSubPanelPad;
        for (int i = 0; i < sizes.size(); ++i) {
            l.sizeChips.append(QRectF(x, (kSubPanelH - kSizeChip) / 2.0, kSizeChip, kSizeChip));
            x += kSizeChip + kSizeChipGap;
        }
        x -= kSizeChipGap;

        if (hasColorRow()) {
            l.hasColors = true;
            x += kSepGap;
            l.separator = QRectF(x, 0, 1, kSubPanelH);
            x += 1 + kSepGap;
            for (int i = 0; i < kColors.size(); ++i) {
                l.colorDots.append(QRectF(x, (kSubPanelH - kColorDot) / 2.0, kColorDot, kColorDot));
                x += kColorDot + kColorGap;
            }
            x -= kColorGap;
        }

        l.contentWidth = x + kSubPanelPad;
        return l;
    }

    void paintSizeGlyph(QPainter& p, const QRectF& chip, int sizeValue, int index) const {
        if (currentTool_ == AnnotationType::Text) {
            // Spell the pixel size out: three bare "T" glyphs read as decoration,
            // not as a font-size selector.
            QFont f = p.font();
            f.setPixelSize(11);
            p.setFont(f);
            p.drawText(chip, Qt::AlignCenter, QString::number(sizeValue));
            return;
        }

        const qreal side = 6 + index * 5;
        const QRectF glyph(chip.center().x() - side / 2.0, chip.center().y() - side / 2.0,
                           side, side);
        if (currentTool_ == AnnotationType::Mosaic) {
            p.drawRect(glyph);      // square for mosaic blocks
        } else {
            p.drawEllipse(glyph);   // circle for stroke width
        }
    }

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
    // Display-only window: it must not take activation away from the overlay.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowDoesNotAcceptFocus);
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

    // Load tool settings. Settings::toolSettings() falls back to the shipped
    // defaults, so this is correct whether or not anything has been stored yet.
    const AnnotationType tools[] = {
        AnnotationType::Rectangle, AnnotationType::Ellipse, AnnotationType::Arrow,
        AnnotationType::Pen, AnnotationType::Mosaic, AnnotationType::Text
    };
    for (AnnotationType type : tools) {
        toolSettings_[type] = Settings::instance().toolSettings(type);
    }
    
    subPanel_ = new SubPanelWidget(this);
    connect(this, &ToolbarWidget::colorChanged, this, [this](QColor c) {
        if (currentTool_ != AnnotationType::None) {
            toolSettings_[currentTool_].color = c;
            Settings::instance().setToolSettings(currentTool_, toolSettings_[currentTool_]);
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
            Settings::instance().setToolSettings(currentTool_, toolSettings_[currentTool_]);
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

    int currentSize = currentSettings().lineWidth;
    if (currentTool_ == AnnotationType::Mosaic) currentSize = currentSettings().mosaicSize;
    if (currentTool_ == AnnotationType::Text) currentSize = currentSettings().fontSize;

    // Update before positioning: a tool change resizes the panel, and the
    // centring below depends on its final width.
    subPanel_->updateSelection(currentTool_, currentSettings().color, currentSize);

    // Position subpanel above toolbar
    int targetX = x() + width() / 2 - subPanel_->width() / 2;
    int targetY = y() - subPanel_->height() - 8;

    // Quick boundary check
    if (targetY < 0) {
        targetY = y() + height() + 8; // move below if no space
    }

    subPanel_->move(targetX, targetY);
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
        
        drawButton(p, btnRect, b.type, b.iconName,
                   b.label == Str::Count ? QString() : text(b.label),
                   isHovered, isSelected, isDisabled);
        
        currentX += buttonSize_ + spacing_;
    }
}

void ToolbarWidget::drawButton(QPainter& p, const QRect& rect, AnnotationType toolType,
                               const QString& iconName, const QString& label,
                               bool isHovered, bool isSelected, bool isDisabled) {
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
        // Drawn as two strokes rather than a font glyph. The other five icons are
        // 2px line drawings that fill `inner`; a glyph rendered with the default
        // font came out visibly smaller and thinner than its neighbours, ignored
        // the 2px pen entirely, and varied with whatever font the system had.
        p.drawLine(inner.left(), inner.top(), inner.right(), inner.top());
        p.drawLine(inner.center().x(), inner.top(), inner.center().x(), inner.bottom());
    } else if (!label.isEmpty()) {
        // Action buttons (Undo / Copy / Save / Cancel) are labelled with text. The
        // caption arrives already translated, so the toolbar follows the selected
        // language. The font is derived from the widget font rather than a
        // hardcoded family, so CJK captions do not depend on per-glyph fallback.
        p.save();
        QFont labelFont = p.font();
        labelFont.setPointSize(8);
        p.setFont(labelFont);
        p.drawText(rect, Qt::AlignCenter, label);
        p.restore();
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

void ToolbarWidget::leaveEvent(QEvent* /*event*/) {
    // Without this the last hovered button stays highlighted after the cursor
    // leaves the toolbar, because hoverPos_ is only ever updated by mouseMoveEvent.
    if (hoverPos_ != QPoint(-1, -1)) {
        hoverPos_ = QPoint(-1, -1);
        update();
    }
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
