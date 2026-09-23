#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

namespace qshot {

/// Byte budget for the whole store, across every entry. Deliberately far above what the
/// default entry limit occupies, so it never fires in ordinary use -- it exists so that a
/// run of unusually large captures cannot fill the disk.
constexpr qint64 kDefaultMaxHistoryBytes = 200LL * 1024 * 1024;

/// One capture kept in the history.
struct HistoryEntry {
    /// Monotonic and never reused. Also the file name stem, so a menu built before a
    /// change can never silently act on a different capture afterwards.
    int id = 0;
    QDateTime capturedAt;
    /// Physical pixel size of the stored image -- what the file actually holds, which
    /// is what the user wants to see, not the logical size a scaled display implies.
    QSize size;
    /// Absolute path of the full-resolution image.
    QString filePath;
    /// Preview, kept in memory so the menu can be built without touching the disk.
    QImage thumbnail;
    /// Size of `filePath` on disk, used for the total-bytes budget.
    qint64 bytes = 0;
};

/**
 * A bounded, on-disk record of recent captures.
 *
 * Why on disk rather than in memory: the whole point is surviving a restart, and a
 * handful of full-screen captures is tens of megabytes -- far more than a tray
 * application should hold resident. Only the thumbnails stay in memory.
 *
 * Why the thumbnails are *also* written to disk: the alternative is decoding every
 * stored PNG at startup to rebuild them, which is ~20 full-image decodes before the
 * tray icon can appear. A thumbnail is a few kilobytes, so persisting it makes startup
 * a series of tiny reads instead.
 *
 * The store owns its files. A capture that was saved by the user is *copied* in rather
 * than referenced, so tidying up the Pictures folder cannot silently empty the history
 * -- the user's file and the history entry are independent artefacts. The copy is a
 * file copy, not a re-encode, so it costs no encoding time.
 *
 * Single-threaded, like the rest of QShot.
 */
class HistoryStore : public QObject {
    Q_OBJECT
public:
    /// The application-wide store, rooted at QStandardPaths::AppDataLocation/history.
    static HistoryStore& instance();

    /**
     * A store rooted at `directory`, which is created on first write.
     *
     * Taking the directory as a parameter rather than hardcoding it is what makes the
     * store testable: the real location is user data, and a test must never be able to
     * write there. `maxTotalBytes` is injectable for the same reason -- at the shipped
     * budget, exercising the byte limit would mean writing 200MB of test data.
     */
    explicit HistoryStore(const QString& directory,
                          qint64 maxTotalBytes = kDefaultMaxHistoryBytes,
                          QObject* parent = nullptr);
    ~HistoryStore() override;

    /**
     * Record a capture, newest first.
     *
     * `sourceFile`, when it is an image format we recognise, is copied into the store
     * instead of re-encoding `image`. The caller has just written that file, so this
     * both skips a second encode and preserves the exact artefact -- including a
     * JPEG's lossy encoding, which a PNG re-encode would quietly "improve" into
     * something the user never saved.
     *
     * Returns true when an entry was added. A no-op (history disabled, null image,
     * unwritable directory) returns false rather than failing silently.
     */
    bool add(const QImage& image, const QString& sourceFile = QString());

    /// Newest first.
    const QList<HistoryEntry>& entries() const { return entries_; }
    int count() const { return int(entries_.size()); }
    bool isEmpty() const { return entries_.isEmpty(); }

    /// Forget one entry and delete its files. Out-of-range indices are ignored.
    void removeAt(int index);

    /// Delete every capture in the store, referenced or not.
    void clear();

    /// The directory the store owns. Exposed so the settings dialog can offer to open
    /// it, and so tests can assert on it.
    QString directory() const { return directory_; }

signals:
    /// The entry list changed. Emitted only after the on-disk state is consistent.
    void changed();

private:
    QString indexPath() const;
    QString imagePath(int id, const QString& suffix) const;
    QString thumbPath(int id) const;

    void load();
    /// Write the index atomically, so a crash mid-write cannot truncate it.
    void saveIndex() const;
    /// Enforce the count and byte budgets, oldest first.
    void trim();
    qint64 totalBytes() const;
    /// Drop the oldest entry, deleting its files. Does not write the index.
    void dropOldest();
    /// Delete an entry's image and thumbnail. Does not touch the list or the index.
    void deleteFiles(const HistoryEntry& entry) const;
    /// Delete files in the directory that no entry refers to. Only safe once the index
    /// is known to be readable -- see load().
    void sweepOrphans();

    QString directory_;
    QList<HistoryEntry> entries_;
    int nextId_ = 1;
    qint64 maxTotalBytes_ = kDefaultMaxHistoryBytes;
};

} // namespace qshot
