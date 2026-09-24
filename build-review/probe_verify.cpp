// Acceptance probe: replays the exact call order of the FIXED SnapOverlay
// (showToolbar / handleToolSelection / startInput / editingFinished / hideToolbar)
// and asserts who owns the keyboard at every step.
#include <QApplication>
#include <QWidget>
#include <QTextEdit>
#include <QWindow>
#include <QKeyEvent>
#include <QThread>
#include <cstdio>

struct Overlay : QWidget {
    bool gotEsc = false;
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) gotEsc = true;
        e->accept();
    }
};

struct Panel : QWidget {
    explicit Panel(QWidget* parent = nullptr) : QWidget(parent) {}
};

static int failures = 0;

static void settle() {
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

static void check(const char* what, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
    fflush(stdout);
}

static QWindow* fw() { return QGuiApplication::focusWindow(); }

static bool escReaches(Overlay* o) {
    o->gotEsc = false;
    if (!fw()) return false;
    QKeyEvent e(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(fw(), &e);
    return o->gotEsc;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    Overlay* overlay = new Overlay;
    overlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::Tool);
    overlay->resize(500, 360);
    overlay->show();
    overlay->activateWindow();          // ShotApplication: after show()
    settle();
    printf("step 1 - overlay shown\n");
    check("focusWindow == overlay", fw() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));

    // showToolbar(): updatePosition() (may show sub-panel) + show() + reclaim
    Panel* toolbar = new Panel(overlay);
    toolbar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    toolbar->resize(300, 40);
    Panel* sub = new Panel(overlay);
    sub->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                        | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    sub->resize(240, 36);
    sub->show();
    toolbar->show();
    overlay->activateWindow();          // reclaimKeyboardFocus()
    settle();
    printf("step 2 - showToolbar() [toolbar + sub-panel shown]\n");
    check("focusWindow == overlay", fw() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));
    check("toolbar still visible", toolbar->isVisible());

    // handleToolSelection() -> reclaimKeyboardFocus()
    overlay->activateWindow();
    settle();
    printf("step 3 - tool button clicked (handleToolSelection)\n");
    check("focusWindow == overlay", fw() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));

    // TextInputWidget::startInput(): show() + activateWindow() + setFocus()
    QTextEdit* te = new QTextEdit(overlay);
    te->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    te->resize(160, 40);
    te->show();
    te->activateWindow();
    te->setFocus();
    settle();
    printf("step 4 - startInput() [text editor open]\n");
    check("focusWindow == text editor", fw() == te->windowHandle());
    {
        QKeyEvent h(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
        QKeyEvent i(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier, QStringLiteral("i"));
        QCoreApplication::sendEvent(fw(), &h);
        QCoreApplication::sendEvent(fw(), &i);
        settle();
        printf("  typing \"hi\" -> \"%s\"\n", te->toPlainText().toUtf8().constData());
        check("typed text landed in the editor", te->toPlainText() == QStringLiteral("hi"));
    }

    // editingFinished: editor hides itself, then reclaimKeyboardFocus()
    te->hide();
    overlay->activateWindow();
    settle();
    printf("step 5 - text committed (editingFinished)\n");
    check("focusWindow == overlay", fw() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));

    // hideToolbar()
    toolbar->hide();
    sub->hide();
    overlay->activateWindow();
    settle();
    printf("step 6 - hideToolbar()\n");
    check("focusWindow == overlay", fw() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));

    printf("\n%s (%d failure(s))\n", failures == 0 ? "ALL CHECKS PASSED" : "FAILURES PRESENT",
           failures);
    return failures == 0 ? 0 : 1;
}
