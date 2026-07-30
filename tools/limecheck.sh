#!/bin/sh
# Does LIME accept this .lime? Run before handing any generated prefab over.
#
# WHY THIS EXISTS. The first helm was verified by loading it through EDEN's
# LimeLoader, which fan-triangulates the `f` lines and never reads a half-edge.
# It passed cleanly and then crashed LIME, which IS a half-edge editor: seventy-
# eight "Face N has invalid halfEdge 0" and a failed allocation.
#
# One format, two readers, and only one of them exercised. So this builds against
# LIME's OWN EditableMesh and calls the same triangulate() that failed. It is the
# only verdict that counts for a file meant to be opened in the modeller.
#
#   ./tools/limecheck.sh assets/models/prefabs/helm.lime
set -e
LIME="$HOME/Desktop/LIME"
OUT="${TMPDIR:-/tmp}/limecheck"
g++ -std=c++17 -I "$LIME/src" -I "$LIME/engine" -I "$LIME/engine/Renderer" \
    -I "$LIME/engine/include" \
    "$(dirname "$0")/limecheck.cpp" "$LIME/src/EditableMesh.cpp" -o "$OUT"
exec "$OUT" "$@"
