#include "FloatingPanel.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QtMath>

namespace qshot {
namespace panel {

namespace {

// Magnifier tuning. Shared by paintMagnifier() and computeMagnifierLayout() so
// that the drawn panel and the repainted region can never disagree about size.
constexpr int kMagMinSize        = 140; // minimum magnifier edge length, logical px
constexpr int kMagZoom           = 4;   // magnification factor
constexpr int kMagPadding        = 8;
constexpr int kMagLineSpacing    = 4;
constexpr int kMagColorBlockSize = 12;
constexpr int kMagFontPixelSize  = 12;
constexpr int kMagCursorGap      = 12;  // gap between cursor and panel
constexpr int kMagRowCount       = 3;   // RGB / HEX / POS

constexpr int kUiFontPixelSize   = 12;

QFont buildFixedFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(kMagFontPixelSize);
    return font;
}

QFont buildUiFont()
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(kUiFontPixelSize);
    return font;
}

} // namespace

QRect placeNearCursor(const QSize& size, const QPoint& mousePos,
                      const QRect& bounds, int gap)
{
    int x = mousePos.x() + gap;
    int y = mousePos.y() + gap;
    if (x + size.width() > bounds.right()) {
        x = mousePos.x() - gap - size.width();
    }
    if (y + size.height() > bounds.bottom()) {
        y = mousePos.y() - gap - size.height();
    }
    return QRect(QPoint(x, y), size);
}

const QFont& fixedFont()
{
    static const QFont font = buildFixedFont();
    return font;
}

const QFont& uiFont()
{
    static const QFont font = buildUiFont();
    return font;
}

const QFontMetrics& fixedFontMetrics()
{
    // Initialising from fixedFont() rather than rebuilding the font keeps the
    // metrics and the drawn font provably identical.
    static const QFontMetrics metrics(fixedFont());
    return metrics;
}

const QFontMetrics& uiFontMetrics()
{
    static const QFontMetrics metrics(uiFont());
    return metrics;
}

MagnifierLayout computeMagnifierLayout(const QPoint& mousePos,
                                       const QRect& bounds, qreal dpr)
{
    const QFontMetrics& fm = fixedFontMetrics();

    // Worst-case text widths, used to size the panel once and keep it stable.
    const int rgbWidth  = fm.horizontalAdvance(QStringLiteral("RGB: (255, 255, 255)"));
    const int hexWidth  = kMagColorBlockSize + 4
                        + fm.horizontalAdvance(QStringLiteral("HEX: #FFFFFF"));
    const int posWidth  = fm.horizontalAdvance(QStringLiteral("POS: -99999, -99999"));
    const int textWidth = qMax(qMax(rgbWidth, hexWidth), posWidth);

    const int initialBoxWidth = qMax(kMagMinSize, textWidth + kMagPadding * 2);

    // The source crop is taken in physical pixels and scaled with nearest
    // neighbour. Forcing an odd crop size guarantees a true centre pixel, which
    // is what lets the crosshair be drawn at exactly 50% of the panel without
    // any subpixel drift.
    int srcPhysicalW = qRound(initialBoxWidth * dpr / kMagZoom);
    if (srcPhysicalW % 2 == 0) ++srcPhysicalW;
    int srcPhysicalH = qRound(kMagMinSize * dpr / kMagZoom);
    if (srcPhysicalH % 2 == 0) ++srcPhysicalH;

    MagnifierLayout layout;
    layout.srcPhysicalW = srcPhysicalW;
    layout.srcPhysicalH = srcPhysicalH;
    layout.magHeight = qCeil(srcPhysicalH * kMagZoom / dpr);
    const int boxWidth = qCeil(srcPhysicalW * kMagZoom / dpr);
    const int boxHeight = layout.magHeight + kMagPadding * 2
                        + fm.height() * kMagRowCount
                        + kMagLineSpacing * (kMagRowCount - 1);

    layout.panel = placeNearCursor(QSize(boxWidth, boxHeight), mousePos, bounds, kMagCursorGap);
    return layout;
}

void paintMagnifier(QPainter& painter, const QImage& background, qreal dpr,
                    const QPoint& mousePos, const QPoint& relPos,
                    const MagnifierLayout& layout)
{
    const QFontMetrics& fm = fixedFontMetrics();

    // Sample the pixel under the cursor. The coordinates are physical: the
    // background image is a screen-sized bitmap and valid()/pixelColor() index it
    // in image pixels, not logical ones.
    const QPoint scaledPos(qRound(mousePos.x() * dpr), qRound(mousePos.y() * dpr));

    QColor pixelColor = Qt::black;
    if (background.valid(scaledPos)) {
        pixelColor = background.pixelColor(scaledPos);
    }

    const QString rgbText = QString("RGB: (%1, %2, %3)")
                                .arg(pixelColor.red())
                                .arg(pixelColor.green())
                                .arg(pixelColor.blue());
    const QString hexText = QString("HEX: %1").arg(pixelColor.name().toUpper());
    const QString coordText = QString("POS: %1, %2").arg(relPos.x()).arg(relPos.y());

    const int boxX = layout.panel.x();
    const int boxY = layout.panel.y();
    const int boxWidth = layout.panel.width();
    const int boxHeight = layout.panel.height();
    const int magHeight = layout.magHeight;
    const int magPhysicalW = layout.srcPhysicalW * kMagZoom;
    const int magPhysicalH = layout.srcPhysicalH * kMagZoom;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath clipPath;
    clipPath.addRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, 8);
    painter.setClipPath(clipPath);

    painter.setPen(Qt::NoPen);

    // Black plate, then a white plate for the readout section.
    painter.setBrush(Qt::black);
    painter.drawRect(boxX, boxY, boxWidth, boxHeight);

    const QRectF textBgRect(boxX, boxY + magHeight, boxWidth, boxHeight - magHeight);
    painter.setBrush(Qt::white);
    painter.drawRect(textBgRect);

    // Crop the source in physical pixels. Both the destination image and the
    // copied crop are forced to DPR 1.0: otherwise QPainter would rescale them by
    // their own device pixel ratio and the "magnified" pixels would come out at
    // the wrong size.
    const int srcX = scaledPos.x() - layout.srcPhysicalW / 2;
    const int srcY = scaledPos.y() - layout.srcPhysicalH / 2;
    const QRect physicalSrcRect(srcX, srcY, layout.srcPhysicalW, layout.srcPhysicalH);

    QImage srcImage(physicalSrcRect.size(), QImage::Format_ARGB32);
    srcImage.fill(Qt::black);
    srcImage.setDevicePixelRatio(1.0);

    const QRect intersect = physicalSrcRect.intersected(background.rect());
    if (!intersect.isEmpty()) {
        QImage cropped = background.copy(intersect);
        cropped.setDevicePixelRatio(1.0);
        QPainter p(&srcImage);
        p.drawImage(intersect.topLeft() - physicalSrcRect.topLeft(), cropped);
    }

    const QImage magnifiedImage =
        srcImage.scaled(magPhysicalW, magPhysicalH,
                        Qt::IgnoreAspectRatio, Qt::FastTransformation);
    painter.drawImage(QRectF(boxX, boxY, boxWidth, magHeight), magnifiedImage);

    // Crosshair. It sits at the geometric centre because the crop is odd-sized.
    const qreal crosshairX = boxX + boxWidth / 2.0;
    const qreal crosshairY = boxY + magHeight / 2.0;
    painter.setPen(QPen(QColor(0, 255, 0, 150), 2));
    painter.drawLine(QPointF(crosshairX, boxY), QPointF(crosshairX, boxY + magHeight));
    painter.drawLine(QPointF(boxX, crosshairY), QPointF(boxX + boxWidth, crosshairY));

    painter.setFont(fixedFont());

    int currentY = boxY + magHeight + kMagPadding + fm.ascent();

    // Row 1: RGB
    painter.setPen(Qt::black);
    painter.drawText(boxX + kMagPadding, currentY, rgbText);

    // Row 2: colour swatch + HEX
    currentY += fm.height() + kMagLineSpacing;
    const int blockY = currentY - fm.ascent() + (fm.height() - kMagColorBlockSize) / 2;
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(pixelColor);
    painter.drawRect(boxX + kMagPadding, blockY, kMagColorBlockSize, kMagColorBlockSize);
    painter.setPen(Qt::black);
    painter.drawText(boxX + kMagPadding + kMagColorBlockSize + 4, currentY, hexText);

    // Row 3: coordinates
    currentY += fm.height() + kMagLineSpacing;
    painter.setPen(Qt::black);
    painter.drawText(boxX + kMagPadding, currentY, coordText);

    painter.restore();
}

} // namespace panel
} // namespace qshot
