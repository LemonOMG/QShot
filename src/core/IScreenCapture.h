#pragma once

#include <QPixmap>
#include <QRect>

// Only ever used as a pointer in this interface, so the full definition is not needed here.
// Keeping the include out means a translation unit that only implements or consumes the
// interface does not drag QScreen in, and -- more to the point -- the interface stops
// advertising a dependency it does not actually have.
QT_BEGIN_NAMESPACE
class QScreen;
QT_END_NAMESPACE

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
