#pragma once

#include <QVector>
#include <QPoint>
#include <QColor>
#include <QString>
#include "AnnotationType.h"

namespace qshot {

struct Annotation {
    AnnotationType type = AnnotationType::None;
    QVector<QPoint> points;   // 逻辑坐标，相对选区左上角
    QColor color = Qt::red;
    int lineWidth = 4;            // 逻辑像素
    int mosaicSize = 16;       // 阶段 B 用
    int fontSize = 18;         // 阶段 B 用
    int badgeDiameter = 28;    // 序号徽标直径（逻辑像素）
    int number = 0;            // 序号值，仅 Number 用；0 表示尚未分配
    QString text;             // 阶段 B 用
};

} // namespace qshot
