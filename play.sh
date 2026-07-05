#!/usr/bin/env bash
# Build and run the EDEN Tabletop.
#
#   ./play.sh                 -> Orlen's shop scene (orlen_shop_v4; has the door to trade_street_v4)
#   ./play.sh <level_name>    -> any level in the terrain_editor levels folder
#   ./play.sh --combat        -> the combat sandbox (no level, sample party)
#
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/build/examples/tabletop"
LEVELS="$ROOT/build/examples/terrain_editor/levels"

echo "Building tabletop..."
cmake --build "$ROOT/build" --target tabletop -j >/dev/null

cd "$BIN"   # run from here so it finds shaders/ and assets/

if [ "$1" = "--combat" ]; then
    echo "Launching combat sandbox..."
    exec ./tabletop
fi

LEVEL="${1:-orlen_shop_v4}"
LEVELFILE="$LEVELS/${LEVEL}.edenbin"
if [ ! -f "$LEVELFILE" ]; then
    echo "Level '$LEVEL' not found. Available levels:"
    ls "$LEVELS"/*.edenbin 2>/dev/null | xargs -n1 basename | sed 's/\.edenbin$//'
    exit 1
fi

echo "Launching level: $LEVEL"
exec env TABLETOP_LEVEL="$LEVELFILE" ./tabletop
