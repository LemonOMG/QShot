cmake_minimum_required(VERSION 3.21)
project(qshot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

add_executable(qshot WIN32
    src/main.cpp
    src/app/ShotApplication.h
    src/app/ShotApplication.cpp
    src/core/IScreenCapture.h
    src/core/IGlobalHotkey.h
    src/core/IWindowDetector.h
    src/platform/windows/WinScreenCapture.h
    src/platform/windows/WinScreenCapture.cpp
    src/platform/windows/WinGlobalHotkey.h
    src/platform/windows/WinGlobalHotkey.cpp
    src/platform/windows/WinWindowDetector.h
    src/platform/windows/WinWindowDetector.cpp
    src/overlay/SnapOverlay.h
    src/overlay/SnapOverlay.cpp
    src/overlay/ToolbarWidget.h
    src/overlay/ToolbarWidget.cpp
    
    src/annotation/AnnotationType.h
    src/annotation/Annotation.h
    src/annotation/AnnotationLayer.h
    src/annotation/AnnotationLayer.cpp
)

target_link_libraries(qshot PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets dwmapi)
target_include_directories(qshot PRIVATE src)
