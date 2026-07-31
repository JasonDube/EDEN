#pragma once

// Flight for a player-built platform.
//
// THE RULE THIS SERVES: the h-slab is the hull. Stand at a helm you bought and
// placed, press E, and the deck the helm stands on -- with everything standing
// on it -- flies as one thing. WASD moves it (relative to where you look),
// Space and Shift take it up and down, E again sets it back to being a floor.
// Containment is SPATIAL, not wired: what makes something part of the vessel
// is that it is aboard, decided once at takeoff.
//
// CONTROLS ARE A SHIP'S, not a strafing camera's: A and D TURN the vessel,
// W and S run it forward and astern along its own heading, Space and Shift
// are lift. The heading is set to wherever the pilot faces at the moment they
// take the helm. Turning rotates everything aboard around the deck's centre
// -- positions orbit, facings turn, and the pilot's view turns with the ship
// (the mouse still looks freely on top of that).
//
// V1 limits, stated rather than hidden: one deck (the slab under the helm, no
// flood-fill across joined slabs); the manifest is frozen at takeoff --
// nothing joins mid-flight; and flying is for the walking-mode player
// (build-mode's cursor movement is not carried).
//
// WHAT THIS DELIBERATELY DOES NOT TOUCH: the collision system. At takeoff each
// carried object's parked static body is dropped through a host hook (a parked
// body would stay behind -- solid air at the old spot, ghost deck at the new
// one), and standing on the moving deck is carried by the same homegrown
// floor-follow that already holds the player on slabs. The next F5 re-registers
// bodies wherever the deck landed, through the same golden loop as always.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace eden {
class Camera;
class SceneObject;
class Terrain;
}

struct VesselFlightDeps {
    std::vector<std::unique_ptr<eden::SceneObject>>* sceneObjects = nullptr;
    eden::Terrain* terrain = nullptr;
    eden::Camera*  camera  = nullptr;

    // The player's feet, for the "take the helm" prompt.
    std::function<glm::vec3()> playerFeet;

    // Drop an object's static collision body if it has one. Implemented by the
    // host with the API that already exists; this file never reaches into the
    // collision system itself.
    std::function<void(eden::SceneObject*)> dropStaticBody;
};

class VesselFlight {
public:
    void setDeps(const VesselFlightDeps& deps) { m_deps = deps; }

    // Once per frame. Reads E / WASD / Space / Shift itself unless the GUI
    // wants the keyboard; leaves play mode -> lands automatically.
    void update(float dt, bool isPlayMode, bool guiWantsKeys);

    bool isFlying() const { return m_flying; }

    // What the deck did THIS frame, for the host to apply identically to the
    // player so pilot and ship cannot drift apart: a translation, and a turn
    // (degrees, camera-yaw convention) about a pivot.
    const glm::vec3& playerCarry() const { return m_frameDelta; }
    float carryTurnDeg() const { return m_frameTurn; }
    const glm::vec3& carryPivot() const { return m_framePivot; }

    // The prompt / flight HUD line.
    void renderUI(float screenW, float screenH) const;

    // Programmatic controls -- the self-test flies without a keyboard, and a
    // script could too. takeHelm skips the proximity requirement.
    bool takeHelm(const std::string& helmName);
    void releaseHelm();
    bool tick(float dt, const glm::vec3& worldMove);
    // Turn the whole vessel by `deg` (camera-yaw convention: positive = right)
    // about the deck's centre. Everything aboard orbits and rotates.
    bool turnVessel(float deg);
    const std::vector<std::string>& manifest() const { return m_manifest; }
    float tonnage() const { return m_tonnage; }
    // Is this object part of the flying vessel? The host's collision pass asks,
    // because what is aboard is your floor and furniture, not an obstacle.
    bool isAboard(const std::string& name) const {
        if (!m_flying) return false;
        for (const auto& n : m_manifest) if (n == name) return true;
        return false;
    }
    const std::string& lastError() const { return m_error; }

private:
    eden::SceneObject* find(const std::string& name) const;
    eden::SceneObject* helmNearPlayer(float within) const;
    bool buildManifest(eden::SceneObject* helm);
    void applyMove(const glm::vec3& delta);
    void applyYaw(float deg, const glm::vec3& pivot);

    VesselFlightDeps m_deps;
    bool m_flying = false;
    bool m_showPrompt = false;
    std::string m_helmName;
    std::string m_deckName;
    // Names, not pointers -- the scene list reorders and deletes freely, and a
    // name that stops resolving is an object that left the vessel, not a crash.
    std::vector<std::string> m_manifest;
    glm::vec3 m_frameDelta{0.0f};
    float m_frameTurn = 0.0f;          // this frame's turn, camera-yaw degrees
    glm::vec3 m_framePivot{0.0f};      // about the deck's centre
    float m_headingDeg = 0.0f;         // where the bow points, camera-yaw degrees
    // The weighing, computed at takeoff from the manifest's files.
    float m_tonnage  = 0.0f;
    float m_thrust   = 0.0f;
    float m_steering = 0.0f;
    float m_flySpeed  = 8.0f;
    float m_liftSpeed = 5.0f;
    float m_turnRate  = 50.0f;
    std::string m_error;
    float m_errorTimer = 0.0f;   // seconds the HUD keeps showing a refusal
};
