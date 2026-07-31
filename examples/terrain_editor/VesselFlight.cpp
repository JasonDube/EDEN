#include "VesselFlight.hpp"
#include "Diag.hpp"

#include "Editor/SceneObject.hpp"

#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Terrain.hpp>

#include <imgui.h>

#include <cmath>
#include <cstdio>

using namespace eden;

namespace {

// A helm is a helm because its FILE said so: the catalog writes `role` into
// the hotbar slot's metadata, and the ordinary placement path copies slot
// metadata onto the placed object. No name matching, no special object class.
bool hasRole(SceneObject* o, const char* role) {
    const auto& meta = o->getModelMetadata();
    auto it = meta.find("role");
    return it != meta.end() && it->second == role;
}
bool isHelm(SceneObject* o) { return hasRole(o, "helm"); }

constexpr float kFlySpeed  = 8.0f;   // units/s along the heading
constexpr float kLiftSpeed = 5.0f;   // units/s up and down
constexpr float kTurnRate  = 50.0f;  // degrees/s of rudder
constexpr float kReach     = 3.0f;   // how close "at the helm" is

} // namespace

SceneObject* VesselFlight::find(const std::string& name) const {
    if (!m_deps.sceneObjects) return nullptr;
    for (auto& o : *m_deps.sceneObjects) {
        if (o && o->getName() == name) return o.get();
    }
    return nullptr;
}

SceneObject* VesselFlight::helmNearPlayer(float within) const {
    if (!m_deps.sceneObjects || !m_deps.playerFeet) return nullptr;
    const glm::vec3 feet = m_deps.playerFeet();
    for (auto& o : *m_deps.sceneObjects) {
        if (!o || !isHelm(o.get())) continue;
        const glm::vec3 p = o->getTransform().getPosition();
        const float d = std::hypot(p.x - feet.x, p.z - feet.z);
        if (d < within) return o.get();
    }
    return nullptr;
}

bool VesselFlight::buildManifest(SceneObject* helm) {
    // The deck is the slab under the helm's feet -- footprint contains the
    // helm, top within a step of its base. The helm names the vessel; the
    // player only names the helm.
    const AABB hb = helm->getWorldBounds();
    const glm::vec3 hp = helm->getTransform().getPosition();
    SceneObject* deck = nullptr;
    for (auto& o : *m_deps.sceneObjects) {
        if (!o || o->getBuildingType() != "platform_slab") continue;
        const AABB wb = o->getWorldBounds();
        if (hp.x < wb.min.x - 0.3f || hp.x > wb.max.x + 0.3f) continue;
        if (hp.z < wb.min.z - 0.3f || hp.z > wb.max.z + 0.3f) continue;
        if (std::fabs(hb.min.y - wb.max.y) > 0.8f) continue;
        deck = o.get();
        break;
    }
    if (!deck) {
        m_error = "the helm is not standing on a deck";
        return false;
    }

    m_deckName = deck->getName();
    m_manifest.clear();
    m_manifest.push_back(deck->getName());

    // Everything standing ON the deck comes along: centre over the footprint,
    // base within reach of the top. Frozen NOW -- what is aboard at takeoff is
    // the crew, and nothing joins mid-flight.
    const AABB db = deck->getWorldBounds();
    for (auto& o : *m_deps.sceneObjects) {
        if (!o || o.get() == deck) continue;
        const glm::vec3 p = o->getTransform().getPosition();
        if (p.x < db.min.x - 0.3f || p.x > db.max.x + 0.3f) continue;
        if (p.z < db.min.z - 0.3f || p.z > db.max.z + 0.3f) continue;
        const AABB ob = o->getWorldBounds();
        if (ob.min.y < db.max.y - 0.4f || ob.min.y > db.max.y + 2.5f) continue;
        m_manifest.push_back(o->getName());
    }
    return true;
}

bool VesselFlight::takeHelm(const std::string& helmName) {
    SceneObject* helm = find(helmName);
    if (!helm) { m_error = "no such helm: " + helmName; return false; }
    if (!isHelm(helm)) { m_error = helmName + " is not a helm"; return false; }
    if (!buildManifest(helm)) return false;

    // NO ENGINE, NO LIFT. The helm's own summary has promised this since the
    // day it was authored -- "Needs an engine on the same hull" -- and the rule
    // is spatial like everything else: an engine counts if it is ABOARD. The
    // role comes from the engine's file, through the shop, through placement.
    bool engineAboard = false;
    for (const std::string& name : m_manifest) {
        if (SceneObject* o = find(name)) {
            if (hasRole(o, "engine")) { engineAboard = true; break; }
        }
    }
    if (!engineAboard) {
        m_error = "no engine aboard -- the deck will not lift";
        m_manifest.clear();
        m_deckName.clear();
        return false;
    }

    // Parked collision bodies would stay behind at the old spot -- solid air
    // there, ghost deck at the new one. Dropped through the host's hook; the
    // next F5 re-registers everything wherever the deck stands then.
    if (m_deps.dropStaticBody) {
        for (const std::string& name : m_manifest) {
            if (SceneObject* o = find(name)) m_deps.dropStaticBody(o);
        }
    }

    m_helmName = helmName;
    m_flying = true;
    m_frameDelta = glm::vec3(0.0f);
    m_frameTurn = 0.0f;
    // The bow points wherever the pilot faces as they take the helm -- the
    // most natural "which way is forward" there is, and it needs no authored
    // forward on the deck.
    m_headingDeg = m_deps.camera ? m_deps.camera->getYaw() : 0.0f;
    if (g_diagnostics) std::printf("[Vessel] took the helm '%s' -- deck '%s', %zu aboard\n",
                helmName.c_str(), m_deckName.c_str(), m_manifest.size());
    std::fflush(stdout);
    return true;
}

void VesselFlight::releaseHelm() {
    if (m_flying) {
        if (g_diagnostics) std::printf("[Vessel] released the helm -- deck '%s' is a floor again\n",
                    m_deckName.c_str());
        std::fflush(stdout);
    }
    m_flying = false;
    m_helmName.clear();
    m_deckName.clear();
    m_manifest.clear();
    m_frameDelta = glm::vec3(0.0f);
}

void VesselFlight::applyMove(const glm::vec3& delta) {
    for (const std::string& name : m_manifest) {
        if (SceneObject* o = find(name)) {
            o->getTransform().setPosition(o->getTransform().getPosition() + delta);
        }
    }
    m_frameDelta = delta;
}

// Rotate everything aboard by `deg` about `pivot`: positions orbit, facings
// turn with them. glm's positive rotation about +Y runs opposite to the
// camera-yaw convention this class speaks, hence the negation -- worked out
// from R_y(+90) taking +X to -Z while camera yaw+ takes +X to +Z.
void VesselFlight::applyYaw(float deg, const glm::vec3& pivot) {
    const glm::quat q = glm::angleAxis(glm::radians(-deg), glm::vec3(0.0f, 1.0f, 0.0f));
    for (const std::string& name : m_manifest) {
        if (SceneObject* o = find(name)) {
            const glm::vec3 rel = o->getTransform().getPosition() - pivot;
            o->getTransform().setPosition(pivot + q * rel);
            o->getTransform().setRotation(q * o->getTransform().getRotation());
        }
    }
    m_frameTurn += deg;
    m_framePivot = pivot;
}

bool VesselFlight::turnVessel(float deg) {
    if (!m_flying) return false;
    SceneObject* deck = find(m_deckName);
    if (!deck) return false;
    // The pivot is the deck's own position -- the slab primitive is centred on
    // it in X and Z, so the ship turns about its middle, not a corner.
    applyYaw(deg, deck->getTransform().getPosition());
    m_headingDeg += deg;
    return true;
}

bool VesselFlight::tick(float dt, const glm::vec3& worldMove) {
    if (!m_flying) return false;
    glm::vec3 delta = worldMove * dt;

    // The deck does not go underground. Clamped against the terrain under its
    // centre -- v1 of "flying", not v1 of "tunnelling".
    if (delta.y < 0.0f && m_deps.terrain) {
        if (SceneObject* deck = find(m_deckName)) {
            const AABB db = deck->getWorldBounds();
            const glm::vec3 c = (db.min + db.max) * 0.5f;
            float ground = m_deps.terrain->getHeightAt(c.x, c.z);
            if (ground < -1000.0f) ground = 0.0f;   // hole sentinel
            const float floorY = ground + 0.2f;
            if (db.min.y + delta.y < floorY) {
                delta.y = std::min(0.0f, floorY - db.min.y);
            }
        }
    }

    applyMove(delta);
    return true;
}

void VesselFlight::update(float dt, bool isPlayMode, bool guiWantsKeys) {
    m_frameDelta = glm::vec3(0.0f);
    m_frameTurn  = 0.0f;
    m_showPrompt = false;

    if (!isPlayMode) {
        if (m_flying) releaseHelm();   // leaving play mode lands the ship
        return;
    }

    const bool e = !guiWantsKeys && Input::isKeyPressed(Input::KEY_E);

    if (m_errorTimer > 0.0f) m_errorTimer -= dt;

    if (!m_flying) {
        SceneObject* helm = helmNearPlayer(kReach);
        m_showPrompt = (helm != nullptr);
        // A refusal is worth three seconds on screen -- "nothing happened" is
        // the worst possible answer to pressing E.
        if (helm && e && !takeHelm(helm->getName())) m_errorTimer = 3.0f;
        return;
    }

    if (e) { releaseHelm(); return; }
    if (guiWantsKeys || !m_deps.camera) return;

    // A ship's controls, not a strafing camera's: A/D are the rudder, W/S run
    // along the vessel's own heading. Mouse-look plays no part in steering --
    // the pilot can look over the stern while flying forward.
    float rudder = 0.0f;
    if (Input::isKeyDown(Input::KEY_D)) rudder += kTurnRate * dt;
    if (Input::isKeyDown(Input::KEY_A)) rudder -= kTurnRate * dt;
    if (rudder != 0.0f) turnVessel(rudder);

    const float h = glm::radians(m_headingDeg);
    const glm::vec3 fwd(std::cos(h), 0.0f, std::sin(h));

    glm::vec3 move(0.0f);
    if (Input::isKeyDown(Input::KEY_W)) move += fwd;
    if (Input::isKeyDown(Input::KEY_S)) move -= fwd;
    if (glm::length(move) > 0.001f) move = glm::normalize(move) * kFlySpeed;

    float lift = 0.0f;
    if (Input::isKeyDown(Input::KEY_SPACE))      lift += kLiftSpeed;
    if (Input::isKeyDown(Input::KEY_LEFT_SHIFT)) lift -= kLiftSpeed;

    tick(dt, move + glm::vec3(0.0f, lift, 0.0f));
}

void VesselFlight::renderUI(float screenW, float screenH) const {
    const char* line = nullptr;
    if (m_flying)               line = "FLYING -- W/S ahead/astern, A/D turn, Space/Shift lift, E to set down";
    else if (m_errorTimer > 0.0f) line = m_error.c_str();
    else if (m_showPrompt)      line = "E -- take the helm";
    if (!line) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 sz = ImGui::CalcTextSize(line);
    const ImVec2 at(screenW * 0.5f - sz.x * 0.5f, screenH - 140.0f);
    dl->AddRectFilled(ImVec2(at.x - 8, at.y - 4), ImVec2(at.x + sz.x + 8, at.y + sz.y + 4),
                      IM_COL32(0, 0, 0, 160), 4.0f);
    const ImU32 col = m_flying              ? IM_COL32(120, 220, 255, 255)
                    : (m_errorTimer > 0.0f) ? IM_COL32(255, 140, 110, 255)
                                            : IM_COL32(255, 230, 150, 255);
    dl->AddText(at, col, line);
}
