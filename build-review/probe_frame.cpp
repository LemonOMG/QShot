// Determine (a) whether grab() captures content drawn by a QTextEdit subclass's own
// paintEvent in a translucent top-level window, and (b) whether a stylesheet border
// is rendered at all. Decides how to implement the text editor frame.
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

// Draws an opaque frame around itself after the base class paints.
class FramedEdit : public QTextEdit {
public:
    explicit FramedEdit(QWidget* parent = nullptr) : QTextEdit(parent) {}
    bool shrinkViewport = false;
protected:
    void paintEvent(QPaintEvent* e) override {
        QTextEdit::paintEvent(e);
        QPainter p(this);
        p.setPen(QPen(QColor(255, 0, 0), 4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(2, 2, -3, -3));
    }
    void resizeEvent(QResizeEvent* e) override {
        QTextEdit::resizeEvent(e);
        if (shrinkViewport) setViewportMargins(6, 6, 6, 6);
    }
};

static int countFrame(const QImage& img) {
    int n = 0;
    for (int x = 0; x < img.width(); ++x)
        for (int y = 0; y < 5; ++y)
            if (qAlpha(img.pixel(x, y)) > 8) ++n;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < 5; ++x)
            if (qAlpha(img.pixel(x, y)) > 8) ++n;
    return n;
}

static void report(const char* name, QWidget* w, const char* file) {
    QImage img = w->grab().toImage();
    img.setDevicePixelRatio(1.0);
    printf("%-40s frame px=%4d\n", name, countFrame(img));
    QImage canvas(img.size() + QSize(16, 16), QImage::Format_ARGB32);
    canvas.fill(QColor(176, 190, 210));
    QPainter p(&canvas);
    p.drawImage(8, 8, img);
    p.end();
    canvas.save(file);
    fflush(stdout);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 1) plain QTextEdit with the OLD stylesheet
    {
        QTextEdit* te = new QTextEdit;
        te->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        te->setAttribute(Qt::WA_TranslucentBackground);
        te->setStyleSheet("QTextEdit { background: transparent; border: 1px dashed #666666; padding: 2px; }");
        te->resize(180, 46);
        te->show();
        settle();
        report("1 plain + OLD stylesheet border", te, "frame_1_old_qss.png");
        te->hide();
    }

    // 2) subclass drawing its own frame, no viewport margins
    {
        FramedEdit* fe = new FramedEdit;
        fe->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        fe->setAttribute(Qt::WA_TranslucentBackground);
        fe->resize(180, 46);
        fe->show();
        settle();
        report("2 self-drawn frame, no viewport margin", fe, "frame_2_selfdrawn.png");
        fe->hide();
    }

    // 3) subclass drawing its own frame + shrunken viewport
    {
        FramedEdit* fe = new FramedEdit;
        fe->shrinkViewport = true;
        fe->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        fe->setAttribute(Qt::WA_TranslucentBackground);
        fe->resize(180, 46);
        fe->show();
        settle();
        report("3 self-drawn frame + viewport margin", fe, "frame_3_selfdrawn_margin.png");
        fe->hide();
    }

    return 0;
}
