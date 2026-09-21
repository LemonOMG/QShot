#pragma once

#include "core/IScreenCapture.h"

namespace qshot {

class WinScreenCapture : public IScreenCapture {
public:
    QPixmap captureScreen(QScreen* screen, bool includeCursor) override;
};

} // namespace qshot
