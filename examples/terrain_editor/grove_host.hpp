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

// ── Building Catalog ─────────────────────────────

struct CityBuildingDef {
    std::string type;        // "farm", "house", "lumber_mill", etc.
    std::string name;        // Display name: "Farm", "Lumber Mill"
    std::string category;    // "housing", "food", "resource", "industry", "service", "commercial"
    std::string zoneReq;     // Required zone: "residential", "resource", "industrial", "commercial", "" = any
    std::string modelPath;   // Path to .glb/.lime model, empty = use placeholder
    float cost;              // Credits to build
    int maxWorkers;          // Worker slots
    float footprint;         // Approximate radius in meters (for spacing)
    std::string produces;    // What it outputs: "food", "wood", "metal", "goods", ""
    std::string requires;    // Required nearby resource: "wood", "iron", "limestone", "oil", ""
};

// Get the full building catalog (8 types)
const std::vector<CityBuildingDef>& getCityBuildingCatalog();

// Look up a building definition by type string. Returns nullptr if not found.
const CityBuildingDef* findCityBuildingDef(const std::string& type);

// ── Grove Context ────────────────────────────────

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
    float*               cityCredits;
    bool*                isPlayMode;
    std::string*         currentLevelPath;

    // Method callbacks (call TerrainEditor methods without knowing the class)
    std::function<void(int, int)> spawnPlotPosts;
    std::function<void(int, int)> removePlotPosts;
    std::function<void(eden::SceneObject*, const eden::Action&)> loadPathForAction;
};

void registerGroveHostFunctions(GroveVm* vm, GroveContext* ctx);
