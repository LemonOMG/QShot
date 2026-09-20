#include "WinScreenCapture.h"
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>

namespace qshot {

QPixmap WinScreenCapture::captureScreen(QScreen* screen) {
    if (!screen) {
        return QPixmap();
    }
    return screen->grabWindow(0);
}

} // namespace qshot
