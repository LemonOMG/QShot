#pragma once

#include <QTextEdit>
#include <QKeyEvent>

namespace qshot {

class TextInputWidget : public QTextEdit {
    Q_OBJECT
public:
    explicit TextInputWidget(QWidget* parent = nullptr);
    ~TextInputWidget() override;

    void startInput(const QPoint& pos, const QColor& color, int fontSize);
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
