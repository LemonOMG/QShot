#pragma once

#include <QPixmap>
#include <QRect>

namespace qshot {

class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    /**
     * @brief Capture all connected screens into a single large pixmap
     * @return A pixmap covering the virtual geometry of all screens
     */
    virtual QPixmap captureEntireScreen() = 0;
};

} // namespace qshot
