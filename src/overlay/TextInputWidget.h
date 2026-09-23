#pragma once

#include <QTextEdit>
#include <QKeyEvent>

namespace qshot {

class TextInputWidget : public QTextEdit {
    Q_OBJECT
public:
    explicit TextInputWidget(QWidget* parent = nullptr);
    ~TextInputWidget() override;

    // `globalPos` is in global desktop coordinates, not the parent overlay's space.
    // The editor is a top-level window, so move() interprets its argument globally;
    // feeding it a screen-local point silently places the box on the primary
    // monitor whenever the overlay is showing on any other one.
    void startInput(const QPoint& globalPos, const QColor& color, int fontSize);
    // Discard any in-progress input and hide the editor (used when the overlay
    // resets, so a half-finished text box cannot linger on an empty canvas).
    void cancelInput();

signals:
    void editingFinished(const QString& text);
    void canceled();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    
private:
    void finish();
    void cancel();
    void adjustSizeToContents();
    // Width needed for the placeholder hint, so it is never clipped.
    int minimumEditorWidth() const;
    
    bool finished_ = false;
};

} // namespace qshot
