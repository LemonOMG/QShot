#include "HistoryStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include "Settings.h"

namespace qshot {

namespace {

constexpr auto kIndexFileName = "index.json";
constexpr int kIndexVersion = 1;

/// Longest side of a stored thumbnail, in physical pixels. The menu shows these in a
/// 16px slot, so anything beyond a couple of hundred pixels is wasted bytes -- but
/// keeping it generous means the same thumbnail stays usable if the menu ever grows
/// bigger icons.
constexpr int kThumbnailMax = 160;

QString defaultDirectory() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // Only reachable on a platform with no writable app-data location; better than
    // refusing to keep a history at all.
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.qshot");
    return base + QStringLiteral("/history");
}

/**
 * A preview for the menu, at most kThumbnailMax on its longest side.
 *
 * Scaled by *physical* pixels and handed back at a device pixel ratio of 1. The source
 * carries the capture's own ratio, and a thumbnail that inherited it would claim to be
 * 1.5x smaller than it is, which is exactly the kind of quiet mismatch that makes an
 * icon look soft for no visible reason.
 */
QImage makeThumbnail(const QImage& image) {
    const QSize capped(kThumbnailMax, kThumbnailMax);
    if (image.width() <= capped.width() && image.height() <= capped.height()) {
        QImage thumb = image;
        thumb.setDevicePixelRatio(1.0);
        return thumb;
    }

    // QImage::size() is the physical size, and the target keeps the aspect ratio, so
    // IgnoreAspectRatio here is exact rather than a distortion.
    QImage thumb = image.scaled(image.size().scaled(capped, Qt::KeepAspectRatio),
                                Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    thumb.setDevicePixelRatio(1.0);
    return thumb;
}

} // namespace

HistoryStore& HistoryStore::instance() {
    static HistoryStore store(defaultDirectory());
    return store;
}

HistoryStore::HistoryStore(const QString& directory, qint64 maxTotalBytes, QObject* parent)
    : QObject(parent)
    , directory_(QDir::cleanPath(directory))
    , maxTotalBytes_(qMax<qint64>(1, maxTotalBytes))
{
    load();
}

HistoryStore::~HistoryStore() = default;

QString HistoryStore::indexPath() const {
    return directory_ + QLatin1Char('/') + QLatin1String(kIndexFileName);
}

QString HistoryStore::imagePath(int id, const QString& suffix) const {
    return directory_ + QLatin1Char('/') + QString::number(id) + QLatin1Char('.') + suffix;
}

QString HistoryStore::thumbPath(int id) const {
    // A suffix rather than a subdirectory: eviction has to remove both files of an
    // entry, and keeping them adjacent makes that a single directory listing.
    return imagePath(id, QStringLiteral("t.png"));
}

void HistoryStore::load() {
    entries_.clear();
    nextId_ = 1;

    QFile index(indexPath());
    if (!index.exists()) {
        // First run, or a store that was cleared. There is no index to be wrong about,
        // so anything left in the directory is unreferenced and safe to remove.
        sweepOrphans();
        return;
    }

    if (!index.open(QIODevice::ReadOnly)) {
        qWarning() << "HistoryStore: cannot read" << indexPath() << index.errorString();
        return;
    }

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(index.readAll(), &error);
    index.close();

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        // Ambiguous, so do nothing destructive. The image files may be perfectly good
        // and only the index is damaged; deleting the user's captures on that signal
        // would be the worst possible response. Starting with an empty list leaves the
        // files unreachable, but they are still there, and the next successful add
        // writes a fresh index.
        qWarning() << "HistoryStore: index is unreadable, keeping files:" << error.errorString();
        return;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != kIndexVersion) {
        // A future layout. Refuse to guess at it rather than misreading fields.
        qWarning() << "HistoryStore: unsupported index version"
                   << root.value(QStringLiteral("version")).toInt();
        return;
    }

    const QJsonArray array = root.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();

        HistoryEntry entry;
        entry.id = object.value(QStringLiteral("id")).toInt();
        entry.capturedAt = QDateTime::fromString(
            object.value(QStringLiteral("time")).toString(), Qt::ISODateWithMs);
        entry.size = QSize(object.value(QStringLiteral("w")).toInt(),
                           object.value(QStringLiteral("h")).toInt());
        entry.filePath = QDir(directory_).absoluteFilePath(
            object.value(QStringLiteral("file")).toString());
        entry.bytes = qint64(object.value(QStringLiteral("bytes")).toDouble());

        if (entry.id <= 0) continue;

        // Staleness check. Deliberately only "does it exist": decoding every stored
        // capture at startup to validate it would cost exactly what keeping thumbnails
        // on disk was meant to avoid.
        if (!QFileInfo::exists(entry.filePath)) {
            qWarning() << "HistoryStore: dropping stale entry" << entry.filePath;
            deleteFiles(entry); // also removes an orphaned thumbnail
            continue;
        }

        entry.thumbnail = QImage(thumbPath(entry.id));
        if (entry.thumbnail.isNull()) {
            // Thumbnail lost but the capture is fine: rebuild it once rather than
            // showing an entry with no preview forever.
            entry.thumbnail = makeThumbnail(QImage(entry.filePath));
            entry.thumbnail.save(thumbPath(entry.id), "PNG");
        }

        entries_.append(entry);
    }

    // Ids must keep climbing even if the index was written by an older build.
    for (const HistoryEntry& entry : entries_) {
        nextId_ = qMax(nextId_, entry.id + 1);
    }

    // The user may have lowered the limit since this index was written.
    if (entries_.size() > qMax(1, Settings::instance().historyLimit())) {
        trim();
        saveIndex();
    }

    sweepOrphans();
}

void HistoryStore::sweepOrphans() {
    QDir dir(directory_);
    if (!dir.exists()) return;

    QSet<QString> keep;
    keep.insert(QLatin1String(kIndexFileName));
    for (const HistoryEntry& entry : entries_) {
        keep.insert(QFileInfo(entry.filePath).fileName());
        keep.insert(QFileInfo(thumbPath(entry.id)).fileName());
    }

    // This directory belongs to the store, so anything else in it is a leftover: an
    // image whose index write never happened, or a thumbnail whose entry was dropped.
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    for (const QFileInfo& file : files) {
        if (keep.contains(file.fileName())) continue;
        qWarning() << "HistoryStore: removing orphaned file" << file.fileName();
        QFile::remove(file.absoluteFilePath());
    }
}

bool HistoryStore::add(const QImage& image, const QString& sourceFile) {
    if (image.isNull()) return false;
    if (!Settings::instance().historyEnabled()) return false;

    if (!QDir().mkpath(directory_)) {
        qWarning() << "HistoryStore: cannot create" << directory_;
        return false;
    }

    const int id = nextId_++;
    QString stored;

    // Reuse the caller's file when it is a format we can hand back later. The suffix is
    // checked rather than trusted blindly: the save dialog derives the encoder from the
    // extension, so an extension we do not recognise may not describe the bytes.
    const QString suffix = QFileInfo(sourceFile).suffix().toLower();
    const bool reusable = !sourceFile.isEmpty()
                          && (suffix == QLatin1String("png") || suffix == QLatin1String("jpg")
                              || suffix == QLatin1String("jpeg"))
                          && QFileInfo::exists(sourceFile);
    if (reusable) {
        const QString destination = imagePath(id, suffix);
        // QFile::copy refuses to overwrite, which is the behaviour we want: the id is
        // fresh, so a collision would mean the id counter is out of step with the
        // directory.
        if (QFile::copy(sourceFile, destination)) stored = destination;
    }

    if (stored.isEmpty()) {
        stored = imagePath(id, QStringLiteral("png"));
        if (!image.save(stored, "PNG")) {
            qWarning() << "HistoryStore: cannot write" << stored;
            --nextId_; // nothing was written, so the id was never used
            return false;
        }
    }

    const QImage thumb = makeThumbnail(image);
    const QString thumbFile = thumbPath(id);
    if (!thumb.save(thumbFile, "PNG")) {
        // Not fatal: the entry still works, it just has no preview until the next load
        // rebuilds one.
        qWarning() << "HistoryStore: cannot write thumbnail" << thumbFile;
    }

    HistoryEntry entry;
    entry.id = id;
    entry.capturedAt = QDateTime::currentDateTime();
    entry.size = image.size();
    entry.filePath = stored;
    entry.thumbnail = thumb;
    entry.bytes = QFileInfo(stored).size();

    entries_.prepend(entry);
    trim();
    saveIndex();
    emit changed();
    return true;
}

void HistoryStore::trim() {
    const int limit = qMax(1, Settings::instance().historyLimit());
    while (entries_.size() > limit) {
        dropOldest();
    }

    // The byte budget never evicts the newest entry. Deleting a capture the instant it
    // was added would be inexplicable to the user, so a single oversized capture is
    // kept and the budget is knowingly exceeded.
    while (entries_.size() > 1 && totalBytes() > maxTotalBytes_) {
        dropOldest();
    }
}

qint64 HistoryStore::totalBytes() const {
    qint64 total = 0;
    for (const HistoryEntry& entry : entries_) total += entry.bytes;
    return total;
}

void HistoryStore::dropOldest() {
    if (entries_.isEmpty()) return;
    deleteFiles(entries_.takeLast());
}

void HistoryStore::deleteFiles(const HistoryEntry& entry) const {
    if (!entry.filePath.isEmpty()) QFile::remove(entry.filePath);
    QFile::remove(thumbPath(entry.id));
}

void HistoryStore::removeAt(int index) {
    if (index < 0 || index >= entries_.size()) return;
    deleteFiles(entries_.takeAt(index));
    saveIndex();
    emit changed();
}

void HistoryStore::clear() {
    entries_.clear();
    nextId_ = 1;

    // Every file, not only the referenced ones: an index that failed to parse leaves
    // files behind that no entry points at, and if "clear" did not remove those they
    // would be unreachable forever.
    QDir dir(directory_);
    if (dir.exists()) {
        const QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo& file : files) {
            QFile::remove(file.absoluteFilePath());
        }
    }

    saveIndex();
    emit changed();
}

void HistoryStore::saveIndex() const {
    if (!QDir().mkpath(directory_)) {
        qWarning() << "HistoryStore: cannot create" << directory_;
        return;
    }

    // QSaveFile writes a temporary and renames it, so a crash mid-write leaves the
    // previous index intact instead of a truncated one -- which load() would then have
    // to treat as unreadable.
    QSaveFile file(indexPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "HistoryStore: cannot write" << indexPath() << file.errorString();
        return;
    }

    QJsonArray array;
    for (const HistoryEntry& entry : entries_) {
        QJsonObject object;
        object[QStringLiteral("id")] = entry.id;
        object[QStringLiteral("time")] = entry.capturedAt.toString(Qt::ISODateWithMs);
        object[QStringLiteral("w")] = entry.size.width();
        object[QStringLiteral("h")] = entry.size.height();
        // Basename only: the directory is derivable, and an absolute path in a config
        // file breaks if the profile is moved or the user is renamed.
        object[QStringLiteral("file")] = QFileInfo(entry.filePath).fileName();
        object[QStringLiteral("bytes")] = double(entry.bytes);
        array.append(object);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kIndexVersion;
    // No "nextId" field on purpose: the counter is derived from the largest stored id
    // on load, and a persisted copy could only ever disagree with that. Writing a field
    // nobody reads is how a future reader ends up trusting it.
    root[QStringLiteral("entries")] = array;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning() << "HistoryStore: cannot commit" << indexPath();
    }
}

} // namespace qshot
