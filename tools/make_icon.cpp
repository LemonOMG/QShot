// Generates the application icon: resources/qshot.ico and resources/qshot_256.png.
//
// Committed as a tool rather than kept as a build step, because an icon is a build *input*:
// it should be regenerated deliberately, not on every compile. Keeping the source means the
// icon can be adjusted later instead of being an opaque binary nobody can edit.
//
// Build and run:  bash tools/make_icon.sh
//
// The motif is a viewfinder: four corner brackets inside a rounded square. Deliberately not
// a camera or a rectangle-with-handles -- at 16px, which is the size that actually matters in
// a system tray, anything with more than a few strokes turns to mud, and brackets survive
// because they are four bold shapes with a lot of space between them.

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include <QByteArray>
#include <QVector>
#include <cstdio>

namespace {

// Sizes Windows actually asks for. 16 (tray, list views), 20 and 24 (taskbar and small
// icons at higher scaling), 32 (alt-tab, desktop), 48 and 64 (Explorer's larger views),
// 128 and 256 (thumbnails and the store).
const QVector<int> kSizes = { 16, 20, 24, 32, 48, 64, 128, 256 };

QImage drawIcon(int size) {
    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    img.setDevicePixelRatio(1.0);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = size;

    // --- rounded-square plate -------------------------------------------------------
    // Inset by half a pixel so the antialiased edge is not clipped by the image bounds.
    const qreal plateInset = s * 0.03;
    const QRectF plate(plateInset, plateInset, s - 2 * plateInset, s - 2 * plateInset);
    const qreal radius = s * 0.22;

    QLinearGradient grad(plate.topLeft(), plate.bottomLeft());
    grad.setColorAt(0.0, QColor(0x42, 0xA5, 0xF5));
    grad.setColorAt(1.0, QColor(0x15, 0x65, 0xC0));

    QPainterPath platePath;
    platePath.addRoundedRect(plate, radius, radius);
    p.fillPath(platePath, grad);

    // --- viewfinder brackets --------------------------------------------------------
    // Arm length and inset are fractions of the icon so the composition holds at every
    // size. The stroke has a floor of one device pixel: below that it antialiases away to
    // a grey smear and the icon loses its shape entirely at 16px.
    //
    // The arm-to-thickness ratio is deliberately around 3:1. At 2:1 the brackets read as
    // four fat blobs rather than as a frame -- visible immediately in the magnified
    // preview, and the reason that preview exists.
    const qreal inset = s * 0.195;
    const qreal arm   = s * 0.225;
    const qreal w     = qMax(1.0, s * 0.075);

    QPen pen(Qt::white, w, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal left   = inset;
    const qreal top    = inset;
    const qreal right  = s - inset;
    const qreal bottom = s - inset;

    // Each bracket is an L: two strokes meeting at a corner.
    p.drawLine(QPointF(left, top + arm), QPointF(left, top));
    p.drawLine(QPointF(left, top), QPointF(left + arm, top));

    p.drawLine(QPointF(right - arm, top), QPointF(right, top));
    p.drawLine(QPointF(right, top), QPointF(right, top + arm));

    p.drawLine(QPointF(left, bottom - arm), QPointF(left, bottom));
    p.drawLine(QPointF(left, bottom), QPointF(left + arm, bottom));

    p.drawLine(QPointF(right - arm, bottom), QPointF(right, bottom));
    p.drawLine(QPointF(right, bottom), QPointF(right, bottom - arm));

    p.end();
    return img;
}

// --- ICO container -------------------------------------------------------------------
// Little-endian throughout, as the format is defined by the Windows headers.
void appendU16(QByteArray& out, quint16 v) {
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

void appendU32(QByteArray& out, quint32 v) {
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

QByteArray pngBytes(const QImage& img) {
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    buf.close();
    return data;
}

// Writes an ICO whose entries are PNGs.
//
// PNG entries are legal at every size from Vista onwards and are what makes a 256px entry
// practical -- as a raw DIB it alone would be 256KB, larger than the rest of the file
// combined. The alternative (DIB for the small sizes, PNG only for 256) exists for the
// benefit of Windows XP's shell, which is not a target.
QByteArray buildIco(const QVector<QImage>& images) {
    QVector<QByteArray> payloads;
    payloads.reserve(images.size());
    for (const QImage& img : images) {
        payloads.append(pngBytes(img));
    }

    QByteArray out;
    appendU16(out, 0);                      // reserved
    appendU16(out, 1);                      // type: 1 = icon
    appendU16(out, quint16(images.size())); // image count

    // The directory precedes the pixel data, so the offset of the first image is known
    // once every entry has been accounted for.
    quint32 offset = 6 + 16 * quint32(images.size());
    for (int i = 0; i < images.size(); ++i) {
        const QImage& img = images.at(i);
        // 256 is encoded as 0 in a single byte; that is the format's own convention, not a
        // bug, and getting it wrong makes the entry unloadable.
        out.append(char(img.width() >= 256 ? 0 : img.width()));
        out.append(char(img.height() >= 256 ? 0 : img.height()));
        out.append(char(0));  // palette size: 0 for true colour
        out.append(char(0));  // reserved
        appendU16(out, 1);    // colour planes
        appendU16(out, 32);   // bits per pixel
        appendU32(out, quint32(payloads.at(i).size()));
        appendU32(out, offset);
        offset += quint32(payloads.at(i).size());
    }

    for (const QByteArray& payload : payloads) {
        out.append(payload);
    }
    return out;
}

bool writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        printf("  FAIL could not open %s for writing\n", qPrintable(path));
        return false;
    }
    const qint64 written = f.write(data);
    f.close();
    if (written != data.size()) {
        printf("  FAIL short write to %s (%lld of %lld)\n", qPrintable(path),
               static_cast<long long>(written), static_cast<long long>(data.size()));
        return false;
    }
    return true;
}

// A contact sheet of every size, each magnified by an integer factor chosen to bring it to
// roughly the same on-screen height.
//
// Nearest-neighbour on purpose: the question being asked is "what do the actual pixels look
// like", and smooth scaling would invent detail that is not in the file. The 16px entry is
// the one that matters most (system tray, list views) and is unreadable at 1:1 on a
// high-DPI display, which is exactly why the icon has to be judged here rather than by
// squinting at the .ico in a viewer.
QImage buildPreview(const QVector<QImage>& images, int targetHeight) {
    QVector<int> zooms;
    QVector<QRect> boxes;
    int totalW = 8;
    int maxH = 0;

    for (const QImage& img : images) {
        const int zoom = qMax(1, qRound(qreal(targetHeight) / img.height()));
        zooms.append(zoom);
        const QRect box(totalW, 8, img.width() * zoom, img.height() * zoom);
        boxes.append(box);
        totalW += box.width() + 8;
        maxH = qMax(maxH, box.height());
    }

    QImage sheet(totalW, maxH + 16, QImage::Format_ARGB32);
    // A mid-grey backdrop: the icon is mostly blue and white, so it needs a background that
    // is neither, otherwise a white bracket against white would read as a gap.
    sheet.fill(QColor(128, 128, 128));

    QPainter p(&sheet);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int i = 0; i < images.size(); ++i) {
        p.drawImage(boxes.at(i), images.at(i));
    }
    p.end();
    return sheet;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // Resolve relative to this source file's project root, so the tool works regardless of
    // the directory it is invoked from.
    const QString outDir = QDir(QStringLiteral(QSHOT_RESOURCE_DIR)).absolutePath();
    if (!QDir().mkpath(outDir)) {
        printf("  FAIL could not create %s\n", qPrintable(outDir));
        return 1;
    }

    QVector<QImage> images;
    for (int size : kSizes) {
        images.append(drawIcon(size));
    }

    const QByteArray ico = buildIco(images);
    const QString icoPath = outDir + QStringLiteral("/qshot.ico");
    if (!writeFile(icoPath, ico)) return 1;
    printf("  ok   wrote %s (%lld bytes, %d sizes)\n", qPrintable(icoPath),
           static_cast<long long>(ico.size()), int(images.size()));

    // A standalone 256px PNG as well: useful for the README, and it gives an independent
    // handle on the rendering (a viewer can show it even if the ICO reader is suspect).
    const QString pngPath = outDir + QStringLiteral("/qshot_256.png");
    if (!writeFile(pngPath, pngBytes(images.last()))) return 1;
    printf("  ok   wrote %s\n", qPrintable(pngPath));

    // Optional contact sheet, magnified 4x. Written to the path given on the command line so
    // it lands wherever the caller keeps evidence, rather than beside the icon.
    if (argc > 1) {
        const QString previewPath = QString::fromLocal8Bit(argv[1]);
        const QImage sheet = buildPreview(images, 128);
        if (!writeFile(previewPath, pngBytes(sheet))) return 1;
        printf("  ok   wrote %s (%dx%d)\n", qPrintable(previewPath),
               sheet.width(), sheet.height());
    }

    return 0;
}
