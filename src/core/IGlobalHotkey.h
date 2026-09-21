#pragma once

#include <QObject>
#include <QString>
#include <QKeySequence>

namespace qshot {

class IGlobalHotkey : public QObject {
    Q_OBJECT
public:
    explicit IGlobalHotkey(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IGlobalHotkey() = default;

    /**
     * @brief Register a system-wide hotkey.
     *
     * Takes a QKeySequence rather than a key name plus modifiers: the settings
     * dialog produces a QKeySequence, and converting it to a string only to parse
     * it back is a lossy round trip through Qt's key-name grammar.
     *
     * @param sequence Key plus modifiers, e.g. Alt+A. Only the first chord is used.
     * @return true if successful, false if occupied, unmappable or otherwise failed
     */
    virtual bool registerHotkey(const QKeySequence& sequence) = 0;

    virtual void unregisterHotkey() = 0;

signals:
    void hotkeyPressed();
};

} // namespace qshot
