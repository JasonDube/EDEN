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

bool poweredOff(SceneObject* o) {
    const auto& meta = o->getModelMetadata();
    auto it = meta.find("power_off");
    return it != meta.end() && it->second == "1";
}

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

    assembleManifestFrom(deck);
    return true;
}

// Everything a ship IS, from a seed deck plate: the hull flood fill, the
// standing cargo, the superstructure weld, the fittings weld. Split out of
// buildManifest so the painter's live survey can weigh a hull that has no
// helm yet -- the deck is the ship's identity; the helm is furniture.
void VesselFlight::assembleManifestFrom(SceneObject* deck) {
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
            // Up to 12 units above a plate: lofted walls carry ceiling-
            // mounted junction boxes, and those must fly with the ship.
            if (ob.min.y < db2.max.y - 0.4f || ob.min.y > db2.max.y + 12.0f) continue;
            m_manifest.push_back(o->getName());
            break;
        }
    }

    // THE SUPERSTRUCTURE WELD. A revolve shell lives OUTSIDE the deck's
    // footprint -- rings beyond the plates, bulkheads beside the walls -- so
    // containment never captured it and the hull flew out of her own shell
    // (field report). Structure that TOUCHES structure is one ship: any
    // platform piece whose box meets a manifest member's box within the weld
    // tolerance joins, transitively, until nothing new does. Structural
    // pieces only -- a crate parked against the hull is cargo with opinions,
    // not superstructure, and the gap rule (touching, not proximity) stands.
    {
        std::vector<AABB> aboardBounds;
        std::vector<std::string> aboardNames = m_manifest;
        for (const std::string& n : m_manifest)
            if (SceneObject* o = find(n)) aboardBounds.push_back(o->getWorldBounds());
        auto listed = [&aboardNames](const std::string& n) {
            for (const auto& an : aboardNames) if (an == n) return true;
            return false;
        };
        bool welded = true;
        while (welded) {
            welded = false;
            for (auto& o : *m_deps.sceneObjects) {
                if (!o) continue;
                const auto& bt = o->getBuildingType();
                if (bt != "platform_slab" && bt != "platform_wall") continue;
                if (listed(o->getName())) continue;
                const AABB wb = o->getWorldBounds();
                for (const AABB& ab : aboardBounds) {
                    if (wb.min.x > ab.max.x + kWeldEps || ab.min.x > wb.max.x + kWeldEps) continue;
                    if (wb.min.y > ab.max.y + kWeldEps || ab.min.y > wb.max.y + kWeldEps) continue;
                    if (wb.min.z > ab.max.z + kWeldEps || ab.min.z > wb.max.z + kWeldEps) continue;
                    aboardNames.push_back(o->getName());
                    aboardBounds.push_back(wb);
                    m_manifest.push_back(o->getName());
                    welded = true;
                    break;
                }
            }
        }
    }

    // THE FITTINGS WELD -- rule five. Frames, surface-mounted boxes, and
    // every extension bolted to the hull's OUTSIDE stand over no plate and
    // are not structural, so rules two and three both miss them: exterior
    // radiators would stay behind at takeoff with the wires stretching.
    // Gear that TOUCHES a manifest member comes along, transitively --
    // gear meaning frames, salvage, and anything with a role or a
    // surface_mount claim. Touching, never proximity, as always.
    {
        std::vector<AABB> aboardBounds;
        std::vector<std::string> aboardNames = m_manifest;
        for (const std::string& n : m_manifest)
            if (SceneObject* o = find(n)) aboardBounds.push_back(o->getWorldBounds());
        auto listed = [&aboardNames](const std::string& n) {
            for (const auto& an : aboardNames) if (an == n) return true;
            return false;
        };
        auto isGear = [](SceneObject* o) {
            const auto& bt = o->getBuildingType();
            if (bt == "salvage" || bt == "wall_frame" || bt == "window_frame") return true;
            const auto& md = o->getModelMetadata();
            return md.count("role") > 0 || md.count("surface_mount") > 0;
        };
        bool welded = true;
        while (welded) {
            welded = false;
            for (auto& o : *m_deps.sceneObjects) {
                if (!o || listed(o->getName()) || !isGear(o.get())) continue;
                const AABB wb = o->getWorldBounds();
                for (const AABB& ab : aboardBounds) {
                    if (wb.min.x > ab.max.x + 0.08f || ab.min.x > wb.max.x + 0.08f) continue;
                    if (wb.min.y > ab.max.y + 0.08f || ab.min.y > wb.max.y + 0.08f) continue;
                    if (wb.min.z > ab.max.z + 0.08f || ab.min.z > wb.max.z + 0.08f) continue;
                    aboardNames.push_back(o->getName());
                    aboardBounds.push_back(wb);
                    m_manifest.push_back(o->getName());
                    welded = true;
                    break;
                }
            }
        }
    }
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
    bool gridAboard = false;
    m_powerOut = 0.0f;
    m_powerNeed = 0.0f;
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
        const bool off = poweredOff(o);
        if (hasRole(o, "engine")) {
            engineAboard = true;             // aboard even when shut down
            if (!off) {
                m_thrust += metaFloat(o, "thrust", kDefaultThrust);
                m_powerNeed += metaFloat(o, "power_in", 120.0f);
            }
        }
        if (o->getName().find("thruster_grid") != std::string::npos) gridAboard = true;
        if (hasRole(o, "helm")) {
            m_steering = std::max(m_steering, metaFloat(o, "steering", kDefaultSteer));
            if (!off) m_powerNeed += metaFloat(o, "power_in", 10.0f);
        }
        if (hasRole(o, "power") && !off) {
            m_powerOut += metaFloat(o, "power_out", 0.0f);
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

    // NOWHERE TO PUSH, NO LIFT. Thrust is a grid in the hull, not a wish:
    // the plan's X cells (each backed by an engine room, the drafting
    // table's law) become thruster_grid pieces, and an engine without one
    // is furniture. The old grid-less fleet was retired by decree -- "just
    // forget about those previous ones."
    if (!gridAboard) {
        m_error = "no exhaust grid in the hull -- the engines have nowhere to push";
        m_lastManifest = m_manifest;   // the console still needs the roster
        m_manifest.clear();
        m_deckName.clear();
        return false;
    }

    // UNPOWERED, NO LIFT -- rung four, the Scotty rung. Supply is the sum of
    // enabled reactors; demand is what the enabled flight systems draw. Kill
    // a system at the helm console to free the kilowatts, then press E again.
    if (m_powerOut < m_powerNeed) {
        char buf[128];
        std::snprintf(buf, sizeof buf,
                      "engines unpowered -- divert power (need %.0f kW, have %.0f)",
                      m_powerNeed, m_powerOut);
        m_error = buf;
        m_lastManifest = m_manifest;
        m_manifest.clear();
        m_deckName.clear();
        return false;
    }

    // THE TRACED RUNG. A wired hull is held to a higher standard: if ANY
    // manifest piece has a wire on it, every enabled engine -- and the helm
    // -- must actually RECEIVE power through the graph, not merely share a
    // deck with a reactor. The ledger above still caps total supply; this
    // checks the plumbing. Wireless hulls keep the ledger alone.
    if (m_deps.hasWire && m_deps.powerReaches) {
        bool anyWire = false;
        for (const std::string& name : m_manifest)
            if (SceneObject* o = find(name)) if (m_deps.hasWire(o)) { anyWire = true; break; }
        if (anyWire) {
            for (const std::string& name : m_manifest) {
                SceneObject* o = find(name);
                if (!o || poweredOff(o)) continue;
                const bool needsLine = hasRole(o, "engine") || hasRole(o, "helm");
                if (!needsLine) continue;
                if (!m_deps.powerReaches(o)) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                                  "%s '%s' has no powered line -- check the run",
                                  hasRole(o, "engine") ? "engine" : "helm",
                                  o->getName().c_str());
                    m_error = buf;
                    m_lastManifest = m_manifest;
                    m_manifest.clear();
                    m_deckName.clear();
                    return false;
                }
            }
        }
    }

    // The flight envelope, from power-to-weight and helm authority.
    m_readyTimer = 2.5f;   // ALL SYSTEMS READY -- every rung passed
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
    // The manifest survives landing as the LAST manifest -- the collision
    // pass needs to know what just flew, because a yawed hull's boxes may
    // still contain the pilot at the instant the flying exemption ends.
    m_lastManifest = std::move(m_manifest);
    m_manifest.clear();
    m_frameDelta = glm::vec3(0.0f);
}

bool VesselFlight::surveyNearestShip(const glm::vec3& nearPos, float range, float& tonnageOut) {
    if (m_flying) { tonnageOut = m_tonnage; return true; }
    // Seed from the nearest DECK PLATE -- a helmless hull mid-build is
    // still a ship, and the painter weighs her while she grows.
    SceneObject* deck = nullptr;
    float best = range * range;
    for (auto& o : *m_deps.sceneObjects) {
        if (!o || o->getBuildingType() != "platform_slab") continue;
        const glm::vec3 d = o->getTransform().getPosition() - nearPos;
        const float d2 = glm::dot(d, d);
        if (d2 < best) { best = d2; deck = o.get(); }
    }
    if (!deck) return false;
    m_manifest.clear();
    assembleManifestFrom(deck);
    float t = 0.0f;
    for (const std::string& name : m_manifest) {
        SceneObject* o = find(name);
        if (!o) continue;
        const auto& bt = o->getBuildingType();
        if (bt == "socket_marker") continue;
        if (bt == "platform_slab" || bt == "platform_wall") {
            const glm::vec3 sc = o->getTransform().getScale();
            t += sc.x * sc.y * sc.z * metaFloat(o, "density", kPlateDensity);
        } else {
            t += metaFloat(o, "mass", kDefaultMass);
        }
    }
    tonnageOut = t;
    m_manifest.clear();
    m_deckName.clear();
    return true;
}

void VesselFlight::renderPowerConsole() {
    if (!m_flying && !m_showPrompt && m_errorTimer <= 0.0f) return;
    const std::vector<std::string>& roster = m_flying ? m_manifest : m_lastManifest;
    if (roster.empty()) return;

    // Only rows that mean something: suppliers and consumers.
    struct Row { SceneObject* o; float out, in; };
    std::vector<Row> rows;
    float supply = 0.0f, demand = 0.0f;
    for (const std::string& n : roster) {
        SceneObject* o = find(n);
        if (!o) continue;
        float out = 0.0f, in = 0.0f;
        if (hasRole(o, "power"))  out = metaFloat(o, "power_out", 0.0f);
        if (hasRole(o, "engine")) in = metaFloat(o, "power_in", 120.0f);
        if (hasRole(o, "helm"))   in = metaFloat(o, "power_in", 10.0f);
        if (in <= 0.0f && out <= 0.0f) in = metaFloat(o, "power_in", 0.0f);
        if (out <= 0.0f && in <= 0.0f) continue;
        rows.push_back({o, out, in});
        if (!poweredOff(o)) { supply += out; demand += in; }
    }
    if (rows.empty()) return;

    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(30, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ship Power -- helm console")) {
        const bool deficit = supply < demand;
        ImGui::TextColored(deficit ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                   : ImVec4(0.55f, 0.9f, 0.6f, 1.0f),
                           "POWER  %.0f kW drawn of %.0f kW supplied", demand, supply);
        ImGui::Separator();
        for (auto& r : rows) {
            bool on = !poweredOff(r.o);
            ImGui::PushID(r.o);
            if (ImGui::Checkbox("##on", &on)) {
                auto meta = r.o->getModelMetadata();
                meta["power_off"] = on ? "0" : "1";
                r.o->setModelMetadata(meta);
            }
            ImGui::SameLine();
            const auto& meta = r.o->getModelMetadata();
            auto t = meta.find("title");
            ImGui::TextUnformatted(t != meta.end() ? t->second.c_str()
                                                   : r.o->getName().c_str());
            ImGui::SameLine(230);
            if (r.out > 0.0f) ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.6f, 1.0f), "+%.0f kW", r.out);
            else              ImGui::TextDisabled("-%.0f kW", r.in);
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::TextDisabled("toggles apply at the next takeoff (E)");
    }
    ImGui::End();
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
    if (m_readyTimer > 0.0f) m_readyTimer -= dt;
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
                      "FLYING  %.0f t  |  PWR %.0f/%.0f kW  |  spd %.0f  turn %.0f deg/s  |  E to set down",
                      m_tonnage, m_powerNeed, m_powerOut, m_flySpeed, m_turnRate);
        line = flyLine;
    }
    else if (m_errorTimer > 0.0f) line = m_error.c_str();
    if (m_flying && m_readyTimer > 0.0f) {
        char ready[96];
        std::snprintf(ready, sizeof ready, "ALL SYSTEMS READY -- %.0f kW routed", m_powerNeed);
        ImDrawList* rdl = ImGui::GetForegroundDrawList();
        const ImVec2 rsz = ImGui::CalcTextSize(ready);
        const ImVec2 rat(screenW * 0.5f - rsz.x * 0.5f, screenH - 170.0f);
        rdl->AddRectFilled(ImVec2(rat.x - 8, rat.y - 4), ImVec2(rat.x + rsz.x + 8, rat.y + rsz.y + 4),
                           IM_COL32(0, 0, 0, 160), 4.0f);
        rdl->AddText(rat, IM_COL32(110, 235, 130, 255), ready);
    }
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
