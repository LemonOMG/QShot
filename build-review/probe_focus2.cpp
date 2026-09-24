// Probe: mirror the real SnapOverlay/ToolbarWidget structure and check who owns
// keyboard focus after showToolbar() (toolbar_->show()), i.e. whether Esc /
// Ctrl+Z / Enter still reach the overlay once a selection exists.
#include <QApplication>
#include <QWidget>
#include <QWindow>
#include <QKeyEvent>
#include <QThread>
#include <cstdio>

struct Plain : QWidget {
    explicit Plain(QWidget* parent = nullptr) : QWidget(parent) {}
};

struct Overlay : QWidget {
    bool got = false;
    void keyPressEvent(QKeyEvent* e) override {
        got = true;
        e->accept();
    }
};

static void settle() {
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(15);
    }
}

static void report(const char* stage, Overlay* overlay) {
    QWindow* fw = QGuiApplication::focusWindow();
    printf("%-28s focusWindow=%s  activeWindow=%s  overlay.got=%d\n",
           stage,
           fw ? fw->metaObject()->className() : "null",
           QApplication::activeWindow()
               ? QApplication::activeWindow()->metaObject()->className()
               : "null",
           (int)overlay->got);
    fflush(stdout);
}

static void tryEsc(Overlay* overlay) {
    overlay->got = false;
    QWindow* fw = QGuiApplication::focusWindow();
    if (!fw) {
        printf("    -> no focusWindow, Esc dropped\n");
        return;
    }
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(fw, &esc);
    printf("    -> Esc delivered to focusWindow, overlay.got = %d\n", (int)overlay->got);
    fflush(stdout);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    Overlay* overlay = new Overlay;
    overlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::Tool);
    overlay->resize(500, 360);
    overlay->show();
    overlay->activateWindow();
    settle();
    report("[1] overlay shown", overlay);
    tryEsc(overlay);

    // showToolbar() equivalent: toolbar_->updatePosition(); toolbar_->show();
    Plain* toolbar = new Plain(overlay);
    toolbar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    toolbar->setAttribute(Qt::WA_TranslucentBackground);
    toolbar->resize(300, 40);
    toolbar->move(100, 380);
    toolbar->show();
    settle();
    report("[2] toolbar->show()", overlay);
    tryEsc(overlay);

    // hideToolbar() equivalent
    toolbar->hide();
    settle();
    report("[3] toolbar->hide()", overlay);
    tryEsc(overlay);

    // Does an explicit activateWindow() on the overlay restore it?
    overlay->activateWindow();
    settle();
    report("[4] overlay->activateWindow()", overlay);
    tryEsc(overlay);

    return 0;
}
