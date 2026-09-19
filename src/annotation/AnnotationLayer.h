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
    void undo();
    bool isEmpty() const;
    // Draw all fixed annotations. The painter is expected to be translated 
    // to the selectionRect.topLeft() before drawing.
    void paint(QPainter& p, const QRect& selectionRect) const;

    // Helper to draw a single annotation
    static void paintAnnotation(QPainter& p, const Annotation& a);

    // Mosaic support
    void setBaseImage(const QImage& fullBg, const QRect& selectionRect, qreal dpr);
    void updateMosaic(const Annotation& mosaicAnnotation);
    void rebuildMosaicCache();

private:
    QVector<Annotation> annotations_;

    // Mosaic caching
    QImage baseImage_;       // Cropped original image (physical pixels)
    qreal dpr_ = 1.0;
    QImage mosaicLayer_;     // Transparent image holding mosaic blocks (physical pixels)
    QImage mosaicMask_;      // Grayscale/Alpha mask to track processed pixels
    QRect selectionRect_;
};

} // namespace qshot
