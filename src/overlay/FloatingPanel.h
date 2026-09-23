#pragma once

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>

class QPainter;

// The small panels that float above the frozen screen and follow the cursor:
// the magnifier with its pixel readout, and the "click to type" badge.
//
// They are grouped because they share two things that used to be duplicated:
// the rule for placing a panel next to the cursor without leaving the screen,
// and the font metrics used to size it. The metrics are cached here because the
// magnifier layout is recomputed twice per mouse move and once more per repaint,
// and rebuilding a QFontMetrics from QFontDatabase::systemFont() every time was
// measurable work on a path that runs at mouse-move rate.

namespace qshot {
namespace panel {

/// Places a `size` panel next to `mousePos`: offset down-right by `gap`, flipped
/// to the opposite side on each axis that would otherwise leave `bounds`.
///
/// `bounds` is the overlay rectangle in logical coordinates. Flipping both axes
/// independently is what keeps the panel on screen in the corners.
QRect placeNearCursor(const QSize& size, const QPoint& mousePos,
                      const QRect& bounds, int gap);

/// The font the magnifier's pixel readout is drawn with.
///
/// Exposed alongside the metrics because QFontMetrics in Qt 6 offers no way back
/// to the font it was built from, and the painter has to be given the *same* font
/// the layout was measured with or the text will not fit the panel.
const QFont& fixedFont();

/// Metrics for the magnifier's fixed-width pixel readout.
///
/// Cached for the lifetime of the process. This is safe because it derives from
/// the system fixed font, which does not change while the application runs --
/// unlike the readout *text*, which must never be cached because it follows the
/// selected language.
const QFontMetrics& fixedFontMetrics();

/// The font used for short UI-chrome strings, currently the text-tool badge.
const QFont& uiFont();

/// Metrics for short UI-chrome strings.
///
/// Cached on the same terms: this application never changes the application font
/// at runtime. If that ever becomes false, this must become a real cache keyed
/// on the font.
const QFontMetrics& uiFontMetrics();

/// Geometry of the magnifier panel, in logical widget coordinates.
struct MagnifierLayout {
    QRect panel;          ///< the whole panel
    int magHeight = 0;    ///< height of the magnified-image section, at the top
    int srcPhysicalW = 0; ///< physical size of the source crop (always odd)
    int srcPhysicalH = 0; ///< odd so that one physical pixel lands on the crosshair
};

/// Computes the panel rectangle and the crop size for a cursor at `mousePos`.
///
/// Width is derived from worst-case text (three RGB components, five-digit
/// coordinates) rather than the values actually under the cursor. That keeps the
/// panel from resizing as the mouse moves, which is both steadier to look at and
/// what makes the rectangle usable as a partial-repaint region.
MagnifierLayout computeMagnifierLayout(const QPoint& mousePos,
                                       const QRect& bounds, qreal dpr);

/// Draws the magnifier and its RGB / HEX / POS readout.
///
/// `background` is the frozen screen image in physical pixels; `dpr` converts the
/// logical cursor position into it. `relPos` is the coordinate shown in the POS
/// row, which the caller supplies because only it knows whether the user is
/// dragging (selection-relative) or idle (absolute).
void paintMagnifier(QPainter& painter, const QImage& background, qreal dpr,
                    const QPoint& mousePos, const QPoint& relPos,
                    const MagnifierLayout& layout);

} // namespace panel
} // namespace qshot
