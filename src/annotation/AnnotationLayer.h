#pragma once

#include <QVector>
#include <QPainter>
#include <QRect>
#include "Annotation.h"

namespace qshot {

class AnnotationLayer {
public:
    void add(const Annotation& a);
    void clear();
    /// Removes the newest annotation and returns its type, or None if there was nothing
    /// to remove. The caller needs the type to keep derived state in step -- the sequence
    /// counter has to give a number back when its badge is undone.
    AnnotationType undo();
    bool isEmpty() const;
    // Draw all fixed annotations. The painter is expected to be translated 
    // to the selectionRect.topLeft() before drawing.
    void paint(QPainter& p, const QRect& selectionRect) const;

    // Helper to draw a single annotation
    static void paintAnnotation(QPainter& p, const Annotation& a);

    // Render annotations to a physical image copy
    QImage renderToImage(const QImage& basePhysical) const;

    // Mosaic support
    void setBaseImage(const QImage& fullBg, const QRect& selectionRect, qreal dpr);

    /**
     * Mosaic every block the stroke's ink touches.
     *
     * `logicalDirty` is the logical-space rectangle the caller has just added ink to; an
     * empty one means "the whole annotation", which is what rebuildMosaicCache() wants
     * after an undo.
     *
     * Passing the increment is not an optimisation detail. A mosaic annotation appends one
     * point per mouse move, so the path -- and therefore its bounding box -- grows with the
     * drag: mosaicking the full path on every move makes a stroke across the screen rescan
     * the screen each time. With the dirty rectangle the work stays proportional to the ink
     * that was just laid down.
     *
     * The *result* does not depend on which rectangle is passed: the whole path is always
     * drawn into the stroke mask, which is merely sized to the rectangle and therefore clips
     * it. Antialiasing is off, so coverage is a binary inside/outside test and clipping
     * cannot change a pixel that both runs would have touched -- a block is mosaicked when
     * any ink falls inside it, and the union of the per-segment masks covers the path, so
     * every inked block is reached by at least one call.
     *
     * What the rectangle *does* change is where the mask image sits, so the path has to be
     * placed against the mask's own origin rather than against the logical rectangle: see
     * the translate() in updateMosaic(). Getting that wrong kept the two feeds' results
     * different by a whole block along any stroke that touched the selection edge.
     */
    void updateMosaic(const Annotation& mosaicAnnotation,
                      const QRect& logicalDirty = QRect());

    void rebuildMosaicCache();

private:
    QVector<Annotation> annotations_;

    // Mosaic caching
    QImage baseImage_;       // Cropped original image (physical pixels)
    qreal dpr_ = 1.0;
    QImage mosaicLayer_;     // Transparent image holding mosaic blocks (physical pixels)
    QImage mosaicMask_;      // Grayscale/Alpha mask to track processed pixels
    QRect selectionRect_;

    // Whether mosaicLayer_ actually holds any block.
    //
    // The layer is allocated once and kept, so "not null" stops meaning "has content"
    // the moment the first mosaic is undone or the layer is cleared: an emptied layer
    // is a full-selection transparent image, and blitting one costs a pass over every
    // pixel of the selection on every repaint for nothing. Tracked here rather than
    // recomputed by scanning annotations_ so the paint path stays free of that loop.
    bool mosaicInkPresent_ = false;
};

} // namespace qshot
