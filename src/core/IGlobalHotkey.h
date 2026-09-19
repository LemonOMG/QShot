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
     * @param key E.g. "A"
     * @param modifiers E.g. Qt::AltModifier
     * @return true if successful, false if occupied or failed
     */
    virtual bool registerHotkey(const QString& key, Qt::KeyboardModifiers modifiers) = 0;
    
    virtual void unregisterHotkey() = 0;

signals:
    void hotkeyPressed();
};

} // namespace qshot
