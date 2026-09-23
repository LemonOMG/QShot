#include "WinSingleInstance.h"

#include <QDebug>
#include <windows.h>

namespace qshot {

namespace {
/**
 * "Local\" scopes the name to the logon session, so two users logged in at once (fast
 * user switching) each get their own QShot instead of the second one being told to go
 * away by a mutex it can see but whose owner it cannot reach.
 *
 * The name carries no version: the point is "one QShot per desktop", and an update that
 * changed the name would happily let the old and new builds run side by side -- which is
 * exactly the state this guard exists to prevent.
 */
constexpr auto kDefaultName = L"Local\\QShot.SingleInstance";
} // namespace

WinSingleInstance::WinSingleInstance(const QString& name) : name_(name) {}

WinSingleInstance::~WinSingleInstance() {
    if (handle_) {
        // Closing the last handle is what removes the name. A mutex that was never
        // waited on has no separate release call.
        CloseHandle(static_cast<HANDLE>(handle_));
    }
}

bool WinSingleInstance::acquire() {
    if (held_) return true;

    const QString name = name_.isEmpty() ? QString::fromWCharArray(kDefaultName) : name_;
    HANDLE handle = CreateMutexW(nullptr, FALSE,
                                 reinterpret_cast<LPCWSTR>(name.utf16()));

    // GetLastError() has to be read before anything else can clobber it, so both
    // branches below act on it first and only then format a message.
    const DWORD error = GetLastError();

    if (!handle) {
        // No handle at all means the object could not be created -- out of handles, or a
        // name the caller is not allowed to use. Refusing to start would be worse than
        // starting twice, so this instance goes ahead and says so.
        qWarning() << "CreateMutexW failed for" << name << "error" << error
                   << "- single-instance guard is not active";
        return true;
    }

    if (error == ERROR_ALREADY_EXISTS) {
        // CreateMutexW succeeds even when the object already exists; the error code is the
        // only signal. Hand back the handle we were given -- it is not the guard.
        CloseHandle(handle);
        return false;
    }

    handle_ = handle;
    held_ = true;
    return true;
}

} // namespace qshot
