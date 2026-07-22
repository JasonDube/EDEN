#!/usr/bin/env bash
# Build and run Slag Legion (the sci-fi game). Sibling to play.sh (which runs
# Chronicles of the Iron Temple).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "Building Slag Legion..."
cmake --build "$ROOT/build" --target slag_legion -j >/dev/null

cd "$ROOT/build/examples/slag_legion"   # run from here so it finds assets/
echo "Launching Slag Legion..."
exec ./slag_legion
