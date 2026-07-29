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

    // ---- the launch sequence -----------------------------------------------
    // Rally, seal, ready. Deliberately a sequence with named states rather than
    // a button that does everything: each step waits on a thing that can fail or
    // take a while -- a walker on the far side of the field, a ramp that will not
    // shut because somebody is standing on it -- and a state you can name is a
    // state you can show, and one the player can see the ship is stuck in.
    enum class Launch { Idle, Rallying, Sealing, Ready, Flying };

    void callRally();     // everybody in, to their own station
    void standDown();     // as you were
    void launch();        // up, once the hold is sealed
    void setDown();       // and back onto the ground

    // The module takes the movement keys while somebody is flying from the helm,
    // so the same W that walks you about the hold does not also walk you about
    // the hold WHILE steering the ship you are standing in.
    // ...and only while it is actually flying. m_atHelm is worked out in the
    // flying branch and nowhere else, so on its own it stays true after landing
    // and quietly keeps the movement keys forever.
    bool wantsCaptureKeyboard() const override {
        return m_atHelm && m_launch == Launch::Flying;
    }

    // The deck moved and the player was standing on it.
    bool carriedPlayer(glm::vec3& outMove, float& outTurnDegrees,
                       glm::vec3& outAbout) const override;
    Launch launchState() const { return m_launch; }

    // Is the player standing on this ship? Public because it is the precondition
    // for the whole launch, and a precondition nothing outside can ask about is
    // one nothing outside can test.
    bool playerAboard() const;

    // Standing at the foot of the ramp looking up it -- which is both the most
    // useful place to be dropped and the one that shows you at a glance whether
    // any of this is working.
    bool playerStart(glm::vec3& outPosition, float& outYawDegrees) const override;

    // The deck and the ramp, offered to whoever the host is standing up -- so the
    // player walks aboard on the same terms the creatures do, by the same rules,
    // up the same ramp. And the hull, so they cannot simply walk through it.
    bool groundHeight(float x, float z, float fromY, float& outHeight) const override;
    bool resolvePosition(float& x, float& z, float footY, float height,
                         float radius) const override;

    bool isReady() const override { return m_terrain != nullptr; }
    std::string getStatusMessage() const override;

    // The ship and the ground it stands on, for anything that needs to ask them
    // where things are -- chiefly the headless harness, which stands a pretend
    // player on the deck and flies the thing without a screen. Read-only: the
    // launch sequence is the way to make it do anything.
    const Ship& ship() const { return m_ship; }
    const Ground* ground() const { return m_ground.get(); }
    const Biped& biped() const { return m_biped; }
    const Walker& walker() const { return m_walker; }

private:
    void placeShip();
    void scatterCrates();
    void republishGround();
    void updateHauling();
    void updateLaunch(float dt);
    // Who was standing in the hold when it moved. Read before the ship goes
    // anywhere and acted on afterwards -- see TessaraModule::manifest.
    struct Manifest {
        bool player = false;
        bool biped = false;
        std::vector<bool> crates;
    };
    Manifest manifest() const;
    void carryPassengers(const Manifest& aboard, const glm::vec3& move, float turn);
    const char* launchLabel() const;
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
    uint32_t m_glassHandle = UINT32_MAX;

    std::vector<SceneVertex> m_verts;
    std::vector<uint32_t>    m_indices;

    std::string m_renderError;

    Launch m_launch = Launch::Idle;
    bool   m_atHelm = false;

    // What the ship did to its own floor this frame, and whether the player was
    // standing on it when it did.
    glm::vec3 m_carryMove{0.0f};
    glm::vec3 m_carryAbout{0.0f};
    float     m_carryTurn = 0.0f;
    bool      m_carriedPlayer = false;
    float     m_reportAt = 0.0f;

    bool m_playing = false;
    bool m_placed = false;
};

} // namespace tessara
