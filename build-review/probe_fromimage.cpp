// Probe: does QPixmap::fromImage() carry the QImage's devicePixelRatio over?
// Decides whether the extra setDevicePixelRatio() in copyToClipboard() is redundant.
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <cstdio>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    QImage img(60, 40, QImage::Format_ARGB32);
    img.fill(Qt::red);
    img.setDevicePixelRatio(1.5);

    QPixmap pm = QPixmap::fromImage(img);
    printf("image dpr = %.2f   pixmap dpr after fromImage() = %.2f   %s\n",
           img.devicePixelRatio(), pm.devicePixelRatio(),
           qFuzzyCompare(pm.devicePixelRatio(), 1.5) ? "PRESERVED (extra call is redundant)"
                                                     : "LOST (keep the extra call)");
    return 0;
}
