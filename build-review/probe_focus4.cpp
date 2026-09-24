// Probe: which window actually holds focus after the real showToolbar() ->
// TextInputWidget::startInput() sequence, and does an explicit activateWindow()
// fix typing? Prints identity comparisons, not just class names.
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
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

struct Ctx {
    QWidget* overlay;
    QWidget* toolbar;
    QTextEdit* textInput;
};

static void report(const char* stage, const Ctx& c) {
    QWindow* fw = QGuiApplication::focusWindow();
    const char* who = "none";
    if (fw && fw == c.overlay->windowHandle())   who = "OVERLAY";
    else if (fw && fw == c.toolbar->windowHandle())   who = "TOOLBAR";
    else if (fw && fw == c.textInput->windowHandle()) who = "TEXTINPUT";
    else if (fw) who = "other";
    printf("%-38s focusWindow=%-9s focusWidget=%-11s activeWindow=%s\n", stage, who,
           QApplication::focusWidget()
               ? QApplication::focusWidget()->metaObject()->className()
               : "null",
           QApplication::activeWindow()
               ? QApplication::activeWindow()->metaObject()->className()
               : "null");
    fflush(stdout);
}

static void typeH(QTextEdit* te) {
    te->clear();
    QWindow* fw = QGuiApplication::focusWindow();
    if (!fw) { printf("     type 'h' -> dropped (no focusWindow)\n"); fflush(stdout); return; }
    QKeyEvent k(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
    QCoreApplication::sendEvent(fw, &k);
    settle();
    printf("     type 'h' -> toPlainText()=\"%s\"\n", te->toPlainText().toUtf8().constData());
    fflush(stdout);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    Ctx c{};
    c.overlay = new QWidget;
    c.overlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                              | Qt::WindowStaysOnTopHint | Qt::Tool);
    c.overlay->resize(500, 360);
    c.overlay->show();
    c.overlay->activateWindow();
    settle();
    report("[1] overlay shown + activateWindow", c);

    c.toolbar = new Plain(c.overlay);
    c.toolbar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    c.toolbar->resize(300, 40);
    c.toolbar->move(100, 380);
    c.toolbar->show();            // <- showToolbar()
    settle();
    report("[2] toolbar->show()  (showToolbar)", c);

    c.textInput = new QTextEdit(c.overlay);
    c.textInput->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    c.textInput->resize(160, 40);
    c.textInput->move(120, 120);
    c.textInput->show();
    c.textInput->setFocus();      // <- startInput() as written today
    settle();
    report("[3] textInput show()+setFocus()  (as-is)", c);
    typeH(c.textInput);

    // Candidate fix: activate the text input window explicitly
    c.textInput->activateWindow();
    c.textInput->setFocus();
    settle();
    report("[4] + textInput->activateWindow()", c);
    typeH(c.textInput);

    return 0;
}
