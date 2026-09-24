// Probe: does QPainter honour the target QImage's devicePixelRatio?
// Decides whether AnnotationLayer::renderToImage()/updateMosaic() coordinates
// (which are logical) land correctly on physical-pixel images.
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <cstdio>

static void probe(const char* name, const QImage& img, int x, int y) {
    printf("  %-46s pixel(%d,%d) = %s\n", name, x, y,
           img.pixelColor(x, y).name().toUtf8().constData());
    fflush(stdout);
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // ---- Test 1: fillRect(0,0,10,10) on a 30x30 image with dpr 1.5 ----
    {
        QImage img(30, 30, QImage::Format_ARGB32);
        img.fill(Qt::white);
        img.setDevicePixelRatio(1.5);
        QPainter p(&img);
        p.fillRect(QRect(0, 0, 10, 10), Qt::red);
        p.end();
        printf("[T1] fillRect(0,0,10,10) on a dpr=1.5 target\n");
        probe("(5,5) must be red either way", img, 5, 5);
        probe("(12,12) red => QPainter SCALES by dpr", img, 12, 12);
        probe("(14,14) red => SCALES", img, 14, 14);
    }

    // ---- Test 2: drawImage of a dpr'd source onto a dpr'd target ----
    {
        QImage src(20, 20, QImage::Format_ARGB32);
        src.fill(Qt::blue);
        src.setDevicePixelRatio(2.0);

        QImage dst(40, 40, QImage::Format_ARGB32);
        dst.fill(Qt::white);
        dst.setDevicePixelRatio(2.0);
        QPainter p(&dst);
        p.drawImage(0, 0, src);
        p.end();
        printf("[T2] drawImage(src 20px@dpr2) into dst 40px@dpr2\n");
        probe("(9,9) blue expected", dst, 9, 9);
        probe("(11,11) blue => source dpr honoured (10px)", dst, 11, 11);
        probe("(19,19) blue => source dpr ignored (20px)", dst, 19, 19);
    }

    // ---- Test 3: mosaic-layer style blit, equal dpr ----
    {
        QImage src(30, 30, QImage::Format_ARGB32);
        src.fill(Qt::green);
        src.setDevicePixelRatio(1.5);

        QImage dst(60, 60, QImage::Format_ARGB32);
        dst.fill(Qt::white);
        dst.setDevicePixelRatio(1.5);
        QPainter p(&dst);
        p.drawImage(0, 0, src);
        p.end();
        printf("[T3] mosaic blit: src 30px@1.5 -> dst 60px@1.5\n");
        probe("(29,29) green => covers 30px", dst, 29, 29);
        probe("(31,31) white => mosaic left half blank", dst, 31, 31);
        probe("(59,59) ", dst, 59, 59);
    }

    // ---- Test 4: logical-coord stroke into a physical-size mask (updateMosaic) ----
    {
        QImage mask(60, 60, QImage::Format_Grayscale8);
        mask.fill(0);
        mask.setDevicePixelRatio(1.5);
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(Qt::white, 16, Qt::SolidLine, Qt::SquareCap, Qt::RoundJoin));
        p.drawLine(QPoint(10, 10), QPoint(20, 20));
        p.end();
        int minX = 1 << 30, maxX = -1, minY = 1 << 30, maxY = -1, n = 0;
        for (int y = 0; y < mask.height(); ++y) {
            const uchar* row = mask.constScanLine(y);
            for (int x = 0; x < mask.width(); ++x) {
                if (row[x] > 0) {
                    ++n;
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        printf("[T4] logical-coord stroke (10,10)-(20,20) pen 16 into 60px mask@dpr1.5\n");
        printf("     white px=%d  bbox=(%d,%d)-(%d,%d)\n", n, minX, minY, maxX, maxY);
        printf("     dpr applied => bbox ~ (3,3)-(42,42)\n");
        printf("     dpr ignored => bbox ~ (2,2)-(28,28)\n");
    }

    return 0;
}
