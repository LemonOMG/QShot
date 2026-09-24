// Render the real ToolbarWidget + its colour/size sub-panel for each tool, so the
// actual on-screen appearance can be inspected instead of guessed from the code.
#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QImage>
#include <QPixmap>
#include <QThread>
#include <cstdio>
#include "core/Settings.h"
#include "overlay/ToolbarWidget.h"

static void settle(int ms = 300) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

// Composite onto an opaque backdrop: the panels use WA_TranslucentBackground, so a
// raw grab() would be mostly transparent and hard to read.
static void saveComposited(QWidget* w, const QString& path) {
    QPixmap pm = w->grab();
    QImage src = pm.toImage();
    src.setDevicePixelRatio(1.0);
    QImage canvas(src.size(), QImage::Format_ARGB32);
    canvas.fill(QColor(105, 105, 105));
    QPainter p(&canvas);
    p.drawImage(0, 0, src);
    p.end();
    if (canvas.save(path)) {
        printf("saved %-34s %dx%d\n", path.toUtf8().constData(),
               canvas.width(), canvas.height());
    } else {
        printf("FAILED to save %s\n", path.toUtf8().constData());
    }
    fflush(stdout);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    using namespace qshot;

    // Throwaway settings location: the language is switched here, and this must
    // not touch the real user configuration.
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("PanelRender"));

    ToolbarWidget* toolbar = new ToolbarWidget(nullptr);
    toolbar->show();
    settle();

    struct Case { AnnotationType type; const char* name; };
    const Case cases[] = {
        { AnnotationType::Rectangle, "rect" },
        { AnnotationType::Pen,       "pen" },
        { AnnotationType::Mosaic,    "mosaic" },
        { AnnotationType::Text,      "text" },
    };

    // Both languages: the action buttons are captioned with translated text, so
    // the captions and their fit inside a 32px button have to be checked in each.
    struct Lang { Language value; const char* prefix; };
    const Lang langs[] = {
        { Language::Chinese, "zh" },
        { Language::English, "en" },
    };

    for (const Lang& lang : langs) {
        Settings::instance().setLanguage(lang.value);
        settle();

        for (const Case& c : cases) {
            toolbar->handleToolClick(c.type);   // select the tool -> shows the sub-panel
            settle();
            saveComposited(toolbar, QString("panel_%1_%2_toolbar.png").arg(lang.prefix, c.name));
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (w != toolbar && w->isVisible()) {
                    saveComposited(w, QString("panel_%1_%2_sub.png").arg(lang.prefix, c.name));
                }
            }
            toolbar->handleToolClick(c.type);   // toggle off
            settle();
        }
    }
    return 0;
}
