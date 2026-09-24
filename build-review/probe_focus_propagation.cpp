// Probe: with a Qt::Tool child window (ToolbarWidget / SubPanelWidget pattern)
//  (a) does parentWidget() point at the overlay?
//  (b) do key events propagate from the Tool window up to the overlay?
//  (c) does the Tool window's *default* keyPressEvent swallow Escape?
#include <QApplication>
#include <QWidget>
#include <QKeyEvent>
#include <cstdio>

struct Plain : QWidget {   // deliberately does NOT override keyPressEvent
    explicit Plain(QWidget* parent = nullptr) : QWidget(parent) {}
};

struct Overlay : QWidget {
    bool got = false;
    int lastKey = 0;
    void keyPressEvent(QKeyEvent* e) override {
        got = true;
        lastKey = e->key();
        e->accept();
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    Overlay* overlay = new Overlay;
    overlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::Tool);
    overlay->resize(400, 300);
    overlay->show();

    Plain* tool = new Plain(overlay); // same construction as ToolbarWidget
    tool->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    tool->resize(120, 40);
    tool->show();

    printf("tool->parentWidget() == overlay ? %s\n",
           tool->parentWidget() == overlay ? "YES" : "NO");
    printf("tool->isWindow() = %d, overlay->isWindow() = %d\n",
           (int)tool->isWindow(), (int)overlay->isWindow());

    // --- (c) does the plain Tool window's default keyPressEvent accept Escape? ---
    tool->setFocus();
    printf("focusWidget is tool ? %s\n",
           app.focusWidget() == tool ? "YES" : "NO");
    {
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(tool, &esc);
        printf("[Esc] accepted by tool = %d   overlay.got = %d\n",
               (int)esc.isAccepted(), (int)overlay->got);
    }

    // --- (b) does Ctrl+Z reach the overlay? ---
    overlay->got = false;
    {
        QKeyEvent z(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(tool, &z);
        printf("[Ctrl+Z] accepted = %d   overlay.got = %d\n",
               (int)z.isAccepted(), (int)overlay->got);
    }

    // --- (b) does Enter reach the overlay? ---
    overlay->got = false;
    {
        QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(tool, &ret);
        printf("[Enter] accepted = %d   overlay.got = %d\n",
               (int)ret.isAccepted(), (int)overlay->got);
    }

    return 0;
}
