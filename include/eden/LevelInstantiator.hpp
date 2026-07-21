#pragma once

#include <memory>
#include <vector>

namespace eden {

class Terrain;
class ChunkManager;
class VulkanContext;
class ModelRenderer;
class SkinnedModelRenderer;
class PhysicsWorld;
class SceneObject;
struct LevelData;

// Dependencies the object-spawning path needs from a host game: the renderers
// that create GPU models, the physics world to register colliders with (may be
// null), and the scene-object list to append spawned objects to. Bundled into
// one struct so the instantiator API stays stable as the loop grows, and so a
// host wires it up once instead of threading four args through every call.
struct SpawnContext {
    ModelRenderer& modelRenderer;
    SkinnedModelRenderer& skinnedRenderer;
    PhysicsWorld* physicsWorld;   // nullable — objects without collision still load
    std::vector<std::unique_ptr<SceneObject>>& sceneObjects;  // output
};

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

    // Spawn scene objects from LevelData::objects — the JSON/GLB path used when
    // no binary object sidecar (.edenbin) is present. Creates GPU models
    // (primitive, skinned, LIME, GLB), applies transforms/material/collision,
    // re-bakes frozen transforms, registers Bullet colliders, restores wall
    // holes and behaviors, then appends each object to ctx.sceneObjects.
    // Behavior-preserving lift of terrain_editor loadLevel()'s object loop.
    static void spawnObjects(const LevelData& data, const SpawnContext& ctx);
};

} // namespace eden
