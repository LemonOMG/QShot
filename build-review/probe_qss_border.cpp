// Which QTextEdit stylesheet actually produces a visible frame?
// Renders each variant and reports how many border-ish pixels are present.
#include <QApplication>
#include <QTextEdit>
#include <QPainter>
#include <QImage>
#include <QThread>
#include <cstdio>

static void settle(int ms = 200) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

static void test(const char* name, const QString& qss) {
    QTextEdit* te = new QTextEdit;
    te->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    te->setAttribute(Qt::WA_TranslucentBackground);
    te->setStyleSheet(qss);
    te->setPlaceholderText(QStringLiteral("输入文字…"));
    te->resize(180, 46);
    te->show();
    settle();

    QImage img = te->grab().toImage();
    img.setDevicePixelRatio(1.0);
    // Count "frame" pixels on the top row band and left column band.
    int frame = 0;
    for (int x = 0; x < img.width(); ++x) {
        for (int y = 0; y < 4; ++y) {
            if (qAlpha(img.pixel(x, y)) > 8) ++frame;
        }
    }
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < 4; ++x) {
            if (qAlpha(img.pixel(x, y)) > 8) ++frame;
        }
    }
    printf("%-58s frame px=%4d  %s\n", name, frame,
           frame > 50 ? "VISIBLE" : "missing/too faint");
    fflush(stdout);

    QImage canvas(img.size() + QSize(16, 16), QImage::Format_ARGB32);
    canvas.fill(QColor(176, 190, 210));
    QPainter p(&canvas);
    p.drawImage(8, 8, img);
    p.end();
    canvas.save(QString("qss_%1.png").arg(name).replace(' ', '_').replace(';', '_')
                    .replace(':', '_').replace('{', '_').replace('}', '_'));
    te->hide();
    delete te;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    test("A_old_1px_666", "QTextEdit { background: transparent; border: 1px dashed #666666; padding: 2px; }");
    test("B_new_rgba_spaces", "QTextEdit { background: rgba(255, 255, 255, 30); border: 2px dashed #1AAD19; padding: 3px; }");
    test("C_rgba_no_spaces", "QTextEdit { background: rgba(255,255,255,30); border: 2px dashed #1AAD19; padding: 3px; }");
    test("D_border_only", "QTextEdit { border: 2px dashed #1AAD19; }");
    test("E_bgcolor_rgba", "QTextEdit { background-color: rgba(255,255,255,30); border: 2px dashed #1AAD19; }");
    test("F_solid_green_2px", "QTextEdit { background: transparent; border: 2px solid #1AAD19; padding: 3px; }");
    return 0;
}
