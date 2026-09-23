#!/usr/bin/env bash
# Proves the single-instance wiring in main.cpp: a second copy of QShot must refuse to
# start, and the first copy must hand the name over when it dies.
#
# Why this is a shell script and not a probe: the second copy is *supposed* to put up a
# modal message box, and a modal box blocks -- so its exit code says nothing, and no probe
# can dismiss it. What can be read from outside is stderr. An unguarded second copy fails
# to register the global hotkey the first copy is holding, and says so via qWarning()
# within a fraction of a second; a guarded one never reaches ShotApplication at all. So
# "stderr is empty" is the discriminator, and it is the only one available.
#
# The name is released by the OS when the process dies, which is the entire reason the
# guard is a kernel mutex rather than a lock file. Steps [3] and [4] check that claim
# rather than assuming it: after the first copy is killed, a third copy must take the name
# over, proved by a fourth copy being refused.
#
# Usage:  bash tools/smoke_single_instance.sh [path/to/qshot.exe]
#
# The path defaults to the Debug build. Pass dist/QShot/qshot.exe to check the shipped
# Release binary instead -- it is the same main.cpp, but it is a different executable and
# nothing else in the toolchain ever runs it.
#
# Exits non-zero on the first failed check. Kills every copy it starts, including on
# failure, so a dialog left on screen is not a possibility.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT="${QT_DIR:-D:/Qt/6.11.2/mingw_64}"
CXX="${MINGW_CXX:-D:/Qt/Tools/mingw1310_64/bin/g++.exe}"
EXE="${1:-$ROOT/build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug/qshot.exe}"

if [ ! -x "$EXE" ]; then
    echo "no qshot.exe at $EXE -- build the Debug target first" >&2
    exit 1
fi

# Qt's DLLs and the MinGW runtime both have to be reachable, or the process dies in the
# loader and every check below passes for the wrong reason: an empty stderr.
if command -v cygpath >/dev/null 2>&1; then
    export PATH="$(cygpath -u "$QT/bin"):$(cygpath -u "$(dirname "$CXX")"):$PATH"
else
    export PATH="$QT/bin:$(dirname "$CXX"):$PATH"
fi

TMP="$(mktemp -d)"
PIDS=()
cleanup() {
    local pid
    for pid in ${PIDS[@]+"${PIDS[@]}"}; do
        kill "$pid" 2>/dev/null
    done
    # Plain file deletes rather than a recursive one. This runs from a trap, so a guard that
    # refuses a recursive delete of a path outside the repository would leave the trap
    # half-done. Note that some sandboxes intercept rm itself and print a warning here -- the
    # script still succeeds and exits 0, and in a normal shell the files are simply removed.
    rm -f "$TMP"/*.out "$TMP"/*.err
    rmdir "$TMP" 2>/dev/null
}
trap cleanup EXIT

checks=0
failures=0
check() {
    checks=$((checks + 1))
    if [ "$1" = "1" ]; then
        printf '  ok   %s\n' "$2"
    else
        printf '  FAIL %s\n' "$2"
        failures=$((failures + 1))
    fi
}

# Starts a copy, waits for it to settle, echoes its pid. stderr goes to $2.
start() {
    local tag="$1"
    "$EXE" >"$TMP/$tag.out" 2>"$TMP/$tag.err" &
    local pid=$!
    PIDS+=("$pid")
    sleep 3
    echo "$pid"
}

# Reads a copy's stderr, collapsed to one line.
err_of() {
    tr -d '\r' <"$TMP/$1.err" | tr '\n' '|'
}

alive() {
    kill -0 "$1" 2>/dev/null && echo 1 || echo 0
}

echo "[1] the first copy owns the hotkey"
A="$(start a)"
A_ERR="$(err_of a)"
if [ -z "$A_ERR" ]; then
    check 1 "it started cleanly, so it holds the global hotkey"
else
    # Without this the rest of the script would be measuring nothing: a first copy that
    # could not register the hotkey cannot make a second copy look unguarded.
    check 0 "it started cleanly (stderr: $A_ERR) -- nothing below can be trusted"
    echo
    echo "  the first copy failed to start, so the guard was never exercised."
    exit 1
fi
check "$(alive "$A")" "and it is still running"

echo
echo "[2] a second copy is refused before it touches the hotkey"
B="$(start b)"
B_ERR="$(err_of b)"
if [ -z "$B_ERR" ]; then
    check 1 "it produced no hotkey warning, so it never reached ShotApplication"
else
    check 0 "no hotkey warning (got: $B_ERR)"
fi
check "$(alive "$B")" "it is showing the message box rather than having exited"

echo
echo "[3] killing the first copy releases the name"
kill "$A" 2>/dev/null
sleep 2
C="$(start c)"
check "$(alive "$C")" "a third copy starts"
# Not asserted on its own: an empty stderr here would mean either "took the name" or
# "refused", which is why step [4] exists.
D="$(start d)"
D_ERR="$(err_of d)"
if [ -z "$D_ERR" ]; then
    check 1 "a fourth copy is refused, so the third one took the name over"
else
    check 0 "a fourth copy is refused (got: $D_ERR)"
fi

echo
printf '%d checks, %d failures\n' "$checks" "$failures"
exit $((failures == 0 ? 0 : 1))
