// Verification for HistoryMenu: the tray's history submenu.
//
// The store probe proves the bookkeeping. This one proves the menu is actually wired to
// it, which is where the interesting failure lives:
//
//  1. **Actions must be located by entry id, not by row index.** An index captured when
//     the menu was built goes stale the moment another entry is deleted -- and "delete
//     one, then copy another" is an ordinary sequence of clicks. With indices, the copy
//     silently returns the wrong capture; there is no crash and nothing looks wrong.
//     Section [6] is the regression test for exactly that.
//  2. Each entry has to carry a thumbnail icon, or a history of twenty captures is an
//     unreadable list of timestamps.
//  3. The menu has to survive an entry whose file disappeared underneath it.
//
// The menu is built against an injected store on a scratch directory, so this runs
// without touching the real history, and without constructing ShotApplication -- which
// would register the real global hotkey and steal it from the developer's session.
//
// The clipboard is snapshotted and restored: a test has no business clobbering whatever
// the developer had copied.
//
// Run: probe_history_menu.exe      (the menu is never shown, so nothing flashes)

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QStringList>
#include <QThread>
#include <cstdio>

#include "core/HistoryStore.h"
#include "core/Settings.h"
#include "ui/HistoryMenu.h"

using namespace qshot;

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void checkEqInt(int got, int want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL", what, got, want);
}

static void settle(int ms = 60) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

static QString g_root;

static QString freshDir(const char* name) {
    const QString dir = g_root + QLatin1Char('/') + QLatin1String(name);
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    return dir;
}

/// A capture whose colour identifies it, so a copy that returns the wrong entry is
/// detectable from a single pixel rather than from "the images look similar".
static QImage tagged(int index) {
    QImage image(80, 60, QImage::Format_ARGB32);
    image.fill(QColor(20 * index, 40, 200));
    return image;
}

static QColor tagOf(int index) {
    return QColor(20 * index, 40, 200);
}

/// The submenus, in order, which is one per entry.
static QList<QMenu*> entryMenus(QMenu& menu) {
    QList<QMenu*> menus;
    const QList<QAction*> actions = menu.actions();
    for (QAction* action : actions) {
        if (action->menu()) menus << action->menu();
    }
    return menus;
}

/// Keeps whatever the developer had on the clipboard.
class ClipboardGuard {
public:
    ClipboardGuard() {
        const QMimeData* source = QApplication::clipboard()->mimeData();
        if (!source) return;
        const QStringList formats = source->formats();
        for (const QString& format : formats) {
            backup_.setData(format, source->data(format));
        }
    }
    ~ClipboardGuard() {
        if (backup_.formats().isEmpty()) return;
        // QMimeData::clone() is protected, so the copy is made by hand. The clipboard
        // takes ownership of whatever is handed to setMimeData().
        QMimeData* restored = new QMimeData;
        const QStringList formats = backup_.formats();
        for (const QString& format : formats) {
            restored->setData(format, backup_.data(format));
        }
        QApplication::clipboard()->setMimeData(restored);
    }

private:
    QMimeData backup_;
};

// ---------------------------------------------------------------------------- 1 ---
static void checkEmptyStore() {
    printf("\n[1] an empty store produces a disabled placeholder\n");
    const QString dir = freshDir("empty");
    HistoryStore store(dir);
    HistoryMenu menu(store);

    const QList<QAction*> actions = menu.actions();
    checkEqInt(actions.size(), 1, "one row");
    checkEqInt(entryMenus(menu).size(), 0, "no entry submenus");
    check(!actions.at(0)->isEnabled(), "and it cannot be clicked");
    check(!actions.at(0)->text().isEmpty(), "with a label");
    printf("       label: %s\n", actions.at(0)->text().toUtf8().constData());
}

// ---------------------------------------------------------------------------- 2 ---
static void checkStructure() {
    printf("\n[2] one submenu per entry, each with a thumbnail and four actions\n");
    const QString dir = freshDir("structure");
    HistoryStore store(dir);
    for (int i = 1; i <= 3; ++i) store.add(tagged(i));
    HistoryMenu menu(store);
    menu.rebuild();

    const QList<QMenu*> entries = entryMenus(menu);
    checkEqInt(entries.size(), 3, "three entry submenus");

    const QList<QAction*> all = menu.actions();
    checkEqInt(all.size(), 5, "3 entries + separator + clear");
    check(all.last()->text().contains(QStringLiteral("…")), "the last row is the clear entry");
    check(all.last()->isEnabled(), "and it is clickable");

    int withoutIcon = 0;
    int wrongActionCount = 0;
    for (QMenu* entry : entries) {
        if (entry->icon().isNull()) ++withoutIcon;
        // copy, save as, pin, separator, delete
        if (entry->actions().size() != 5) ++wrongActionCount;
    }
    checkEqInt(withoutIcon, 0, "every entry carries a thumbnail icon");
    checkEqInt(wrongActionCount, 0, "every entry has the four actions plus a separator");

    for (QMenu* entry : entries) {
        printf("       \"%s\" icon %dx%d\n",
               entry->title().toUtf8().constData(),
               entry->icon().pixmap(16, 16).width(), entry->icon().pixmap(16, 16).height());
    }
    check(entries.at(0)->title().contains(QStringLiteral("×")), "the title carries the size");

    // Titles are allowed to collide: two captures in the same second of the same region
    // legitimately read identically, and there is no honest extra precision to add. The
    // thumbnail is what has to tell them apart, which is why every entry carries one.
    printf("       titles: \"%s\" / \"%s\"\n",
           entries.at(0)->title().toUtf8().constData(),
           entries.at(2)->title().toUtf8().constData());
    check(entries.at(0)->icon().pixmap(16, 16).toImage()
              != entries.at(2)->icon().pixmap(16, 16).toImage(),
          "entries are told apart by their thumbnails");
}

// ---------------------------------------------------------------------------- 3 ---
static void checkCopy() {
    printf("\n[3] copying from the menu reaches the clipboard\n");
    const QString dir = freshDir("copy");
    HistoryStore store(dir);
    store.add(tagged(1));
    store.add(tagged(2));

    HistoryMenu menu(store);
    menu.rebuild();

    int copiedSignals = 0;
    QObject::connect(&menu, &HistoryMenu::copied, [&]() { ++copiedSignals; });

    // Entry 0 is the newest, which is capture 2.
    entryMenus(menu).at(0)->actions().at(0)->trigger();
    settle();

    checkEqInt(copiedSignals, 1, "the copied() signal fired once");

    const QImage onClipboard = QApplication::clipboard()->image().convertToFormat(QImage::Format_ARGB32);
    check(!onClipboard.isNull(), "the clipboard holds an image");
    checkEqInt(onClipboard.width(), 80, "with the stored width");
    printf("       clipboard pixel = %s, expected %s\n",
           qPrintable(QColor(onClipboard.pixel(10, 10)).name()),
           qPrintable(tagOf(2).name()));
    check(QColor(onClipboard.pixel(10, 10)) == tagOf(2), "and it is the newest capture, not the older one");
}

// ---------------------------------------------------------------------------- 4 ---
static void checkPin() {
    printf("\n[4] pinning from the menu reports the right capture\n");
    const QString dir = freshDir("pin");
    HistoryStore store(dir);
    store.add(tagged(1));
    store.add(tagged(2));

    HistoryMenu menu(store);
    menu.rebuild();

    QImage pinned;
    int pinSignals = 0;
    QObject::connect(&menu, &HistoryMenu::pinRequested, [&](const QImage& image) {
        pinned = image;
        ++pinSignals;
    });

    // Index 2 inside an entry is the pin action.
    entryMenus(menu).at(1)->actions().at(2)->trigger();
    settle();

    checkEqInt(pinSignals, 1, "pinRequested fired once");
    check(!pinned.isNull(), "and carried an image");
    check(QColor(pinned.pixel(10, 10)) == tagOf(1), "the older capture, matching the entry clicked");
}

// ---------------------------------------------------------------------------- 5 ---
static void checkDelete() {
    printf("\n[5] deleting from the menu removes the entry and its file\n");
    const QString dir = freshDir("delete");
    HistoryStore store(dir);
    store.add(tagged(1));
    store.add(tagged(2));
    store.add(tagged(3));

    HistoryMenu menu(store);
    menu.rebuild();

    const QString file = store.entries().at(1).filePath;
    // Index 4 inside an entry is delete (after the separator).
    entryMenus(menu).at(1)->actions().at(4)->trigger();
    settle();

    checkEqInt(store.count(), 2, "the entry is gone from the store");
    check(!QFileInfo::exists(file), "and its file was deleted");

    menu.rebuild();
    checkEqInt(entryMenus(menu).size(), 2, "a rebuilt menu shows two entries");
}

// ---------------------------------------------------------------------------- 6 ---
static void checkActionsFollowTheIdNotTheRow() {
    printf("\n[6] a deletion elsewhere does not make the menu act on the wrong capture\n");
    const QString dir = freshDir("stale");
    HistoryStore store(dir);
    for (int i = 1; i <= 3; ++i) store.add(tagged(i));

    HistoryMenu menu(store);
    menu.rebuild();

    // The third row is capture 1 (the list is newest first).
    QAction* thirdRowCopy = entryMenus(menu).at(2)->actions().at(0);

    // Something removes the newest entry while this menu is open. With row indices, the
    // third row's action would now resolve to capture 2.
    store.removeAt(0);
    checkEqInt(store.count(), 2, "one entry was removed behind the menu's back");

    thirdRowCopy->trigger();
    settle();

    const QImage onClipboard = QApplication::clipboard()->image().convertToFormat(QImage::Format_ARGB32);
    printf("       clipboard pixel = %s, expected %s\n",
           qPrintable(QColor(onClipboard.pixel(10, 10)).name()),
           qPrintable(tagOf(1).name()));
    check(QColor(onClipboard.pixel(10, 10)) == tagOf(1),
          "the copy still returns capture 1, not the entry that shifted into its row");
}

// ---------------------------------------------------------------------------- 7 ---
static void checkVanishedFile() {
    printf("\n[7] an entry whose file vanished is dropped instead of crashing\n");
    const QString dir = freshDir("vanished");
    HistoryStore store(dir);
    store.add(tagged(1));
    store.add(tagged(2));

    HistoryMenu menu(store);
    menu.rebuild();

    const QString file = store.entries().at(0).filePath;
    check(QFile::remove(file), "the newest capture's file was deleted behind the menu's back");

    entryMenus(menu).at(0)->actions().at(0)->trigger();
    settle();

    checkEqInt(store.count(), 1, "the dead entry was dropped");
    check(!QFileInfo::exists(dir + QStringLiteral("/2.t.png")), "and so was its thumbnail");
    check(QFileInfo::exists(store.entries().at(0).filePath), "the surviving entry still has its file");

    printf("       remaining entry: %s\n", store.entries().at(0).filePath.toUtf8().constData());
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Isolate QSettings before anything reads it, and before the store is built: the
    // store asks Settings for the entry limit.
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("QShotProbe"));

    ClipboardGuard guard;

    g_root = QDir::tempPath() + QStringLiteral("/qshot-history-menu-probe");
    QDir(g_root).removeRecursively();
    QDir().mkpath(g_root);

    Settings::instance().setHistoryEnabled(true);
    Settings::instance().setHistoryLimit(kDefaultHistoryLimit);

    checkEmptyStore();
    checkStructure();
    checkCopy();
    checkPin();
    checkDelete();
    checkActionsFollowTheIdNotTheRow();
    checkVanishedFile();

    QDir(g_root).removeRecursively();

    printf("\n%d checks, %d failures\n", checks, failures);
    fflush(stdout);
    return failures == 0 ? 0 : 1;
}
