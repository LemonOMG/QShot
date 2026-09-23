#pragma once

#include <memory>
#include <QObject>

#include "IScreenCapture.h"
#include "IGlobalHotkey.h"
#include "IWindowDetector.h"
#include "IAutoStart.h"
#include "ISingleInstance.h"

namespace qshot {

class PlatformFactory {
public:
    static std::unique_ptr<IScreenCapture> createScreenCapture();
    static IGlobalHotkey* createGlobalHotkey(QObject* parent = nullptr);
    static std::unique_ptr<IWindowDetector> createWindowDetector();
    static std::unique_ptr<IAutoStart> createAutoStart();
    /// nullptr on platforms without an implementation, in which case the application
    /// simply runs without a single-instance guard.
    static std::unique_ptr<ISingleInstance> createSingleInstance();
};

} // namespace qshot
