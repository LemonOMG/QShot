// Probe: after the toolbar steals/voids focus, can TextInputWidget still be typed into?
// Mirrors SnapOverlay::showToolbar() -> TextInputWidget::startInput() (show(); setFocus();)
#include <QApplication>
#include <QWidget>
#include <QTextEdit>
#include <QWindow>
#include <QKeyEvent>
#include <QThread>
#include <cstdio>

struct Plain : QWidget {
    explicit Plain(QWidget* parent = nullptr) : QWidget(parent) {}
};

static void settle() {
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(15);
    }
}

static void report(const char* stage) {
    QWindow* fw = QGuiApplication::focusWindow();
    QWidget* fwid = QApplication::focusWidget();
    printf("%-34s focusWindow=%-14s focusWidget=%-14s\n", stage,
           fw ? fw->metaObject()->className() : "null",
           fwid ? fwid->metaObject()->className() : "null");
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
    report("[1] overlay shown+activated");

    Plain* toolbar = new Plain(overlay);
    toolbar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    toolbar->resize(300, 40);
    toolbar->show();
    settle();
    report("[2] toolbar->show()");

    // TextInputWidget::startInput() equivalent
    QTextEdit* textInput = new QTextEdit(overlay);
    textInput->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    textInput->resize(160, 40);
    textInput->move(120, 120);
    textInput->show();
    textInput->setFocus();
    settle();
    report("[3] textInput show()+setFocus()");

    // Try to type into it
    QWindow* fw = QGuiApplication::focusWindow();
    if (!fw) {
        printf("    -> focusWindow is null: keystrokes have nowhere to go (text input unusable)\n");
    } else {
        QKeyEvent k(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
        QCoreApplication::sendEvent(fw, &k);
        settle();
        printf("    -> textInput->toPlainText() = \"%s\"\n",
               textInput->toPlainText().toUtf8().constData());
    }
    fflush(stdout);

    return 0;
}
