#pragma once

#include "core/IWindowDetector.h"

namespace qshot {

class WinWindowDetector : public IWindowDetector {
public:
    QRect windowRectAt(const QPoint& globalPos) const override;
};

} // namespace qshot
