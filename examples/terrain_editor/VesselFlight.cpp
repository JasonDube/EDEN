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

// Performance is DERIVED, not declared: tonnage from what the ship is made
// of, thrust from her engines' files, steering from her helm's. The engine is
// linear authority (does she lift, how fast), the helm rotational authority
// (how sharply this tonnage answers the rudder) -- which is exactly why a
// player buys a better one of either.
constexpr float kPlateDensity   = 2.0f;    // tons per unit^3 of slab/wall
constexpr float kDefaultMass    = 25.0f;   // a part with no mass metadata
constexpr float kDefaultThrust  = 3000.0f; // an engine authored before thrust existed
constexpr float kDefaultSteer   = 700.0f;  // a helm authored before steering existed
constexpr float kReach          = 3.0f;    // how close "at the helm" is

float metaFloat(SceneObject* o, const char* key, float fallback) {
    const auto& meta = o->getModelMetadata();
    auto it = meta.find(key);
    if (it == meta.end()) return fallback;
    try { return std::stof(it->second); } catch (...) { return fallback; }
}
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

    // THE HULL IS A FLOOD FILL, not one slab. A drawn ship's floor is many
    // abutting plates (the generator lays greedy rectangles over an irregular
    // hull), and lifting only the plate under the helm would tear the ship
    // apart along invisible seams. Starting from that plate, every slab that
    // TOUCHES the hull -- same deck height, edges meeting within the weld
    // tolerance -- joins, then everything touching those, until nothing new
    // does. Level ground between two ships keeps them separate ships: the
    // weld requires touching, not proximity.
    constexpr float kWeldEps = 0.05f;
    std::vector<SceneObject*> hullSlabs;
    std::vector<AABB> hullBounds;
    hullSlabs.push_back(deck);
    hullBounds.push_back(deck->getWorldBounds());
    m_manifest.push_back(deck->getName());
    auto inHull = [&hullSlabs](SceneObject* o) {
        for (SceneObject* h : hullSlabs) if (h == o) return true;
        return false;
    };
    bool grew = true;
    while (grew) {
        grew = false;
        for (auto& o : *m_deps.sceneObjects) {
            if (!o || o->getBuildingType() != "platform_slab" || inHull(o.get())) continue;
            const AABB wb = o->getWorldBounds();
            for (const AABB& hb2 : hullBounds) {
                if (std::fabs(wb.max.y - hb2.max.y) > kWeldEps) continue;
                if (wb.min.x > hb2.max.x + kWeldEps || hb2.min.x > wb.max.x + kWeldEps) continue;
                if (wb.min.z > hb2.max.z + kWeldEps || hb2.min.z > wb.max.z + kWeldEps) continue;
                hullSlabs.push_back(o.get());
                hullBounds.push_back(wb);
                m_manifest.push_back(o->getName());
                grew = true;
                break;
            }
        }
    }

    // Everything standing ON any hull plate comes along: centre over that
    // plate's footprint, base within reach of its top. Frozen NOW -- what is
    // aboard at takeoff is the crew, and nothing joins mid-flight.
    for (auto& o : *m_deps.sceneObjects) {
        if (!o || inHull(o.get())) continue;
        const glm::vec3 p = o->getTransform().getPosition();
        const AABB ob = o->getWorldBounds();
        for (const AABB& db2 : hullBounds) {
            if (p.x < db2.min.x - 0.3f || p.x > db2.max.x + 0.3f) continue;
            if (p.z < db2.min.z - 0.3f || p.z > db2.max.z + 0.3f) continue;
            if (ob.min.y < db2.max.y - 0.4f || ob.min.y > db2.max.y + 2.5f) continue;
            m_manifest.push_back(o->getName());
            break;
        }
    }
    return true;
}

bool VesselFlight::takeHelm(const std::string& helmName) {
    SceneObject* helm = find(helmName);
    if (!helm) { m_error = "no such helm: " + helmName; return false; }
    if (!isHelm(helm)) { m_error = helmName + " is not a helm"; return false; }
    if (!buildManifest(helm)) return false;

    // THE WEIGHING. Plates by volume, parts by their files' mass metadata;
    // engines contribute thrust, the helm contributes steering. All of it
    // came out of files the player bought -- nothing here is declared.
    m_tonnage = 0.0f;
    m_thrust = 0.0f;
    m_steering = 0.0f;
    bool engineAboard = false;
    for (const std::string& name : m_manifest) {
        SceneObject* o = find(name);
        if (!o) continue;
        const auto& bt = o->getBuildingType();
        if (bt == "socket_marker") {
            // A socket pad is painted intent -- where a part WILL go, not a
            // part. Weighing pads at the default part mass once inflated a
            // corvette by 225 t and cost her a clean takeoff.
        } else if (bt == "platform_slab" || bt == "platform_wall") {
            // A plate weighs what it is MADE OF: the yard stamps the hull
            // material's density into each piece; plates from before the
            // materials ladder weigh the old default.
            const glm::vec3 sc = o->getTransform().getScale();
            m_tonnage += sc.x * sc.y * sc.z * metaFloat(o, "density", kPlateDensity);
        } else {
            m_tonnage += metaFloat(o, "mass", kDefaultMass);
        }
        if (hasRole(o, "engine")) {
            engineAboard = true;
            m_thrust += metaFloat(o, "thrust", kDefaultThrust);
        }
        if (hasRole(o, "helm")) {
            m_steering = std::max(m_steering, metaFloat(o, "steering", kDefaultSteer));
        }
    }
    if (m_steering <= 0.0f) m_steering = kDefaultSteer;

    // NO ENGINE, NO LIFT -- and now also: NOT ENOUGH ENGINE, NO LIFT. The
    // helm's summary has promised the first since the day it was authored;
    // tonnage makes the second true. A heavier ship needs more or better
    // engines, which is what the catalogue's expensive shelf is FOR.
    if (!engineAboard) {
        m_error = "no engine aboard -- the deck will not lift";
        m_manifest.clear();
        m_deckName.clear();
        return false;
    }
    if (m_thrust < m_tonnage) {
        char buf[128];
        std::snprintf(buf, sizeof buf,
                      "engines cannot lift her -- %.0f t of ship, %.0f t of thrust",
                      m_tonnage, m_thrust);
        m_error = buf;
        m_manifest.clear();
        m_deckName.clear();
        return false;
    }

    // The flight envelope, from power-to-weight and helm authority.
    const float pw = m_thrust / m_tonnage;
    m_flySpeed  = std::clamp(8.0f * pw, 3.0f, 16.0f);
    m_liftSpeed = 0.6f * m_flySpeed;
    m_turnRate  = std::clamp(100.0f * m_steering / m_tonnage, 8.0f, 80.0f);

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
    // The pivot is the centre of the whole hull's footprint -- her true
    // midships -- not the seed plate's centre. With a multi-plate hull,
    // turning about one plate would wheel the ship about her prow.
    glm::vec3 mn(1e30f), mx(-1e30f);
    bool any = false;
    for (const std::string& name : m_manifest) {
        if (SceneObject* o = find(name)) {
            if (o->getBuildingType() != "platform_slab") continue;
            const AABB wb = o->getWorldBounds();
            mn = glm::min(mn, wb.min);
            mx = glm::max(mx, wb.max);
            any = true;
        }
    }
    if (!any) return false;
    applyYaw(deg, (mn + mx) * 0.5f);
    m_headingDeg += deg;
    return true;
}

bool VesselFlight::tick(float dt, const glm::vec3& worldMove) {
    if (!m_flying) return false;
    glm::vec3 delta = worldMove * dt;

    // The deck does not go underground. Clamped against the terrain under its
    // centre -- v1 of "flying", not v1 of "tunnelling".
    if (delta.y < 0.0f && m_deps.terrain) {
        // Every hull plate is checked, not just the seed -- a wing must not be
        // driven into a hillside while the centre hovers clear.
        for (const std::string& name : m_manifest) {
            SceneObject* o = find(name);
            if (!o || o->getBuildingType() != "platform_slab") continue;
            const AABB db2 = o->getWorldBounds();
            const glm::vec3 c = (db2.min + db2.max) * 0.5f;
            float ground = m_deps.terrain->getHeightAt(c.x, c.z);
            if (ground < -1000.0f) ground = 0.0f;   // hole sentinel
            const float floorY = ground + 0.2f;
            if (db2.min.y + delta.y < floorY) {
                delta.y = std::min(0.0f, floorY - db2.min.y);
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
    if (Input::isKeyDown(Input::KEY_D)) rudder += m_turnRate * dt;
    if (Input::isKeyDown(Input::KEY_A)) rudder -= m_turnRate * dt;
    if (rudder != 0.0f) turnVessel(rudder);

    const float h = glm::radians(m_headingDeg);
    const glm::vec3 fwd(std::cos(h), 0.0f, std::sin(h));

    glm::vec3 move(0.0f);
    if (Input::isKeyDown(Input::KEY_W)) move += fwd;
    if (Input::isKeyDown(Input::KEY_S)) move -= fwd;
    if (glm::length(move) > 0.001f) move = glm::normalize(move) * m_flySpeed;

    float lift = 0.0f;
    if (Input::isKeyDown(Input::KEY_SPACE))      lift += m_liftSpeed;
    if (Input::isKeyDown(Input::KEY_LEFT_SHIFT)) lift -= m_liftSpeed;

    tick(dt, move + glm::vec3(0.0f, lift, 0.0f));
}

void VesselFlight::renderUI(float screenW, float screenH) const {
    char flyLine[160];
    const char* line = nullptr;
    if (m_flying) {
        std::snprintf(flyLine, sizeof flyLine,
                      "FLYING  %.0f t  |  spd %.0f  turn %.0f deg/s  |  W/S A/D Space/Shift, E to set down",
                      m_tonnage, m_flySpeed, m_turnRate);
        line = flyLine;
    }
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
