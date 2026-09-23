#!/usr/bin/env bash
# Builds and runs the icon generator, writing resources/qshot.ico and resources/qshot_256.png.
#
# Not part of the CMake build on purpose: the icon is a build *input*, so it should be
# regenerated when someone changes the design, not on every compile. Run this after editing
# tools/make_icon.cpp and commit the result.
#
# Usage:  bash tools/make_icon.sh

set -eu

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT="${QT_DIR:-D:/Qt/6.11.2/mingw_64}"
CXX="${MINGW_CXX:-D:/Qt/Tools/mingw1310_64/bin/g++.exe}"

# MinGW's g++ does not understand the /d/... mount paths Git Bash hands out, so anything
# passed to it has to be in the mixed C:/... form. Bash needs the opposite form to *run*
# what comes out, hence the two variables.
if command -v cygpath >/dev/null 2>&1; then
    ROOT_M="$(cygpath -m "$ROOT")"
else
    ROOT_M="$ROOT"
fi

OUT="${PROBE_OUT:-}"
if [ -z "$OUT" ]; then
    OUT="$(cygpath -m "$HOME" 2>/dev/null || echo "$HOME")/AppData/Local/Temp/qshot-probe"
fi
mkdir -p "$OUT"

# QSHOT_RESOURCE_DIR is baked in rather than derived at runtime so the generator cannot
# silently write the icon somewhere unexpected depending on the working directory.
"$CXX" -std=c++17 -Wall -Wextra -Wshadow -Wunused \
    -DQSHOT_RESOURCE_DIR="\"$ROOT_M/resources\"" \
    -I"$QT/include" -I"$QT/include/QtCore" -I"$QT/include/QtGui" \
    -o "$OUT/make_icon.exe" "$ROOT_M/tools/make_icon.cpp" \
    -L"$QT/lib" -lQt6Gui -lQt6Core

# The DLL directories have to be on PATH, or the loader fails before main() and the shell
# reports it as a bare `exit=127` with no output at all.
if command -v cygpath >/dev/null 2>&1; then
    export PATH="$(cygpath -u "$QT/bin"):$(cygpath -u "$(dirname "$CXX")"):$PATH"
else
    export PATH="$QT/bin:$(dirname "$CXX"):$PATH"
fi

"$(cygpath -u "$OUT" 2>/dev/null || echo "$OUT")/make_icon.exe" "$ROOT_M/build-review/icon_preview.png"
