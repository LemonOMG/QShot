#include "ToolbarIcons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

namespace qshot::toolbaricons {

namespace {

// One pen for the whole set. The previous version left the cap and join at their defaults
// (square and bevel), so the rectangle had sharp corners while the ellipse next to it was
// smooth -- a difference nobody would name but everybody sees as "assembled from parts".
constexpr qreal kStroke = 2.0;

QPen glyphPen(const QColor& color) {
    return QPen(color, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

// The box every glyph is drawn in, inset by half a stroke so the *outer* edge of the stroke
// lands on the box edge. A painter puts the pen on the path, so an un-inset rectangle is a
// full stroke wider than an inset ellipse beside it; that is exactly why the original pair
// looked like different sizes even though both were given the same rect.
QRectF strokedBox(const QRectF& b) {
    return b.adjusted(kStroke / 2, kStroke / 2, -kStroke / 2, -kStroke / 2);
}

// --- the eight glyphs -------------------------------------------------------------------

void paintRect(QPainter& p, const QRectF& b) {
    p.drawRect(strokedBox(b));
}

void paintEllipse(QPainter& p, const QRectF& b) {
    p.drawEllipse(strokedBox(b));
}

void paintArrow(QPainter& p, const QRectF& b) {
    const QPointF tail(b.left(), b.bottom());
    const QPointF tip(b.right(), b.top());
    p.drawLine(tail, tip);

    // The barbs are the tip's two neighbours along the shaft's own axes. On a 45-degree
    // shaft those are simply horizontal and vertical, which is why this needs no
    // trigonometry.
    //
    // 6px, up from the original 4. Measured: a 4px barb added 10px of ink over a bare shaft,
    // a 5px one adds 18, and this adds 26 -- the barbs overlap the shaft's own round cap, so
    // the visible gain is smaller than the barb length suggests. Below about 20px of extra
    // ink the head stops reading as a head, and the arrow becomes the diagonal stroke the
    // pen used to be.
    constexpr qreal kBarb = 6.0;
    p.drawLine(tip, QPointF(tip.x() - kBarb, tip.y()));
    p.drawLine(tip, QPointF(tip.x(), tip.y() + kBarb));
}

void paintPen(QPainter& p, const QRectF& b) {
    // A wave, not a straight line.
    //
    // The original glyph was a single diagonal stroke from corner to corner -- visually
    // identical to the arrow with its head taken off, so two buttons claimed to be the same
    // tool. Freehand drawing has to look hand-drawn to say "freehand".
    //
    // Two quadratics, with the control points placed *outside* the box vertically. A
    // quadratic does not pass through its control point, so the crest and trough come close
    // to the top and bottom edges without the stroke ever leaving the box.
    const qreal l = b.left(), r = b.right(), t = b.top(), bo = b.bottom();
    const qreal w = b.width(), h = b.height();
    const qreal mid = (t + bo) / 2.0;
    const qreal q = h * 0.25;

    QPainterPath path;
    path.moveTo(l, mid + q);
    path.quadTo(l + w * 0.25, t, l + w * 0.5, mid);
    path.quadTo(r - w * 0.25, bo, r, mid - q);
    p.drawPath(path);
}

void paintMosaic(QPainter& p, const QRectF& b, const QColor& color) {
    // A 3x3 checkerboard. The original was two overlapping squares, which reads as
    // "duplicate" rather than as "pixelate" -- it is the *number* of blocks that carries the
    // meaning, so two is not enough and the pattern has to be a grid.
    //
    // Cell edges are computed with integer arithmetic, `(i * width) / 3`, so the boundaries
    // land on whole pixels and the remainder is spread across the columns. A qreal division
    // would antialias every edge of a glyph whose entire subject is hard edges.
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const int left = qRound(b.left()), top = qRound(b.top());
    const int side = qRound(b.width());
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if ((row + col) % 2 != 0) continue; // the filled squares of a checkerboard
            const int x0 = left + (col * side) / 3;
            const int x1 = left + ((col + 1) * side) / 3;
            const int y0 = top + (row * side) / 3;
            const int y1 = top + ((row + 1) * side) / 3;
            p.fillRect(QRect(x0, y0, x1 - x0, y1 - y0), color);
        }
    }
}

void paintText(QPainter& p, const QRectF& b) {
    // Two strokes rather than a font glyph. A glyph ignores the pen width entirely, so it
    // came out visibly smaller and thinner than its neighbours, and it varied with whatever
    // font the system happened to have.
    p.drawLine(QPointF(b.left(), b.top()), QPointF(b.right(), b.top()));
    p.drawLine(QPointF(b.center().x(), b.top()), QPointF(b.center().x(), b.bottom()));
}

void paintNumber(QPainter& p, const QRectF& b) {
    // The badge the tool actually draws, with the "1" reduced to two strokes: a stem plus
    // the small flag at its top-left. Without the flag a bare bar reads as a power symbol.
    const QRectF disc = strokedBox(b);
    p.drawEllipse(disc);

    const qreal cx = disc.center().x();
    const qreal stemTop = disc.top() + b.height() * 0.20;
    const qreal stemBottom = disc.bottom() - b.height() * 0.20;
    p.drawLine(QPointF(cx, stemTop), QPointF(cx, stemBottom));
    p.drawLine(QPointF(cx, stemTop), QPointF(cx - b.width() * 0.15, stemTop + b.height() * 0.15));
}

void paintHighlight(QPainter& p, const QRectF& b, const QColor& color) {
    // A marker stroke: one thick band.
    //
    // The band's *fill* is translucent, because that is the whole point of the tool -- it has
    // to leave the text underneath readable. But the band also gets the set's ordinary 2px
    // opaque outline, and that outline is not decoration: with a translucent fill alone the
    // icon was the only one in the set drawn below full strength, so unselected it came out
    // a dim grey slash next to seven crisp white glyphs. The outline restores the shared
    // optical weight while the fill still says "translucent".
    QColor band = color;
    band.setAlpha(140);

    const qreal l = b.left(), r = b.right(), t = b.top(), bo = b.bottom();
    QPolygonF stroke;
    stroke << QPointF(l, bo - b.height() * 0.15)
           << QPointF(r - b.width() * 0.15, t)
           << QPointF(r, t + b.height() * 0.20)
           << QPointF(l + b.width() * 0.15, bo);

    p.setBrush(band);
    p.drawPolygon(stroke); // the pen is already glyphPen(color) -- opaque, 2px, round joins
}

} // namespace

bool paintTool(QPainter& p, AnnotationType type, const QRect& box, const QColor& color) {
    if (type == AnnotationType::None || box.isEmpty()) return false;

    // QRectF, not QRect, and this conversion happens exactly once.
    //
    // QRect::right() and bottom() are x + width - 1, so a QRect(6,6,20,20) reports right() ==
    // 25 and center() == (15,15) -- one pixel inside its own geometric centre. Drawing in
    // QRect arithmetic therefore produced glyphs that filled 19 of their 20 pixels and put
    // the text stem and the badge numeral half a pixel off centre. QRectF::right() is x +
    // width, so the box means what it says. A probe caught this by measuring a stroke where
    // the centre should have been and finding nothing there.
    const QRectF b(box);

    p.save();
    p.setPen(glyphPen(color));
    p.setBrush(Qt::NoBrush);

    switch (type) {
        case AnnotationType::Rectangle: paintRect(p, b); break;
        case AnnotationType::Ellipse:   paintEllipse(p, b); break;
        case AnnotationType::Arrow:     paintArrow(p, b); break;
        case AnnotationType::Pen:       paintPen(p, b); break;
        case AnnotationType::Mosaic:    paintMosaic(p, b, color); break;
        case AnnotationType::Text:      paintText(p, b); break;
        case AnnotationType::Number:    paintNumber(p, b); break;
        case AnnotationType::Highlight: paintHighlight(p, b, color); break;
        case AnnotationType::None:      break; // handled above; keeps -Wswitch honest
    }

    p.restore();
    return true;
}

} // namespace qshot::toolbaricons
