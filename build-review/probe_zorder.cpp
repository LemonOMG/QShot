// Probe: does overlay->activateWindow() push the toolbar behind the fullscreen
// overlay? Checks the real Win32 z-order between the overlay and its Tool windows.
#include <QApplication>
#include <QWidget>
#include <QWindow>
#include <QThread>
#include <cstdio>
#include <windows.h>

struct Panel : QWidget {
    explicit Panel(QWidget* parent = nullptr) : QWidget(parent) {}
};

static void settle() {
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

// GW_HWNDPREV walks UP the z-order (towards the topmost window), so finding B
// while walking up from A means B is drawn above A.
static const char* order(QWidget* a, QWidget* b) {
    HWND ha = reinterpret_cast<HWND>(a->winId());
    HWND hb = reinterpret_cast<HWND>(b->winId());
    for (HWND h = ha; h; h = GetWindow(h, GW_HWNDPREV)) {
        if (h == hb) return "B(tool) above A(overlay)";
    }
    for (HWND h = hb; h; h = GetWindow(h, GW_HWNDPREV)) {
        if (h == ha) return "A(overlay) above B(tool)  <-- BAD";
    }
    return "indeterminate";
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QWidget* overlay = new QWidget;
    overlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::Tool);
    overlay->resize(500, 360);
    overlay->show();
    overlay->activateWindow();
    settle();

    Panel* toolbar = new Panel(overlay);
    toolbar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    toolbar->resize(300, 40);
    toolbar->move(100, 320);
    toolbar->show();
    settle();
    printf("[1] toolbar shown (no reclaim)      overlay vs toolbar: %s\n",
           order(overlay, toolbar));

    overlay->activateWindow();
    settle();
    printf("[2] + overlay->activateWindow()     overlay vs toolbar: %s\n",
           order(overlay, toolbar));

    toolbar->raise();
    settle();
    printf("[3] + toolbar->raise()              overlay vs toolbar: %s\n",
           order(overlay, toolbar));

    Panel* sub = new Panel(overlay);
    sub->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                        | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    sub->resize(240, 36);
    sub->move(120, 270);
    sub->show();
    settle();
    printf("[4] subPanel shown                  overlay vs subPanel: %s\n",
           order(overlay, sub));

    overlay->activateWindow();
    settle();
    printf("[5] + overlay->activateWindow()     overlay vs subPanel: %s\n",
           order(overlay, sub));

    printf("    overlay->isVisible()=%d  toolbar->isVisible()=%d  sub->isVisible()=%d\n",
           (int)overlay->isVisible(), (int)toolbar->isVisible(), (int)sub->isVisible());

    return 0;
}
