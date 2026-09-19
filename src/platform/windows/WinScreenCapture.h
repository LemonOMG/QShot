#pragma once

#include "core/IScreenCapture.h"

namespace qshot {

class WinScreenCapture : public IScreenCapture {
public:
    QPixmap captureEntireScreen() override;
};

} // namespace qshot
