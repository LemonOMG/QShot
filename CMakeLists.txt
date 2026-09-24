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

# Warnings on by default. Both review rounds reported defects that these flags would have
# surfaced on their own (-Wshadow caught a shadowed `currentSelection`, -Wunused caught an
# unused `drawButton` parameter), and the project's own static-check command has used them
# since round 1 -- so leaving them off the real build meant the *cheap* check was stricter
# than the build that actually ships.
#
# Guarded rather than unconditional because -Wextra is GCC/Clang syntax; MSVC would need
# /W4. Nothing here targets MSVC today, but a bare `-Wall` in a cross-platform list is the
# kind of thing that breaks a build years later with no obvious cause.
#
# A function rather than a variable because the probes below are separate targets and each
# one has to be told individually.
function(qshot_enable_warnings target)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow)
    endif()
endfunction()

# --- the application, as a library ----------------------------------------------------
#
# The probes in build-review/ each need a *different* subset of the application's
# translation units. That used to be expressed as 23 hand-written link lines in
# build-review/build_probes.sh, and hand-writing them produced the same two mistakes over
# and over: a missing moc_*.cpp, and a path into a hash-named autogen subdirectory that
# had stopped existing after the next CMake re-run. The script worked around the second by
# globbing for the moc file by class name, which is a symptom of the real problem.
#
# Naming the sources once, in a library, removes the failure mode instead of managing it:
# there is no link line to get wrong, AUTOMOC is CMake's job, and a new probe is one line.
#
# It is also faster. The script recompiled SnapOverlay.cpp and friends once per probe that
# needed them -- roughly eight times -- where a library compiles each translation unit once.
#
# The .qrc deliberately stays out of the library. A resource initialiser in a static
# library is only pulled in if something references it, and a probe whose entire purpose is
# to ask "is the icon in the resource table?" would then be asking about a table the linker
# had thrown away. It belongs to the executables that need it: qshot and probe_icon_resource.
add_library(qshot_core STATIC
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

target_include_directories(qshot_core PUBLIC src)
target_link_libraries(qshot_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
if (WIN32)
    # PUBLIC, so every probe inherits them. The platform layer needs DWM (the window
    # detector), GDI (the capturer's CreateCompatibleDC/SelectObject/DeleteObject) and
    # user32 (the global hotkey); a probe that linked the platform group without these
    # failed at link time with no clue as to which one was missing.
    target_link_libraries(qshot_core PUBLIC dwmapi gdi32 user32)
endif()
qshot_enable_warnings(qshot_core)

add_executable(qshot WIN32
    src/main.cpp
    # The tray/window icon. AUTORCC is on, so listing the .qrc is enough -- it is compiled
    # and linked in, and the icon ends up inside the executable rather than beside it.
    resources/resources.qrc
)
target_link_libraries(qshot PRIVATE qshot_core)
qshot_enable_warnings(qshot)

# --- verification ---------------------------------------------------------------------
#
# include(CTest) defines BUILD_TESTING (default ON) and enables `ctest`. Configure with
# -DBUILD_TESTING=OFF to build just the application.
include(CTest)

if (BUILD_TESTING)
    # Every target below runs with build-review/ as its working directory. Two reasons,
    # both inherited from the shell script this replaces: the render scaffolds write their
    # evidence PNGs next to the sources (that is where the documentation points people to
    # look), and two of them read a PNG that another one produced. Binaries go to the build
    # tree; only sources and evidence live in the repository.
    set(QSHOT_VERIFY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build-review)

    # The probes link Qt and the MinGW C++ runtime dynamically, so both bin directories have
    # to be on PATH before the loader runs main(). Git Bash reports a loader failure as a
    # bare `exit=127` with no output, which is indistinguishable from "that binary does not
    # exist" -- a trap documented in the project notes after it cost three debugging rounds.
    # Setting it here rather than asking the caller to export PATH is what makes a plain
    # `ctest` work: from a shell, from Qt Creator's test runner, or from CI, with no setup.
    #
    # Qt6_DIR is <prefix>/lib/cmake/Qt6, so three levels up is the Qt installation.
    get_filename_component(QSHOT_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    set(QSHOT_QT_BIN "${QSHOT_QT_PREFIX}/bin")
    get_filename_component(QSHOT_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if (WIN32 AND NOT EXISTS "${QSHOT_QT_BIN}/Qt6Core.dll")
        message(WARNING "No Qt6Core.dll in ${QSHOT_QT_BIN} -- the probes will not start. "
                        "Point -DQt6_DIR at the right Qt installation.")
    endif()
    # Kept in a variable because ENVIRONMENT_MODIFICATION is a *list* property, and
    # set_tests_properties takes `property value` pairs -- passing its two entries as two
    # separate arguments makes the second one a value with no property name, which fails
    # with the unhelpful "incorrect number of arguments".
    set(QSHOT_TEST_ENV_MODS
        "PATH=path_list_prepend:${QSHOT_QT_BIN}"
        "PATH=path_list_prepend:${QSHOT_MINGW_BIN}")

    # A target that asserts. These print "<n> checks, <m> failures" and return non-zero if
    # any assertion failed.
    function(qshot_add_probe name)
        add_executable(${name} build-review/${name}.cpp ${ARGN})
        target_link_libraries(${name} PRIVATE qshot_core)
        qshot_enable_warnings(${name})
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES
            WORKING_DIRECTORY ${QSHOT_VERIFY_DIR}
            ENVIRONMENT_MODIFICATION "${QSHOT_TEST_ENV_MODS}"
            # The summary line is the last thing a probe prints, immediately before it
            # returns, so requiring it proves the run reached the end of main with zero
            # failures. That is a *stronger* condition than the exit code, not a weaker
            # one: a probe whose output never reaches the pipe exits 0 and would otherwise
            # be counted as passing. Not hypothetical -- probe_quiet_save ran all 29 of its
            # assertions without printing a single byte, and the shell script this replaces
            # reported the run as green and 29 assertions short, because it only summed a
            # regex over the output and a missing summary line was simply not counted.
            # When PASS_REGULAR_EXPRESSION is set, CTest decides pass/fail on it alone.
            #
            # Deliberately unanchored. CTest applies this to the whole captured output in
            # one pass, not line by line, so "^...$" only matches a probe that printed
            # nothing else at all -- which fails every probe that prints its checks.
            PASS_REGULAR_EXPRESSION "[0-9]+ checks, 0 failures")
    endfunction()

    # A scaffold that renders something for a human to look at. It has no assertions and
    # no meaningful exit code, so registering it as a test would add a green light that
    # means nothing -- which is worse than no test at all. It is built and runnable, and
    # listed in the render_evidence / archaeology targets further down.
    #
    # EXCLUDE_FROM_ALL so that a plain build stays fast and does not produce a dozen
    # executables nobody asked for.
    function(qshot_add_tool name)
        add_executable(${name} EXCLUDE_FROM_ALL build-review/${name}.cpp ${ARGN})
        target_link_libraries(${name} PRIVATE qshot_core)
        qshot_enable_warnings(${name})
    endfunction()

    # Pure logic, no widgets.
    qshot_add_probe(probe_selection_geometry)
    qshot_add_probe(probe_history_store)
    qshot_add_probe(probe_quiet_save)
    qshot_add_probe(probe_toolbar_icons)
    qshot_add_probe(probe_annotation_types)
    qshot_add_probe(probe_mosaic_incremental)

    # Needs the compiled resource table, hence the explicit .qrc.
    qshot_add_probe(probe_icon_resource resources/resources.qrc)

    # Integration: real widgets, real events, real Win32 window styles.
    qshot_add_probe(probe_pin_geometry)
    qshot_add_probe(probe_toolbar_layout)
    qshot_add_probe(probe_history_menu)
    qshot_add_probe(probe_pin_wiring)
    qshot_add_probe(probe_multiscreen_coords)
    qshot_add_probe(probe_noactivate)
    qshot_add_probe(probe_magnifier_render)
    qshot_add_probe(probe_single_instance)

    # Dirty-region correctness for the overlay's mouse handlers. Must be launched, not
    # merely built: the assertions read the region out of a real paint event, which only
    # happens for a visible widget.
    qshot_add_probe(probe_partial_repaint)

    # Settings round trip plus a WinAutoStart round trip against the real HKCU Run key.
    # Labelled because it is the one test here with a side effect outside the build tree:
    # it snapshots the existing Run value, writes a test value, and restores the snapshot.
    # Exclude it with `ctest -LE side-effects` if you would rather not have a test run
    # touch the registry. It was written long ago and never wired into the suite, so
    # WinAutoStart -- a shipped feature -- had no automated coverage at all until now.
    qshot_add_probe(probe_settings_autostart)
    set_tests_properties(probe_settings_autostart PROPERTIES LABELS "side-effects")

    # Renders that do assert something (geometry, scale reached through real wheel
    # events), so they belong in the suite as well as in the evidence set.
    qshot_add_probe(render_pin)
    qshot_add_probe(render_pin_hover)

    # Two targets summarise in their own words rather than as "<n> checks, <m> failures",
    # and rewording them for uniformity would buy nothing. Naming the exception here keeps
    # the reason next to the rule.
    set_tests_properties(probe_magnifier_render PROPERTIES
        PASS_REGULAR_EXPRESSION "identical:")
    set_tests_properties(probe_settings_autostart PROPERTIES
        PASS_REGULAR_EXPRESSION "ALL PASS")

    # Renders for the eye only.
    qshot_add_tool(render_panels)
    qshot_add_tool(render_settings_dialog)
    qshot_add_tool(render_history_menu)
    qshot_add_tool(render_toolbar_tools)
    qshot_add_tool(render_text_editor)

    # Builds the renders and runs them, so the PNGs the documentation refers to can be
    # regenerated with one command instead of `build_probes.sh --run` plus a trip to the
    # right directory. render_pin and render_pin_hover are included even though `ctest -R
    # render` also runs them -- this target is about producing evidence, not about passing.
    add_custom_target(render_evidence
        COMMAND render_panels
        COMMAND render_pin
        COMMAND render_pin_hover
        COMMAND render_settings_dialog
        COMMAND render_history_menu
        COMMAND render_toolbar_tools
        COMMAND render_text_editor
        WORKING_DIRECTORY ${QSHOT_VERIFY_DIR}
        DEPENDS render_panels render_pin render_pin_hover
                render_settings_dialog render_history_menu
                render_toolbar_tools render_text_editor
        COMMENT "Rendering the evidence PNGs into build-review/")

    # Links nothing from the application on purpose. It runs *inside* dist/QShot/, where
    # Qt resolves its plugins and translations from the folder rather than from the Qt
    # installation, and it would be measuring the wrong thing if any of the app's own code
    # were in the binary. Not a CTest test: it has to be copied into a deployed folder
    # first, which is what tools/smoke_deploy.sh does.
    add_executable(probe_deploy EXCLUDE_FROM_ALL build-review/probe_deploy.cpp)
    # Qt6::Widgets is not optional here: the probe builds a real QApplication, because
    # finding the platform plugin is one of the things it exists to verify. The
    # prohibition above is about the *application's* code, not about Qt.
    target_link_libraries(probe_deploy PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    qshot_enable_warnings(probe_deploy)

    # --- historical probes ------------------------------------------------------------
    #
    # Written to answer one question at one moment -- "does QPixmap::fromImage() carry the
    # devicePixelRatio over?", "which window actually holds focus after showToolbar()?" --
    # and kept because they are the evidence behind the measured-semantics table in the
    # project notes. They assert nothing that the suite above does not, so they are not
    # tests; they are built on demand (target `archaeology`) so that the question can be
    # asked again rather than taken on trust.
    set(QSHOT_ARCHAEOLOGY
        dprtest
        probe_activate
        probe_cursor_capture
        probe_cursor_diag
        probe_cursor_pinned
        probe_cursor_roundtrip
        probe_dpr_paint
        probe_fix
        probe_focus2
        probe_focus3
        probe_focus4
        probe_focus5
        probe_focus_propagation
        probe_frame
        probe_fromimage
        probe_qss_border
        probe_subpanel_zorder
        probe_verify
        probe_verify2
        probe_zorder
    )
    foreach (name IN LISTS QSHOT_ARCHAEOLOGY)
        qshot_add_tool(${name})
    endforeach()
    add_custom_target(archaeology DEPENDS ${QSHOT_ARCHAEOLOGY}
        COMMENT "Building the historical one-off probes")
endif()
