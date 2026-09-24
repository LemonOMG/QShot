// Visual check for the M5 toolbar: eight tools instead of six, plus the two new icons.
//
// The two new icons ("Num" and "Hilt") are drawn geometry, not glyphs, so the only way to
// know they read as "a numbered badge" and "a highlighter stroke" -- and that they carry the
// same visual weight as the six beside them -- is to look at them. The assertions live in
// probe_toolbar_layout.cpp (size and clamping) and probe_annotation_types.cpp (the drawing);
// this file exists to be looked at.
//
// It renders the toolbar once per tool so the selected state of each icon is visible, and
// pairs each with its size/colour panel, which is where the two new tools differ most
// (the highlighter shows bands rather than circles, and the badge shows a badge ladder).

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QWidget>
#include <cstdio>

#include "overlay/ToolbarWidget.h"
#include "overlay/ToolbarIcons.h"
#include "core/Settings.h"

using namespace qshot;

static QImage renderAt(QWidget* w, qreal dpr) {
    QImage img(w->size() * dpr, QImage::Format_ARGB32);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    w->render(&p);
    p.end();
    return img;
}

// The size/colour panel is a top-level window with no Q_OBJECT, so its className is just
// "QWidget" and it cannot be found by type. Identify it by shape instead: it is the only
// other visible top-level window the toolbar creates, and it has a fixed 36px height.
static QWidget* findSubPanel(ToolbarWidget* toolbar) {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w == toolbar) continue;
        if (w->isVisible() && w->height() == 36 && w->width() > 0) return w;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QShot"));
    QCoreApplication::setApplicationName(QStringLiteral("QShot"));

    // The shipped defaults, so the render shows what a first-run user sees.
    Settings::instance().restoreDefaults();

    ToolbarWidget toolbar;
    toolbar.show();
    toolbar.updatePosition(QRect(300, 300, 600, 400), QRect(0, 0, 1707, 1067));

    const AnnotationType tools[] = {
        AnnotationType::Rectangle, AnnotationType::Ellipse, AnnotationType::Arrow,
        AnnotationType::Pen, AnnotationType::Mosaic, AnnotationType::Text,
        AnnotationType::Number, AnnotationType::Highlight
    };
    const char* names[] = { "Rect", "Ellp", "Arrw", "Pen", "Mosc", "Text", "Num", "Hilt" };

    const int rows = 8;
    const int rowH = 88; // toolbar 40 + gap 4 + panel 36, plus breathing room
    const int contentW = qMax(toolbar.width(), 300) + 20;

    QImage canvas(contentW + 20, rows * rowH + 20, QImage::Format_ARGB32);
    canvas.fill(QColor(190, 190, 190)); // a mid-grey "desktop" so the toolbar reads
    {
        QPainter p(&canvas);
        for (int i = 0; i < rows; ++i) {
            toolbar.handleToolClick(tools[i]);
            QCoreApplication::processEvents(); // let showSubPanel() actually map the panel

            const int top = 10 + i * rowH;
            const QImage bar = renderAt(&toolbar, 1.0);
            p.drawImage(10, top, bar);

            QWidget* panel = findSubPanel(&toolbar);
            if (panel) {
                const QImage sub = renderAt(panel, 1.0);
                // The panel sits above the toolbar in the real product; drawn below it here
                // so that eight of them stack without overlapping each other.
                p.drawImage(10, top + bar.height() + 4, sub);
            }
            printf("       %-5s toolbar %dx%d", names[i], toolbar.width(), toolbar.height());
            if (panel) printf(", panel %dx%d", panel->width(), panel->height());

            // Bright pixels in the action-button half of the bar. The action captions are the
            // only thing drawn there, and this is the number that answers "do the captions
            // look dimmer on the mosaic row?" without anybody having to trust their eyes on a
            // scaled screenshot. It should be the same on every row.
            int bright = 0;
            for (int y = 0; y < bar.height(); ++y) {
                for (int x = 300; x < bar.width(); ++x) {
                    const QColor c = bar.pixelColor(x, y);
                    if (c.red() > 200 && c.green() > 200 && c.blue() > 200) ++bright;
                }
            }
            printf(", action ink %d px\n", bright);
        }
        p.end();
    }
    canvas.save(QStringLiteral("toolbar_eight_tools.png"));
    printf("       saved toolbar_eight_tools.png (%dx%d)\n", canvas.width(), canvas.height());

    // A 2x copy of just the toolbar with the highlighter selected, for judging the icon
    // weights against each other at a size the eye can actually resolve.
    toolbar.handleToolClick(AnnotationType::Highlight); // toggles off, then on again
    toolbar.handleToolClick(AnnotationType::Highlight);
    QCoreApplication::processEvents();
    const QImage big = renderAt(&toolbar, 2.0);
    big.save(QStringLiteral("toolbar_eight_tools_2x.png"));
    printf("       saved toolbar_eight_tools_2x.png (%dx%d)\n", big.width(), big.height());

    printf("       toolbar width is now %d px\n", toolbar.width());

    // --- magnified contact sheet --------------------------------------------------------
    // The glyphs are 20px drawings inside 32px buttons; at 1:1 nobody can judge whether the
    // highlighter band reads as a marker or whether the mosaic reads as pixelation. Nearest
    // neighbour on purpose -- the question is what the pixels are, and smooth scaling would
    // invent detail that is not in the drawing.
    //
    // Drawn directly with toolbaricons::paintTool onto the toolbar's own background colour,
    // so what is judged here is the same call the button makes, not a copy of it.
    {
        constexpr int kCell = 32;
        constexpr int kZoom = 6;
        constexpr int kGap = 12;
        constexpr int kLabelH = 20;
        const int shown = 8;
        const int sheetW = kGap + shown * (kCell * kZoom + kGap);
        const int sheetH = kGap + kCell * kZoom + kLabelH + kGap;

        QImage sheet(sheetW, sheetH, QImage::Format_ARGB32);
        sheet.fill(QColor(43, 43, 43)); // the toolbar's own fill
        {
            QPainter p(&sheet);
            QFont f = p.font();
            f.setPointSize(9);
            p.setFont(f);

            for (int i = 0; i < shown; ++i) {
                QImage cell(kCell, kCell, QImage::Format_ARGB32);
                cell.fill(QColor(43, 43, 43));
                {
                    QPainter cp(&cell);
                    cp.setRenderHint(QPainter::Antialiasing, true);
                    toolbaricons::paintTool(cp, tools[i], QRect(6, 6, kCell - 12, kCell - 12),
                                            Qt::white);
                }
                const int x = kGap + i * (kCell * kZoom + kGap);
                p.drawImage(QRect(x, kGap, kCell * kZoom, kCell * kZoom),
                            cell.scaled(kCell * kZoom, kCell * kZoom, Qt::IgnoreAspectRatio,
                                        Qt::FastTransformation));
                p.setPen(QColor(220, 220, 220));
                p.drawText(QRect(x, kGap + kCell * kZoom, kCell * kZoom, kLabelH),
                           Qt::AlignCenter, QString::fromLatin1(names[i]));
            }
            p.end();
        }
        sheet.save(QStringLiteral("toolbar_icons_zoom.png"));
        printf("       saved toolbar_icons_zoom.png (%dx%d, %dx magnification)\n",
               sheet.width(), sheet.height(), kZoom);
    }

    return 0;
}
