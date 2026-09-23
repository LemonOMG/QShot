#pragma once

#include <QPoint>
#include <QRect>
#include <QVector>
#include <Qt>

// Pure geometry for the selection rectangle.
//
// Nothing here touches a widget, a window, or the screen: every function is a
// total function of its arguments. That is deliberate -- it makes the parts of
// the overlay that are easiest to get wrong (grip hit-testing, the eight-way
// resize maths, logical-to-physical mapping) testable with plain assertions,
// which is the only kind of verification that is reliable in a headless or
// terminal-launched session.
//
// The widget keeps ownership of state and event routing; it only asks this
// module what the resulting rectangle should be.

namespace qshot {
namespace selection {

/// Which of the eight resize grips a point falls on.
enum class Handle {
    None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left
};

/// Half-width of a grip's square hit area, in logical pixels. Each grip covers
/// the square running from (anchor - margin) to (anchor + margin - 1) on both
/// axes -- QRect's far edge is inclusive, so the box is one pixel wider on the
/// near side. That asymmetry is imperceptible for a hit target; what matters is
/// that all eight grips use it identically, since an inconsistent one is
/// invisible but very annoying.
constexpr int kHandleMargin = 8;

/// How far a press must travel before it counts as a drag rather than a click.
constexpr int kDragThreshold = 5;

/// A dragged selection narrower than this in either axis is discarded, so that a
/// stray click cannot leave a useless sliver behind.
constexpr int kMinimumSelectionEdge = 4;

/// Returns the grip at `pos`, or Handle::None when the point is not on one.
/// Returns Handle::None for an empty selection.
Handle hitTestHandle(const QRect& selection, const QPoint& pos,
                     int margin = kHandleMargin);

/// Applies a grip drag to `startRect`. The result is normalised, so dragging a
/// grip past the opposite edge flips the rectangle rather than collapsing it.
QRect resizedRect(const QRect& startRect, Handle handle, const QPoint& pos);

/// The cursor shape that advertises `handle`.
///
/// Returns Qt::ArrowCursor for Handle::None, which is a sentinel rather than a
/// real answer: callers that need to distinguish "on a grip" from "not on a
/// grip" must test the handle itself, not the returned shape.
Qt::CursorShape cursorForHandle(Handle handle);

/// The eight grip squares drawn on an unlocked selection, in no particular
/// order. The centre of the 3x3 anchor grid is omitted on purpose.
QVector<QRect> controlPointRects(const QRect& selection, int size);

/// Whether a press-then-move pair should be treated as a drag.
bool isDragGesture(const QPoint& start, const QPoint& current,
                   int threshold = kDragThreshold);

/// The selection produced by dragging from `start` to `current`, normalised so
/// it is valid regardless of drag direction.
QRect dragRect(const QPoint& start, const QPoint& current);

/// Maps a logical selection onto the physical pixels of a screen-sized image.
///
/// `clampTo` is the image rectangle in physical pixels; the result is clipped to
/// it, because a selection dragged past the screen edge has no corresponding
/// source pixels. Returns a null rectangle for an empty selection.
QRect toPhysicalRect(const QRect& logical, qreal dpr, const QRect& clampTo);

} // namespace selection
} // namespace qshot
