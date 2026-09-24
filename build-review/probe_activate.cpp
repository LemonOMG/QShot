// Probe: how reliable is activateWindow() after a panel show() voids the focus?
// Prints Qt's view AND the raw Win32 foreground window, and retries.
#include <QApplication>
#include <QWidget>
#include <QWindow>
#include <QThread>
#include <cstdio>
#include <windows.h>

struct Panel : QWidget {
    explicit Panel(QWidget* parent = nullptr) : QWidget(parent) {}
};

static void settle(int ms = 200) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

static void state(const char* tag, QWidget* overlay, QWidget* toolbar) {
    QWindow* f = QGuiApplication::focusWindow();
    const char* who = "none";
    if (f && f == overlay->windowHandle()) who = "OVERLAY";
    else if (f && toolbar && f == toolbar->windowHandle()) who = "TOOLBAR";
    else if (f) who = "other";
    HWND fg = GetForegroundWindow();
    HWND ho = reinterpret_cast<HWND>(overlay->winId());
    HWND ht = toolbar ? reinterpret_cast<HWND>(toolbar->winId()) : nullptr;
    const char* fgWho = "other";
    if (fg == ho) fgWho = "OVERLAY";
    else if (ht && fg == ht) fgWho = "TOOLBAR";
    else if (fg == nullptr) fgWho = "none";
    printf("%-40s qtFocus=%-9s win32Foreground=%s\n", tag, who, fgWho);
    fflush(stdout);
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
    state("[1] overlay shown + activateWindow", overlay, nullptr);

    Panel* toolbar = new Panel(overlay);
    toolbar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    toolbar->resize(300, 40);
    toolbar->show();
    settle();
    state("[2] toolbar->show() (focus voided)", overlay, toolbar);

    for (int i = 1; i <= 5; ++i) {
        overlay->activateWindow();
        settle();
        char tag[64];
        snprintf(tag, sizeof(tag), "[3.%d] overlay->activateWindow() #%d", i, i);
        state(tag, overlay, toolbar);
    }

    printf("\nNote: SetForegroundWindow() is refused by Windows unless the process\n"
           "owns the foreground or received the last user input. A synthetic probe\n"
           "cannot satisfy that rule, so retries are expected to behave differently\n"
           "from a real click/hotkey.\n");
    return 0;
}
