// Probe variant: overlay is ACTIVE, then TextInputWidget::startInput() runs.
// Isolates whether the text box fails because of the toolbar, or on its own.
#include <QApplication>
#include <QWidget>
#include <QTextEdit>
#include <QWindow>
#include <QKeyEvent>
#include <QThread>
#include <cstdio>

static void settle() {
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

static void report(const char* stage, QWidget* overlay, QTextEdit* te) {
    QWindow* fw = QGuiApplication::focusWindow();
    const char* who = "none";
    if (fw && fw == overlay->windowHandle()) who = "OVERLAY";
    else if (fw && te && fw == te->windowHandle()) who = "TEXTINPUT";
    else if (fw) who = "other";
    printf("%-44s focusWindow=%-9s focusWidget=%s\n", stage, who,
           QApplication::focusWidget()
               ? QApplication::focusWidget()->metaObject()->className()
               : "null");
    fflush(stdout);
}

static void typeH(QTextEdit* te) {
    te->clear();
    QWindow* fw = QGuiApplication::focusWindow();
    if (!fw) { printf("     type 'h' -> DROPPED (no focusWindow)\n"); fflush(stdout); return; }
    QKeyEvent k(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
    QCoreApplication::sendEvent(fw, &k);
    settle();
    printf("     type 'h' -> toPlainText()=\"%s\"\n", te->toPlainText().toUtf8().constData());
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
    report("[1] overlay active (no toolbar yet)", overlay, nullptr);

    QTextEdit* te = new QTextEdit(overlay);
    te->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    te->resize(160, 40);
    te->move(120, 120);
    te->show();
    te->setFocus();
    settle();
    report("[2] te->show()+setFocus() while overlay active", overlay, te);
    typeH(te);

    return 0;
}
