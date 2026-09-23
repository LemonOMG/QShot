#pragma once

#include <QMenu>

namespace qshot {

class HistoryStore;

/**
 * The tray's "History" submenu: one sub-entry per stored capture, newest first.
 *
 * Each capture is a submenu of its own -- the thumbnail as the icon, "time  WxH" as the
 * title -- holding copy / save-as / pin / delete. It cannot be a plain action with a
 * submenu attached: Qt shows a submenu *instead of* triggering the action, so "click
 * the entry to copy it" and "the entry has a submenu of extra actions" cannot both be
 * true of the same item. Copy is the first row inside instead, which keeps the common
 * action one hover away rather than making it unreachable.
 *
 * The list is rebuilt when the menu is about to open rather than kept in sync, because
 * the store also changes from the overlay: every copy and every save adds an entry
 * while this menu is closed.
 */
class HistoryMenu : public QMenu {
    Q_OBJECT
public:
    /// `store` is borrowed and must outlive the menu.
    explicit HistoryMenu(HistoryStore& store, QWidget* parent = nullptr);

    /// Rebuild the entries from the store. Called automatically when the menu opens.
    void rebuild();

signals:
    /// The user asked to pin a stored capture. The menu does not create windows -- the
    /// application owns that, exactly as it does for the overlay's pin button.
    void pinRequested(const QImage& image);
    /// A capture was copied to the clipboard from this menu, so the application can
    /// show whatever confirmation it wants.
    void copied();

private:
    void copyEntry(int id);
    void saveEntry(int id);
    void pinEntry(int id);
    void deleteEntry(int id);
    void clearAll();

    /// Load the full-resolution image of an entry, dropping the entry if its file has
    /// gone missing underneath us.
    QImage loadImage(int id);

    HistoryStore& store_;
};

} // namespace qshot
