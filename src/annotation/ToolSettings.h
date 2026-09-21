#pragma once

#include <QColor>

namespace qshot {

/**
 * Per-tool annotation parameters.
 *
 * Lives in its own header (rather than inside ToolbarWidget.h) because the
 * settings model has to persist it, and the settings model must not depend on
 * the overlay widgets.
 *
 * The inline initialisers are the "factory defaults" used by
 * Settings::restoreDefaults().
 */
struct ToolSettings {
    QColor color = QColor(251, 140, 0); // Orange default #FB8C00
    int lineWidth = 4;                  // stroke width for shapes / pen
    int mosaicSize = 16;                // mosaic block size
    int fontSize = 18;                  // text annotation size
};

} // namespace qshot
