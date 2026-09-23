cmake_minimum_required(VERSION 3.21)
project(qshot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

set(PLATFORM_SOURCES "")
if (WIN32)
    list(APPEND PLATFORM_SOURCES
        src/platform/windows/WinScreenCapture.h
        src/platform/windows/WinScreenCapture.cpp
        src/platform/windows/WinGlobalHotkey.h
        src/platform/windows/WinGlobalHotkey.cpp
        src/platform/windows/WinWindowDetector.h
        src/platform/windows/WinWindowDetector.cpp
        src/platform/windows/WinAutoStart.h
        src/platform/windows/WinAutoStart.cpp
        src/platform/windows/WinSingleInstance.h
        src/platform/windows/WinSingleInstance.cpp
    )
endif()

add_executable(qshot WIN32
    src/main.cpp
    # The tray/window icon. AUTORCC is on, so listing the .qrc is enough -- it is compiled
    # and linked in, and the icon ends up inside the executable rather than beside it.
    resources/resources.qrc
    src/app/ShotApplication.h
    src/app/ShotApplication.cpp
    src/core/IScreenCapture.h
    src/core/IGlobalHotkey.h
    src/core/IWindowDetector.h
    src/core/IAutoStart.h
    src/core/ISingleInstance.h
    src/core/Settings.h
    src/core/Settings.cpp
    src/core/Strings.h
    src/core/Strings.cpp
    src/core/ImageExport.h
    src/core/ImageExport.cpp
    src/core/HistoryStore.h
    src/core/HistoryStore.cpp
    src/core/PlatformFactory.h
    src/core/PlatformFactory.cpp
    ${PLATFORM_SOURCES}
    src/pin/PinWindow.h
    src/pin/PinWindow.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/HistoryMenu.h
    src/ui/HistoryMenu.cpp
    src/overlay/SelectionGeometry.h
    src/overlay/SelectionGeometry.cpp
    src/overlay/FloatingPanel.h
    src/overlay/FloatingPanel.cpp
    src/overlay/SnapOverlay.h
    src/overlay/SnapOverlay.cpp
    src/overlay/ToolbarWidget.h
    src/overlay/ToolbarWidget.cpp
    src/overlay/ToolbarIcons.h
    src/overlay/ToolbarIcons.cpp
    src/overlay/TextInputWidget.h
    src/overlay/TextInputWidget.cpp
    
    src/annotation/AnnotationType.h
    src/annotation/ToolSettings.h
    src/annotation/Annotation.h
    src/annotation/AnnotationLayer.h
    src/annotation/AnnotationLayer.cpp
)

target_link_libraries(qshot PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
if (WIN32)
    target_link_libraries(qshot PRIVATE dwmapi)
endif()
target_include_directories(qshot PRIVATE src)

# Warnings on by default. Both review rounds reported defects that these flags would have
# surfaced on their own (-Wshadow caught a shadowed `currentSelection`, -Wunused caught an
# unused `drawButton` parameter), and the project's own static-check command has used them
# since round 1 -- so leaving them off the real build meant the *cheap* check was stricter
# than the build that actually ships.
#
# Guarded rather than unconditional because -Wextra is GCC/Clang syntax; MSVC would need
# /W4. Nothing here targets MSVC today, but a bare `-Wall` in a cross-platform list is the
# kind of thing that breaks a build years later with no obvious cause.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qshot PRIVATE -Wall -Wextra -Wshadow)
endif()
