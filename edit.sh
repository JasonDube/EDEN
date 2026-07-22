#!/usr/bin/env bash
# Build and run the EDEN terrain/level editor (author levels here, then play them
# with play.sh). Levels save to build/examples/terrain_editor/levels/.
#
# The console now lives INSIDE the editor (Window > Console (log)), so the desktop
# icon launches WITHOUT a separate terminal window. All output still mirrors to
# editor_console.log for crash post-mortems (symbolize a backtrace with
# addr2line -f -C -e ./terrain_editor <offset>).
ROOT="$(cd "$(dirname "$0")" && pwd)"
LOG="$ROOT/editor_console.log"

# Build (best-effort). Fresh log each launch. If the build fails, we still try to
# run the last good binary rather than leaving the icon doing nothing.
echo "Building TED (TerrainEDitor)..." > "$LOG"
cmake --build "$ROOT/build" --target terrain_editor -j >> "$LOG" 2>&1
echo "Launching TED..." >> "$LOG"

cd "$ROOT/build/examples/terrain_editor"   # run from here so it finds shaders/ and levels/

# When run from a real terminal (a dev doing ./edit.sh), also show output live.
# From the icon (no controlling terminal), it goes to the log + the in-app Console
# — no extra terminal window.
if [ -t 1 ]; then
    ./terrain_editor 2>&1 | tee -a "$LOG"
else
    ./terrain_editor >> "$LOG" 2>&1
fi
