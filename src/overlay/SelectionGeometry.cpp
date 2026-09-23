#include "SelectionGeometry.h"

#include <QtMath>

namespace qshot {
namespace selection {

Handle hitTestHandle(const QRect& selection, const QPoint& pos, int margin)
{
    if (selection.isEmpty()) return Handle::None;

    // One rule for all eight grips. Whatever the box size is, using it uniformly
    // is what keeps the grips from feeling inconsistent under the cursor.
    const auto hits = [&pos, margin](const QPoint& anchor) {
        return QRect(anchor - QPoint(margin, margin),
                     QSize(2 * margin, 2 * margin)).contains(pos);
    };

    // Corners win over edges: the two hit areas overlap on small selections, and
    // the corner is the one the user is actually aiming at.
    if (hits(selection.topLeft()))     return Handle::TopLeft;
    if (hits(selection.topRight()))    return Handle::TopRight;
    if (hits(selection.bottomLeft()))  return Handle::BottomLeft;
    if (hits(selection.bottomRight())) return Handle::BottomRight;
    if (hits(QPoint(selection.center().x(), selection.top())))    return Handle::Top;
    if (hits(QPoint(selection.center().x(), selection.bottom()))) return Handle::Bottom;
    if (hits(QPoint(selection.left(),  selection.center().y())))  return Handle::Left;
    if (hits(QPoint(selection.right(), selection.center().y())))  return Handle::Right;
    return Handle::None;
}

QRect resizedRect(const QRect& startRect, Handle handle, const QPoint& pos)
{
    QRect r = startRect;
    switch (handle) {
        case Handle::TopLeft:     r.setTopLeft(pos);     break;
        case Handle::Top:         r.setTop(pos.y());     break;
        case Handle::TopRight:    r.setTopRight(pos);    break;
        case Handle::Right:       r.setRight(pos.x());   break;
        case Handle::BottomRight: r.setBottomRight(pos); break;
        case Handle::Bottom:      r.setBottom(pos.y());  break;
        case Handle::BottomLeft:  r.setBottomLeft(pos);  break;
        case Handle::Left:        r.setLeft(pos.x());    break;
        case Handle::None:        break;
    }
    return r.normalized();
}

Qt::CursorShape cursorForHandle(Handle handle)
{
    switch (handle) {
        case Handle::TopLeft:
        case Handle::BottomRight: return Qt::SizeFDiagCursor;
        case Handle::TopRight:
        case Handle::BottomLeft:  return Qt::SizeBDiagCursor;
        case Handle::Top:
        case Handle::Bottom:      return Qt::SizeVerCursor;
        case Handle::Left:
        case Handle::Right:       return Qt::SizeHorCursor;
        case Handle::None:        break;
    }
    return Qt::ArrowCursor;
}

QVector<QRect> controlPointRects(const QRect& selection, int size)
{
    // Anchors sit on the selection's own edges, hence the -1 on the far side:
    // right() and bottom() are the last addressable pixel, not one past it.
    const int xs[3] = { selection.left(), selection.center().x(), selection.right() - 1 };
    const int ys[3] = { selection.top(), selection.center().y(), selection.bottom() - 1 };

    QVector<QRect> rects;
    rects.reserve(8);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (i == 1 && j == 1) continue; // skip the centre
            rects.append(QRect(xs[i] - size / 2, ys[j] - size / 2, size, size));
        }
    }
    return rects;
}

bool isDragGesture(const QPoint& start, const QPoint& current, int threshold)
{
    return (current - start).manhattanLength() > threshold;
}

QRect dragRect(const QPoint& start, const QPoint& current)
{
    // Built from min/max rather than QRect(topLeft, bottomRight).normalized().
    // That constructor treats its second point as an inclusive corner, so a drag
    // running up-and-left normalises one pixel short on every edge: the same
    // gesture would produce a different rectangle depending on its direction.
    // (Measured before the fix: down-right gave 41x31, up-left gave 39x29.)
    const int left   = qMin(start.x(), current.x());
    const int top    = qMin(start.y(), current.y());
    const int right  = qMax(start.x(), current.x());
    const int bottom = qMax(start.y(), current.y());
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

QRect toPhysicalRect(const QRect& logical, qreal dpr, const QRect& clampTo)
{
    if (logical.isEmpty()) return QRect();

    const QRect physical(
        qRound(logical.x() * dpr),
        qRound(logical.y() * dpr),
        qRound(logical.width() * dpr),
        qRound(logical.height() * dpr));

    return physical.intersected(clampTo);
}

} // namespace selection
} // namespace qshot
