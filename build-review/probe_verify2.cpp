// Acceptance probe, second attempt: the first version was unreliable because a
// probe launched from a terminal never owns the OS foreground, and Windows refuses
// SetForegroundWindow() in that case. Here we force the foreground with the
// standard AttachThreadInput trick so the activation semantics match a real run.
#include <QApplication>
#include <QWidget>
#include <QTextEdit>
#include <QWindow>
#include <QKeyEvent>
#include <QThread>
#include <cstdio>
#include <windows.h>

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

static void settle(int ms = 250) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

static void forceForeground(QWidget* w) {
    HWND h = reinterpret_cast<HWND>(w->winId());
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();
    if (fgThread && fgThread != myThread) AttachThreadInput(myThread, fgThread, TRUE);
    ShowWindow(h, SW_SHOW);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    SetActiveWindow(h);
    if (fgThread && fgThread != myThread) AttachThreadInput(myThread, fgThread, FALSE);
}

static bool foregroundIs(QWidget* w) {
    return GetForegroundWindow() == reinterpret_cast<HWND>(w->winId());
}

static void check(const char* what, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
    fflush(stdout);
}

static bool escReaches(Overlay* o) {
    o->gotEsc = false;
    QWindow* f = QGuiApplication::focusWindow();
    if (!f) return false;
    QKeyEvent e(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(f, &e);
    return o->gotEsc;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    Overlay* overlay = new Overlay;
    overlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::Tool);
    overlay->resize(500, 360);
    overlay->show();
    overlay->activateWindow();
    forceForeground(overlay);
    settle();
    printf("step 1 - overlay shown\n");
    check("Win32 foreground == overlay", foregroundIs(overlay));
    check("focusWindow == overlay", QGuiApplication::focusWindow() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));

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
    overlay->activateWindow();
    forceForeground(overlay);
    settle();
    printf("step 2 - showToolbar() [toolbar + sub-panel shown]\n");
    check("focusWindow == overlay", QGuiApplication::focusWindow() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));
    check("toolbar visible", toolbar->isVisible());
    check("toolbar does not accept focus",
          toolbar->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));

    // Tool button clicked: real code path is a mouse click on the toolbar, which
    // must not steal activation because of WindowDoesNotAcceptFocus.
    forceForeground(toolbar);
    settle();
    printf("step 3 - attempt to activate the toolbar itself (simulates clicking it)\n");
    check("toolbar refuses activation, overlay still focused",
          QGuiApplication::focusWindow() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));

    QTextEdit* te = new QTextEdit(overlay);
    te->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    te->resize(160, 40);
    te->show();
    te->activateWindow();
    te->setFocus();
    settle();
    printf("step 4 - startInput() [text editor open]\n");
    check("focusWindow == text editor", QGuiApplication::focusWindow() == te->windowHandle());
    {
        QKeyEvent h(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
        QKeyEvent i(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier, QStringLiteral("i"));
        QCoreApplication::sendEvent(QGuiApplication::focusWindow(), &h);
        QCoreApplication::sendEvent(QGuiApplication::focusWindow(), &i);
        settle();
        printf("  typing \"hi\" -> \"%s\"\n", te->toPlainText().toUtf8().constData());
        check("typed text landed in the editor", te->toPlainText() == QStringLiteral("hi"));
    }

    te->hide();
    overlay->activateWindow();
    forceForeground(overlay);
    settle();
    printf("step 5 - text committed (editingFinished)\n");
    check("focusWindow == overlay", QGuiApplication::focusWindow() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));

    toolbar->hide();
    sub->hide();
    overlay->activateWindow();
    forceForeground(overlay);
    settle();
    printf("step 6 - hideToolbar()\n");
    check("focusWindow == overlay", QGuiApplication::focusWindow() == overlay->windowHandle());
    check("Esc reaches overlay", escReaches(overlay));

    printf("\n%s (%d failure(s))\n",
           failures == 0 ? "ALL CHECKS PASSED" : "FAILURES PRESENT", failures);
    return failures == 0 ? 0 : 1;
}
