#include "TessaraModule.hpp"

#include "BipedMesh.hpp"
#include "CreatureMesh.hpp"
#include "ScenePipeline.hpp"

#include "eden/Terrain.hpp"
#include "Renderer/Buffer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace tessara {

namespace {

constexpr float kCrateSize      = 0.85f;
constexpr int   kDefaultCrates  = 6;
constexpr int   kCratesPerLayer = 4;
constexpr float kPanelRange     = 9.0f;

// How high a ledge the PLAYER will step onto. The creatures each have their own;
// this is the host's body, and it wants the same treatment for the same reason --
// a ramp is a slope you walk up, a deck is a wall you do not climb.
constexpr float kPlayerStepUp   = 0.90f;

// How far from the ship either of them will get before turning back.
//
// A quarter of a kilometre, which is a working area rather than a planet. Both
// of them wander by scoring how much unwalked ground a direction opens, and on
// four thousand units of terrain the freshest ground is always further out --
// so without a leash they set off in a straight line and are specks inside a
// minute. On the generated field the edge of the world did this job.
constexpr float kHomeRange      = 250.0f;

} // namespace

TessaraModule::TessaraModule() = default;
TessaraModule::~TessaraModule() = default;

bool TessaraModule::initialize() { return true; }

void TessaraModule::shutdown() {
    detachRenderer();
    m_ground.reset();
    m_source.reset();
    m_terrain = nullptr;
    m_placed = false;
}

std::string TessaraModule::getStatusMessage() const {
    if (!m_renderError.empty()) return "no world rendering: " + m_renderError;
    if (!m_terrain) return "waiting for a level with terrain in it";
    if (!m_placed)  return "ship not set down yet";
    char buf[96];
    std::snprintf(buf, sizeof buf, "%d of %d crates aboard", m_stored, (int)m_crates.size());
    return buf;
}

// The host's terrain, seen through the eight questions everything here asks.
//
// The lattice is the terrain's OWN tile grid rather than a resampling of it:
// red_planet's tileSize is 2.0 and so is this example's node spacing, so the
// walker's feet land exactly on terrain samples and nothing is interpolated
// twice. If those ever diverge this is the line that has to think about it.
void TessaraModule::setTerrain(eden::Terrain* terrain) {
    m_terrain = terrain;
    m_ground.reset();
    m_source.reset();
    m_placed = false;
    if (!m_terrain) return;

    // The lattice these creatures walk, taken from the level rather than chosen.
    //
    // Terrain does not publish a tile size at the world level -- only chunks know
    // theirs -- so it is derived from the world's own extent instead, which is the
    // same number by construction and does not depend on a chunk being to hand.
    // red_planet comes out at 2016 nodes of 2.0, which is exactly this example's
    // own node spacing, so nothing is resampled and a foot lands on a real sample.
    const glm::vec2 size = m_terrain->getWorldSize();
    constexpr float kTile = 2.0f;
    const float tile = kTile;
    const int nodes = std::max(2, static_cast<int>(std::lround(size.x / tile)));

    eden::Terrain* t = m_terrain;
    m_source = std::make_unique<BorrowedTerrain>(
        nodes, tile,
        [t](float x, float z) { return t->getHeightAt(x, z); });

    m_ground = std::make_unique<Ground>(*m_source);
}

bool TessaraModule::groundHeight(float x, float z, float fromY, float& outHeight) const {
    if (!m_ground || !m_placed) return false;

    // Only worth answering if we actually have something here. Everywhere else
    // Ground falls back to the terrain, which the host has already asked.
    const float mine = m_ground->heightAt(x, z, fromY, kPlayerStepUp);
    if (!m_ground->onPatch(x, z, fromY, kPlayerStepUp)) return false;

    outHeight = mine;
    return true;
}

bool TessaraModule::resolvePosition(float& x, float& z, float footY, float height,
                                    float radius) const {
    if (!m_ground || !m_placed) return false;

    const glm::vec2 fixed = m_ground->resolve(glm::vec2(x, z), footY, height, radius);
    if (glm::length(fixed - glm::vec2(x, z)) < 1e-4f) return false;

    x = fixed.x;
    z = fixed.y;
    return true;
}

void TessaraModule::placeShip() {
    if (!m_source || !m_ground) return;

    // Set down near the middle of whatever this level is, on the flattest ground
    // it can find. It does not flatten a pad here: editing the level's terrain
    // out from under the player is a much larger promise than landing a ship.
    m_ship.place(*m_source, glm::vec2(0.0f, 0.0f), 34.0f);
    m_ship.openRamp();
    for (int i = 0; i < 300; ++i) m_ship.update(1.0f / 60.0f);

    m_storage = m_ship.bayStoragePoint();
    republishGround();

    // Both creatures start out on the field, well clear of the hull.
    const glm::vec3 o = m_ship.origin();
    const glm::vec3 back = -m_ship.forward();

    m_walker.reset(*m_ground, m_ground->nodeNear(o + back * 40.0f) - glm::ivec2(1), 0);
    m_biped.reset(*m_ground, glm::vec2(o.x + back.x * 30.0f, o.z + back.z * 30.0f), 0.0f, 1u);

    // Both of them work the ground around the ship rather than the whole planet.
    m_walker.setHome(m_ship.origin(), kHomeRange);
    m_biped.setHome(m_ship.origin(), kHomeRange);

    scatterCrates();
    m_placed = true;
}

void TessaraModule::scatterCrates() {
    m_crates.clear();
    m_freeSlots.clear();
    m_nextSlot = 0;
    m_stored = 0;
    for (Hauler& h : m_haul) { h = Hauler{}; }

    const glm::vec3 o = m_ship.origin();
    uint32_t rng = 0x9E3779B9u;
    auto next = [&rng] {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (rng & 0xFFFFFF) / 16777216.0f;
    };

    int guard = 0;
    while (static_cast<int>(m_crates.size()) < kDefaultCrates && guard++ < 400) {
        const float a = next() * 6.2831853f;
        const float r = 25.0f + next() * 55.0f;
        const float x = o.x + std::cos(a) * r;
        const float z = o.z + std::sin(a) * r;

        // Somewhere a box will sit rather than slide.
        if (m_source->normalAtWorld(x, z).y < 0.90f) continue;

        Crate crate;
        crate.position = glm::vec3(x, m_source->heightAtWorld(x, z) + kCrateSize * 0.5f, z);
        m_crates.push_back(crate);
    }
}

void TessaraModule::republishGround() {
    if (!m_ground) return;

    m_ground->clearPatches();
    m_ground->addPatch(m_ship.deckPatch());
    m_ground->addPatch(m_ship.rampPatch());

    std::vector<Blocker> solids;
    m_ship.appendBlockers(solids);
    m_ground->clearBlockers();
    for (const Blocker& b : solids) m_ground->addBlocker(b);

    m_ground->clearEnclosures();
    m_ground->addEnclosure(m_ship.enclosure());
}

void TessaraModule::onEnterPlayMode() {
    m_playing = true;
    if (m_ground && !m_placed) placeShip();

    // Said out loud, because on a planet two and a half miles across a ship you
    // cannot find and a ship that was never placed look identical.
    const glm::vec3 o = m_ship.origin();
    std::printf("[tessara] ship at (%.0f, %.1f, %.0f), %d crates scattered\n",
                o.x, o.y, o.z, static_cast<int>(m_crates.size()));
}

void TessaraModule::onExitPlayMode() { m_playing = false; }

bool TessaraModule::playerStart(glm::vec3& outPosition, float& outYawDegrees) const {
    if (!m_placed || !m_source) return false;

    // A few paces behind the muster point, on the ground, facing up the ramp.
    // Far enough back that the whole ship is in frame, near enough that the
    // creatures are already doing something when you arrive.
    const glm::vec3 muster = m_ship.rampApproachPoint();
    const glm::vec3 back = -m_ship.forward();
    const glm::vec3 at = muster + back * 14.0f;

    // Eye height above whatever is actually under that spot -- it is a planet,
    // not a plane, and the muster point's own height is not the ground's here.
    outPosition = glm::vec3(at.x, m_source->heightAtWorld(at.x, at.z) + 4.0f, at.z);

    // Camera yaw runs the same way the ship's does: atan2(x, z) of the heading.
    const glm::vec3 look = glm::normalize(m_ship.origin() - outPosition);
    outYawDegrees = glm::degrees(std::atan2(look.x, look.z));
    return true;
}

void TessaraModule::update(float dt) {
    if (!m_playing || !m_ground || !m_placed || dt <= 0.0f) return;

    // The biped works the panel when a job needs the way open. Noticed rather
    // than commanded, exactly as the standalone example does it -- he stands in
    // front of the control and the scene sees him there.
    if (m_biped.wayShut() && !m_ship.isOpening()) {
        const glm::vec3 at = m_biped.hipCentre();
        if (glm::length(at - m_ship.controlPosition()) < kPanelRange ||
            glm::length(at - m_ship.innerControlPosition()) < kPanelRange) {
            m_ship.openRamp();
        }
    }

    const bool rampBusy = m_ship.isOnRamp(m_biped.hipCentre()) ||
                          m_ship.isOnRamp(m_walker.bodyCentre(*m_ground)) ||
                          m_ship.isOnRamp(m_playerPosition);

    m_ship.update(dt, rampBusy);
    republishGround();

    m_walker.update(*m_ground, dt);

    const glm::vec3 eye = m_playerPosition;
    m_biped.update(*m_ground, dt, &eye);

    updateHauling();
    rebuildGeometry();
}

// One pass of the haul loop per creature. Lifted from the example unchanged in
// behaviour: whichever crate is nearest and unclaimed, a reserved slot on the
// pile, and a refusal remembered so the other one gets a turn at it.
void TessaraModule::updateHauling() {
    auto stackSlot = [this](int n) {
        const int layer = n / kCratesPerLayer;
        const int within = n % kCratesPerLayer;
        const float sx = (within & 1) ? 0.5f : -0.5f;
        const float sz = (within & 2) ? 0.5f : -0.5f;
        const float gap = kCrateSize * 1.04f;
        return m_storage + glm::vec3(sx * gap, kCrateSize * (0.5f + layer * 1.02f), sz * gap);
    };
    auto takeSlot = [this] {
        if (!m_freeSlots.empty()) { int s = m_freeSlots.back(); m_freeSlots.pop_back(); return s; }
        return m_nextSlot++;
    };
    auto claim = [this](const glm::vec3& from, int self) {
        int best = -1;
        float bestDistance = 1e9f;
        for (size_t i = 0; i < m_crates.size(); ++i) {
            if (m_crates[i].stored) continue;
            if (m_haul[1 - self].crate == static_cast<int>(i)) continue;
            const auto& no = m_haul[self].refused;
            if (std::find(no.begin(), no.end(), static_cast<int>(i)) != no.end()) continue;
            const float d = glm::length(glm::vec2(m_crates[i].position.x - from.x,
                                                  m_crates[i].position.z - from.z));
            if (d < bestDistance) { bestDistance = d; best = static_cast<int>(i); }
        }
        return best;
    };

    for (int self = 0; self < 2; ++self) {
        const bool walker = (self == 1);
        const bool carrying = walker ? m_walker.hasCargo() : m_biped.hasCargo();
        const bool busy     = walker ? m_walker.hasTask()  : m_biped.hasTask();
        const glm::vec3 at  = walker ? m_walker.bodyCentre(*m_ground) : m_biped.hipCentre();
        const float dropYaw = walker ? 0.0f : m_biped.yawDegrees() * 0.0174533f;

        Hauler& h = m_haul[self];

        if (h.crate >= 0) {
            if (carrying) {
                m_crates[h.crate].position = walker ? m_walker.cargoPosition(*m_ground)
                                                    : m_biped.cargoPosition();
            } else if (!busy) {
                if (h.wasCarrying) {
                    m_crates[h.crate].position = stackSlot(h.slot);
                    m_crates[h.crate].yaw = dropYaw;
                    m_crates[h.crate].stored = true;
                    ++m_stored;
                } else {
                    m_freeSlots.push_back(h.slot);
                }
                h.crate = h.slot = -1;
            }
        }
        h.wasCarrying = carrying;

        if (h.crate < 0 && !busy && !carrying) {
            const int next = claim(at, self);
            if (next >= 0) {
                const int slot = takeSlot();
                if (walker) m_walker.assignFetch(*m_ground, m_crates[next].position, stackSlot(slot));
                else        m_biped.assignFetch(*m_ground, m_crates[next].position, stackSlot(slot));

                const bool took = walker ? m_walker.hasTask() : m_biped.hasTask();
                if (took) { h.crate = next; h.slot = slot; }
                else      { h.refused.push_back(next); m_freeSlots.push_back(slot); }
            }
        }
    }
}

void TessaraModule::attachRenderer(const eden::ModuleRenderSetup& setup) {
    detachRenderer();
    m_buffers = &setup.buffers;

    // A module that cannot build its pipeline goes quiet. It does NOT take the
    // host down with it, which is what happened the first time this ran: the
    // shaders had not been copied next to the editor, ScenePipeline threw out of
    // attachRenderer, and the whole application failed to start on a level it
    // had otherwise loaded perfectly. An editor must survive its plugins.
    try {
        m_pipeline = std::make_unique<ScenePipeline>(setup.context, setup.renderPass, setup.extent);
        m_renderError.clear();
    } catch (const std::exception& e) {
        m_pipeline.reset();
        m_renderError = e.what();
        std::fprintf(stderr, "[tessara] no world rendering: %s\n", m_renderError.c_str());
    }
}

void TessaraModule::detachRenderer() {
    if (m_buffers) {
        if (m_shipHandle != UINT32_MAX)     m_buffers->destroyMeshBuffers(m_shipHandle);
        if (m_creatureHandle != UINT32_MAX) m_buffers->destroyMeshBuffers(m_creatureHandle);
    }
    m_shipHandle = m_creatureHandle = UINT32_MAX;
    m_pipeline.reset();
    m_buffers = nullptr;
}

void TessaraModule::upload(const std::vector<SceneVertex>& verts,
                           const std::vector<uint32_t>& indices, uint32_t& handle) {
    if (!m_buffers || verts.empty() || indices.empty()) return;

    // Rebuilt rather than updated, because the creatures are re-meshed every
    // frame -- their feet are somewhere new and their legs are a different shape.
    if (handle != UINT32_MAX) m_buffers->destroyMeshBuffers(handle);
    handle = m_buffers->createMeshBuffers(
        verts.data(), static_cast<uint32_t>(verts.size()), sizeof(SceneVertex),
        indices.data(), static_cast<uint32_t>(indices.size()));
}

void TessaraModule::rebuildGeometry() {
    if (!m_buffers || !m_ground) return;

    // The ship, which only changes when its ramp does.
    m_verts.clear();
    m_indices.clear();
    appendShipMesh(m_ship, m_verts, m_indices);
    for (const Crate& crate : m_crates) {
        const glm::vec3 right(std::cos(crate.yaw), 0.0f, -std::sin(crate.yaw));
        const glm::vec3 fwd(std::sin(crate.yaw), 0.0f, std::cos(crate.yaw));
        appendCrate(crate.position, kCrateSize, right, glm::vec3(0, 1, 0), fwd,
                    m_verts, m_indices);
    }
    appendStoragePad(m_storage, 1.6f, m_stored > 0, m_verts, m_indices);
    if (m_ship.rampProgress() > 0.15f) {
        appendApproachMark(m_ship.rampApproachPoint(), 1.5f, m_biped.hasCargo(),
                           m_verts, m_indices);
    }
    upload(m_verts, m_indices, m_shipHandle);

    // And the two of them, which change every frame.
    m_verts.clear();
    m_indices.clear();
    buildCreatureMesh(*m_ground, m_walker, m_verts, m_indices);
    // No head model here: the sentinel's head is a textured GLB that goes to
    // eden::ModelRenderer rather than through this untextured pipeline, and the
    // editor already has a renderer for exactly that. Body only for now.
    static const HeadModel kNoHead;
    appendBipedMesh(m_biped, kNoHead, m_verts, m_indices);
    upload(m_verts, m_indices, m_creatureHandle);
}

void TessaraModule::renderWorld(const eden::ModuleRenderFrame& frame) {
    if (!m_pipeline || !m_buffers || !m_placed) return;

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getHandle());

    ScenePush push{};
    push.mvp  = frame.viewProj;
    push.tint = glm::vec4(1.0f);
    push.eye  = glm::vec4(frame.eye, 0.0f);
    vkCmdPushConstants(frame.cmd, m_pipeline->getLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(ScenePush), &push);

    auto draw = [&](uint32_t handle) {
        if (handle == UINT32_MAX) return;
        auto* mesh = m_buffers->getMeshBuffers(handle);
        if (!mesh || !mesh->vertexBuffer || !mesh->indexBuffer) return;

        VkDeviceSize offset = 0;
        VkBuffer vb = mesh->vertexBuffer->getHandle();
        vkCmdBindVertexBuffers(frame.cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(frame.cmd, mesh->indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(frame.cmd, mesh->indexCount, 1, 0, 0, 0);
    };

    draw(m_shipHandle);
    draw(m_creatureHandle);
}

void TessaraModule::renderUI(float, float) {
    ImGui::Begin("TESSARA:AXIOM");

    if (!m_terrain) {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "no terrain in this level");
        ImGui::End();
        return;
    }

    const glm::vec3 o = m_ship.origin();
    ImGui::Text("ship down at (%.0f, %.1f, %.0f)", o.x, o.y, o.z);
    ImGui::Text("pile: %d of %d", m_stored, static_cast<int>(m_crates.size()));
    ImGui::Text("biped:  %s", m_biped.activityName());
    ImGui::Text("walker: %s", m_walker.activityName());

    if (m_ship.rampHeld()) {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
                           "ramp reopening - somebody is standing on it");
    } else if (m_biped.wayShut()) {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
                           "biped is waiting at a shut ramp");
    }

    if (ImGui::Button("scatter crates again")) scatterCrates();
    ImGui::SameLine();
    if (ImGui::Button("work the ramp")) m_ship.toggleRamp();

    ImGui::End();
}

} // namespace tessara
