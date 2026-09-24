// Off-screen render of the settings dialog, in both languages and both save
// formats. Linked against the real SettingsDialog / Settings / Strings sources so
// what is rendered is the shipped layout, not a mock-up.
//
// Writes into a throwaway QSettings key (QShotProbe) so running this never
// touches the real user configuration.

#include <QApplication>
#include <QGroupBox>
#include <QImage>
#include <QLayout>
#include <QPainter>
#include <QPixmap>
#include <cstdio>

#include "core/Settings.h"
#include "core/Strings.h"
#include "ui/SettingsDialog.h"

using namespace qshot;

namespace {

void renderDialog(const QString& fileName, Language language, SaveFormat format) {
    Settings& settings = Settings::instance();
    settings.setLanguage(language);
    settings.setSaveFormat(format);
    installQtTranslations();

    SettingsDialog dialog;
    dialog.show();
    QApplication::processEvents();
    dialog.adjustSize();
    // adjustSize() only posts a layout request; without activating the layout the
    // children are still drawn at the previous size and the grab is half empty.
    if (dialog.layout()) {
        dialog.layout()->activate();
    }
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();

    // Render explicitly at devicePixelRatio 1 so the image is 1:1 with the
    // logical geometry printed below. QWidget::grab() would return a
    // DPR-scaled pixmap, which makes the two impossible to compare by eye.
    QPixmap shot(dialog.size());
    shot.setDevicePixelRatio(1.0);
    dialog.render(&shot);
    shot.save(fileName);

    // Diagnostics: the dialog background is close in tone to any backdrop we
    // might composite onto, so report the real geometry instead of eyeballing it.
    std::printf("saved %s  dialog=%dx%d\n", qPrintable(fileName),
                shot.width(), shot.height());
    for (QGroupBox* box : dialog.findChildren<QGroupBox*>()) {
        std::printf("    group '%s'  x=%d y=%d w=%d h=%d\n",
                    qPrintable(box->title()),
                    box->x(), box->y(), box->width(), box->height());
    }
    std::fflush(stdout);

    dialog.close();
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Throwaway settings location - never the real one.
    QCoreApplication::setOrganizationName(QStringLiteral("QShotProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("SettingsRender"));

    renderDialog(QStringLiteral("settings_zh_png.png"), Language::Chinese, SaveFormat::Png);
    renderDialog(QStringLiteral("settings_zh_jpeg.png"), Language::Chinese, SaveFormat::Jpeg);
    renderDialog(QStringLiteral("settings_en_png.png"), Language::English, SaveFormat::Png);

    return 0;
}
