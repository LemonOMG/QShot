#pragma once

#include <QPixmap>
#include <QRect>
#include <QScreen>

namespace qshot {

class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    /**
     * @brief Capture a specific screen
     * @param screen The screen to capture
     * @return A pixmap of the screen
     */
    virtual QPixmap captureScreen(QScreen* screen) = 0;
};

} // namespace qshot
