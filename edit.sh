#!/usr/bin/env bash
# Build and run the EDEN terrain/level editor (author levels here, then play them
# with play.sh). Levels save to build/examples/terrain_editor/levels/.
ROOT="$(cd "$(dirname "$0")" && pwd)"

# When launched without a controlling terminal (e.g. the desktop icon), relaunch
# inside a terminal window so the console is visible LIVE — the desktop icon
# otherwise has nowhere to print. EDEN_IN_TERM guards against re-exec looping.
if [ ! -t 1 ] && [ -z "$EDEN_IN_TERM" ]; then
    export EDEN_IN_TERM=1
    if   command -v cosmic-term >/dev/null 2>&1; then exec cosmic-term -e "$0" "$@"
    elif command -v konsole     >/dev/null 2>&1; then exec konsole --hold -e "$0" "$@"
    elif command -v foot        >/dev/null 2>&1; then exec foot "$0" "$@"
    elif command -v xterm       >/dev/null 2>&1; then exec xterm -e "$0" "$@"
    fi
    # No terminal emulator found — fall through to file-log-only mode below.
fi

set -e
echo "Building terrain editor..."
cmake --build "$ROOT/build" --target terrain_editor -j
set +e

cd "$ROOT/build/examples/terrain_editor"   # run from here so it finds shaders/ and levels/
echo "Launching editor..."
# Show the console LIVE and mirror it to a log file (fresh each run) so it can
# also be inspected afterward.
./terrain_editor 2>&1 | tee "$ROOT/editor_console.log"
code=${PIPESTATUS[0]}

echo
echo "=== Editor exited (code $code). Full console saved to editor_console.log ==="
# Keep the window open so exit/crash output stays readable when launched from the
# icon (cosmic-term/foot close when the command exits).
if [ -n "$EDEN_IN_TERM" ]; then
    echo "Press Enter to close this window..."
    read -r _
fi
