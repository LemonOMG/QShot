// Step-by-step diagnosis of the system-cursor rendering used by
// WinScreenCapture::captureScreen(..., includeCursor = true).
//
// Reports every Win32 return value and, crucially, how many pixels of the
// rendered cursor actually have a non-zero alpha channel - that is the usual
// failure mode (DrawIconEx leaving alpha at 0 on a DIB section).

#include <QApplication>
#include <QImage>
#include <cstdio>
#include <cstring>

#include <windows.h>

namespace {

void report(const QImage& img, const char* label) {
    if (img.isNull()) {
        std::printf("%s: NULL image\n", label);
        return;
    }
    int opaque = 0;
    int anyAlpha = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = img.pixel(x, y);
            if (qAlpha(p) != 0) ++anyAlpha;
            if (qAlpha(p) == 255) ++opaque;
        }
    }
    std::printf("%s: %dx%d format=%d  alpha>0: %d  alpha==255: %d\n",
                label, img.width(), img.height(), static_cast<int>(img.format()),
                anyAlpha, opaque);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    std::printf("SM_CXCURSOR=%d SM_CYCURSOR=%d\n",
                GetSystemMetrics(SM_CXCURSOR), GetSystemMetrics(SM_CYCURSOR));

    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    const BOOL ok = GetCursorInfo(&ci);
    std::printf("GetCursorInfo ok=%d flags=0x%lx showing=%d hCursor=%p pt=(%ld,%ld)\n",
                static_cast<int>(ok), static_cast<unsigned long>(ci.flags),
                (ci.flags & CURSOR_SHOWING) ? 1 : 0,
                static_cast<void*>(ci.hCursor),
                static_cast<long>(ci.ptScreenPos.x), static_cast<long>(ci.ptScreenPos.y));
    if (!ok || (ci.flags & CURSOR_SHOWING) == 0 || ci.hCursor == nullptr) {
        std::printf("=> early return: cursor not available\n");
        return 0;
    }

    ICONINFO ii{};
    const BOOL okIcon = GetIconInfo(ci.hCursor, &ii);
    std::printf("GetIconInfo ok=%d hotspot=(%ld,%ld) color=%p mask=%p\n",
                static_cast<int>(okIcon), static_cast<long>(ii.xHotspot),
                static_cast<long>(ii.yHotspot),
                static_cast<void*>(ii.hbmColor), static_cast<void*>(ii.hbmMask));
    if (!okIcon) {
        std::printf("=> early return: GetIconInfo failed\n");
        return 0;
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);

    const int w = GetSystemMetrics(SM_CXCURSOR);
    const int h = GetSystemMetrics(SM_CYCURSOR);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    std::printf("CreateDIBSection dib=%p bits=%p\n",
                static_cast<void*>(dib), bits);
    if (!dib || !bits) return 0;

    HGDIOBJ previous = SelectObject(memDc, dib);
    std::printf("SelectObject previous=%p (HGDI_ERROR=%p)\n",
                previous, HGDI_ERROR);
    std::memset(bits, 0, static_cast<size_t>(w) * h * 4u);

    const BOOL okDraw = DrawIconEx(memDc, 0, 0, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    std::printf("DrawIconEx ok=%d GetLastError=%lu\n",
                static_cast<int>(okDraw), static_cast<unsigned long>(GetLastError()));
    SelectObject(memDc, previous);

    const QImage view(reinterpret_cast<const uchar*>(bits), w, h, w * 4,
                      QImage::Format_ARGB32_Premultiplied);
    const QImage owned = view.copy();
    report(owned, "DIB render (DI_NORMAL)");

    // Control: the raw DIB bytes, interpreted as straight ARGB, in case the
    // premultiplied interpretation is what hides the cursor.
    const QImage straight(reinterpret_cast<const uchar*>(bits), w, h, w * 4,
                          QImage::Format_ARGB32);
    report(straight.copy(), "DIB render (ARGB32 view)");

    owned.scaled(w * 6, h * 6, Qt::IgnoreAspectRatio, Qt::FastTransformation)
        .save(QStringLiteral("cursor_bitmap.png"));
    std::printf("saved cursor_bitmap.png\n");

    DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    std::fflush(stdout);
    return 0;
}
