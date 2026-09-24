// Probe: validate the intended fix for R3-1/R3-2/R3-3 before touching the app.
//  (a) Qt::WindowDoesNotAcceptFocus on panels: does the overlay keep focus?
//  (b) overlay->activateWindow() right after toolbar->show(): does Esc reach the overlay?
//  (c) textInput activateWindow()+setFocus(): can we type?
//  (d) after the text input hides, does the overlay get the keys back?
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

static void settle() {
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

static void esc(Overlay* o) {
    o->gotEsc = false;
    QWindow* fw = QGuiApplication::focusWindow();
    if (!fw) { printf("     Esc -> DROPPED (focusWindow null)\n"); fflush(stdout); return; }
    QKeyEvent e(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(fw, &e);
    printf("     Esc -> overlay.gotEsc = %d\n", (int)o->gotEsc);
    fflush(stdout);
}

static const char* who(QWindow* fw, Overlay* o, Panel* p1, Panel* p2, QTextEdit* te) {
    if (!fw) return "none";
    if (fw == o->windowHandle())  return "OVERLAY";
    if (fw == p1->windowHandle()) return "TOOLBAR";
    if (fw == p2->windowHandle()) return "SUBPANEL";
    if (fw == te->windowHandle()) return "TEXTINPUT";
    return "other";
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
    printf("[1] overlay active                        focusWindow=OVERLAY\n");
    esc(overlay);

    // ToolbarWidget with the new flag
    Panel* toolbar = new Panel(overlay);
    toolbar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    toolbar->setAttribute(Qt::WA_TranslucentBackground);
    toolbar->resize(300, 40);
    toolbar->show();
    overlay->activateWindow(); // <- the fix
    settle();
    printf("[2] toolbar->show()+activateWindow()      focusWindow=%s\n",
           who(QGuiApplication::focusWindow(), overlay, toolbar, nullptr, nullptr));
    esc(overlay);

    // SubPanelWidget with the new flag
    Panel* sub = new Panel(overlay);
    sub->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                        | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    sub->setAttribute(Qt::WA_TranslucentBackground);
    sub->resize(240, 36);
    sub->show();
    settle();
    printf("[3] subPanel->show()                      focusWindow=%s\n",
           who(QGuiApplication::focusWindow(), overlay, toolbar, sub, nullptr));
    esc(overlay);

    // TextInputWidget with activateWindow()
    QTextEdit* te = new QTextEdit(overlay);
    te->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    te->resize(160, 40);
    te->show();
    te->activateWindow();  // <- the fix
    te->setFocus();
    settle();
    printf("[4] textInput show+activateWindow+setFocus focusWindow=%s\n",
           who(QGuiApplication::focusWindow(), overlay, toolbar, sub, te));
    {
        QKeyEvent k(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
        QCoreApplication::sendEvent(QGuiApplication::focusWindow(), &k);
        settle();
        printf("     type 'h' -> \"%s\"\n", te->toPlainText().toUtf8().constData());
        fflush(stdout);
    }

    // Text input closes -> overlay must get the keys back
    te->hide();
    overlay->activateWindow(); // <- the fix
    settle();
    printf("[5] te->hide()+activateWindow()            focusWindow=%s\n",
           who(QGuiApplication::focusWindow(), overlay, toolbar, sub, te));
    esc(overlay);

    // Mouse clicks must still reach a WindowDoesNotAcceptFocus panel
    {
        bool clicked = false;
        struct ClickSpy : Panel {
            bool* flag;
            ClickSpy(QWidget* p, bool* f) : Panel(p), flag(f) {}
            void mousePressEvent(QMouseEvent*) override { *flag = true; }
        };
        ClickSpy* spy = new ClickSpy(overlay, &clicked);
        spy->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
        spy->resize(80, 30);
        spy->show();
        settle();
        QMouseEvent me(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(spy, &me);
        settle();
        printf("[6] click on WindowDoesNotAcceptFocus panel reached widget = %d\n",
               (int)clicked);
    }

    return 0;
}
