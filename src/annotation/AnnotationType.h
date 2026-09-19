#pragma once

namespace qshot {

enum class AnnotationType {
    None,       // 无（用于默认未选中工具的状态）
    Rectangle,
    Ellipse,    // 阶段 B
    Arrow,      // 阶段 B
    Pen,        // 画笔
    Mosaic,     // 阶段 B
    Text        // 阶段 B
};

} // namespace qshot
