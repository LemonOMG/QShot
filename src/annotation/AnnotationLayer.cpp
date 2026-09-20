#include "AnnotationLayer.h"
#include <QPainterPath>

namespace qshot {

void AnnotationLayer::add(const Annotation& a) {
    annotations_.append(a);
}

void AnnotationLayer::clear() {
    annotations_.clear();
    rebuildMosaicCache();
}

void AnnotationLayer::undo() {
    if (!annotations_.isEmpty()) {
        annotations_.removeLast();
        rebuildMosaicCache();
    }
}

bool AnnotationLayer::isEmpty() const {
    return annotations_.isEmpty();
}

void AnnotationLayer::paint(QPainter& p, const QRect& selectionRect) const {
    p.save();
    p.translate(selectionRect.topLeft());

    // 1. Paint mosaic layer first (it's underneath vector annotations).
    // mosaicLayer_ carries dpr_ from the moment it is allocated, so it can be drawn
    // directly. Copying it just to call setDevicePixelRatio() would detach and copy
    // the whole layer on every repaint.
    if (!mosaicLayer_.isNull()) {
        p.drawImage(0, 0, mosaicLayer_);
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& a : annotations_) {
        if (a.type != AnnotationType::Mosaic) {
            paintAnnotation(p, a);
        }
    }

    p.restore();
}

void AnnotationLayer::paintAnnotation(QPainter& p, const Annotation& a) {
    if (a.points.isEmpty()) return;

    QPen pen(a.color, a.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (a.type) {
        case AnnotationType::Rectangle: {
            if (a.points.size() >= 2) {
                QRect rect(a.points.first(), a.points.last());
                p.drawRect(rect.normalized());
            }
            break;
        }
        case AnnotationType::Pen: {
            if (a.points.size() == 1) {
                p.drawPoint(a.points.first());
            } else if (a.points.size() > 1) {
                QPainterPath path;
                path.moveTo(a.points.first());
                for (int i = 1; i < a.points.size(); ++i) {
                    path.lineTo(a.points[i]);
                }
                p.drawPath(path);
            }
            break;
        }
        case AnnotationType::Ellipse: {
            if (a.points.size() >= 2) {
                QRect rect(a.points.first(), a.points.last());
                p.drawEllipse(rect.normalized());
            }
            break;
        }
        case AnnotationType::Arrow: {
            if (a.points.size() >= 2) {
                QPointF p1 = a.points.first();
                QPointF p2 = a.points.last();
                if (p1 == p2) break;
                
                QLineF line(p1, p2);
                qreal arrowHeadLength = a.lineWidth * 4;
                qreal arrowHeadWidth = a.lineWidth * 3;
                
                if (line.length() < arrowHeadLength) {
                    arrowHeadLength = line.length();
                    arrowHeadWidth = arrowHeadLength * 0.75;
                }
                
                QLineF norm = line.normalVector();
                norm.setLength(arrowHeadWidth / 2.0);
                
                QPointF p2Back = line.pointAt(1.0 - arrowHeadLength / line.length());
                QPointF p3 = p2Back + (norm.p2() - norm.p1());
                QPointF p4 = p2Back - (norm.p2() - norm.p1());
                
                // Draw the line
                p.drawLine(p1, p2Back);
                
                // Draw the arrow head
                QPolygonF arrowHead;
                arrowHead << p2 << p3 << p4;
                p.setBrush(a.color);
                p.setPen(Qt::NoPen);
                p.drawPolygon(arrowHead);
            }
            break;
        }
        case AnnotationType::Text: {
            if (a.points.size() >= 1 && !a.text.isEmpty()) {
                p.setPen(a.color);
                QFont font = p.font();
                font.setPixelSize(a.fontSize); 
                p.setFont(font);
                
                QRectF rect(a.points.first(), QSizeF(10000, 10000));
                p.drawText(rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, a.text);
            }
            break;
        }
        case AnnotationType::Mosaic:
        case AnnotationType::None:
            break;
    }
}

QImage AnnotationLayer::renderToImage(const QImage& basePhysical) const {
    if (basePhysical.isNull()) return basePhysical;
    
    QImage result = basePhysical;
    result.setDevicePixelRatio(dpr_);
    
    QPainter p(&result);
    // Draw mosaic layer if available (already carries dpr_).
    if (!mosaicLayer_.isNull()) {
        p.drawImage(0, 0, mosaicLayer_);
    }
    
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& a : annotations_) {
        if (a.type != AnnotationType::Mosaic) {
            paintAnnotation(p, a);
        }
    }
    p.end();
    
    return result;
}

void AnnotationLayer::setBaseImage(const QImage& fullBg, const QRect& selectionRect, qreal dpr) {
    dpr_ = dpr;
    selectionRect_ = selectionRect;

    QRect physicalRect(
        qRound(selectionRect.x() * dpr),
        qRound(selectionRect.y() * dpr),
        qRound(selectionRect.width() * dpr),
        qRound(selectionRect.height() * dpr)
    );
    
    physicalRect = physicalRect.intersected(fullBg.rect());
    if (!physicalRect.isEmpty()) {
        baseImage_ = fullBg.copy(physicalRect);
        baseImage_.setDevicePixelRatio(1.0); 
    }
    
    mosaicLayer_ = QImage();
    mosaicMask_ = QImage();
    
    rebuildMosaicCache();
}

void AnnotationLayer::rebuildMosaicCache() {
    mosaicLayer_.fill(Qt::transparent);
    mosaicMask_.fill(0);
    for (const auto& a : annotations_) {
        if (a.type == AnnotationType::Mosaic) {
            updateMosaic(a);
        }
    }
}

void AnnotationLayer::updateMosaic(const Annotation& a) {
    if (a.type != AnnotationType::Mosaic || a.points.isEmpty() || baseImage_.isNull()) return;

    if (mosaicLayer_.isNull()) {
        // Allocated lazily and tagged with dpr_ once, so paint()/renderToImage() can
        // blit it without a detaching copy on every frame. Pixel access below goes
        // through scanLine() and is therefore unaffected by the device pixel ratio.
        mosaicLayer_ = QImage(baseImage_.size(), QImage::Format_ARGB32);
        mosaicLayer_.fill(Qt::transparent);
        mosaicLayer_.setDevicePixelRatio(dpr_);
        
        // Byte mask only, never drawn -- stays at 1:1 with the physical pixels.
        mosaicMask_ = QImage(baseImage_.size(), QImage::Format_Grayscale8);
        mosaicMask_.fill(0);
        mosaicMask_.setDevicePixelRatio(1.0);
    }

    int mSizePhysical = qRound(a.mosaicSize * dpr_);
    if (mSizePhysical < 1) mSizePhysical = 1;
    
    QPainterPath path;
    path.moveTo(a.points.first());
    for (int i = 1; i < a.points.size(); ++i) {
        path.lineTo(a.points[i]);
    }
    
    QRect logicalBounding = path.boundingRect().toAlignedRect();
    logicalBounding.adjust(-a.mosaicSize, -a.mosaicSize, a.mosaicSize, a.mosaicSize);
    
    QRect physicalBounding(
        qRound(logicalBounding.x() * dpr_),
        qRound(logicalBounding.y() * dpr_),
        qRound(logicalBounding.width() * dpr_),
        qRound(logicalBounding.height() * dpr_)
    );
    physicalBounding = physicalBounding.intersected(baseImage_.rect());
    if (physicalBounding.isEmpty()) return;

    int startY = (physicalBounding.top() / mSizePhysical) * mSizePhysical;
    int startX = (physicalBounding.left() / mSizePhysical) * mSizePhysical;
    int endY = qMin(baseImage_.height(), physicalBounding.bottom() + mSizePhysical);
    int endX = qMin(baseImage_.width(), physicalBounding.right() + mSizePhysical);

    QImage strokeMask(physicalBounding.size(), QImage::Format_Grayscale8);
    strokeMask.fill(0);
    strokeMask.setDevicePixelRatio(dpr_); 
    
    QPainter p(&strokeMask);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.translate(-logicalBounding.topLeft());
    QPen pen(Qt::white, a.mosaicSize, Qt::SolidLine, Qt::SquareCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawPath(path);
    if (a.points.size() == 1) {
        p.drawPoint(a.points.first());
    }
    p.end();
    
    int strokeOffsetX = physicalBounding.x();
    int strokeOffsetY = physicalBounding.y();

    for (int y = startY; y < endY; y += mSizePhysical) {
        for (int x = startX; x < endX; x += mSizePhysical) {
            int bw = qMin(mSizePhysical, baseImage_.width() - x);
            int bh = qMin(mSizePhysical, baseImage_.height() - y);
            
            bool needsMosaic = false;
            for (int by = 0; by < bh && !needsMosaic; ++by) {
                int py = y + by - strokeOffsetY;
                if (py < 0 || py >= strokeMask.height()) continue;
                const uchar* maskRow = strokeMask.scanLine(py);
                for (int bx = 0; bx < bw; ++bx) {
                    int px = x + bx - strokeOffsetX;
                    if (px >= 0 && px < strokeMask.width() && maskRow[px] > 0) {
                        needsMosaic = true;
                        break;
                    }
                }
            }
            
            if (needsMosaic) {
                bool alreadyDone = true;
                for (int by = 0; by < bh && alreadyDone; ++by) {
                    const uchar* mMaskRow = mosaicMask_.scanLine(y + by);
                    for (int bx = 0; bx < bw; ++bx) {
                        if (mMaskRow[x + bx] == 0) {
                            alreadyDone = false;
                            break;
                        }
                    }
                }
                
                if (!alreadyDone) {
                    long r = 0, g = 0, b = 0;
                    for (int by = 0; by < bh; ++by) {
                        const QRgb* srcRow = reinterpret_cast<const QRgb*>(baseImage_.scanLine(y + by));
                        for (int bx = 0; bx < bw; ++bx) {
                            QRgb px = srcRow[x + bx];
                            r += qRed(px);
                            g += qGreen(px);
                            b += qBlue(px);
                        }
                    }
                    int count = bw * bh;
                    QRgb avg = qRgb(r / count, g / count, b / count);
                    
                    for (int by = 0; by < bh; ++by) {
                        QRgb* destRow = reinterpret_cast<QRgb*>(mosaicLayer_.scanLine(y + by));
                        uchar* mMaskRow = mosaicMask_.scanLine(y + by);
                        for (int bx = 0; bx < bw; ++bx) {
                            destRow[x + bx] = avg;
                            mMaskRow[x + bx] = 255;
                        }
                    }
                }
            }
        }
    }
}

} // namespace qshot
