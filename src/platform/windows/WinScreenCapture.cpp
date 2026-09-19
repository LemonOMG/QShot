#include "WinScreenCapture.h"
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>

namespace qshot {

QPixmap WinScreenCapture::captureEntireScreen() {
    auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        return QPixmap();
    }

    // Calculate total virtual geometry and maximum device pixel ratio
    QRect virtualGeometry;
    qreal maxDpr = 1.0;
    for (QScreen* screen : screens) {
        virtualGeometry = virtualGeometry.united(screen->geometry());
        if (screen->devicePixelRatio() > maxDpr) {
            maxDpr = screen->devicePixelRatio();
        }
    }

    // Create a large pixmap to hold all screens
    QPixmap fullPixmap(virtualGeometry.size() * maxDpr);
    fullPixmap.setDevicePixelRatio(maxDpr);
    fullPixmap.fill(Qt::transparent);

    QPainter painter(&fullPixmap);
    
    // Draw each screen onto the large pixmap
    for (QScreen* screen : screens) {
        QPixmap screenPixmap = screen->grabWindow(0);
        
        // Calculate offset relative to the virtual geometry's top-left
        QPoint offset = screen->geometry().topLeft() - virtualGeometry.topLeft();
        painter.drawPixmap(offset, screenPixmap);
    }
    
    painter.end();
    
    return fullPixmap;
}

} // namespace qshot
