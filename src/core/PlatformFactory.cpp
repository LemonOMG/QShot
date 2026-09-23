#include "PlatformFactory.h"

#ifdef Q_OS_WIN
#include "../platform/windows/WinScreenCapture.h"
#include "../platform/windows/WinGlobalHotkey.h"
#include "../platform/windows/WinWindowDetector.h"
#include "../platform/windows/WinAutoStart.h"
#include "../platform/windows/WinSingleInstance.h"
#endif

namespace qshot {

std::unique_ptr<IScreenCapture> PlatformFactory::createScreenCapture() {
#ifdef Q_OS_WIN
    return std::make_unique<WinScreenCapture>();
#else
    return nullptr; // Fallback for unsupported platforms
#endif
}

IGlobalHotkey* PlatformFactory::createGlobalHotkey(QObject* parent) {
#ifdef Q_OS_WIN
    return new WinGlobalHotkey(parent);
#else
    return nullptr;
#endif
}

std::unique_ptr<IWindowDetector> PlatformFactory::createWindowDetector() {
#ifdef Q_OS_WIN
    return std::make_unique<WinWindowDetector>();
#else
    return nullptr;
#endif
}

std::unique_ptr<IAutoStart> PlatformFactory::createAutoStart() {
#ifdef Q_OS_WIN
    return std::make_unique<WinAutoStart>();
#else
    return nullptr;
#endif
}

std::unique_ptr<ISingleInstance> PlatformFactory::createSingleInstance() {
#ifdef Q_OS_WIN
    return std::make_unique<WinSingleInstance>();
#else
    return nullptr;
#endif
}

} // namespace qshot
