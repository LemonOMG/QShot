#include <QGuiApplication>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QString>
#include <cstdio>

static void out(const QString& s) { fputs(s.toUtf8().constData(), stderr); fputc('\n', stderr); }

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const qreal dpr = 2.0;
    QPixmap full(800 * dpr, 600 * dpr);
    full.setDevicePixelRatio(dpr);
    full.fill(Qt::red);
    {
        QPainter p(&full);
        p.fillRect(QRect(100, 100, 50, 50), Qt::blue);
    }

    out(QString("full.size()                  = %1 x %2").arg(full.size().width()).arg(full.size().height()));
    out(QString("full.rect()                  = %1,%2 %3x%4").arg(full.rect().x()).arg(full.rect().y())
            .arg(full.rect().width()).arg(full.rect().height()));
    out(QString("full.deviceIndependentSize() = %1 x %2").arg(full.deviceIndependentSize().width()).arg(full.deviceIndependentSize().height()));

    QRect sel(100, 100, 50, 50);

    QRect manual(qRound(sel.x() * dpr), qRound(sel.y() * dpr),
                 qRound(sel.width() * dpr), qRound(sel.height() * dpr));
    manual = manual.intersected(full.rect());
    QPixmap viaManual = full.copy(manual);
    out(QString("[existing] rect passed = %1,%2 %3x%4").arg(manual.x()).arg(manual.y())
            .arg(manual.width()).arg(manual.height()));

    QPixmap viaLogical = full.copy(sel);

    auto describe = [](const char* name, const QPixmap& pm) {
        if (pm.isNull()) { out(QString("%1 = NULL").arg(QString::fromUtf8(name))); return; }
        QImage img = pm.toImage();
        QColor c = img.pixelColor(img.width() / 2, img.height() / 2);
        out(QString("%1 physical=%2x%3 dpr=%4 centerColor=%5 %6")
                .arg(QString::fromUtf8(name))
                .arg(pm.size().width()).arg(pm.size().height())
                .arg(pm.devicePixelRatio())
                .arg(c.name())
                .arg(c == QColor(Qt::blue) ? "[BLUE -> crop CORRECT]" : "[NOT blue -> crop WRONG]"));
    };
    describe("[existing] manual*dpr then copy()", viaManual);
    describe("[suggest]  copy(logicalRect)    ", viaLogical);

    QImage im = full.toImage();
    out(QString("QImage: size=%1x%2 dpr=%3 valid(QPoint(200,200))=%4")
            .arg(im.width()).arg(im.height()).arg(im.devicePixelRatio())
            .arg(im.valid(QPoint(200, 200)) ? "true" : "false"));
    return 0;
}
