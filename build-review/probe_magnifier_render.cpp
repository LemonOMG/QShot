// Proves the magnifier extraction is pixel-for-pixel behaviour preserving.
//
// The magnifier is the largest single block that moved out of SnapOverlay.cpp
// into FloatingPanel.cpp. Re-reading the new code and concluding "that looks
// equivalent" is exactly the kind of reasoning this project has already been
// burned by, so instead the *original* implementation is reproduced verbatim
// below and both versions are rendered with identical inputs and diffed.
//
// Also writes both renders as PNGs so the result can be looked at, not just
// measured.
//
// Build (from the repository root), then run with $QT/bin on PATH:
//
//   QT=D:/Qt/6.11.2/mingw_64
//   g++ -std=c++17 -Wall -Wextra -Wshadow -Wunused -Isrc -I$QT/include
//       -I$QT/include/QtCore -I$QT/include/QtGui -I$QT/include/QtWidgets
//       build-review/probe_magnifier_render.cpp src/overlay/FloatingPanel.cpp
//       -L$QT/lib -lQt6Core -lQt6Gui -lQt6Widgets -o <output.exe>
//
// Exit code 0 means the two renders are identical.

#include "overlay/FloatingPanel.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QRect>
#include <QString>

#include <cstdio>

namespace {

// ===========================================================================
// The ORIGINAL implementation, copied out of SnapOverlay.cpp before the split.
// Do not "improve" anything here: its only job is to be the reference.
// ===========================================================================

constexpr int kOldMagMinSize        = 140;
constexpr int kOldMagZoom           = 4;
constexpr int kOldMagPadding        = 8;
constexpr int kOldMagLineSpacing    = 4;
constexpr int kOldMagColorBlockSize = 12;
constexpr int kOldMagFontPixelSize  = 12;
constexpr int kOldMagCursorGap      = 12;
constexpr int kOldMagRowCount       = 3;

struct OldMagnifierLayout {
    QRect panel;
    int magHeight = 0;
    int srcPhysicalW = 0;
    int srcPhysicalH = 0;
};

OldMagnifierLayout oldMagnifierLayout(const QPoint& mousePos, const QRect& bounds, qreal dpr)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(kOldMagFontPixelSize);
    const QFontMetrics fm(font);

    const int rgbWidth  = fm.horizontalAdvance(QStringLiteral("RGB: (255, 255, 255)"));
    const int hexWidth  = kOldMagColorBlockSize + 4 + fm.horizontalAdvance(QStringLiteral("HEX: #FFFFFF"));
    const int posWidth  = fm.horizontalAdvance(QStringLiteral("POS: -99999, -99999"));
    const int textWidth = qMax(qMax(rgbWidth, hexWidth), posWidth);

    const int initialBoxWidth = qMax(kOldMagMinSize, textWidth + kOldMagPadding * 2);

    int srcPhysicalW = qRound(initialBoxWidth * dpr / kOldMagZoom);
    if (srcPhysicalW % 2 == 0) ++srcPhysicalW;
    int srcPhysicalH = qRound(kOldMagMinSize * dpr / kOldMagZoom);
    if (srcPhysicalH % 2 == 0) ++srcPhysicalH;

    OldMagnifierLayout layout;
    layout.srcPhysicalW = srcPhysicalW;
    layout.srcPhysicalH = srcPhysicalH;
    layout.magHeight = qCeil(srcPhysicalH * kOldMagZoom / dpr);
    const int boxWidth = qCeil(srcPhysicalW * kOldMagZoom / dpr);
    const int boxHeight = layout.magHeight + kOldMagPadding * 2
                        + fm.height() * kOldMagRowCount + kOldMagLineSpacing * (kOldMagRowCount - 1);

    int boxX = mousePos.x() + kOldMagCursorGap;
    int boxY = mousePos.y() + kOldMagCursorGap;
    if (boxX + boxWidth > bounds.right()) {
        boxX = mousePos.x() - kOldMagCursorGap - boxWidth;
    }
    if (boxY + boxHeight > bounds.bottom()) {
        boxY = mousePos.y() - kOldMagCursorGap - boxHeight;
    }

    layout.panel = QRect(boxX, boxY, boxWidth, boxHeight);
    return layout;
}

void oldPaintMagnifier(QPainter& painter, const QImage& backgroundImage_, qreal dpr_,
                       const QPoint& currentMousePos_, const QPoint& relPos,
                       const OldMagnifierLayout& layout)
{
    const int padding = kOldMagPadding;
    const int lineSpacing = kOldMagLineSpacing;
    const int colorBlockSize = kOldMagColorBlockSize;

    qreal dpr = dpr_;
    QPoint scaledPos(qRound(currentMousePos_.x() * dpr), qRound(currentMousePos_.y() * dpr));

    QColor pixelColor = Qt::black;
    if (backgroundImage_.valid(scaledPos)) {
        pixelColor = backgroundImage_.pixelColor(scaledPos);
    }

    QString rgbText = QString("RGB: (%1, %2, %3)").arg(pixelColor.red()).arg(pixelColor.green()).arg(pixelColor.blue());
    QString hexText = QString("HEX: %1").arg(pixelColor.name().toUpper());
    QString coordText = QString("POS: %1, %2").arg(relPos.x()).arg(relPos.y());

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(kOldMagFontPixelSize);
    QFontMetrics fm(font);

    const int boxX = layout.panel.x();
    const int boxY = layout.panel.y();
    const int boxWidth = layout.panel.width();
    const int boxHeight = layout.panel.height();
    const int finalMagSize = layout.magHeight;
    const int srcPhysicalW = layout.srcPhysicalW;
    const int srcPhysicalH = layout.srcPhysicalH;
    const int magPhysicalW = srcPhysicalW * kOldMagZoom;
    const int magPhysicalH = srcPhysicalH * kOldMagZoom;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath clipPath;
    clipPath.addRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, 8);
    painter.setClipPath(clipPath);

    painter.setPen(Qt::NoPen);

    painter.setBrush(Qt::black);
    painter.drawRect(boxX, boxY, boxWidth, boxHeight);

    QRectF textBgRect(boxX, boxY + finalMagSize, boxWidth, boxHeight - finalMagSize);
    painter.setBrush(Qt::white);
    painter.drawRect(textBgRect);

    int srcX = scaledPos.x() - srcPhysicalW / 2;
    int srcY = scaledPos.y() - srcPhysicalH / 2;
    QRect physicalSrcRect(srcX, srcY, srcPhysicalW, srcPhysicalH);

    QImage srcImage(physicalSrcRect.size(), QImage::Format_ARGB32);
    srcImage.fill(Qt::black);
    srcImage.setDevicePixelRatio(1.0);

    QRect intersect = physicalSrcRect.intersected(backgroundImage_.rect());
    if (!intersect.isEmpty()) {
        QImage cropped = backgroundImage_.copy(intersect);
        cropped.setDevicePixelRatio(1.0);
        QPainter p(&srcImage);
        p.drawImage(intersect.topLeft() - physicalSrcRect.topLeft(), cropped);
    }

    QImage magnifiedImage = srcImage.scaled(magPhysicalW, magPhysicalH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    painter.drawImage(QRectF(boxX, boxY, boxWidth, finalMagSize), magnifiedImage);

    qreal crosshairX = boxX + boxWidth / 2.0;
    qreal crosshairY = boxY + finalMagSize / 2.0;

    painter.setPen(QPen(QColor(0, 255, 0, 150), 2));
    painter.drawLine(QPointF(crosshairX, boxY), QPointF(crosshairX, boxY + finalMagSize));
    painter.drawLine(QPointF(boxX, crosshairY), QPointF(boxX + boxWidth, crosshairY));

    painter.setFont(font);

    int currentY = boxY + finalMagSize + padding + fm.ascent();

    painter.setPen(Qt::black);
    painter.drawText(boxX + padding, currentY, rgbText);

    currentY += fm.height() + lineSpacing;
    int blockY = currentY - fm.ascent() + (fm.height() - colorBlockSize) / 2;

    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(pixelColor);
    painter.drawRect(boxX + padding, blockY, colorBlockSize, colorBlockSize);

    painter.setPen(Qt::black);
    painter.drawText(boxX + padding + colorBlockSize + 4, currentY, hexText);

    currentY += fm.height() + lineSpacing;
    painter.setPen(Qt::black);
    painter.drawText(boxX + padding, currentY, coordText);

    painter.restore();
}

// ===========================================================================
// Harness
// ===========================================================================

constexpr int kLogicalW = 1707;
constexpr int kLogicalH = 1067;
constexpr int kPhysicalW = 2560;  // kLogicalW * 1.5, truncated as Qt does
constexpr int kPhysicalH = 1600;
constexpr qreal kDpr = 1.5;

/// A background with real structure, so a wrong crop or a wrong scale shows up
/// as a mismatch rather than as two equally blank panels.
QImage makeBackground()
{
    QImage img(kPhysicalW, kPhysicalH, QImage::Format_ARGB32);
    for (int y = 0; y < kPhysicalH; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < kPhysicalW; ++x) {
            // Distinct per-pixel values make any sampling offset visible.
            const int r = (x * 7) % 256;
            const int g = (y * 5) % 256;
            const int b = ((x + y) * 3) % 256;
            line[x] = qRgba(r, g, b, 255);
        }
    }
    img.setDevicePixelRatio(kDpr);
    return img;
}

QImage renderWithOld(const QImage& background, const QPoint& mousePos, const QPoint& relPos)
{
    const QRect bounds(0, 0, kLogicalW, kLogicalH);
    QImage canvas(kPhysicalW, kPhysicalH, QImage::Format_ARGB32_Premultiplied);
    canvas.setDevicePixelRatio(kDpr);

    QPainter painter(&canvas);
    painter.drawImage(0, 0, background);
    oldPaintMagnifier(painter, background, kDpr, mousePos, relPos,
                      oldMagnifierLayout(mousePos, bounds, kDpr));
    painter.end();
    return canvas;
}

QImage renderWithNew(const QImage& background, const QPoint& mousePos, const QPoint& relPos)
{
    const QRect bounds(0, 0, kLogicalW, kLogicalH);
    QImage canvas(kPhysicalW, kPhysicalH, QImage::Format_ARGB32_Premultiplied);
    canvas.setDevicePixelRatio(kDpr);

    QPainter painter(&canvas);
    painter.drawImage(0, 0, background);
    qshot::panel::paintMagnifier(painter, background, kDpr, mousePos, relPos,
                                 qshot::panel::computeMagnifierLayout(mousePos, bounds, kDpr));
    painter.end();
    return canvas;
}

struct DiffResult {
    int differingPixels = 0;
    int maxChannelDelta = 0;
};

DiffResult diff(const QImage& a, const QImage& b)
{
    DiffResult result;
    if (a.size() != b.size()) {
        result.differingPixels = -1;
        result.maxChannelDelta = -1;
        return result;
    }

    const QImage ia = a.convertToFormat(QImage::Format_ARGB32);
    const QImage ib = b.convertToFormat(QImage::Format_ARGB32);

    for (int y = 0; y < ia.height(); ++y) {
        const QRgb* pa = reinterpret_cast<const QRgb*>(ia.constScanLine(y));
        const QRgb* pb = reinterpret_cast<const QRgb*>(ib.constScanLine(y));
        for (int x = 0; x < ia.width(); ++x) {
            const int dr = qAbs(qRed(pa[x])   - qRed(pb[x]));
            const int dg = qAbs(qGreen(pa[x]) - qGreen(pb[x]));
            const int db = qAbs(qBlue(pa[x])  - qBlue(pb[x]));
            const int da = qAbs(qAlpha(pa[x]) - qAlpha(pb[x]));
            const int d = qMax(qMax(dr, dg), qMax(db, da));
            if (d != 0) {
                ++result.differingPixels;
                result.maxChannelDelta = qMax(result.maxChannelDelta, d);
            }
        }
    }
    return result;
}

int g_failures = 0;

void runCase(const QImage& background, const QPoint& mousePos, const QPoint& relPos,
             const char* label)
{
    const QImage oldRender = renderWithOld(background, mousePos, relPos);
    const QImage newRender = renderWithNew(background, mousePos, relPos);
    const DiffResult d = diff(oldRender, newRender);

    std::printf("%-28s differing=%d  maxDelta=%d\n",
                label, d.differingPixels, d.maxChannelDelta);

    if (d.differingPixels != 0) {
        ++g_failures;
        std::printf("      ^^ MISMATCH: the extraction changed the rendered output\n");
    }
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    const QImage background = makeBackground();

    // A point in open space, a point in the bottom-right corner (which forces the
    // panel to flip to the other side), and a point in the top-left corner.
    runCase(background, QPoint(400, 300),  QPoint(400, 300),  "centre, idle (absolute pos)");
    runCase(background, QPoint(1650, 1000), QPoint(100, 100), "bottom-right corner (flips)");
    runCase(background, QPoint(20, 20),    QPoint(20, 20),    "top-left corner");
    runCase(background, QPoint(1690, 1050), QPoint(500, 200), "bottom-right corner, drag-relative");
    runCase(background, QPoint(855, 533),  QPoint(0, 0),      "exact centre");

    // Save both renders for visual inspection: numbers can be right while the
    // picture is obviously wrong.
    //
    // Bare file names, like every other render scaffold here: they all run with
    // build-review/ as the working directory. These three used to be written as
    // "build-review/magnifier_*.png", which resolved to build-review/build-review/
    // -- a directory that does not exist, so QImage::save() failed silently and
    // the evidence was simply never produced. The PNGs on disk came from a manual
    // run from the repository root, which is why the breakage went unnoticed.
    const QImage oldRender = renderWithOld(background, QPoint(400, 300), QPoint(400, 300));
    const QImage newRender = renderWithNew(background, QPoint(400, 300), QPoint(400, 300));
    oldRender.save(QStringLiteral("magnifier_before.png"));
    newRender.save(QStringLiteral("magnifier_after.png"));

    // And a close-up of just the panel region, which is what a human actually
    // needs to look at.
    const QRect panel = qshot::panel::computeMagnifierLayout(
                            QPoint(400, 300), QRect(0, 0, kLogicalW, kLogicalH), kDpr).panel;
    const QRect physicalPanel(qRound(panel.x() * kDpr), qRound(panel.y() * kDpr),
                              qRound(panel.width() * kDpr), qRound(panel.height() * kDpr));
    newRender.copy(physicalPanel.adjusted(-8, -8, 8, 8))
              .save(QStringLiteral("magnifier_after_crop.png"));

    std::printf("\n%s\n", g_failures == 0
                              ? "identical: the extraction is behaviour preserving"
                              : "MISMATCHES FOUND");
    return g_failures == 0 ? 0 : 1;
}
