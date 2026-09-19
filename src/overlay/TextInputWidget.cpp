#include "TextInputWidget.h"
#include <QPainter>
#include <QTextBlock>

namespace qshot {

TextInputWidget::TextInputWidget(QWidget* parent)
    : QTextEdit(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    
    setStyleSheet("QTextEdit { background: transparent; border: 1px dashed #666666; padding: 2px; }");
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::NoWrap);
    
    connect(this, &QTextEdit::textChanged, this, &TextInputWidget::adjustSizeToContents);
}

TextInputWidget::~TextInputWidget() = default;

void TextInputWidget::startInput(const QPoint& pos, const QColor& color, int fontSize) {
    finished_ = false;
    clear();
    
    // Set font
    QFont f = font();
    f.setPixelSize(fontSize);
    setFont(f);
    
    // Set color
    QPalette p = palette();
    p.setColor(QPalette::Text, color);
    setPalette(p);
    
    // Initial size
    QFontMetrics fm(f);
    resize(fm.averageCharWidth() * 10 + 10, fm.height() + 10);
    move(pos);
    
    show();
    setFocus();
}

void TextInputWidget::adjustSizeToContents() {
    QFontMetrics fm(font());
    
    int maxWidth = 0;
    QString txt = toPlainText();
    QStringList lines = txt.split('\n');
    for (const QString& line : lines) {
        int w = fm.horizontalAdvance(line);
        if (w > maxWidth) maxWidth = w;
    }
    
    int newWidth = qMax(maxWidth + 20, fm.averageCharWidth() * 10);
    int newHeight = fm.height() * qMax(1, lines.size()) + 10;
    
    resize(newWidth, newHeight);
}

void TextInputWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        cancel();
        return;
    }
    // Enter behavior: shift+enter or ctrl+enter to new line? 
    // Or just normal enter to new line, and click outside to finish.
    QTextEdit::keyPressEvent(e);
}

void TextInputWidget::focusOutEvent(QFocusEvent* e) {
    finish();
    QTextEdit::focusOutEvent(e);
}

void TextInputWidget::finish() {
    if (finished_) return;
    finished_ = true;
    
    QString txt = toPlainText().trimmed();
    if (txt.isEmpty()) {
        emit canceled();
    } else {
        emit editingFinished(txt);
    }
    hide();
}

void TextInputWidget::cancel() {
    if (finished_) return;
    finished_ = true;
    emit canceled();
    hide();
}

void TextInputWidget::paintEvent(QPaintEvent* e) {
    QTextEdit::paintEvent(e);
}

} // namespace qshot
