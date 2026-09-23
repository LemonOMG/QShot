#!/usr/bin/env bash
#
# Runs build-review/probe_deploy.cpp from *inside* the deployed folder.
#
#   bash tools/smoke_deploy.sh           # the side-effect-free probe
#   bash tools/smoke_deploy.sh --app     # also launch the real application briefly
#
# Requires the folder to exist first:  bash tools/deploy.sh --build
#
# Why the probe has to be copied in rather than run from the temp directory it is built into:
# Qt resolves its plugin and translation directories from QLibraryInfo's prefix, which for a
# deployed application is derived from the location of Qt6Core.dll. Run the same binary from
# anywhere else and Qt finds the *installed* Qt instead, so the probe would report on the Qt
# installation and pass regardless of what is in the folder. The copy is deleted afterwards.
#
# The child runs with Qt's and MinGW's bin directories removed from PATH, on purpose. If the
# folder is genuinely self-contained that changes nothing; if it is not, the failure is loud
# here instead of silent on a user's machine.
#
# --app is off by default because launching the real application is not free of consequences:
# it registers the global hotkey, puts an icon in the system tray, and writes to HKCU. It is
# worth doing once before shipping, but not every time someone re-runs the deploy.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE_DIR="${STAGE_DIR:-$ROOT/dist/QShot}"
PROBE_OUT="${PROBE_OUT:-$(cygpath -m "$HOME")/AppData/Local/Temp/qshot-probe}"
SMOKE_SECONDS="${SMOKE_SECONDS:-6}"

RUN_APP=0
for arg in "$@"; do
    case "$arg" in
        --app) RUN_APP=1 ;;
        *) echo "smoke_deploy: unknown option '$arg'" >&2; exit 2 ;;
    esac
done

PROBE="$PROBE_OUT/probe_deploy.exe"
DEPLOYED="$STAGE_DIR/probe_deploy.exe"

if [ ! -f "$STAGE_DIR/qshot.exe" ]; then
    echo "smoke_deploy: $STAGE_DIR/qshot.exe is not there -- run tools/deploy.sh first" >&2
    exit 2
fi

echo "== building probe_deploy"
bash "$ROOT/build-review/build_probes.sh" probe_deploy

[ -f "$(cygpath -u "$PROBE")" ] || { echo "smoke_deploy: $PROBE was not produced" >&2; exit 1; }

# Removed on any exit, so an interrupted run cannot leave a stray executable in the folder
# that a later deploy would ship.
cleanup() { rm -f "$DEPLOYED"; }
trap cleanup EXIT

echo
echo "== running it from $STAGE_DIR"
cp -f "$(cygpath -u "$PROBE")" "$(cygpath -u "$DEPLOYED")"

# Windows searches the executable's own directory before PATH, so the folder's DLLs win. PATH
# is reduced to the Windows system directories anyway, to remove the Qt installation as a
# possible source: whatever this process loads, it loads from the folder.
set +e
PATH="/c/Windows/System32:/c/Windows" "$(cygpath -u "$DEPLOYED")"
status=$?
set -e

echo
if [ "$status" -eq 0 ]; then
    echo "the deployed folder starts up and finds its plugins and translations"
else
    echo "the deployed folder failed to run cleanly (exit=$status)" >&2
    exit "$status"
fi

if [ "$RUN_APP" -eq 1 ]; then
    echo
    echo "== launching the real application for ${SMOKE_SECONDS}s"
    echo "   (this registers the global hotkey and adds a tray icon)"
    # /usr/bin/timeout by absolute path, not `timeout`. With PATH reduced to the Windows
    # directories the name resolves to C:\Windows\System32\timeout.exe, which takes /t and
    # rejects a bare number with "invalid syntax" -- a confusing failure that has nothing to
    # do with the deployment being tested.
    set +e
    PATH="/c/Windows/System32:/c/Windows" /usr/bin/timeout "$SMOKE_SECONDS" \
        "$(cygpath -u "$STAGE_DIR/qshot.exe")"
    app_status=$?
    set -e

    # 124 is timeout's own code for "still running when the limit expired", which is exactly
    # the success condition: a tray application has no reason to exit on its own.
    if [ "$app_status" -eq 124 ]; then
        echo "   it started and stayed alive (killed by the timeout, as expected)"
    else
        echo "   it exited early with $app_status -- that is a failure, not a timeout" >&2
        exit 1
    fi
fi

exit 0
