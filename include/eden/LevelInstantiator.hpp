#pragma once

namespace eden {

class Terrain;
class ChunkManager;
class VulkanContext;
struct LevelData;

// LevelInstantiator turns parsed LevelData (from LevelSerializer::load) into a
// LIVE world: terrain, and — as this module grows — scene objects, behaviors,
// components, and triggers. It is the shared "instantiation" layer that every
// game currently hand-rolls inside its own main.cpp. See docs/EDEN_FORMAT.md §4
// (Loader Contract) for the target surface.
//
// Extraction is incremental (memory: refactor_incrementally). Step 1 owns only
// terrain configuration + apply — a straight lift of terrain_editor loadLevel()'s
// terrain block, behavior preserved exactly.
class LevelInstantiator {
public:
    // Configure the terrain's world scale/bounds for this level, then apply its
    // height/splat data.
    //
    // Two cases, matching the original loadLevel() logic:
    //   1. Levels saved WITH terrain config (hasTerrainConfig) reconfigure the
    //      terrain to the authored tileSize/bounds/wrap so they reload at their
    //      real size instead of dropping height pixels into whatever terrain is
    //      currently active.
    //   2. Old archives (no terrain config) carry only chunk data; bounds are
    //      derived from the chunk coordinates so an archive loaded over the tiny
    //      worktable terrain doesn't come up as a postage stamp.
    //
    // A reconfigure requires a GPU idle + chunk-buffer release/reload, hence the
    // ChunkManager and VulkanContext dependencies.
    static void applyTerrain(const LevelData& data,
                             Terrain& terrain,
                             ChunkManager& chunkManager,
                             VulkanContext& context);
};

} // namespace eden
