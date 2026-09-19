#pragma once

#include <QRect>
#include <QPoint>

namespace qshot {

class IWindowDetector {
public:
    virtual ~IWindowDetector() = default;
    
    // Returns the logical screen rectangle of the top-level window under the global position.
    // Returns an empty QRect if no valid window is found.
    virtual QRect windowRectAt(const QPoint& globalPos) const = 0;
};

} // namespace qshot
