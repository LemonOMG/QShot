#pragma once

#include <QString>

#include "core/IAutoStart.h"

namespace qshot {

/**
 * Windows implementation: an entry under
 * HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run.
 *
 * HKCU (not HKLM) on purpose - it needs no elevation, and the setting is
 * per-user, which matches a per-user configuration.
 */
class WinAutoStart : public IAutoStart {
public:
    WinAutoStart() = default;

    bool isEnabled() const override;
    bool setEnabled(bool enabled) override;

private:
    /// Value name inside the Run key.
    static const char* valueName();
    /// Fully quoted, native-separator command line for the current executable.
    static QString commandLine();
};

} // namespace qshot
