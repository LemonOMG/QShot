#include "TextInputWidget.h"
#include <QPainter>
#include <QTextBlock>

#include "../core/Strings.h"

namespace qshot {

namespace {
// Input frame. Deliberately UI chrome (the app accent) rather than the annotation
// colour, so the box stays visible whatever colour the user picked.
constexpr int kFrameWidth = 2;
constexpr int kTextMargin = 4; // gap between the frame and the text
} // namespace

TextInputWidget::TextInputWidget(QWidget* parent)
    : QTextEdit(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // No border in the stylesheet: with WA_TranslucentBackground a QTextEdit never
    // paints a stylesheet border (measured: a 2px dashed border produced 0 visible
    // pixels), so the frame is drawn in paintEvent() instead. The transparent
    // background is still needed here, otherwise the viewport paints an opaque white
    // rectangle over the screenshot.
    setStyleSheet(QStringLiteral("QTextEdit { background: transparent; border: none; }"));

    document()->setDocumentMargin(kTextMargin);

    // No native frame: the frame is drawn on the viewport in paintEvent().
    setFrameStyle(QFrame::NoFrame);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::NoWrap);

    connect(this, &QTextEdit::textChanged, this, &TextInputWidget::adjustSizeToContents);
}

TextInputWidget::~TextInputWidget() = default;

int TextInputWidget::minimumEditorWidth() const {
    const QFontMetrics fm(font());
    // A little slack: the placeholder must never be clipped at its tail.
    return fm.horizontalAdvance(placeholderText()) + (kTextMargin + kFrameWidth) * 2 + 12;
}

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
    // Placeholder in the annotation colour (semi-transparent) so an empty box still
    // tells the user that it is waiting for input.
    p.setColor(QPalette::PlaceholderText, QColor(color.red(), color.green(), color.blue(), 150));
    setPalette(p);
    setPlaceholderText(text(Str::TextInputPlaceholder));
    
    // Initial size: wide enough for the placeholder, otherwise the hint is clipped
    // and the user sees a half-empty box.
    QFontMetrics fm(f);
    resize(qMax(minimumEditorWidth(), fm.averageCharWidth() * 10),
           fm.height() + (kTextMargin + kFrameWidth) * 2);
    move(pos);
    
    show();
    // setFocus() only sets the focus widget *inside* this window; it does not make
    // the window active. Without activateWindow() the overlay keeps the keyboard,
    // every typed character is delivered to SnapOverlay and dropped, and the text
    // tool is silently unusable. Verified with a probe: activateWindow() + setFocus()
    // is what actually lets the user type.
    activateWindow();
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

    // While the box is empty the placeholder is what has to fit.
    const int minWidth = txt.isEmpty() ? minimumEditorWidth() : fm.averageCharWidth() * 6;
    int newWidth = qMax(maxWidth + (kTextMargin + kFrameWidth) * 2 + 8, minWidth);
    int newHeight = fm.height() * qMax(1, lines.size()) + (kTextMargin + kFrameWidth) * 2;
    
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

void TextInputWidget::cancelInput() {
    cancel();
}

void TextInputWidget::paintEvent(QPaintEvent* e) {
    QTextEdit::paintEvent(e);

    // Draw the input frame on the viewport, i.e. the same surface QTextEdit paints
    // its text on, so it is guaranteed to end up on top.
    //
    // Two dead ends worth recording: a stylesheet border is never painted by a
    // QTextEdit with WA_TranslucentBackground (measured: 0 visible pixels), and a
    // frame drawn with QPainter(this) gets covered by the viewport child. Both left
    // the editor looking like a bare patch of screen, which is why users could not
    // tell that the text tool had opened an input box.
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(26, 173, 25), kFrameWidth, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(viewport()->rect().adjusted(kFrameWidth / 2, kFrameWidth / 2,
                                           -kFrameWidth / 2 - 1, -kFrameWidth / 2 - 1));
}

} // namespace qshot
