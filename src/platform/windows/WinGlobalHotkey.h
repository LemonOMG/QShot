#pragma once

#include "core/IGlobalHotkey.h"
#include <QAbstractNativeEventFilter>

namespace qshot {

class WinGlobalHotkey : public IGlobalHotkey, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit WinGlobalHotkey(QObject* parent = nullptr);
    ~WinGlobalHotkey() override;

    bool registerHotkey(const QString& key, Qt::KeyboardModifiers modifiers) override;
    void unregisterHotkey() override;

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    int hotkeyId_;
};

} // namespace qshot
