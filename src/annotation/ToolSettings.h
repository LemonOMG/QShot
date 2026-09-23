#pragma once

#include <QColor>
#include "AnnotationType.h"

namespace qshot {

/**
 * Per-tool annotation parameters.
 *
 * Lives in its own header (rather than inside ToolbarWidget.h) because the
 * settings model has to persist it, and the settings model must not depend on
 * the overlay widgets.
 */
struct ToolSettings {
    QColor color = QColor(251, 140, 0); // Orange default #FB8C00
    int lineWidth = 4;                  // stroke width for shapes / pen
    int mosaicSize = 16;                // mosaic block size
    int fontSize = 18;                  // text annotation size
    int badgeDiameter = 28;             // sequence badge diameter
};

/**
 * The shipped defaults for one tool.
 *
 * Two tools deliberately do not use the shared orange: an opaque highlighter would
 * hide the very text it is meant to emphasise, so it is a translucent yellow, and the
 * sequence badge is a solid red disc so that it reads as a label rather than as
 * another drawing in the user's chosen colour.
 *
 * Kept here rather than inline in Settings so that Settings::restoreDefaults(), the
 * first-run fallback and the "partially written config" path all agree by construction.
 */
inline ToolSettings defaultToolSettings(AnnotationType type) {
    ToolSettings s;
    switch (type) {
        case AnnotationType::Highlight:
            // Alpha 110 rather than something higher: at 1:1 the text under the marker
            // still has to be readable, which is the entire point of the tool.
            s.color = QColor(253, 216, 53, 110);
            s.lineWidth = 16; // a marker, not a pen -- thick by default
            break;
        case AnnotationType::Number:
            s.color = QColor(229, 57, 53);
            s.badgeDiameter = 28;
            break;
        default:
            break;
    }
    return s;
}

} // namespace qshot
