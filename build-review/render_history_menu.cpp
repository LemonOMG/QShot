// Renders the tray's history submenu, so the thumbnails and the entry labels can be
// looked at. The menu probe proves the wiring; this one answers "is it readable?" --
// four identical-looking rows would pass every assertion and still be useless.
//
// Menus lay out only once they are shown, so this is the one probe here that puts a
// window on screen for a moment. It is a popup at a fixed position, not a fullscreen
// overlay, and it is hidden again immediately.
//
// Run: render_history_menu.exe

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QThread>
#include <cstdio>

#include "core/HistoryStore.h"
#include "core/Settings.h"
#include "ui/HistoryMenu.h"

using namespace qshot;

static void settle(int ms = 150) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

// Four visually distinct captures, so a thumbnail that renders the wrong entry -- or a
// column of identical grey rectangles -- is obvious at a glance.
static QImage gradient(int w, int h, const QColor& from, const QColor& to) {
    QImage image(w, h, QImage::Format_ARGB32);
    QPainter p(&image);
    QLinearGradient g(0, 0, w, h);
    g.setColorAt(0.0, from);
    g.setColorAt(1.0, to);
    p.fillRect(image.rect(), g);
    p.end();
    return image;
}

static QImage bands(int w, int h, int count) {
    QImage image(w, h, QImage::Format_ARGB32);
    QPainter p(&image);
    p.fillRect(image.rect(), QColor(0xF7, 0xF7, 0xF7));
    QFont font = QApplication::font();
    font.setPointSize(10);
    p.setFont(font);
    for (int i = 0; i < count; ++i) {
        p.setPen(QColor(0x33, 0x33, 0x33));
        p.drawText(QRect(12, 14 + i * 22, w - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("第 %1 行示例文字 —— 用于检验缩略图是否可读").arg(i + 1));
    }
    p.end();
    return image;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("QShotProbe"));

    const QString dir = QDir::tempPath() + QStringLiteral("/qshot-history-render");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);

    Settings::instance().setHistoryEnabled(true);
    Settings::instance().setHistoryLimit(20);

    HistoryStore store(dir);
    store.add(gradient(1200, 800, QColor(0x2E, 0x4C, 0x8A), QColor(0xE8, 0x6A, 0x3F)));
    store.add(bands(900, 500, 6));
    store.add(gradient(500, 900, QColor(0x1B, 0x7A, 0x5E), QColor(0xC9, 0xE3, 0x7A)));
    // A real rendered UI from a previous round, if it is around: the case the thumbnail
    // has to stay legible for -- a screenshot of text.
    const QImage sample(QStringLiteral("panel_zh_text_toolbar.png"));
    if (!sample.isNull()) {
        store.add(sample);
    } else {
        printf("(panel_zh_text_toolbar.png not found, using a fourth gradient)\n");
        store.add(gradient(700, 400, QColor(0x5B, 0x2A, 0x86), QColor(0xF0, 0xC0, 0x40)));
    }
    printf("store holds %d entries in %s\n", store.count(), qPrintable(dir));

    HistoryMenu menu(store);
    menu.rebuild();
    menu.popup(QPoint(200, 200));
    settle();

    const QPixmap shot = menu.grab();
    shot.save(QStringLiteral("history_menu.png"));
    printf("menu %dx%d -> history_menu.png\n", menu.width(), menu.height());

    // The submenu of the newest entry, so the four per-entry actions can be seen too.
    const QList<QAction*> actions = menu.actions();
    for (QAction* action : actions) {
        if (!action->menu()) continue;
        action->menu()->popup(QPoint(200 + menu.width() + 4, 200));
        settle();
        const QPixmap sub = action->menu()->grab();
        sub.save(QStringLiteral("history_menu_entry.png"));
        printf("entry submenu %dx%d -> history_menu_entry.png\n",
               action->menu()->width(), action->menu()->height());
        action->menu()->hide();
        break;
    }

    menu.hide();
    QDir(dir).removeRecursively();
    // See probe_quiet_save.cpp: stdout is fully buffered when it is a pipe or a file, and
    // this process does not flush it on the way out. Without this line the scaffold writes
    // its two PNGs and reports nothing at all.
    fflush(stdout);
    return 0;
}
