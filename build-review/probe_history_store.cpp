// Verification for HistoryStore: the bounded on-disk record of recent captures.
//
// What is being checked, and why each one needs a probe rather than a look:
//
//  1. The count limit actually deletes the oldest *files*, not just the list entries.
//     A store that forgets an entry but leaves the file behind grows without bound, and
//     nothing in the UI would ever show it.
//  2. The byte budget evicts, but never down to zero. Deleting a capture the instant it
//     was added would be inexplicable, so the newest entry is kept even when it alone
//     exceeds the budget -- an invariant that is easy to state and easy to break.
//  3. Entries survive a reopen, with thumbnails and timestamps intact. This is the whole
//     point of the feature, and it is the part a manual test cannot check without
//     restarting the application between every step.
//  4. A corrupt index must NOT delete the captures. That is the one failure mode where
//     doing the obvious thing (treat the directory as empty) destroys user data.
//  5. Reusing the caller's saved file must be a real file copy, so the history entry
//     survives the user later tidying up wherever they saved it to.
//
// The store is constructed against a scratch directory rather than QStandardPaths: the
// real location is user data, and a test must not be able to write there. The byte
// budget is injected for the same reason -- at the shipped budget, exercising it would
// mean writing 200MB.
//
// Run: probe_history_store.exe      (no widgets, no windows, nothing flashes)

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <cstdio>

#include "core/HistoryStore.h"
#include "core/Settings.h"

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

static QString g_root;

/// A clean scratch tree for one case.
///
/// The store gets its own subdirectory, and the "user's" files live outside it. That
/// separation matters: anything inside the store directory is subject to the orphan
/// sweep, so a test that put the caller's saved file in there would have it deleted
/// before the reuse path was ever reached.
struct Case {
    QString store;
    QString user;
};

static Case freshCase(const char* name) {
    Case c;
    const QString root = g_root + QLatin1Char('/') + QLatin1String(name);
    QDir(root).removeRecursively();
    c.store = root + QStringLiteral("/store");
    c.user = root + QStringLiteral("/user");
    QDir().mkpath(c.user);
    return c;
}

static QImage solid(int w, int h, const QColor& color) {
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

/// Noise compresses badly, so the PNG size is predictable from the pixel count. That is
/// what makes a byte budget testable without writing anything large.
static QImage noise(int w, int h, quint32 seed) {
    QImage image(w, h, QImage::Format_ARGB32);
    quint32 state = seed | 1u;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            state = state * 1664525u + 1013904223u;
            image.setPixel(x, y, 0xFF000000u | (state & 0x00FFFFFFu));
        }
    }
    return image;
}

/// Writes a file, asserting it worked. Returns the result so callers can check it too.
static bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        printf("       (could not write %s)\n", path.toUtf8().constData());
        return false;
    }
    return file.write(contents) == contents.size();
}

static QStringList filesIn(const QString& dir) {
    QStringList names;
    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files);
    for (const QFileInfo& file : files) names << file.fileName();
    names.sort();
    return names;
}

/// Restores the shipped limits between cases; the store reads them from Settings, so a
/// case that changes one would otherwise leak into the next.
static void resetSettings() {
    Settings::instance().setHistoryEnabled(true);
    Settings::instance().setHistoryLimit(kDefaultHistoryLimit);
}

// ---------------------------------------------------------------------------- 1 ---
static void checkEmpty() {
    printf("\n[1] an empty store\n");
    const Case c = freshCase("empty");
    HistoryStore store(c.store);
    checkEqInt(store.count(), 0, "a store with no index starts empty");
    check(store.isEmpty(), "and reports itself empty");
    check(!QDir(c.store).exists(), "nothing is created before the first write");
}

// ---------------------------------------------------------------------------- 2 ---
static void checkAddWritesFiles() {
    printf("\n[2] add() writes the image, a thumbnail and the index\n");
    const Case c = freshCase("add");
    HistoryStore store(c.store);

    check(store.add(solid(400, 300, Qt::red)), "add() reports success");
    checkEqInt(store.count(), 1, "one entry");

    const HistoryEntry& entry = store.entries().at(0);
    checkEqInt(entry.size.width(), 400, "the entry records the physical width");
    checkEqInt(entry.size.height(), 300, "the entry records the physical height");
    check(entry.bytes > 0, "the entry records the file size");
    check(entry.capturedAt.isValid(), "the entry records a timestamp");
    check(QFileInfo::exists(entry.filePath), "the image file exists");
    check(!entry.thumbnail.isNull(), "the entry carries a thumbnail");

    const QStringList files = filesIn(c.store);
    printf("       %s\n", files.join(QStringLiteral(", ")).toUtf8().constData());
    checkEqInt(files.size(), 3, "image + thumbnail + index, nothing else");
    check(files.contains(QStringLiteral("index.json")), "index.json is written");

    check(!store.add(QImage()), "a null image is refused");
    checkEqInt(store.count(), 1, "and changes nothing");
}

// ---------------------------------------------------------------------------- 3 ---
static void checkReusesSavedFile() {
    printf("\n[3] a saved file is copied in, not re-encoded and not referenced\n");
    const Case c = freshCase("reuse");

    const QString saved = c.user + QStringLiteral("/saved_by_user.png");
    solid(120, 90, Qt::green).save(saved, "PNG");
    const qint64 savedBytes = QFileInfo(saved).size();
    check(savedBytes > 0, "the user's file was written");

    HistoryStore store(c.store);
    check(store.add(solid(120, 90, Qt::green), saved), "the capture is recorded");

    const HistoryEntry& entry = store.entries().at(0);
    check(entry.filePath != saved, "the entry does not point at the user's file");
    checkEqInt(int(QFileInfo(entry.filePath).size()), int(savedBytes),
               "the stored copy is byte-identical to what the user saved");
    check(QFileInfo::exists(saved), "the user's own file is left untouched");

    // The whole reason for copying rather than referencing: tidying up the save
    // location must not empty the history.
    QFile::remove(saved);
    HistoryStore reopened(c.store);
    checkEqInt(reopened.count(), 1, "the entry survives the user deleting their file");
}

// ---------------------------------------------------------------------------- 4 ---
static void checkCountLimit() {
    printf("\n[4] the count limit deletes the oldest files\n");
    const Case c = freshCase("limit");

    Settings::instance().setHistoryLimit(3);
    HistoryStore store(c.store);
    for (int i = 1; i <= 5; ++i) {
        store.add(solid(60, 40, QColor(20 * i, 30, 40)));
    }

    checkEqInt(store.count(), 3, "the limit is enforced");

    QSet<int> ids;
    for (const HistoryEntry& entry : store.entries()) ids.insert(entry.id);
    check(ids.contains(3) && ids.contains(4) && ids.contains(5), "the newest three are kept");
    check(!ids.contains(1) && !ids.contains(2), "the oldest two are gone");

    check(store.entries().at(0).id > store.entries().at(2).id, "the list is newest first");

    check(!QFileInfo::exists(c.store + QStringLiteral("/1.png")), "the oldest image file was deleted");
    check(!QFileInfo::exists(c.store + QStringLiteral("/1.t.png")), "and so was its thumbnail");
    check(QFileInfo::exists(c.store + QStringLiteral("/5.png")), "the newest image file is intact");

    const QStringList files = filesIn(c.store);
    printf("       %s\n", files.join(QStringLiteral(", ")).toUtf8().constData());
    checkEqInt(files.size(), 7, "3 images + 3 thumbnails + index, no leftovers");

    resetSettings();
}

// ---------------------------------------------------------------------------- 5 ---
static void checkByteBudget() {
    printf("\n[5] the byte budget evicts, but never the newest entry\n");
    {
        const Case c = freshCase("bytes");
        Settings::instance().setHistoryLimit(100);

        HistoryStore store(c.store, 40000);
        int perEntry = 0;
        for (int i = 1; i <= 5; ++i) {
            store.add(noise(64, 64, quint32(i)));
            if (i == 1) perEntry = int(store.entries().at(0).bytes);
        }
        printf("       one noise capture is %d bytes, budget 40000\n", perEntry);
        check(perEntry > 10000, "the fixture really is large enough to matter");
        check(store.count() < 5, "the budget evicted entries");
        check(store.count() >= 1, "but kept at least one");
        check(store.entries().at(0).id == 5, "and the one kept is the newest");

        qint64 total = 0;
        for (const HistoryEntry& entry : store.entries()) total += entry.bytes;
        printf("       %d entries, %lld bytes\n", store.count(), (long long)total);
    }
    {
        // A budget smaller than a single capture: the newest is kept anyway. Deleting a
        // capture the instant it was added would be inexplicable to the user.
        const Case c = freshCase("bytes-tiny");
        HistoryStore store(c.store, 1000);
        for (int i = 1; i <= 3; ++i) store.add(noise(64, 64, quint32(i)));
        checkEqInt(store.count(), 1, "a single oversized capture is kept, not deleted");
        check(store.entries().at(0).id == 3, "and it is the newest");
        check(QFileInfo::exists(store.entries().at(0).filePath), "with its file on disk");
    }
    resetSettings();
}

// ---------------------------------------------------------------------------- 6 ---
static void checkIndexRoundTrip() {
    printf("\n[6] entries survive a reopen\n");
    const Case c = freshCase("roundtrip");
    QImage thumbnailBefore;
    {
        HistoryStore store(c.store);
        store.add(solid(100, 50, Qt::red));
        store.add(solid(200, 80, Qt::blue));
        thumbnailBefore = store.entries().at(0).thumbnail;
    }

    HistoryStore reopened(c.store);
    checkEqInt(reopened.count(), 2, "both entries came back");
    checkEqInt(reopened.entries().at(0).size.width(), 200, "still newest first");
    checkEqInt(reopened.entries().at(1).size.width(), 100, "and the older one follows");
    check(!reopened.entries().at(0).thumbnail.isNull(), "thumbnails are restored from disk");

    // Compared against the thumbnail that was written, not merely "not null": a
    // thumbnail that came back the wrong size or upside down would pass the weaker
    // check. Normalised to one format first because the PNG writer is free to drop the
    // alpha channel for a fully opaque image, and QImage's comparison includes it.
    const QImage after = reopened.entries().at(0).thumbnail;
    printf("       200x80 thumbnail: %dx%d before, %dx%d after\n",
           thumbnailBefore.width(), thumbnailBefore.height(), after.width(), after.height());
    check(after.size() == thumbnailBefore.size(), "the restored thumbnail has the stored size");
    check(after.convertToFormat(QImage::Format_ARGB32)
              == thumbnailBefore.convertToFormat(QImage::Format_ARGB32),
          "and is pixel-identical to what was written");

    check(reopened.entries().at(0).capturedAt.isValid(), "timestamps survive");
    check(reopened.entries().at(0).bytes > 0, "byte sizes survive");

    // Ids must not restart, or a stale menu entry could point at a different capture.
    reopened.add(solid(10, 10, Qt::black));
    check(reopened.entries().at(0).id == 3, "the id counter continues across a reopen");
}

// ---------------------------------------------------------------------------- 7 ---
static void checkStaleEntries() {
    printf("\n[7] an entry whose file vanished is dropped\n");
    const Case c = freshCase("stale");
    QString removed;
    {
        HistoryStore store(c.store);
        store.add(solid(60, 60, Qt::red));
        store.add(solid(60, 60, Qt::green));
        store.add(solid(60, 60, Qt::blue));
        removed = store.entries().at(1).filePath;
    }

    check(QFile::remove(removed), "the middle capture's file was deleted behind the store's back");

    HistoryStore reopened(c.store);
    checkEqInt(reopened.count(), 2, "the stale entry is dropped");
    check(!QFileInfo::exists(c.store + QStringLiteral("/2.t.png")),
          "and its thumbnail goes with it");
    check(QFileInfo::exists(c.store + QStringLiteral("/1.png")), "the others are intact");
}

// ---------------------------------------------------------------------------- 8 ---
static void checkOrphanSweep() {
    printf("\n[8] unreferenced files are swept, referenced ones are not\n");
    const Case c = freshCase("orphans");
    QString kept;
    {
        HistoryStore store(c.store);
        store.add(solid(60, 60, Qt::red));
        kept = store.entries().at(0).filePath;
    }

    // What a crash between the image write and the index write leaves behind.
    check(writeFile(c.store + QStringLiteral("/999.png"), "not an image"),
          "a stray file was dropped into the store directory");
    writeFile(c.store + QStringLiteral("/999.t.png"), QByteArray());

    HistoryStore reopened(c.store);
    checkEqInt(reopened.count(), 1, "the real entry is still there");
    check(!QFileInfo::exists(c.store + QStringLiteral("/999.png")), "the stray image was swept");
    check(!QFileInfo::exists(c.store + QStringLiteral("/999.t.png")), "the stray thumbnail too");
    check(QFileInfo::exists(kept), "the referenced image was not");
}

// ---------------------------------------------------------------------------- 9 ---
static void checkCorruptIndexIsNotDestructive() {
    printf("\n[9] a corrupt index does not delete the captures\n");
    const Case c = freshCase("corrupt");
    {
        HistoryStore store(c.store);
        store.add(solid(60, 60, Qt::red));
        store.add(solid(60, 60, Qt::green));
    }
    const int before = filesIn(c.store).size();

    QFile index(c.store + QStringLiteral("/index.json"));
    check(index.open(QIODevice::WriteOnly | QIODevice::Truncate), "the index can be damaged");
    index.write("{ this is not json");
    index.close();

    HistoryStore reopened(c.store);
    checkEqInt(reopened.count(), 0, "the damaged index yields an empty list");
    checkEqInt(filesIn(c.store).size(), before,
               "but every file is still on disk -- the failure is not destructive");

    // And the store recovers on the next write rather than staying broken.
    check(reopened.add(solid(30, 30, Qt::blue)), "a new capture can still be recorded");
    HistoryStore again(c.store);
    checkEqInt(again.count(), 1, "and it survives the next reopen");
}

// --------------------------------------------------------------------------- 10 ---
static void checkDisabled() {
    printf("\n[10] nothing is written while history is switched off\n");
    const Case c = freshCase("disabled");
    Settings::instance().setHistoryEnabled(false);

    HistoryStore store(c.store);
    check(!store.add(solid(50, 50, Qt::red)), "add() reports that it did nothing");
    checkEqInt(store.count(), 0, "nothing was recorded");
    check(!QFileInfo::exists(c.store + QStringLiteral("/1.png")), "nothing was written");
    check(!QDir(c.store).exists(), "not even the directory");

    Settings::instance().setHistoryEnabled(true);
    check(store.add(solid(50, 50, Qt::red)), "and it works again once re-enabled");
    resetSettings();
}

// --------------------------------------------------------------------------- 11 ---
static void checkThumbnailGeometry() {
    printf("\n[11] the thumbnail is capped, aspect-preserving and 1:1\n");
    const Case c = freshCase("thumbs");
    HistoryStore store(c.store);

    store.add(solid(2000, 1200, Qt::red));
    const QImage big = store.entries().at(0).thumbnail;
    printf("       2000x1200 -> thumbnail %dx%d, dpr %.1f\n",
           big.width(), big.height(), big.devicePixelRatio());
    checkEqInt(qMax(big.width(), big.height()), 160, "the long side is capped at 160");
    checkEqInt(big.width() * 1200, big.height() * 2000, "the aspect ratio is preserved");
    check(qFuzzyCompare(big.devicePixelRatio(), 1.0), "the thumbnail is 1:1, not DPR-scaled");
    checkEqInt(store.entries().at(0).size.width(), 2000, "the entry still knows the real size");

    store.add(solid(40, 30, Qt::blue));
    const QImage small = store.entries().at(0).thumbnail;
    checkEqInt(small.width(), 40, "a small capture is not upscaled (width)");
    checkEqInt(small.height(), 30, "a small capture is not upscaled (height)");
}

// --------------------------------------------------------------------------- 12 ---
static void checkRemoveAndClear() {
    printf("\n[12] removeAt() and clear()\n");
    const Case c = freshCase("remove");
    HistoryStore store(c.store);
    store.add(solid(60, 60, Qt::red));
    store.add(solid(60, 60, Qt::green));
    store.add(solid(60, 60, Qt::blue));

    QString removed = store.entries().at(1).filePath;
    store.removeAt(1);
    checkEqInt(store.count(), 2, "removeAt() drops the entry");
    check(!QFileInfo::exists(removed), "and deletes its file");

    store.removeAt(-1);
    store.removeAt(99);
    checkEqInt(store.count(), 2, "out-of-range indices are ignored");

    store.clear();
    checkEqInt(store.count(), 0, "clear() empties the list");
    const QStringList files = filesIn(c.store);
    printf("       after clear: %s\n",
           files.isEmpty() ? "(empty)" : files.join(QStringLiteral(", ")).toUtf8().constData());
    checkEqInt(files.size(), 1, "only the rewritten index remains");
    checkEqInt(store.count(), 0, "and a reopened store agrees");

    HistoryStore reopened(c.store);
    checkEqInt(reopened.count(), 0, "the clear survives a reopen");
}

// --------------------------------------------------------------------------- 13 ---
static void checkSignals() {
    printf("\n[13] changed() is emitted only after the disk is consistent\n");
    const Case c = freshCase("signals");
    HistoryStore store(c.store);

    int emitted = 0;
    QString indexPathSeen;
    QObject::connect(&store, &HistoryStore::changed, [&]() {
        ++emitted;
        indexPathSeen = c.store + QStringLiteral("/index.json");
    });

    store.add(solid(60, 60, Qt::red));
    checkEqInt(emitted, 1, "add() emits once");
    check(QFileInfo::exists(indexPathSeen), "and the index already exists when it does");

    store.removeAt(0);
    checkEqInt(emitted, 2, "removeAt() emits");
    store.clear();
    checkEqInt(emitted, 3, "clear() emits");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // Isolate QSettings before anything reads it: the store asks Settings for the entry
    // limit, and the real key belongs to the developer's installation.
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("QShotProbe"));

    g_root = QDir::tempPath() + QStringLiteral("/qshot-history-probe");
    QDir(g_root).removeRecursively();
    QDir().mkpath(g_root);

    resetSettings();

    checkEmpty();
    checkAddWritesFiles();
    checkReusesSavedFile();
    checkCountLimit();
    checkByteBudget();
    checkIndexRoundTrip();
    checkStaleEntries();
    checkOrphanSweep();
    checkCorruptIndexIsNotDestructive();
    checkDisabled();
    checkThumbnailGeometry();
    checkRemoveAndClear();
    checkSignals();

    QDir(g_root).removeRecursively();

    printf("\n%d checks, %d failures\n", checks, failures);
    fflush(stdout);
    return failures == 0 ? 0 : 1;
}
