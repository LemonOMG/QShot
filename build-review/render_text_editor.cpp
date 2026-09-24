// Render the text editor box so its new frame + placeholder can be inspected.
#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QImage>
#include <QThread>
#include <cstdio>
#include "overlay/TextInputWidget.h"

static void settle(int ms = 250) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

static void save(QWidget* w, const QString& path) {
    QImage front = w->grab().toImage();
    front.setDevicePixelRatio(1.0);
    QImage canvas(front.size() + QSize(24, 24), QImage::Format_ARGB32);
    canvas.fill(QColor(176, 190, 210)); // stand-in for a light screenshot
    QPainter p(&canvas);
    p.drawImage(12, 12, front);
    p.end();
    printf("saved %-26s editor=%dx%d\n", path.toUtf8().constData(),
           front.width(), front.height());
    canvas.save(path);
    fflush(stdout);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QWidget* owner = new QWidget;
    owner->resize(600, 400);
    owner->show();
    settle();

    qshot::TextInputWidget* te = new qshot::TextInputWidget(owner);
    // startInput() takes a GLOBAL desktop point (the editor is a top-level window,
    // so move() reads it globally). This scaffold has no real overlay, so any
    // absolute point works -- what is rendered is the chrome, not the placement.
    te->startInput(QPoint(120, 160), QColor("#FB8C00"), 18);
    settle();
    save(te, "text_editor_empty.png");   // placeholder visible

    te->setPlainText(QStringLiteral("示例文字"));
    settle();
    save(te, "text_editor_typed.png");   // text in the annotation colour

    return 0;
}
