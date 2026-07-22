#!/usr/bin/env bash
# Build and run Project Simulacra. Sibling to slag.sh (Slag Legion) and
# play.sh (Chronicles of the Iron Temple).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "Building Project Simulacra..."
cmake --build "$ROOT/build" --target project_simulacra -j >/dev/null

cd "$ROOT/build/examples/project_simulacra"   # run from here so it finds assets/
echo "Launching Project Simulacra..."
exec ./project_simulacra
