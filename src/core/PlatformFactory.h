#pragma once

#include <memory>
#include <QObject>

#include "IScreenCapture.h"
#include "IGlobalHotkey.h"
#include "IWindowDetector.h"
#include "IAutoStart.h"

namespace qshot {

class PlatformFactory {
public:
    static std::unique_ptr<IScreenCapture> createScreenCapture();
    static IGlobalHotkey* createGlobalHotkey(QObject* parent = nullptr);
    static std::unique_ptr<IWindowDetector> createWindowDetector();
    static std::unique_ptr<IAutoStart> createAutoStart();
};

} // namespace qshot
