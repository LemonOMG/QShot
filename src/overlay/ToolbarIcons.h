#pragma once

#include <QColor>
#include <QRect>

#include "../annotation/AnnotationType.h"

class QPainter;

namespace qshot::toolbaricons {

/**
 * Draws the toolbar glyph for an annotation tool, filling `box` in `color`.
 *
 * Why this is a file of its own rather than branches inside ToolbarWidget::drawButton: the
 * glyphs are a *set*, and the constraints that make a set readable are cross-glyph ones --
 * one stroke weight, one optical box, one cap style, and no two glyphs that read alike at
 * 32px. Those are easier to hold when the eight drawings sit next to each other than when
 * they are interleaved with button chrome, hit testing and hover state.
 *
 * Returns false for AnnotationType::None, which has no glyph, so the caller can fall back to
 * drawing a text caption instead.
 */
bool paintTool(QPainter& p, AnnotationType type, const QRect& box, const QColor& color);

} // namespace qshot::toolbaricons
