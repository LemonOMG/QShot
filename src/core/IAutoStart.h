#pragma once

namespace qshot {

/**
 * Registering the application to start at login is inherently platform
 * specific: the Windows registry Run key, a launchd plist on macOS, an
 * autostart .desktop file on Linux. The interface is deliberately tiny - the
 * only thing the settings UI needs to know.
 */
class IAutoStart {
public:
    virtual ~IAutoStart() = default;

    /// True when an entry for this application currently exists.
    virtual bool isEnabled() const = 0;

    /**
     * Create or remove the entry.
     * @return false if the platform refused the change.
     */
    virtual bool setEnabled(bool enabled) = 0;
};

} // namespace qshot
