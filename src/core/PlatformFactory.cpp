#include "PlatformFactory.h"

#ifdef Q_OS_WIN
#include "../platform/windows/WinScreenCapture.h"
#include "../platform/windows/WinGlobalHotkey.h"
#include "../platform/windows/WinWindowDetector.h"
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

} // namespace qshot
