#pragma once

#include <QImage>
#include <QString>

class QWidget;

namespace qshot {

/**
 * Put `image` on the system clipboard.
 *
 * QPixmap::fromImage() carries the device pixel ratio over from the QImage (verified
 * with a probe), so a high-DPI capture pastes at its true physical size instead of
 * being upscaled from a logical one. Doing nothing when the image is null is
 * deliberate: the callers use "nothing to copy" and "cancelled" interchangeably.
 */
void copyImageToClipboard(const QImage& image);

/**
 * Write `image` into the save folder under a generated name, without asking.
 *
 * The counterpart of saveImageWithDialog() for Settings::quietSave(). The name is
 * `screenshot_<yyyy-MM-dd_HH-mm-ss>.<ext>` with a numeric suffix when that is taken, and
 * the extension follows Settings::saveFormat().
 *
 * Returns the path written, or an empty string if nothing was written. Returning the path
 * matters for the same reason as in the dialog path: the history store adopts that file
 * instead of encoding the same image a second time.
 *
 * A failure here cannot be reported the way the dialog path's can -- there is no dialog to
 * put a message in -- so `parent`, when given, becomes the parent of a warning box. Pass
 * nullptr only if the caller reports the failure itself, or in a test, which must not
 * block on a modal dialog.
 *
 * `directory` overrides Settings::saveDirectory(); empty means "use the setting". It exists
 * so a probe can exercise this against a temporary folder: the real directory is user data,
 * and a test must never be able to write there.
 */
QString saveImageQuietly(const QImage& image, QWidget* parent = nullptr,
                         const QString& directory = QString());

/**
 * Ask the user where to save `image`, then write it.
 *
 * Returns the path the image was written to, or an empty string if the dialog was
 * cancelled or the write failed. Returning the path rather than a bool is what lets the
 * history store adopt the file the user just saved instead of encoding the same image
 * a second time.
 *
 * The filter list is ordered from Settings::saveFormat(), which only preselects the
 * default; the encoder is derived from the extension the user actually ends up with,
 * so typing a different one in the dialog is honoured.
 *
 * `parent` is used for the dialog because both callers are always-on-top windows, and
 * an unowned dialog can end up behind them.
 *
 * Deliberately still opens a dialog when Settings::quietSave() is on: the two callers are
 * "Save As" entries, and choosing a location is what "Save As" means.
 */
QString saveImageWithDialog(QWidget* parent, const QImage& image);

} // namespace qshot
