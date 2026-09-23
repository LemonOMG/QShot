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

AnnotationType AnnotationLayer::undo() {
    if (annotations_.isEmpty()) return AnnotationType::None;
    const AnnotationType removed = annotations_.last().type;
    annotations_.removeLast();
    rebuildMosaicCache();
    return removed;
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
    //
    // Gated on mosaicInkPresent_ rather than on the image being non-null: the layer is
    // allocated once and then survives an undo or a clear, so a null check would blit a
    // fully transparent selection-sized image on every frame of every later interaction.
    if (mosaicInkPresent_) {
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
        case AnnotationType::Highlight: {
            // A marker, not a pen: flat caps, so the stroke starts and ends exactly where
            // the user dragged. Square caps overshoot by half the pen width at *each* end
            // -- 9px at the default width -- which is plainly visible when the whole point
            // of the gesture was to align the highlight with one line of text.
            //
            // The transparency comes from the colour's own alpha, not from a composition
            // mode, so overlapping passes inside a single stroke stay one flat tone.
            const qreal half = a.lineWidth / 2.0;
            if (a.points.size() == 1) {
                // A flat-capped zero-length line has no area at all, so a plain tap would
                // leave no mark and the tool would look broken. Paint the cap explicitly,
                // as a round dot -- which is also what a real marker does when touched to
                // paper.
                p.setPen(Qt::NoPen);
                p.setBrush(a.color);
                p.drawEllipse(QPointF(a.points.first()), half, half);
                break;
            }
            QPainterPath marker;
            marker.moveTo(a.points.first());
            for (int i = 1; i < a.points.size(); ++i) {
                marker.lineTo(a.points[i]);
            }
            p.setPen(QPen(a.color, a.lineWidth, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawPath(marker);
            break;
        }
        case AnnotationType::Number: {
            // A filled disc with a contrasting ring and a centred digit.
            //
            // The ring is not decoration: the badge lands on an arbitrary screenshot, so a
            // red disc on a red region would disappear without it, and a white digit on a
            // white region likewise. A dark ring around the disc plus a dark halo around
            // the glyph keeps both readable on any background.
            const int d = qMax(8, a.badgeDiameter);
            const QRectF disc(a.points.first().x() - d / 2.0, a.points.first().y() - d / 2.0,
                              d, d);

            p.setPen(QPen(QColor(0, 0, 0, 90), qMax(1.0, d / 16.0)));
            p.setBrush(a.color);
            p.drawEllipse(disc);

            const QString label = QString::number(a.number > 0 ? a.number : 1);
            QFont font = p.font();
            // Derived from the disc rather than from a fixed point size, so the digit
            // scales with the badge and never overflows it.
            font.setPixelSize(qMax(8, qRound(d * 0.58)));
            font.setBold(true);
            p.setFont(font);

            // Two passes: a dark halo underneath, then the glyph. Without the halo a
            // white digit on a light background is invisible; with it, one pass is enough
            // for both light and dark backgrounds.
            QRectF textRect = disc.adjusted(-d, -d, d, d);
            p.setPen(QColor(0, 0, 0, 130));
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    p.drawText(textRect.translated(dx, dy),
                               Qt::AlignCenter, label);
                }
            }
            p.setPen(Qt::white);
            p.drawText(textRect, Qt::AlignCenter, label);
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
    // Draw mosaic layer if it holds anything (it already carries dpr_).
    if (mosaicInkPresent_) {
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
    } else {
        // Keep baseImage_ consistent with the current selection: leaving the
        // previous crop in place would make a later updateMosaic() sample the
        // wrong pixels.
        baseImage_ = QImage();
    }
    
    mosaicLayer_ = QImage();
    mosaicMask_ = QImage();
    
    rebuildMosaicCache();
}

void AnnotationLayer::rebuildMosaicCache() {
    // Reset before replaying: the flag describes the layer, and the layer is about to be
    // emptied. updateMosaic() sets it again for every annotation that actually lays a
    // block down, so an undo that removes the last mosaic correctly leaves it false.
    mosaicInkPresent_ = false;
    mosaicLayer_.fill(Qt::transparent);
    mosaicMask_.fill(0);
    for (const auto& a : annotations_) {
        if (a.type == AnnotationType::Mosaic) {
            updateMosaic(a);
        }
    }
}

void AnnotationLayer::updateMosaic(const Annotation& a, const QRect& logicalDirty) {
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
    
    // Either the whole stroke or just the part the caller says is new. See the header for
    // why clipping to the increment cannot change the result.
    QRect logicalBounding = logicalDirty.isEmpty() ? path.boundingRect().toAlignedRect()
                                                   : logicalDirty;
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
    // Translate by the *physical* origin converted back to logical, not by the logical
    // origin. The two are the same point only while physicalBounding is unclipped: the
    // rectangle is intersected with the image above, so a stroke that reaches the selection
    // edge moves its own mask's origin inward -- and translating by logicalBounding then
    // misplaces every pixel by exactly that clip amount. A tap in the top-left corner had
    // its mosaic pushed a whole block diagonally. Dividing back by dpr_ is exact, and
    // collapses to the old expression whenever nothing was clipped.
    p.translate(-physicalBounding.x() / dpr_, -physicalBounding.y() / dpr_);
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
                    // Set where the block is written, not where the layer is allocated:
                    // an allocated-but-empty layer is exactly the case the paint path has
                    // to be able to skip.
                    mosaicInkPresent_ = true;
                }
            }
        }
    }
}

} // namespace qshot
