#pragma once

#include <QVector>
#include <QPoint>
#include <QColor>
#include <QString>
#include "AnnotationType.h"

namespace qshot {

struct Annotation {
    AnnotationType type;
    QVector<QPoint> points;   // 逻辑坐标，相对选区左上角
    QColor color;
    int lineWidth;            // 逻辑像素
    int mosaicSize = 0;       // 阶段 B 用
    int fontSize = 0;         // 阶段 B 用
    QString text;             // 阶段 B 用
};

} // namespace qshot
