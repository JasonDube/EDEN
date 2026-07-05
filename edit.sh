#!/usr/bin/env bash
# Build and run the EDEN terrain/level editor (author levels here, then play them
# with play.sh). Levels save to build/examples/terrain_editor/levels/.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
echo "Building terrain editor..."
cmake --build "$ROOT/build" --target terrain_editor -j >/dev/null
cd "$ROOT/build/examples/terrain_editor"   # run from here so it finds shaders/ and levels/
echo "Launching editor..."
# Mirror the editor's console to a log file so it can be inspected without a
# terminal (the desktop icon has none). Fresh each run.
exec ./terrain_editor > "$ROOT/editor_console.log" 2>&1
