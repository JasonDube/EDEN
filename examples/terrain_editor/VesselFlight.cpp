#include "VesselFlight.hpp"

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
bool isHelm(SceneObject* o) {
    const auto& meta = o->getModelMetadata();
    auto it = meta.find("role");
    return it != meta.end() && it->second == "helm";
}

constexpr float kFlySpeed  = 8.0f;   // units/s along the deck
constexpr float kLiftSpeed = 5.0f;   // units/s up and down
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
    std::printf("[Vessel] took the helm '%s' -- deck '%s', %zu aboard\n",
                helmName.c_str(), m_deckName.c_str(), m_manifest.size());
    std::fflush(stdout);
    return true;
}

void VesselFlight::releaseHelm() {
    if (m_flying) {
        std::printf("[Vessel] released the helm -- deck '%s' is a floor again\n",
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
    m_showPrompt = false;

    if (!isPlayMode) {
        if (m_flying) releaseHelm();   // leaving play mode lands the ship
        return;
    }

    const bool e = !guiWantsKeys && Input::isKeyPressed(Input::KEY_E);

    if (!m_flying) {
        SceneObject* helm = helmNearPlayer(kReach);
        m_showPrompt = (helm != nullptr);
        if (helm && e) takeHelm(helm->getName());
        return;
    }

    if (e) { releaseHelm(); return; }
    if (guiWantsKeys || !m_deps.camera) return;

    // WASD relative to where the pilot looks, flattened -- the same scheme the
    // tessara ship flies with. Space lifts, Shift descends.
    const float yaw = glm::radians(m_deps.camera->getYaw());
    const glm::vec3 fwd(std::cos(yaw), 0.0f, std::sin(yaw));
    const glm::vec3 right(-fwd.z, 0.0f, fwd.x);

    glm::vec3 move(0.0f);
    if (Input::isKeyDown(Input::KEY_W)) move += fwd;
    if (Input::isKeyDown(Input::KEY_S)) move -= fwd;
    if (Input::isKeyDown(Input::KEY_D)) move += right;
    if (Input::isKeyDown(Input::KEY_A)) move -= right;
    if (glm::length(move) > 0.001f) move = glm::normalize(move) * kFlySpeed;

    float lift = 0.0f;
    if (Input::isKeyDown(Input::KEY_SPACE))      lift += kLiftSpeed;
    if (Input::isKeyDown(Input::KEY_LEFT_SHIFT)) lift -= kLiftSpeed;

    tick(dt, move + glm::vec3(0.0f, lift, 0.0f));
}

void VesselFlight::renderUI(float screenW, float screenH) const {
    const char* line = nullptr;
    if (m_flying)          line = "FLYING -- WASD move, Space/Shift up/down, E to set down";
    else if (m_showPrompt) line = "E -- take the helm";
    if (!line) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 sz = ImGui::CalcTextSize(line);
    const ImVec2 at(screenW * 0.5f - sz.x * 0.5f, screenH - 140.0f);
    dl->AddRectFilled(ImVec2(at.x - 8, at.y - 4), ImVec2(at.x + sz.x + 8, at.y + sz.y + 4),
                      IM_COL32(0, 0, 0, 160), 4.0f);
    dl->AddText(at, m_flying ? IM_COL32(120, 220, 255, 255) : IM_COL32(255, 230, 150, 255),
                line);
}
