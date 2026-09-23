#include "HistoryMenu.h"

#include <QDateTime>
#include <QDebug>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>

#include "../core/HistoryStore.h"
#include "../core/ImageExport.h"
#include "../core/Strings.h"

namespace qshot {

namespace {

/// The entry's own label: when it was taken, and how big it is.
///
/// Seconds while the capture is still from today, the date once it is not. A history
/// that spans days needs the date or two entries look identical; one from this morning
/// needs the seconds, or two captures a minute apart do.
QString entryLabel(const HistoryEntry& entry) {
    const QDateTime now = QDateTime::currentDateTime();
    const QString stamp = entry.capturedAt.date() == now.date()
                              ? entry.capturedAt.toString(QStringLiteral("HH:mm:ss"))
                              : entry.capturedAt.toString(QStringLiteral("MM-dd HH:mm"));
    return QStringLiteral("%1   %2×%3")
        .arg(stamp)
        .arg(entry.size.width())
        .arg(entry.size.height());
}

QIcon entryIcon(const QImage& thumbnail) {
    if (thumbnail.isNull()) return QIcon();
    // Handed over at its full size and let Qt scale it down: a menu icon slot is 16px,
    // and a thumbnail that is several times that stays crisp on a scaled display.
    return QIcon(QPixmap::fromImage(thumbnail));
}

} // namespace

HistoryMenu::HistoryMenu(HistoryStore& store, QWidget* parent)
    : QMenu(parent)
    , store_(store)
{
    // Rebuild on open instead of tracking the store's changes: the menu is closed for
    // almost all of the application's life, and rebuilding a dozen items costs nothing
    // compared with keeping a parallel copy of the list in step.
    connect(this, &QMenu::aboutToShow, this, &HistoryMenu::rebuild);
    rebuild();
}

void HistoryMenu::rebuild() {
    // Deletes the previous entries; QMenu takes ownership of anything added with
    // addMenu(), so the submenus go with them.
    clear();

    const QList<HistoryEntry>& entries = store_.entries();
    if (entries.isEmpty()) {
        QAction* empty = addAction(text(Str::TrayHistoryEmpty));
        empty->setEnabled(false);
        return;
    }

    for (const HistoryEntry& entry : entries) {
        // Captured by id, not by index. The index of an entry shifts as soon as another
        // one is deleted, so an index captured here would, after a delete, silently
        // point at the wrong capture -- and "delete" then "copy" is an ordinary
        // sequence of clicks.
        const int id = entry.id;
        QMenu* item = addMenu(entryIcon(entry.thumbnail), entryLabel(entry));

        item->addAction(text(Str::HistoryCopy), this, [this, id]() { copyEntry(id); });
        item->addAction(text(Str::HistorySaveAs), this, [this, id]() { saveEntry(id); });
        item->addAction(text(Str::HistoryPin), this, [this, id]() { pinEntry(id); });
        item->addSeparator();
        item->addAction(text(Str::HistoryDelete), this, [this, id]() { deleteEntry(id); });
    }

    addSeparator();
    addAction(text(Str::TrayHistoryClear), this, &HistoryMenu::clearAll);
}

QImage HistoryMenu::loadImage(int id) {
    for (int i = 0; i < store_.count(); ++i) {
        if (store_.entries().at(i).id != id) continue;

        const QString path = store_.entries().at(i).filePath;
        QImage image(path);
        if (image.isNull()) {
            // The file disappeared since the menu was built -- deleted from the store's
            // directory, or by whatever else touches it. Dropping the entry is the only
            // honest response; leaving it would keep a dead row in the menu forever.
            qWarning() << "HistoryMenu: dropping unreadable entry" << path;
            store_.removeAt(i);
        }
        return image;
    }
    // The entry is gone already (deleted while this menu was open); nothing to do.
    return QImage();
}

void HistoryMenu::copyEntry(int id) {
    const QImage image = loadImage(id);
    if (image.isNull()) return;
    copyImageToClipboard(image);
    emit copied();
}

void HistoryMenu::saveEntry(int id) {
    const QImage image = loadImage(id);
    if (image.isNull()) return;
    // Parented to the menu: a tray application has no window of its own to inherit a
    // sensible position from, and an unparented dialog can end up behind everything.
    saveImageWithDialog(this, image);
}

void HistoryMenu::pinEntry(int id) {
    const QImage image = loadImage(id);
    if (image.isNull()) return;
    emit pinRequested(image);
}

void HistoryMenu::deleteEntry(int id) {
    for (int i = 0; i < store_.count(); ++i) {
        if (store_.entries().at(i).id == id) {
            // No rebuild here on purpose: the menu is closing as this runs, and the
            // next open rebuilds anyway.
            store_.removeAt(i);
            return;
        }
    }
}

void HistoryMenu::clearAll() {
    const int count = store_.count();
    if (count == 0) return;

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        text(Str::HistoryClearTitle),
        text(Str::HistoryClearBody).arg(count),
        QMessageBox::Yes | QMessageBox::No,
        // Defaulting to No: this deletes files and cannot be undone, so the destructive
        // button must not be the one a stray Enter hits.
        QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    store_.clear();
}

} // namespace qshot
