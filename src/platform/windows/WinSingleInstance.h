#pragma once

#include "core/ISingleInstance.h"
#include <QString>

namespace qshot {

/// Windows single-instance guard, backed by a named mutex.
class WinSingleInstance : public ISingleInstance {
public:
    /**
     * @param name the kernel object name to claim. Empty means the application's own
     *             name. The parameter exists so that a probe can take a guard that
     *             cannot collide with a QShot the user happens to be running -- a
     *             probe that reported "another instance exists" because the real
     *             application was open would be a false failure, and one that stole
     *             the name would stop the real application from starting.
     */
    explicit WinSingleInstance(const QString& name = QString());
    ~WinSingleInstance() override;

    bool acquire() override;

private:
    QString name_;
    /// HANDLE, kept as void* so windows.h stays out of this header.
    void* handle_ = nullptr;
    bool held_ = false;
};

} // namespace qshot
