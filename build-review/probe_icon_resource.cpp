// Does the icon actually end up inside the binary, and is it the real icon?
//
// The tray icon was a `pixmap.fill(Qt::blue)` placeholder for the whole life of the project,
// and the failure mode of the replacement is silent: a resource path that does not resolve
// yields a null QIcon, and Qt then falls back to *something* -- a blank pixmap or the default
// Qt icon -- without complaining. Nothing crashes, and the only symptom is that the tray
// looks wrong, which is exactly the sort of thing that gets noticed late.
//
// So this loads the resources back out of the compiled-in table and asserts on pixels.
//
// The structural check on the .ico container itself lives in tools/verify_icon.py, which
// parses the file without Qt. This probe is the other half: that the build embedded it, that
// Qt resolves it, and that what comes out is the viewfinder rather than a blue square.

#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QList>
#include <QSize>
#include <QByteArray>
#include <QFile>
#include <cstdio>

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

static void report(int got, int want, const char* what) {
    ++checks;
    const bool ok = (got == want);
    if (!ok) ++failures;
    printf("  %-4s %s (got %d, want %d)\n", ok ? "ok" : "FAIL", what, got, want);
}

// Little-endian readers for the .ico directory walk in section [5]. The container is defined
// by the Windows headers, so its integers are little-endian regardless of host endianness.
static int readU16(const QByteArray& b, int at) {
    return quint8(b.at(at)) | (quint8(b.at(at + 1)) << 8);
}

static int readU32(const QByteArray& b, int at) {
    return int(quint32(quint8(b.at(at)))
               | (quint32(quint8(b.at(at + 1))) << 8)
               | (quint32(quint8(b.at(at + 2))) << 16)
               | (quint32(quint8(b.at(at + 3))) << 24));
}

// Counts pixels that are close to white and pixels that are close to the plate blue. The
// placeholder this replaced was a flat blue fill, so "has white pixels" is precisely the
// assertion that distinguishes the real icon from the placeholder.
static void countInk(const QImage& img, int* white, int* blue, int* transparent) {
    *white = *blue = *transparent = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.alpha() < 32) {
                ++*transparent;
            } else if (c.red() > 200 && c.green() > 200 && c.blue() > 200) {
                ++*white;
            } else if (c.blue() > 120 && c.blue() > c.red() + 40) {
                ++*blue;
            }
        }
    }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    printf("[1] the resources are compiled into this binary\n");
    check(QFile::exists(QStringLiteral(":/icons/qshot.ico")), ":/icons/qshot.ico exists");
    check(QFile::exists(QStringLiteral(":/icons/qshot_256.png")),
          ":/icons/qshot_256.png exists");

    printf("\n[2] the icon loads and carries every size\n");
    const QIcon icon(QStringLiteral(":/icons/qshot.ico"));
    check(!icon.isNull(), "the icon is not null");

    const QList<QSize> sizes = icon.availableSizes();
    printf("       available sizes:");
    for (const QSize& s : sizes) printf(" %dx%d", s.width(), s.height());
    printf("\n");

    const int expected[] = { 16, 20, 24, 32, 48, 64, 128, 256 };
    for (int want : expected) {
        bool found = false;
        for (const QSize& s : sizes) {
            if (s.width() == want && s.height() == want) found = true;
        }
        char label[96];
        snprintf(label, sizeof(label), "the %dpx entry is present", want);
        check(found, label);
    }

    printf("\n[3] the smallest size is the real icon, not a blank or a blue square\n");
    // 16px is what the system tray actually draws. This is the assertion that would have
    // caught the placeholder: a flat blue fill has zero white pixels.
    //
    // QIcon::pixmap(w, h) interprets (w, h) as *logical* pixels and returns a pixmap carrying
    // the screen's devicePixelRatio. In Qt 6 QPixmap::size() reports *device* pixels --
    // deviceIndependentSize() is the logical one -- so at dpr 1.5 a request for 16 comes back
    // as a 24x24 pixmap. The first version of this probe asserted size().width() == 16 and was
    // wrong about which of the two size() reports, not about Qt.
    {
        const QPixmap pm = icon.pixmap(16, 16);
        check(!pm.isNull(), "a 16px pixmap renders");

        const qreal dpr = pm.devicePixelRatio();
        printf("       dpr %.2f, device %dx%d, independent %dx%d\n",
               dpr, pm.size().width(), pm.size().height(),
               qRound(pm.deviceIndependentSize().width()),
               qRound(pm.deviceIndependentSize().height()));

        report(qRound(pm.deviceIndependentSize().width()), 16,
               "the logical size is what was asked for");
        report(pm.size().width(), qRound(16 * dpr),
               "and the device size is the logical size times the dpr");

        // The artwork check wants the icon's own pixels, so it forces 1:1 instead of reading
        // the resampled copy the screen asked for. Same reasoning as section [5].
        const QImage img = icon.pixmap(QSize(16, 16), 1.0).toImage();
        report(img.width(), 16, "forcing dpr 1 gives exactly 16 device pixels");

        int white = 0, blue = 0, transparent = 0;
        countInk(img, &white, &blue, &transparent);
        printf("       white %d, blue %d, transparent %d of %d\n",
               white, blue, transparent, img.width() * img.height());

        check(white > 0, "it contains white pixels, so the viewfinder brackets survived");
        check(blue > 0, "and blue pixels for the plate");
        check(white + blue > img.width() * img.height() / 4,
              "most of the icon is drawn, not mostly empty");
    }

    printf("\n[4] the large size keeps the composition\n");
    // dpr 1.0 explicitly: this is about the artwork, and a scaled-up copy would blur the
    // very edges being measured. Same reason the comparison in [5] forces it.
    {
        const QImage big = icon.pixmap(QSize(256, 256), 1.0).toImage();
        report(big.width(), 256, "a 256px pixmap renders");

        int white = 0, blue = 0, transparent = 0;
        countInk(big, &white, &blue, &transparent);
        const int total = big.width() * big.height();
        printf("       white %.1f%%, blue %.1f%%, transparent %.1f%%\n",
               100.0 * white / total, 100.0 * blue / total, 100.0 * transparent / total);

        // The plate is a rounded square inset by 3%, so the corners must be transparent --
        // which also proves the alpha channel made it through the PNG-in-ICO encoding
        // instead of being flattened to black.
        check(transparent > 0, "the rounded corners are transparent");
        check(big.pixelColor(0, 0).alpha() < 32, "specifically the top-left corner is");
        check(big.pixelColor(128, 128).alpha() > 200, "and the centre is opaque");
        check(blue > total / 4, "the plate covers a large share of the icon");
        check(white > total / 50, "the brackets are a visible minority of it");
    }

    printf("\n[5] the standalone PNG is the .ico's own 256px payload\n");
    {
        const QImage png(QStringLiteral(":/icons/qshot_256.png"));
        check(!png.isNull(), "the 256px PNG loads");
        report(png.width(), 256, "and is 256 wide");

        // Byte-level, deliberately not pixel-level.
        //
        // The generator renders 256px once and writes those exact bytes both into the .ico's
        // last entry and to the standalone .png, so byte identity is the real claim. Comparing
        // *rendered* pixels instead drags in two things that are not under test: the icon
        // engine's entry selection, and QPixmap's premultiplied storage -- QImage::pixel()
        // returns the stored value, so every antialiased edge (alpha < 255) differs in RGB
        // between a premultiplied pixmap and a straight-alpha PNG. That was 921 pixels of
        // pure comparison artefact, and this probe's second wrong expectation.
        QFile icoFile(QStringLiteral(":/icons/qshot.ico"));
        QFile pngFile(QStringLiteral(":/icons/qshot_256.png"));
        if (!icoFile.open(QIODevice::ReadOnly) || !pngFile.open(QIODevice::ReadOnly)) {
            check(false, "both resources open for reading");
        } else {
            const QByteArray icoBytes = icoFile.readAll();
            const QByteArray pngBytes = pngFile.readAll();
            icoFile.close();
            pngFile.close();

            // Minimal directory walk: 6-byte header, then 16 bytes per entry. Only the size
            // and offset fields matter here.
            const int count = readU16(icoBytes, 4);
            printf("       the container holds %d entries, the PNG is %d bytes\n",
                   count, int(pngBytes.size()));

            int foundAt = -1;
            for (int i = 0; i < count; ++i) {
                const int base = 6 + 16 * i;
                if (base + 16 > icoBytes.size()) break;
                // 0 in the width byte means 256 -- the format's own convention.
                const int w = quint8(icoBytes.at(base));
                if (w == 0 || w == 256) { foundAt = i; break; }
            }
            check(foundAt >= 0, "a 256px entry exists in the directory");

            if (foundAt >= 0) {
                const int base = 6 + 16 * foundAt;
                const int size = readU32(icoBytes, base + 8);
                const int offset = readU32(icoBytes, base + 12);
                report(size, int(pngBytes.size()), "its payload is the same length as the PNG");

                if (size == pngBytes.size() && offset + size <= icoBytes.size()) {
                    const QByteArray payload = icoBytes.mid(offset, size);
                    check(payload == pngBytes,
                          "and is byte-identical to the standalone PNG");
                } else {
                    check(false, "and is byte-identical to the standalone PNG");
                }
            }
        }
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
