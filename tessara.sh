#!/usr/bin/env bash
# Build and run the TESSARA:AXIOM surface-walker slice. Sibling to slag.sh.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "Building TESSARA:AXIOM..."
cmake --build "$ROOT/build" --target tessara_axiom -j >/dev/null

cd "$ROOT/build/examples/tessara_axiom"   # run from here so it finds shaders/
echo "Launching TESSARA:AXIOM..."
exec ./tessara_axiom
