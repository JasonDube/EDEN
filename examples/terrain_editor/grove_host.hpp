#pragma once

#include <grove.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <glm/glm.hpp>

namespace eden {
    class Camera;
    class Terrain;
    class SceneObject;
    class ZoneSystem;
    class ModelRenderer;
    struct Action;
}

struct GroveContext {
    // Core subsystems (non-owning)
    std::vector<std::unique_ptr<eden::SceneObject>>* sceneObjects;
    eden::Terrain*       terrain;
    eden::Camera*        camera;
    eden::ModelRenderer* modelRenderer;
    eden::ZoneSystem*    zoneSystem;

    // Grove-specific state (pointer-to so grove can read+write)
    GroveVm*             groveVm;
    std::string*         groveOutputAccum;
    eden::SceneObject**  groveBotTarget;      // ptr-to-ptr (grove reassigns it)
    std::string*         groveCurrentScriptName;
    float*               playerCredits;
    bool*                isPlayMode;
    std::string*         currentLevelPath;

    // Method callbacks (call TerrainEditor methods without knowing the class)
    std::function<void(int, int)> spawnPlotPosts;
    std::function<void(int, int)> removePlotPosts;
    std::function<void(eden::SceneObject*, const eden::Action&)> loadPathForAction;
};

void registerGroveHostFunctions(GroveVm* vm, GroveContext* ctx);
