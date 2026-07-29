#include "TessaraModule.hpp"

#include "BipedMesh.hpp"
#include "CreatureMesh.hpp"
#include "ScenePipeline.hpp"

#include "eden/Terrain.hpp"
#include "Renderer/Buffer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>

#include "eden/Input.hpp"
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
    const auto thinkFrom = std::chrono::steady_clock::now();

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

    // The bridge door opens for whoever is standing at it. Proximity rather than
    // a button: nobody wants a control on a doorway they walk through twenty
    // times an hour, and a door that opens as you reach it is invisible in the
    // right way.
    const glm::vec3 doorAt = m_ship.bridgeDoorCentre();
    auto nearDoor = [&](const glm::vec3& who) {
        return glm::length(glm::vec2(who.x - doorAt.x, who.z - doorAt.z)) < 5.0f;
    };
    m_ship.updateBridgeDoor(dt, nearDoor(m_playerPosition) ||
                                nearDoor(m_biped.hipCentre()) ||
                                nearDoor(m_walker.bodyCentre(*m_ground)));

    m_ship.update(dt, rampBusy);
    republishGround();

    m_walker.update(*m_ground, dt);

    const glm::vec3 eye = m_playerPosition;
    m_biped.update(*m_ground, dt, &eye);

    // A rally suspends the haul loop outright: it hands out crates, and handing
    // a crate to something that has been called in is how a rally never finishes.
    if (m_launch == Launch::Idle) updateHauling();
    updateLaunch(dt);

    const float thought = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - thinkFrom).count();
    m_msThink += (thought - m_msThink) * 0.1f;

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

void TessaraModule::callRally() {
    if (!m_ground || !m_placed) return;

    // Each to its OWN station, so five of them line up rather than converge.
    m_biped.orderTo(*m_ground, m_ship.stationPosition(0));
    m_walker.orderTo(*m_ground, m_ship.stationPosition(1));

    // Whatever they were carrying goes down where they stand. A crate held by a
    // creature that has stopped hauling would otherwise hang in the air at the
    // last place its hands were.
    for (int self = 0; self < 2; ++self) {
        Hauler& h = m_haul[self];
        if (h.crate >= 0) {
            Crate& crate = m_crates[h.crate];
            crate.position.y = m_ground->heightAt(crate.position.x, crate.position.z,
                                                  crate.position.y, 2.0f) + kCrateSize * 0.5f;
            m_freeSlots.push_back(h.slot);
            h.crate = h.slot = -1;
            h.wasCarrying = false;
        }
    }

    // The way in has to be open to answer a rally, and shutting it is the NEXT
    // step -- so anything mid-close reverses now rather than stranding whoever is
    // still out on the field.
    m_ship.openRamp();
    m_launch = Launch::Rallying;
}

void TessaraModule::standDown() {
    m_biped.standDown();
    m_walker.standDown();
    m_launch = Launch::Idle;
}

bool TessaraModule::playerAboard() const {
    if (!m_ground || !m_placed) return false;

    // His reported position is his EYE; the floor he is on is a metre and seven
    // below it. Asked three ways and any will do: in the hold's room, on one of
    // the ship's own surfaces, or within the hull by the ship's own reckoning.
    // They disagree only at the edges -- the doorway, the ramp, the lip of the
    // deck -- which is exactly where somebody standing aboard is most likely to
    // be told they are not.
    // Asked of his EYE as well as his feet, and either will do.
    //
    // The biped is carried on the strength of his HIP height passing exactly this
    // test, and he rides correctly -- so the test works on a point well up a
    // body. Deriving the player's feet by subtracting an eye height is one
    // assumption too many: it is right only while whatever owns his position
    // agrees about that offset, and when it does not the result is a player who
    // is plainly standing in the hold and reported as not.
    const glm::vec3 eye = m_playerPosition;
    const glm::vec3 feet = eye - glm::vec3(0.0f, 1.7f, 0.0f);

    return m_ground->enclosureAt(eye) >= 0
        || m_ground->enclosureAt(feet) >= 0
        || m_ground->onPatch(feet.x, feet.z, feet.y, kPlayerStepUp)
        || m_ship.isAboard(eye)
        || m_ship.isAboard(feet + glm::vec3(0.0f, 0.4f, 0.0f));
}

bool TessaraModule::carriedPlayer(glm::vec3& outMove, float& outTurnDegrees,
                                  glm::vec3& outAbout) const {
    if (!m_carriedPlayer) return false;
    outMove = m_carryMove;
    outTurnDegrees = m_carryTurn;
    outAbout = m_carryAbout;
    return true;
}

void TessaraModule::launch() {
    if (m_launch != Launch::Ready) return;
    m_ship.setAirborne(true);
    m_carriedPlayer = false;

    // Recorded in the SHIP's frame, so that wherever the ship goes he is still
    // standing where he was standing. There is no other way to carry him.
    m_walker.parkIn(*m_ground, m_ship.origin(), m_ship.right(), m_ship.forward());

    // Said out loud, because "it left without me" and "it did not carry me" look
    // identical from the pad and have completely different causes.
    const glm::vec3 feet = m_playerPosition - glm::vec3(0.0f, 1.7f, 0.0f);
    std::printf("[tessara] launch: player feet at (%.1f, %.1f, %.1f), deck at %.1f -> %s\n",
                feet.x, feet.y, feet.z, m_ship.origin().y + m_ship.params.deckHeight,
                playerAboard() ? "ABOARD" : "NOT ABOARD");

    m_launch = Launch::Flying;
}

void TessaraModule::land() {
    if (m_launch != Launch::Flying) return;
    m_ship.beginLanding();
}

// The blunt version: on the ground, now, wherever it happens to be over.
//
// It used to clear `airborne` and stop there, which left the ship hanging seven
// units up with its ramp reaching for a pad it had flown away from, and everybody
// aboard standing on a deck that had quietly stopped being a vehicle. It moves the
// ship down and carries the hold with it, in one step, by the same manifest the
// flying path uses -- a big move rather than a lot of small ones, but the same move.
void TessaraModule::setDown() {
    if (!m_ground || !m_placed) {
        m_carriedPlayer = false;
        m_launch = Launch::Ready;
        return;
    }

    const Manifest aboard = manifest();
    const glm::vec3 move = m_ship.settle(*m_source);
    republishGround();

    m_carriedPlayer = aboard.player;
    m_carryMove  = move;
    m_carryTurn  = 0.0f;
    m_carryAbout = m_ship.origin() - move;
    if (glm::dot(move, move) > 1e-10f) carryPassengers(aboard, move, 0.0f);

    m_walker.unpark();
    m_launch = Launch::Ready;
    std::printf("[tessara] set down at (%.0f, %.1f, %.0f), dropped %.1f\n",
                m_ship.origin().x, m_ship.origin().y, m_ship.origin().z, -move.y);
}

// Who is aboard, asked while the ship is still where they are standing.
//
// This has to be a separate step from carrying them. A passenger is somebody the
// floor was under at the moment it left, and every way of asking -- the room, the
// deck patch, the hull -- is derived from where the ship IS. Ask after the move
// and the deck has already gone out from under them, so the honest answer is no,
// and the ship flies off leaving behind exactly the people who were standing in
// it. That was the bug.
TessaraModule::Manifest TessaraModule::manifest() const {
    Manifest m;
    m.player = playerAboard();
    m.biped = m_ground->enclosureAt(m_biped.hipCentre()) >= 0;
    m.crates.reserve(m_crates.size());
    for (const Crate& crate : m_crates) {
        m.crates.push_back(m_ground->enclosureAt(crate.position) >= 0);
    }
    return m;
}

// Everything standing in the hold moves by exactly what the ship moved by.
void TessaraModule::carryPassengers(const Manifest& aboard,
                                    const glm::vec3& move, float turn) {
    const glm::vec3 about = m_ship.origin() - move;   // where the pivot WAS
    const float a = glm::radians(turn);
    const float s = std::sin(a), co = std::cos(a);
    auto shift = [&](glm::vec3 p) {
        const glm::vec3 d = p - about;
        return about + glm::vec3(d.x * co + d.z * s, d.y, -d.x * s + d.z * co) + move;
    };

    if (aboard.biped) m_biped.carry(move, turn, about);

    for (size_t i = 0; i < m_crates.size(); ++i) {
        if (i < aboard.crates.size() && aboard.crates[i]) {
            m_crates[i].position = shift(m_crates[i].position);
            m_crates[i].yaw += a;
        }
    }

    // The leash is a place on the deck now, not a place on the planet.
    m_walker.setHome(m_ship.origin(), kHomeRange);
    m_biped.setHome(m_ship.origin(), kHomeRange);
}

void TessaraModule::updateLaunch(float dt) {
    switch (m_launch) {
        case Launch::Idle:
            break;

        case Launch::Rallying: {
            // Everybody on their spot AND actually inside, because a station is a
            // place on the deck and standing at the foot of the ramp under it is
            // not the same thing.
            const bool bipedIn = m_biped.onStation() &&
                                 m_ground->enclosureAt(m_biped.hipCentre()) >= 0;
            const bool walkerIn = m_walker.onStation() &&
                                  m_ground->enclosureAt(m_walker.bodyCentre(*m_ground)) >= 0;
            if (bipedIn && walkerIn) {
                m_ship.closeRamp();
                m_launch = Launch::Sealing;
            }
            break;
        }

        case Launch::Sealing:
            // The interlock can refuse -- somebody on the ramp reopens it -- and
            // that is reported rather than fought. Shut means shut.
            if (m_ship.rampProgress() <= 0.001f) {
                m_launch = Launch::Ready;
            } else if (m_ship.isOpening()) {
                m_launch = Launch::Rallying;   // it reversed under somebody; wait again
            }
            break;

        case Launch::Ready:
            // Whatever the last airborne frame asked the host to do with the
            // player, it has been done by now. Left standing, it would be redone
            // every frame and walk him off across the planet.
            m_carriedPlayer = false;
            break;

        case Launch::Flying: {
            // Flown from the helm, and only from the helm. Standing at the
            // console is the whole interface -- walk away and the ship holds its
            // heading, which is what a hover does.
            const glm::vec3 helm = m_ship.helmStation();
            m_atHelm = glm::length(glm::vec2(m_playerPosition.x - helm.x,
                                             m_playerPosition.z - helm.z)) < 4.0f;

            float forward = 0.0f, turn = 0.0f, lift = 0.0f;
            if (m_atHelm) {
                using eden::Input;
                if (Input::isKeyDown(Input::KEY_W)) forward += 1.0f;
                if (Input::isKeyDown(Input::KEY_S)) forward -= 1.0f;
                if (Input::isKeyDown(Input::KEY_D)) turn += 1.0f;
                if (Input::isKeyDown(Input::KEY_A)) turn -= 1.0f;
                if (Input::isKeyDown(Input::KEY_SPACE)) lift += 1.0f;
                if (Input::isKeyDown(Input::KEY_LEFT_SHIFT)) lift -= 1.0f;
            }

            // The manifest is taken FIRST, and then the ship moves. See
            // TessaraModule::manifest.
            const Manifest aboard = manifest();

            m_ship.fly(dt, forward, turn, lift, *m_source);
            const glm::vec3 move = m_ship.lastMove();
            const float spun = m_ship.lastTurn();

            // ...and the ground follows it in the same breath. Everything the
            // host asks for the rest of this frame -- what is under the player's
            // feet, what he is standing inside, what he has walked into -- comes
            // out of Ground, and Ground is a snapshot of where the ship was when
            // it was last built. Republished at the top of update(), it describes
            // the pad for one frame after take-off, which is one frame of the
            // player being snapped back down onto a deck that is no longer there.
            republishGround();

            // The player rides too, and had been the one thing that did not.
            // The host owns where he is, so this is reported rather than applied
            // -- see GameModule::carriedPlayer. Asked of his FEET, because his
            // reported position is his eye and the enclosure's floor is the deck:
            // stood on the deck his eye is a metre and a half above it, which is
            // inside the hold either way, but on the ramp it is the difference
            // between being aboard and not.
            // Standing ON the ship, which is a broader and truer question than
            // being in the hold. The deck, the bridge and the ramp are all its
            // floor, and somebody in the doorway is on it whichever side of the
            // enclosure's edge their feet happen to fall.
            m_carriedPlayer = aboard.player;

            // Once a second while airborne, so a launch that leaves somebody
            // behind says why without them having to go looking.
            m_reportAt += dt;
            if (m_reportAt > 1.0f) {
                m_reportAt = 0.0f;
                const glm::vec3 e = m_playerPosition;
                std::printf("[tessara] flying: you %s | eye (%.1f,%.1f,%.1f) "
                            "deck %.1f ship (%.1f,%.1f,%.1f) room %d\n",
                            m_carriedPlayer ? "RIDING" : "left behind",
                            e.x, e.y, e.z,
                            m_ship.origin().y + m_ship.params.deckHeight,
                            m_ship.origin().x, m_ship.origin().y, m_ship.origin().z,
                            m_ground->enclosureAt(e));
            }
            m_carryMove = move;
            m_carryTurn = spun;
            m_carryAbout = m_ship.origin() - move;

            if (glm::dot(move, move) > 1e-10f || std::fabs(spun) > 1e-5f) {
                carryPassengers(aboard, move, spun);
            }

            // And the walker rides, drawn in the ship's frame.
            m_walker.parkFollow(m_ship.origin(), m_ship.right(), m_ship.forward());

            // Down. Checked AFTER the carry, because the frame it touches down on
            // is a frame the ship moved -- the last few inches of the drop -- and
            // everybody aboard has to come the whole way.
            if (!m_ship.airborne()) {
                m_walker.unpark();
                m_launch = Launch::Ready;
                m_reportAt = 0.0f;
                std::printf("[tessara] down at (%.0f, %.1f, %.0f), %.1f/s on the gear -- %s\n",
                            m_ship.origin().x, m_ship.origin().y, m_ship.origin().z,
                            m_ship.touchdownSpeed(),
                            m_ship.landedHard() ? "HARD" : "clean");
            }
            break;
        }
    }
}

const char* TessaraModule::launchLabel() const {
    switch (m_launch) {
        case Launch::Rallying: return "rallying - waiting for the crew";
        case Launch::Sealing:  return "sealing the hold";
        case Launch::Ready:    return "READY TO LAUNCH";
        case Launch::Flying:   return "AIRBORNE";
        default:               return "";
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
        if (m_glassHandle != UINT32_MAX)     m_buffers->destroyMeshBuffers(m_glassHandle);
    }
    m_shipHandle = m_creatureHandle = m_glassHandle = UINT32_MAX;
    m_pipeline.reset();
    m_buffers = nullptr;
}

void TessaraModule::upload(const std::vector<SceneVertex>& verts,
                           const std::vector<uint32_t>& indices, uint32_t& handle) {
    if (!m_buffers || verts.empty() || indices.empty()) return;

    // Refilled, not rebuilt.
    //
    // The creatures ARE re-meshed every frame -- their feet are somewhere new and
    // their legs are a different shape -- but that is a reason to put new numbers
    // in the same buffers, not to get new ones. Destroying and creating three
    // meshes a frame cost twelve device allocations and, on the unbatched path,
    // six vkQueueWaitIdle stalls: six full flushes of a queue with a whole planet
    // of terrain in it. That was one frame a second.
    if (handle != UINT32_MAX &&
        m_buffers->updateMeshBuffers(handle,
            verts.data(), static_cast<uint32_t>(verts.size()), sizeof(SceneVertex),
            indices.data(), static_cast<uint32_t>(indices.size()))) {
        return;
    }
    handle = m_buffers->createMeshBuffers(
        verts.data(), static_cast<uint32_t>(verts.size()), sizeof(SceneVertex),
        indices.data(), static_cast<uint32_t>(indices.size()));
}

void TessaraModule::rebuildGeometry() {
    if (!m_buffers || !m_ground) return;
    const auto meshFrom = std::chrono::steady_clock::now();

    // All of this frame's copies into ONE submit with ONE wait at the end of it,
    // rather than a submit and a full-queue stall per buffer. Only if nobody else
    // has a batch open -- ending theirs would submit a level load half-recorded.
    const bool ownBatch = !m_buffers->batching();
    if (ownBatch) m_buffers->beginBatch();

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

    // The stations, marked. A spot a unit is sent to should be a spot you can see
    // it standing on -- otherwise a crew lined up correctly and a crew lined up by
    // accident look identical.
    for (int i = 0; i < m_ship.stationCount(); ++i) {
        appendApproachMark(m_ship.stationPosition(i), 1.1f, m_launch != Launch::Idle,
                           m_verts, m_indices);
    }
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

    // The windows, on their own so they can be drawn translucent afterwards.
    m_verts.clear();
    m_indices.clear();
    appendShipGlass(m_ship, m_verts, m_indices);
    upload(m_verts, m_indices, m_glassHandle);

    if (ownBatch) m_buffers->endBatch();

    const float took = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - meshFrom).count();
    m_msMesh += (took - m_msMesh) * 0.1f;
}

void TessaraModule::renderWorld(const eden::ModuleRenderFrame& frame) {
    const auto drawFrom = std::chrono::steady_clock::now();
    struct Stop {
        const std::chrono::steady_clock::time_point& from;
        float& into;
        ~Stop() {
            const float took = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - from).count();
            into += (took - into) * 0.1f;
        }
    } stop{drawFrom, m_msDraw};

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

    // Glass last, through the variant that tests depth but does not WRITE it --
    // the same one the creature's see-through panels use. A pane that wrote depth
    // would hide whatever is behind it, which is the one thing a window must not
    // do. Alpha is per-draw here rather than per-vertex, which is exactly why the
    // glass had to be split out of the hull mesh to stop being a blue wall.
    if (m_glassHandle != UINT32_MAX) {
        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline->getGhostHandle());
        ScenePush glass{};
        glass.mvp  = frame.viewProj;
        glass.tint = glm::vec4(1.0f, 1.0f, 1.0f, 0.16f);
        glass.eye  = glm::vec4(frame.eye, 0.0f);
        vkCmdPushConstants(frame.cmd, m_pipeline->getLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(ScenePush), &glass);
        draw(m_glassHandle);
    }
}

void TessaraModule::renderUI(float, float) {
    ImGui::Begin("TESSARA:AXIOM");

    if (!m_terrain) {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "no terrain in this level");
        ImGui::End();
        return;
    }

    // Where you are standing, always, whatever the ship is doing.
    //
    // This lived inside the Ready state, which meant it was invisible unless you
    // had already rallied and sealed -- so somebody stood in the hold wondering
    // what they were supposed to be reading saw nothing at all. It is a fact
    // about the world, not a step in a sequence, and it belongs at the top where
    // it can answer the question before it is asked.
    {
        const bool aboard = playerAboard();
        const glm::vec3 feet = m_playerPosition - glm::vec3(0.0f, 1.7f, 0.0f);
        ImGui::TextColored(aboard ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                                  : ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                           "YOU: %s", aboard ? "aboard" : "on the ground");
        ImGui::TextDisabled("  your feet %.2f   deck %.2f   (%.0f, %.0f)",
                            feet.y, m_ship.origin().y + m_ship.params.deckHeight,
                            feet.x, feet.z);
        ImGui::Separator();
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

    ImGui::Separator();
    if (m_launch == Launch::Idle) {
        if (ImGui::Button("RALLY")) callRally();
        ImGui::SameLine();
        ImGui::TextDisabled("call the crew in and seal the hold");
    } else {
        ImGui::TextColored(m_launch == Launch::Ready ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                                                     : ImVec4(0.95f, 0.82f, 0.35f, 1.0f),
                           "%s", launchLabel());
        ImGui::Text("  biped:  %s", m_biped.onStation() ? "on station" : "coming");
        // "cannot get there" rather than "coming", because a rally that will
        // never finish and one that is merely slow look identical from here, and
        // the first is the one worth interrupting.
        ImGui::Text("  walker: %s", m_walker.onStation()      ? "on station"
                                  : m_walker.stationStalled() ? "cannot reach his spot"
                                                              : "coming");
        if (m_launch == Launch::Ready) {
            const bool aboard = playerAboard();

            if (aboard) {
                if (ImGui::Button("LAUNCH")) launch();
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("LAUNCH");
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("launch anyway")) launch();
            }
            ImGui::SameLine();
        }
        if (m_launch == Launch::Flying) {
            const float agl = m_ship.heightAboveGround(*m_source);
            ImGui::Text("  %.0f above the ground", agl);
            ImGui::TextColored(m_carriedPlayer ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                                               : ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                               "  you are %s", m_carriedPlayer ? "aboard - riding with it"
                                                               : "NOT ABOARD - it left without you");
            ImGui::TextDisabled(m_atHelm ? "  at the helm: WASD to fly, space/shift for height"
                                         : "  nobody at the helm - holding course");

            // Coming down is flown, so the two numbers that matter while you do it
            // are how far there is left and how fast you are using it up.
            if (m_ship.landing()) {
                const float rate = -m_ship.verticalSpeed();
                const bool fast = rate > m_ship.flight.hardAt;
                ImGui::TextColored(fast ? ImVec4(0.95f, 0.45f, 0.35f, 1.0f)
                                        : ImVec4(0.35f, 0.9f, 0.45f, 1.0f),
                                   "  LANDING - down %.1f/s%s", rate,
                                   fast ? "  TOO FAST, hold space" : "");
                ImGui::TextDisabled("  hold space to flare; %.0f to go", agl);
                if (ImGui::Button("abort")) m_ship.abortLanding();
                ImGui::SameLine();
                if (ImGui::Button("cut and drop")) setDown();
            } else {
                if (ImGui::Button("LAND")) land();
                ImGui::SameLine();
                ImGui::TextDisabled("let go of the hover and fly it down");
                if (ImGui::Button("SET DOWN")) setDown();
                ImGui::SameLine();
                ImGui::TextDisabled("put it on the ground, no descent");
            }
        } else if (ImGui::Button("stand down")) {
            standDown();
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("this module: think %.2f ms, mesh %.2f ms, draw %.2f ms",
                        m_msThink, m_msMesh, m_msDraw);
    if (ImGui::Button("scatter crates again")) scatterCrates();
    ImGui::SameLine();
    if (ImGui::Button("work the ramp")) m_ship.toggleRamp();

    ImGui::End();
}

} // namespace tessara
