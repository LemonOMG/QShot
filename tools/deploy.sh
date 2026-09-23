#!/usr/bin/env bash
#
# Assembles a self-contained folder for QShot: qshot.exe plus every Qt DLL, platform plugin
# and translation it needs, so the folder runs on a Windows machine with no Qt installed.
#
#   bash tools/deploy.sh              # deploy an existing Release build
#   bash tools/deploy.sh --build      # configure + build Release first, then deploy
#   bash tools/deploy.sh --clean      # wipe the staging folder before deploying
#   bash tools/deploy.sh --verify-only # re-run only the closure check on the staged folder
#
# Environment overrides: QT_DIR, BUILD_DIR, STAGE_DIR.
#
# Why a script instead of `windeployqt` by hand: the step that actually matters is the check
# at the end. windeployqt exits 0 even when a dependency is missing -- the classic failure is
# a folder that looks complete but dies with "could not find or load the Qt platform plugin
# windows" on the target machine, because the platforms/ subdirectory was not copied. The
# verification below is what turns "windeployqt ran" into "this folder is self-contained".
#
# Note on MinGW paths: this script runs under Git Bash, where the shell and the native tools
# disagree about what a path looks like. Anything handed to cmake/windeployqt/objdump goes
# through `cygpath -m` (C:/...), anything executed goes through `cygpath -u` (/d/...).
# A `C:/...` string cannot be exec'd by bash, and `/d/...` is rejected by MinGW g++.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_M="$(cygpath -m "$ROOT")"

QT_DIR="${QT_DIR:-D:/Qt/6.11.2/mingw_64}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/release}"
STAGE_DIR="${STAGE_DIR:-$ROOT/dist/QShot}"

MINGW_BIN="${MINGW_BIN:-D:/Qt/Tools/mingw1310_64/bin}"
NINJA="${NINJA:-D:/Qt/Tools/Ninja/ninja.exe}"

DO_BUILD=0
DO_CLEAN=0
VERIFY_ONLY=0

for arg in "$@"; do
    case "$arg" in
        --build)       DO_BUILD=1 ;;
        --clean)       DO_CLEAN=1 ;;
        --verify-only) VERIFY_ONLY=1 ;;
        -h|--help)     sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "deploy: unknown option '$arg' (try --help)" >&2; exit 2 ;;
    esac
done

say()  { printf '\n== %s\n' "$*"; }
ok()   { printf '  ok   %s\n' "$*"; }
warn() { printf '  WARN %s\n' "$*"; }
die()  { printf '  FAIL %s\n' "$*" >&2; exit 1; }

# --- 1. build ------------------------------------------------------------------------
if [ "$DO_BUILD" -eq 1 ]; then
    say "configuring and building Release"
    # The compiler is passed as an absolute path because MinGW is not on PATH by default here,
    # and CMake refuses to guess a toolchain it cannot find. Only CXX is set: the project
    # declares `LANGUAGES CXX`, so a CMAKE_C_COMPILER value is silently ignored and CMake
    # prints a "manually-specified variables were not used" warning for it.
    cmake -S "$ROOT_M" -B "$(cygpath -m "$BUILD_DIR")" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_DIR" \
        -DCMAKE_CXX_COMPILER="$MINGW_BIN/g++.exe" \
        -DCMAKE_MAKE_PROGRAM="$NINJA" \
        2>&1 | tail -5
    cmake --build "$(cygpath -m "$BUILD_DIR")" 2>&1 | tail -5
fi

EXE="$BUILD_DIR/qshot.exe"

if [ "$VERIFY_ONLY" -eq 0 ]; then
    [ -f "$EXE" ] || die "no $EXE -- build a Release configuration first (--build)"

    # --- 2. stage ---------------------------------------------------------------------
    if [ "$DO_CLEAN" -eq 1 ]; then
        # Guarded: this is the only destructive operation in the script, and it must never be
        # able to walk out of the project's own output directory.
        case "$STAGE_DIR" in
            "$ROOT"/dist/*) ;;
            *) die "refusing to --clean '$STAGE_DIR': it is not under '$ROOT/dist/'" ;;
        esac
        say "cleaning $STAGE_DIR"
        rm -rf "$STAGE_DIR"
    fi

    say "staging into $STAGE_DIR"
    mkdir -p "$STAGE_DIR"
    cp -f "$EXE" "$STAGE_DIR/qshot.exe"
    ok "$(basename "$EXE") ($(stat -c %s "$STAGE_DIR/qshot.exe") bytes)"

    # --- 3. windeployqt ---------------------------------------------------------------
    say "running windeployqt"
    WINEDEPLOY="$QT_DIR/bin/windeployqt.exe"
    [ -f "$(cygpath -u "$WINEDEPLOY")" ] || die "windeployqt not found at $WINEDEPLOY"

    # --release                the exe name carries no debug marker, so it cannot be inferred
    # --compiler-runtime       libgcc/libstdc++/libwinpthread; a MinGW build dies without them
    # --no-translations        see the explicit copy below -- windeployqt gets this wrong here
    # --no-opengl-sw           Qt Widgets uses the raster paint engine; opengl32sw.dll is a
    #                          ~20MB software rasteriser for QOpenGL, which this app never uses
    # --no-system-d3d-compiler the same, for the ANGLE/D3D path
    # --no-quick-import        no QML anywhere in the project
    "$(cygpath -u "$WINEDEPLOY")" \
        --release \
        --compiler-runtime \
        --no-translations \
        --no-opengl-sw \
        --no-system-d3d-compiler \
        --no-quick-import \
        --no-patchqt \
        --dir "$(cygpath -m "$STAGE_DIR")" \
        "$(cygpath -m "$STAGE_DIR/qshot.exe")"
    ok "windeployqt finished"

    # --- 3b. the Qt translation -------------------------------------------------------
    # Copied by hand because `--translations zh_CN` does not produce the file the app loads.
    # Measured: it deploys only `qt_zh_CN.qm`, a 99-byte meta-catalogue that merely lists other
    # catalogues, and skips `qtbase_zh_CN.qm` (147KB), which is where the actual strings are.
    # Strings.cpp loads "qtbase_zh_CN" by name, so with windeployqt's output alone the app
    # starts, looks fine, and quietly falls back to English for every Qt-supplied string
    # (QMessageBox buttons, the line-edit context menu). Silent, and only visible in Chinese.
    say "copying the Qt translation catalogue"
    mkdir -p "$STAGE_DIR/translations"
    cp -f "$(cygpath -u "$QT_DIR")/translations/qtbase_zh_CN.qm" "$STAGE_DIR/translations/"
    ok "qtbase_zh_CN.qm ($(stat -c %s "$STAGE_DIR/translations/qtbase_zh_CN.qm") bytes)"

    # --- 3c. prune --------------------------------------------------------------------
    # windeployqt deploys for every module its *plugins* pull in, not just the ones the app
    # links. Three groups are dead weight for a screenshot tool:
    #
    #   Qt6Network.dll + networkinformation/ + tls/   nothing in the project opens a socket
    #   generic/qtuiotouchplugin.dll                  a desktop-only application
    #   Qt6Svg.dll + iconengines/qsvgicon.dll +
    #   imageformats/qsvg.dll, imageformats/qgif.dll  see below
    #
    # The tls/ backends are not merely wasted space, they are a TLS stack shipped in a tool
    # that never performs I/O over a network. Removing them is only safe because the closure
    # check below re-resolves every import afterwards: a prune that broke something shows up
    # as an unresolved dependency here, not as a crash on a user's machine.
    #
    # The SVG group was kept for a while on the theory that the toolbar would move to an SVG
    # icon set. That did not happen -- src/overlay/ToolbarIcons.cpp draws the eight glyphs in
    # code -- so the reason expired and what was left was a 632KB XML/SVG parser shipped in a
    # screenshot tool, plus 115KB of plugin. Removed for that reason (surface, not size).
    # `qico` and `qjpeg` are the two that matter and both stay: the tray icon is an embedded
    # .ico that QIcon reads through QImageReader, and .jpg is one of the two formats
    # ImageExport offers. PNG needs no plugin at all.
    #
    # Plugins are loaded by name at runtime, so unlike a DLL they leave no import for the
    # closure check to resolve -- a missing one is not a load failure, just a feature that
    # stops working. That blind spot is why build-review/probe_deploy.cpp section [4] asserts
    # the loaded format set, in both directions: it fails if `ico` or `jpeg` goes missing, and
    # it also fails if `svg` or `gif` comes back, which is what makes this list a decision
    # rather than something that can silently drift.
    say "pruning what a screenshot tool cannot use"
    rm -f "$STAGE_DIR/Qt6Network.dll" "$STAGE_DIR/Qt6Svg.dll"
    rm -rf "$STAGE_DIR/networkinformation" "$STAGE_DIR/tls" "$STAGE_DIR/generic" \
           "$STAGE_DIR/iconengines"
    rm -f "$STAGE_DIR/imageformats/qsvg.dll" "$STAGE_DIR/imageformats/qgif.dll"
    ok "dropped Qt6Network, Qt6Svg, tls/, generic/, iconengines/, qsvg, qgif"
fi

# --- 4. verify ------------------------------------------------------------------------
# Resolve every DLL the binary and its plugins import, and require each one to be either in
# the staging folder or a Windows system library. This is the check that makes the folder
# trustworthy; see the note at the top of the file.
say "verifying the staged folder"

OBJDUMP="$MINGW_BIN/objdump.exe"
[ -f "$(cygpath -u "$OBJDUMP")" ] || die "objdump not found at $OBJDUMP"
SYS32="$(cygpath -u "C:/Windows/System32")"

failures=0
checked=0
dlls=0

# The platform plugin is the one dependency whose absence produces a confusing runtime error
# rather than a loader failure, and it lives in a subdirectory, so it is checked by name.
[ -f "$STAGE_DIR/platforms/qwindows.dll" ] \
    || { warn "platforms/qwindows.dll is missing -- Qt will abort at startup"; failures=$((failures + 1)); }

# Every PE file in the folder, at any depth: the exe, the Qt libraries, and the plugins under
# platforms/ styles/ imageformats/.
while IFS= read -r file; do
    dlls=$((dlls + 1))
    # `objdump -p` prints one "DLL Name: x.dll" line per import descriptor.
    while IFS= read -r name; do
        [ -n "$name" ] || continue
        # API sets are virtual DLLs. The loader resolves api-ms-win-*/ext-ms-win-* through the
        # apiset schema rather than by finding a file, so they are never present in System32
        # and are always available on a supported Windows. Testing for the file reports three
        # false positives (Qt6Core -> api-ms-win-core-synch-l1-2-0, qwindows -> the two winrt
        # ones) and would have made this check unusable.
        case "$name" in
            api-ms-win-*|ext-ms-win-*) continue ;;
        esac
        checked=$((checked + 1))
        if [ -f "$STAGE_DIR/$name" ]; then
            continue
        fi
        # Windows system libraries are expected to be absent from the folder; an allowlist of
        # names would rot, so the presence of the file in System32 is the test.
        if [ -f "$SYS32/$name" ]; then
            continue
        fi
        warn "$(basename "$file") imports $name, which is neither staged nor a system library"
        failures=$((failures + 1))
    done < <("$(cygpath -u "$OBJDUMP")" -p "$(cygpath -m "$file")" 2>/dev/null \
             | sed -n 's/^\s*DLL Name: \(.*\)$/\1/p' | tr -d '\r')
done < <(find "$STAGE_DIR" -type f \( -name '*.exe' -o -name '*.dll' \) | sort)

# The Qt libraries the app links against directly. A missing one of these would have been
# caught above, but naming them makes a failure readable instead of a wall of "not found".
for lib in Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll; do
    [ -f "$STAGE_DIR/$lib" ] || { warn "$lib is missing"; failures=$((failures + 1)); }
done

# A Debug executable linked against Release Qt (or the reverse) fails at load with a version
# mismatch, so the staged set has to be uniformly release: Qt6Core.dll, never Qt6Cored.dll.
if compgen -G "$STAGE_DIR/Qt6*d.dll" > /dev/null; then
    warn "debug Qt libraries are staged next to a release build"
    failures=$((failures + 1))
fi

# A real launch is deliberately not part of this check. Starting the app registers a global
# hotkey, puts a tray icon on the user's desktop and writes to HKCU, and this script may be
# run in the middle of their work. The three failure modes that a launch would catch -- a
# missing platform plugin, a missing runtime DLL, a debug/release mismatch -- are all covered
# statically above.

# The icon is compiled into the executable, so no .ico belongs beside it; and the app is
# bilingual with English as Qt's source language, so qtbase_zh_CN is the only catalogue it
# ever loads. Exactly one .qm is the expected state.
qm_count=$(find "$STAGE_DIR/translations" -name '*.qm' 2>/dev/null | wc -l | tr -d ' ')
printf '  ..   %d PE files, %d imports resolved, %d translation file(s)\n' \
       "$dlls" "$checked" "$qm_count"

if [ "$qm_count" -ne 1 ]; then
    warn "expected exactly 1 translation file, found $qm_count"
    failures=$((failures + 1))
fi

total=$(du -sm "$STAGE_DIR" | cut -f1)
printf '  ..   staged size: %s MB\n' "$total"

if [ "$failures" -gt 0 ]; then
    die "$failures problem(s) in $STAGE_DIR"
fi

ok "the staged folder is self-contained"
printf '\n%s is ready. Run it, or feed it to tools/installer/qshot.iss.\n' "$STAGE_DIR"
