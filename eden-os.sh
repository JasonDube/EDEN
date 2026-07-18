#!/usr/bin/env bash
# Build and boot the old EDEN OS — the 3D-desktop world (silo filesystem, hotbar,
# basement, terminal) that lives as a level inside terrain_editor. Loads the saved
# world from ~/.eden/os_level.eden. Runs as a normal window inside COSMIC.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
echo "Building terrain editor..."
cmake --build "$ROOT/build" --target terrain_editor -j >/dev/null
cd "$ROOT/build/examples/terrain_editor"   # run from here so it finds shaders/ and levels/
echo "Booting EDEN OS..."
# Mirror the console to a log so it can be inspected without a terminal (the
# desktop icon has none). Fresh each run.
exec ./terrain_editor --eden-os > "$ROOT/eden_os_console.log" 2>&1
