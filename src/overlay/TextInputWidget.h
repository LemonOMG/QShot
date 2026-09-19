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
    
    bool finished_ = false;
};

} // namespace qshot
