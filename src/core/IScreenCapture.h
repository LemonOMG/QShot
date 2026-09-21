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
     * @param includeCursor Draw the system cursor into the result
     * @return A pixmap of the screen
     */
    virtual QPixmap captureScreen(QScreen* screen, bool includeCursor) = 0;
};

} // namespace qshot
