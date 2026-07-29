#pragma once

#include "BorrowedTerrain.hpp"
#include "Biped.hpp"
#include "Ground.hpp"
#include "SceneVertex.hpp"
#include "Ship.hpp"
#include "Walker.hpp"

#include "GameModules/GameModule.hpp"

#include <memory>
#include <string>
#include <vector>

namespace eden { class Terrain; class BufferManager; }

namespace tessara {

class ScenePipeline;

// TESSARA:AXIOM as a game module.
//
// The example next door is a standalone Vulkan application that generates its own
// two-hundred-and-fifty-unit field to stand a ship on. This is the same content
// -- the ship, both creatures, the collision, the routing -- hosted by the editor
// instead, standing on whatever planet the level is actually made of.
//
// Almost none of it had to change, and the reason is worth stating: everything
// here only ever asked the ground eight questions, so pointing those at
// eden::Terrain rather than at a generated Heightfield is the whole of the port.
// The gait, the kerbs, the enclosure, both pathfinders and all thirty-three
// checks came across untouched.
class TessaraModule : public eden::GameModule {
public:
    TessaraModule();
    ~TessaraModule() override;

    const char* getName() const override { return "tessara"; }
    const char* getDescription() const override {
        return "TESSARA:AXIOM - a landed hauler, a walker and a biped that load it";
    }

    bool initialize() override;
    void shutdown() override;

    void onEnterPlayMode() override;
    void onExitPlayMode() override;

    void update(float deltaTime) override;
    void renderUI(float screenWidth, float screenHeight) override;

    void attachRenderer(const eden::ModuleRenderSetup& setup) override;
    void detachRenderer() override;
    void renderWorld(const eden::ModuleRenderFrame& frame) override;

    // The host hands us the level's terrain. Without one the module is inert
    // rather than broken -- there is simply nowhere to put a ship.
    void setTerrain(eden::Terrain* terrain);

    // Standing at the foot of the ramp looking up it -- which is both the most
    // useful place to be dropped and the one that shows you at a glance whether
    // any of this is working.
    bool playerStart(glm::vec3& outPosition, float& outYawDegrees) const override;

    bool isReady() const override { return m_terrain != nullptr; }
    std::string getStatusMessage() const override;

private:
    void placeShip();
    void scatterCrates();
    void republishGround();
    void updateHauling();
    void rebuildGeometry();
    void upload(const std::vector<SceneVertex>& verts,
                const std::vector<uint32_t>& indices, uint32_t& handle);

    // ---- the world ---------------------------------------------------------
    eden::Terrain* m_terrain = nullptr;
    std::unique_ptr<BorrowedTerrain> m_source;
    std::unique_ptr<Ground> m_ground;

    // ---- what is in it -----------------------------------------------------
    Ship   m_ship;
    Walker m_walker;
    Biped  m_biped;

    struct Crate {
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        bool  stored = false;
    };
    std::vector<Crate> m_crates;
    glm::vec3 m_storage{0.0f};
    int m_stored = 0;

    struct Hauler {
        int  crate = -1;
        int  slot  = -1;
        bool wasCarrying = false;
        std::vector<int> refused;
    };
    Hauler m_haul[2];
    std::vector<int> m_freeSlots;
    int m_nextSlot = 0;

    // ---- drawing -----------------------------------------------------------
    eden::BufferManager* m_buffers = nullptr;
    std::unique_ptr<ScenePipeline> m_pipeline;
    uint32_t m_shipHandle = UINT32_MAX;
    uint32_t m_creatureHandle = UINT32_MAX;

    std::vector<SceneVertex> m_verts;
    std::vector<uint32_t>    m_indices;

    std::string m_renderError;

    bool m_playing = false;
    bool m_placed = false;
};

} // namespace tessara
