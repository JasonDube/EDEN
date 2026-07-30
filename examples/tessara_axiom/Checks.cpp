// Headless checks on the ship.
//
// WHY THESE EXIST
//
// The ship's collision broke five separate ways in one sitting, and not one of
// them was visible in the code. Each was found by walking something into it and
// watching, and each would have come straight back the next time the deck moved
// half a unit. So each is written down here as a thing that must keep being true.
//
// WHAT THEY ARE FOR, WHICH IS NOT WHAT I FIRST THOUGHT
//
// The first four attempts at checking this measured a QUERY -- "can he reach that
// surface from here" -- and every one of them passed while the creature was
// plainly stuck on screen. The bugs were all in a LOOP: a stride two units long
// against a surface tested at half-unit steps; a foot lagging a body by a stride;
// a camera whose height smooths toward the ground and therefore trails it while
// climbing. A query cannot see any of that, because a query has no history.
//
// So the rule for anything added here: drive the thing the way the thing moves.
// If it takes strides, use its stride. If it runs a follower on its own height,
// run the follower. A check that samples finer than the creature does is a check
// that steps over exactly the hole the creature falls in.

#include "Checks.hpp"

#include "Biped.hpp"
#include "Ground.hpp"
#include "Heightfield.hpp"
#include "Ship.hpp"
#include "Walker.hpp"
#include "BorrowedTerrain.hpp"
#include "TessaraModule.hpp"

#include "eden/LevelSerializer.hpp"
#include "eden/Terrain.hpp"
#include "eden/Camera.hpp"

#include <unordered_map>

#include <cmath>
#include <cstdio>
#include <vector>

namespace tessara {
namespace {

bool g_verbose = false;
int  g_failed  = 0;

void report(const char* name, bool ok, const char* detail) {
    if (!ok) ++g_failed;
    if (!ok || g_verbose) {
        std::printf("  %-34s %-4s %s\n", name, ok ? "ok" : "FAIL", detail);
    }
}

// A world with the ship set down in it, ramp fully open unless asked otherwise.
struct Scene {
    Heightfield terrain;
    Ship        ship;
    Ground      ground;

    Scene(float relief, glm::vec2 at, float yaw, int settleFrames = 300)
        : terrain(128, 2.0f, relief), ground(terrain)
    {
        ship.place(terrain, at, yaw);
        ship.openRamp();
        for (int i = 0; i < settleFrames; ++i) ship.update(1.0f / 60.0f);
        republish();
    }

    void republish() {
        ground.clearPatches();
        ground.addPatch(ship.deckPatch());
        ground.addPatch(ship.rampPatch());

        std::vector<Blocker> solids;
        ship.appendBlockers(solids);
        ground.clearBlockers();
        for (const Blocker& b : solids) ground.addBlocker(b);

        ground.clearEnclosures();
        ground.addEnclosure(ship.enclosure());
    }

    // Shut the ramp again and republish, for the checks about a closed door.
    void closeRamp() {
        ship.closeRamp();
        for (int i = 0; i < 300; ++i) ship.update(1.0f / 60.0f);
        republish();
    }

    glm::vec3 f() const { return ship.forward(); }
    glm::vec3 r() const { return ship.right(); }
    float deckY() const { return ship.origin().y + ship.params.deckHeight; }
};

// ---------------------------------------------------------------------------
// 1. The hull is solid where it looks solid, and hollow where it looks hollow.
// ---------------------------------------------------------------------------
void checkShipSolids() {
    Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
    Biped man;
    const float H = man.standHeight(), R = man.params.bodyRadius;
    const float deck = s.deckY(), ground = s.ship.origin().y;

    auto at = [&](float x, float z) { return s.ship.origin() + s.r() * x + s.f() * z; };
    auto solid = [&](glm::vec3 p, float footY) {
        return s.ground.blocked(p.x, p.z, footY, H, R);
    };

    const bool bayClear =
        !solid(at(0.0f, 0.0f), deck) && !solid(at(-4.0f, 0.0f), deck) &&
        !solid(at(4.0f, 0.0f), deck) && !solid(at(0.0f, -17.0f), deck) &&
        !solid(s.ship.bayStoragePoint(), deck);
    report("bay is clear to walk about in", bayClear,
           "deck, both sides, doorway and the pile itself");

    // The bulkhead is seven units deep rather than the half unit it is drawn at,
    // because the biped checks his path at four points and would straddle a
    // partition thinner than his sample spacing.
    const bool wallsSolid =
        solid(at(-7.0f, 0.0f), deck) && solid(at(7.0f, 0.0f), deck) &&
        solid(at(-4.0f, 11.8f), deck) && solid(at(4.0f, 11.8f), deck);
    report("hull walls and bulkhead posts are solid", wallsSolid,
           "sides, and the bulkhead either side of its doorway");

    // ---- the bridge --------------------------------------------------------
    // The front of the ship used to be solid from the bulkhead to the nose, on
    // the grounds that nobody could get there. Now they can, and the point of
    // going is to see out -- so both halves of that are checked: a way through,
    // and a sightline over the bulwark from where the captain stands.
    // With the door OPEN, which is its state when somebody is walking through it.
    // Shut it blocks, and that is the next check along -- this one asks whether
    // there is a room on the far side worth opening it for.
    for (int i = 0; i < 120; ++i) s.ship.updateBridgeDoor(1.0f / 60.0f, true);
    s.republish();

    const bool doorway = !solid(at(0.0f, 11.8f), deck);
    const bool bridgeFloor = !solid(at(0.0f, 14.0f), deck) &&
                             !solid(s.ship.helmStation(), deck);
    report("the bridge is a room you can walk into", doorway && bridgeFloor,
           "doorway through the bulkhead, floor beyond it, standing room at the helm");

    // The bridge door: shut it blocks, open it does not, and it opens for
    // somebody walking up to it rather than for a button.
    {
        Scene d(0.0f, {0.0f, 0.0f}, 0.0f);
        const glm::vec3 doorAt = d.ship.bridgeDoorCentre();

        for (int i = 0; i < 120; ++i) d.ship.updateBridgeDoor(1.0f / 60.0f, false);
        d.republish();
        const bool shutBlocks = d.ground.blocked(doorAt.x, doorAt.z, d.deckY(), H, R);
        const float shutAt = d.ship.bridgeDoorProgress();

        for (int i = 0; i < 120; ++i) d.ship.updateBridgeDoor(1.0f / 60.0f, true);
        d.republish();
        const bool openClears = !d.ground.blocked(doorAt.x, doorAt.z, d.deckY(), H, R);
        const float openAt = d.ship.bridgeDoorProgress();

        char detail[144];
        std::snprintf(detail, sizeof detail,
                      "shut at %.2f blocks, open at %.2f clears", shutAt, openAt);
        report("the bridge door opens and shuts", shutBlocks && openClears, detail);
    }

    // The helm has to be something you look OVER, not at -- having lowered the
    // bow to see out, a console at chest height is the same mistake one object
    // further in.
    {
        const float eye = deck + 1.7f;
        const float consoleTop = s.ship.helmPosition().y + 0.34f;
        char detail[128];
        std::snprintf(detail, sizeof detail, "eye at %.2f, console tops out at %.2f",
                      eye, consoleTop);
        report("you can see over the helm console", consoleTop < eye - 0.4f, detail);
    }

    // Eye height on this planet is 1.7 above the feet. The bulwark has to come
    // BELOW that from the helm, or the view forward is hull -- which is what it
    // was, by a clear metre.
    {
        const glm::vec3 stand = s.ship.helmStation();
        const float eye = deck + 1.7f;
        const float bulwark = ground + 2.2f + 1.15f;
        const bool stopped = solid(at(0.0f, 18.5f), deck);   // still cannot walk out
        char detail[144];
        std::snprintf(detail, sizeof detail,
                      "eye at %.2f, bulwark tops out at %.2f, %s",
                      eye, bulwark, stopped ? "and it still stops you" : "BUT YOU FALL OUT");
        report("you can see over the bow from the helm",
               eye > bulwark + 0.3f && stopped, detail);
        (void)stand;
    }

    const bool bellySolid =
        solid(at(0.0f, 0.0f), ground) && solid(at(0.0f, 15.0f), ground) &&
        solid(at(-8.0f, 0.0f), ground) && solid(s.ship.controlPosition(), ground);
    report("cannot walk under the hull", bellySolid,
           "belly blocks the whole footprint below the deck");

    const bool outsideFree =
        !solid(s.ship.rampApproachPoint(), ground) && !solid(at(0.0f, -40.0f), ground);
    report("open ground stays open", outsideFree, "muster point and the field");
}

// ---------------------------------------------------------------------------
// 2. The deck and the ramp meet without a gap.
//
// They used to abut: the deck stopped 0.15 short of the hull's rear edge and the
// ramp hinged on that edge, leaving a strip of nothing between them that read as
// terrain two units down. A body is not a point but a FOOT is, and it only has to
// land in the gap once. Sampled at 0.02 because the gap was 0.15 and a probe
// striding 0.5 walked clean over it and reported success.
// ---------------------------------------------------------------------------
void checkSeam() {
    int jumps = 0, stranded = 0, lines = 0;

    for (float yaw : {0.0f, 34.0f, 90.0f, 137.0f, 215.0f, 300.0f}) {
        Scene s(0.0f, {0.0f, 0.0f}, yaw);

        for (float across = -4.4f; across <= 4.4f; across += 0.4f) {
            const glm::vec3 start = s.ship.rampApproachPoint() + s.r() * across;
            float y = s.ship.origin().y, previous = y;
            ++lines;

            for (float in = 0.0f; in <= 26.0f; in += 0.02f) {
                const glm::vec3 p = start + s.f() * in;
                y = s.ground.heightAt(p.x, p.z, previous, 0.75f);
                if (std::fabs(y - previous) > 0.5f) ++jumps;
                previous = y;
            }
            if (std::fabs(y - s.deckY()) > 0.05f) ++stranded;
        }
    }

    char detail[128];
    std::snprintf(detail, sizeof detail,
                  "%d lines, %d stranded, %d jumps over half a unit",
                  lines, stranded, jumps);
    report("deck and ramp meet with no gap", jumps == 0 && stranded == 0, detail);
}

// ---------------------------------------------------------------------------
// 3. The way in works -- for a creature that takes STRIDES, and for a camera
//    that runs a height follower.
//
// Both of those matter. A foot reaching two to four units ahead of the one it is
// leaving has to find the ramp from that distance, and a fixed step-up allowance
// smaller than the rise over a stride means it never sees the ramp at all and
// walks through it on level ground. And eden::Camera smooths its height toward
// the ground and never snaps, so its eye trails the floor by more than a unit
// while climbing -- which read as being UNDER the ramp and threw the player off
// the one surface they were trying to walk up.
// ---------------------------------------------------------------------------
void checkBipedCarriesAboard() {
    // The whole creature, walking, carrying, for as long as it takes. Not a
    // sampled line up the ramp: the first version of this check reached ahead a
    // full stride from a foot a full stride back, which is what footTarget used
    // to do and no longer does, and it failed against working code. Drive the
    // thing the way the thing moves and there is nothing to get out of date.
    int delivered = 0, tried = 0;

    for (float yaw : {0.0f, 34.0f, 213.0f}) {
        Scene s(0.0f, {0.0f, 0.0f}, yaw);

        const glm::vec3 muster = s.ship.rampApproachPoint();
        const glm::vec3 pile   = s.ship.bayStoragePoint();

        // Put a crate out on the flat behind the ramp and set him off from
        // further out still, so he has to walk in, pick it up, and turn round.
        //
        // Sitting ON the ground, half its own height up, the way main.cpp scatters
        // them. Left at ground level it is half buried, and he squats over it
        // reaching for a handle underneath the dirt until the task times out --
        // which looks exactly like a collision failure and is not one.
        glm::vec3 crate = muster - s.f() * 6.0f;
        crate.y += 0.425f;
        const glm::vec3 from = muster - s.f() * 16.0f;

        Biped man;
        man.reset(s.ground, glm::vec2(from.x, from.z), 0.0f, 1u);
        man.assignFetch(s.ground, crate, pile);
        ++tried;

        bool carried = false;
        for (int frame = 0; frame < 60 * 90 && man.hasTask(); ++frame) {
            man.update(s.ground, 1.0f / 60.0f);
            if (man.hasCargo()) carried = true;
        }

        const glm::vec3 end = man.hipCentre();
        const float toPile = glm::length(glm::vec2(end.x - pile.x, end.z - pile.z));
        if (carried && toPile < 5.0f && end.y > s.deckY()) ++delivered;
    }

    char detail[128];
    std::snprintf(detail, sizeof detail, "%d of %d walked a crate up the ramp and into the bay",
                  delivered, tried);
    report("the biped carries a crate aboard", delivered == tried, detail);
}

// Runs eden::Camera's actual walk cycle: the slope gate, the exponential height
// follower, and the push-out, at 60Hz. These constants have to track main.cpp
// and Camera.cpp; if the player stops being able to climb, look here first.
float playerWalksIn(Scene& s, bool run) {
    constexpr float kEye = 4.0f, kRadius = 0.45f, kStepUp = 0.90f;
    constexpr float kSpeed = 14.0f, kSmooth = 4.0f, kMaxSlope = 52.0f;

    glm::vec3 eye = s.ship.rampApproachPoint() - s.f() * 4.0f;
    eye.y = s.ship.origin().y + kEye;

    float floor = s.ship.origin().y;
    const float dt = 1.0f / 60.0f;
    const float speed = kSpeed * (run ? 2.4f : 1.0f);
    float best = floor;

    for (int frame = 0; frame < 900; ++frame) {
        auto reference = [&] { return std::max(floor, eye.y - kEye); };
        auto query = [&](float x, float z) {
            return s.ground.heightAt(x, z, reference(), kStepUp);
        };

        const float here = query(eye.x, eye.z);
        const float step = speed * dt;
        const glm::vec3 want = eye + s.f() * step;
        const float rise = query(want.x, want.z) - here;

        if (rise <= 0.0f || glm::degrees(std::atan2(rise, step)) < kMaxSlope) {
            eye.x = want.x;
            eye.z = want.z;
        }

        eye.y += ((query(eye.x, eye.z) + kEye) - eye.y) * kSmooth * dt;
        floor = s.ground.heightAt(eye.x, eye.z, reference(), kStepUp);

        const glm::vec2 fixed = s.ground.resolve(glm::vec2(eye.x, eye.z), floor,
                                                 kEye + 0.5f, kRadius);
        eye.x = fixed.x;
        eye.z = fixed.y;
        best = std::max(best, floor);
    }
    return best;
}

void checkWayInForPlayer() {
    Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
    const float walked = playerWalksIn(s, false);
    const float ran    = playerWalksIn(s, true);

    char detail[128];
    std::snprintf(detail, sizeof detail, "walking reached %.2f, running %.2f, deck is %.2f",
                  walked, ran, s.deckY());
    report("the player gets aboard, walking or running",
           walked > s.deckY() - 0.1f && ran > s.deckY() - 0.1f, detail);
}

// ---------------------------------------------------------------------------
// 4. And the foot of the ramp is the ONLY way in.
//
// Two units along the ramp and two units across onto its flank are the same rise,
// so no rule about how high a surface is can tell them apart. Direction is what
// separates them, and the way to say a direction in a world of solids is to put
// something in the way of every other one -- hence the kerbs, and hence the
// underside being solid rather than something a short machine can duck beneath.
// ---------------------------------------------------------------------------
void checkNoOtherWayIn() {
    Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
    Biped man;
    const float H = man.standHeight(), R = man.params.bodyRadius;
    const glm::vec3 foot = s.ship.rampFootPosition();
    const float rampHalf = s.ship.params.bayWidth * 0.46f;

    int underOpen = 0, entranceBlocked = 0, shoved = 0;

    for (float along = 1.0f; along <= 6.0f; along += 0.25f) {
        for (float across = 0.0f; across <= rampHalf; across += 0.2f) {
            for (int side = 0; side < 2; ++side) {
                const glm::vec3 p = foot + s.f() * along + s.r() * ((side ? 1.0f : -1.0f) * across);
                if (!s.ground.blocked(p.x, p.z, s.ship.origin().y, H, R)) ++underOpen;
            }
        }
    }
    report("no way onto the ramp from the side", underOpen == 0,
           "the flank and the space beneath it are sealed");

    for (float back = 0.5f; back <= 5.0f; back += 0.5f) {
        const glm::vec3 p = foot - s.f() * back;
        if (s.ground.blocked(p.x, p.z, s.ship.origin().y, H, R)) ++entranceBlocked;
    }
    report("the foot of the ramp is still open", entranceBlocked == 0,
           "the approach behind it is walkable");

    // Standing ON the ramp must not shove anybody off it. This is the check that
    // would have caught taking a body's soles to be its hips minus a full leg --
    // a third of a unit UNDER the floor it was standing on, so it was judged to
    // be inside the thing it stood on and pushed off, every frame.
    for (float along = 0.5f; along <= 5.5f; along += 0.5f) {
        const glm::vec3 p = foot + s.f() * along;
        const float surface = s.ground.heightAt(p.x, p.z, 9.0f, 9.0f);
        const glm::vec2 fixed = s.ground.resolve(glm::vec2(p.x, p.z), surface, H, R);
        if (glm::length(fixed - glm::vec2(p.x, p.z)) > 0.01f) ++shoved;
    }
    report("standing on the ramp is left alone", shoved == 0,
           "the push-out does not fight anything stood on it");
}

// The same question for the walker, whose feet are POINTS on a two-unit lattice.
// Whether a kerb catches him depends on where the ship happens to sit relative to
// that lattice, so the ship is slid across a whole cell and turned.
void checkNoOtherWayInForWalker() {
    int placements = 0, leaked = 0, noWayIn = 0;

    for (float ox = 0.0f; ox < 2.0f; ox += 0.5f) {
    for (float oz = 0.0f; oz < 2.0f; oz += 0.5f) {
    for (float yaw : {0.0f, 34.0f, 90.0f, 213.0f}) {
        Scene s(0.0f, {ox, oz}, yaw);
        Walker w;
        const float reach = 2.0f * std::tan(glm::radians(w.params.maxSlopeDeg));
        ++placements;

        const glm::vec3 fp = s.ship.rampFootPosition();
        const glm::vec3 toShip = s.ship.origin() - fp;
        const glm::vec3 flat = glm::normalize(glm::vec3(toShip.x, 0.0f, toShip.z));

        const float half = 128.0f;
        const glm::ivec2 centre(static_cast<int>(std::round((fp.x + half) / 2.0f)),
                                static_cast<int>(std::round((fp.z + half) / 2.0f)));

        int got = 0;
        for (int dz = -6; dz <= 6; ++dz) {
        for (int dx = -6; dx <= 6; ++dx) {
            const glm::ivec2 from = centre + glm::ivec2(dx, dz);
            const glm::vec3 here = s.terrain.worldAt(from);
            const float g = s.terrain.heightAt(from);

            // He has to be able to stand where he starts, and behind the foot is
            // the entrance rather than a breach. Nodes are two units apart, so a
            // threshold tighter than one calls the last step of a legitimate
            // walk-in a leak.
            if (s.ground.blocked(here.x, here.z, g, w.params.bodyRise)) continue;
            if (glm::dot(here - fp, flat) < 1.0f) continue;

            for (int d = 0; d < 4; ++d) {
                const glm::ivec2 to = from + Walker::dirVec(d);
                if (s.ground.worldAt(to, g, reach).y < g + 0.35f) continue;
                if (w.goodStep(s.ground, from, to, g)) ++got;
            }
        }
        }
        if (got) ++leaked;

        // And he can still climb it, node by node, up the middle.
        Biped man;
        const glm::vec3 start = s.ship.rampApproachPoint();
        float under = s.ship.origin().y;
        bool aboard = false;
        for (int step = 1; step <= 18 && !aboard; ++step) {
            const glm::vec3 p = start + s.f() * (static_cast<float>(step) * 2.0f);
            under = s.ground.heightAt(p.x, p.z, under, man.params.stepUp);
            if (s.ground.blocked(p.x, p.z, under, man.standHeight(), man.params.bodyRadius)) break;
            if (under > s.deckY() - 0.05f) aboard = true;
        }
        if (!aboard) ++noWayIn;
    }}}

    char detail[128];
    std::snprintf(detail, sizeof detail,
                  "%d placements: %d leaked a way up the flank, %d had no way in",
                  placements, leaked, noWayIn);
    report("kerbs hold however the ship is aligned", leaked == 0 && noWayIn == 0, detail);
}

// The walker climbing under his own power, one node at a time.
void checkWalkerClimbs() {
    Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
    Walker w;
    const float reach = 2.0f * std::tan(glm::radians(w.params.maxSlopeDeg));

    int x = 64, y = 48;
    float footY = s.terrain.heightAt(glm::ivec2(x, y));
    bool aboard = false;

    for (int step = 0; step < 20 && !aboard; ++step) {
        const glm::ivec2 from(x, y), to(x, y + 1);
        if (!w.goodStep(s.ground, from, to, footY)) break;
        footY = s.ground.heightAt(to, footY, reach);
        ++y;
        if (footY > s.deckY() - 0.05f) aboard = true;
    }

    int intoWall = 0;
    for (int zn = 56; zn <= 68; ++zn) {
        if (!w.goodStep(s.ground, glm::ivec2(66, zn), glm::ivec2(67, zn), s.deckY())) ++intoWall;
    }

    char detail[128];
    std::snprintf(detail, sizeof detail, "reached %.2f (deck %.2f), %d of 13 steps into the wall refused",
                  footY, s.deckY(), intoWall);
    report("the walker climbs the ramp and stops at walls", aboard && intoWall == 13, detail);
}

// ---------------------------------------------------------------------------
// 4b. The hull is a ROOM, and the only way between it and the field is the door.
//
// The failure this catches has nothing to do with collision and everything to do
// with what a creature believes. Greedy steering asks which way the goal lies; a
// crate on the field lies THROUGH the port hull wall, so that is the way it goes,
// and it spends the rest of the run sidling along the outside of a ship it is
// trying to get out of. The collision is working perfectly the whole time.
//
// Both directions, because they fail independently, and because the version that
// decided the staging point once at assignment got the outbound leg right and the
// return leg wrong -- he crossed the threshold and his plan did not.
// ---------------------------------------------------------------------------
void checkRoomsAndDoors() {
    Scene s(0.0f, {0.0f, 0.0f}, 0.0f);

    const glm::vec3 pile = s.ship.bayStoragePoint();
    const glm::vec3 inBay = pile + s.r() * 2.0f;
    const glm::vec3 onField = s.ship.origin() + s.r() * 40.0f;   // straight out the side

    const bool insideIsInside = s.ground.enclosureAt(inBay) >= 0;
    const bool fieldIsOutside = s.ground.enclosureAt(onField) < 0;
    // Under the belly is outdoors: a creature there wants to walk out from under
    // the hull, not go looking for the door.
    const bool underIsOutside =
        s.ground.enclosureAt(s.ship.origin() + s.f() * 4.0f) < 0;
    report("the hold reads as a room, the field does not",
           insideIsInside && fieldIsOutside && underIsOutside,
           "bay inside, field outside, under the belly outside");

    // A ROUTE between the two goes through the door, in both directions.
    //
    // This used to assert which mark wayThrough handed back next, which was
    // testing the mechanism rather than the thing. The mechanism is gone -- the
    // marks fought the routes and the routes won -- and the thing it was for
    // survives unchanged, so it is asserted directly: a way from the bay to the
    // field passes the foot of the ramp, because there is nowhere else to pass.
    Biped man;
    const float maxRise = s.ground.spacing() * std::tan(glm::radians(man.params.maxSlopeDeg));
    const glm::vec3 foot = s.ship.rampFootPosition();

    auto throughTheDoor = [&](const glm::vec3& from, const glm::vec3& to) {
        std::vector<glm::ivec2> nodes;
        if (!s.ground.findRoute(s.ground.nodeNear(from), from.y, s.ground.nodeNear(to),
                                maxRise, man.params.bodyRadius, man.standHeight(), nodes)) {
            return 1e9f;
        }
        float nearest = 1e9f;
        for (const glm::ivec2& n : nodes) {
            const glm::vec3 p = s.terrain.worldAt(n);
            nearest = std::min(nearest, glm::length(glm::vec2(p.x - foot.x, p.z - foot.z)));
        }
        return nearest;
    };

    const float outbound = throughTheDoor(inBay, onField);
    const float inbound  = throughTheDoor(onField, inBay);

    char detail[128];
    std::snprintf(detail, sizeof detail, "out passes %.1f from the ramp foot, in passes %.1f",
                  outbound, inbound);
    report("routes between bay and field use the ramp", outbound < 6.0f && inbound < 6.0f, detail);

    // Same side of the wall as the goal: nothing to say about it at all.
    glm::vec3 way;
    bool shut = false;
    const bool direct = !s.ground.wayThrough(onField, onField + s.r() * 10.0f, way, shut) &&
                        !s.ground.wayThrough(inBay, pile, way, shut);
    report("no detour when there is no wall between", direct,
           "field to field and bay to bay are walked straight");

    // And a shut ramp is a shut door, not a slow one. The ramp only reaches the
    // ground at the very end of its travel, so half-open has to read as closed --
    // a way through you cannot step onto is not a way through.
    {
        Scene shutShip(0.0f, {0.0f, 0.0f}, 0.0f);
        shutShip.closeRamp();
        bool s1 = false, s2 = false;
        shutShip.ground.wayThrough(onField, pile, way, s1);
        shutShip.ground.wayThrough(inBay, onField, way, s2);
        report("a shut ramp reads as a shut door", s1 && s2,
               "reported closed from both sides rather than walked at");
    }
}

// And the same thing driven rather than queried: a whole round trip that starts
// in the hold, fetches something from the far side of the ship, and brings it
// back. Every leg of that crosses the threshold, and the version of this that
// decided the route once at assignment got exactly half of them right.
void checkBipedRoundTrip() {
    int done = 0, tried = 0;

    for (float yaw : {0.0f, 34.0f}) {
        Scene s(0.0f, {0.0f, 0.0f}, yaw);
        const glm::vec3 pile = s.ship.bayStoragePoint();
        const glm::vec3 muster = s.ship.rampApproachPoint();

        // Leg one: in from the field, so he ends up standing in the hold.
        Biped man;
        glm::vec3 first = muster - s.f() * 6.0f;
        first.y += 0.425f;
        man.reset(s.ground, glm::vec2(muster.x - s.f().x * 16.0f,
                                      muster.z - s.f().z * 16.0f), 0.0f, 1u);
        man.assignFetch(s.ground, first, pile);
        for (int i = 0; i < 60 * 90 && man.hasTask(); ++i) man.update(s.ground, 1.0f / 60.0f);

        if (s.ground.enclosureAt(man.hipCentre()) < 0) continue;   // never got in
        ++tried;

        // Leg two: from inside the hold, out to a crate off the beam and back.
        // The straight line to it runs through the hull wall in both directions.
        glm::vec3 out = s.ship.origin() + s.r() * 34.0f;
        out.y = s.terrain.heightAtWorld(out.x, out.z) + 0.425f;

        man.assignFetch(s.ground, out, pile);
        float nearestFoot = 1e9f;
        bool reachedIt = false;
        for (int i = 0; i < 60 * 120 && man.hasTask(); ++i) {
            man.update(s.ground, 1.0f / 60.0f);
            const glm::vec3 p = man.hipCentre();
            if (man.hasCargo()) reachedIt = true;
            nearestFoot = std::min(nearestFoot,
                glm::length(glm::vec2(p.x - s.ship.rampFootPosition().x,
                                      p.z - s.ship.rampFootPosition().z)));
        }

        const glm::vec3 end = man.hipCentre();
        const bool home = glm::length(glm::vec2(end.x - pile.x, end.z - pile.z)) < 5.0f &&
                          s.ground.enclosureAt(end) >= 0;
        if (reachedIt && home && nearestFoot < 4.0f) ++done;
    }

    char detail[128];
    std::snprintf(detail, sizeof detail,
                  "%d of %d went out of the hold, round the hull and back in by the ramp",
                  done, tried);
    report("the biped leaves and re-enters by the door", tried > 0 && done == tried, detail);
}

// Having finished, does he leave? Both of them, because they fail for opposite
// reasons and one of them does not fail at all.
//
// The walker's heading rule scores a direction by how much UNWALKED ground it
// opens, which says nothing inside a room he has just walked every node of: every
// direction scores zero, the tie-break picks at random, and he paces the bay --
// measured at 153 steps in a minute without finding the door. His stuck counter
// never moves the whole time, which is why it reads as deliberation rather than as
// a fault. The biped drifts and turns away from walls instead, and that happens to
// walk him out on its own; he is checked so it stays that way.
void checkTheyLeaveWhenDone() {
    int leftWalker = 0, leftBiped = 0, tried = 0;

    for (float yaw : {0.0f, 34.0f}) {
        Scene s(0.0f, {0.0f, 0.0f}, yaw);
        const glm::vec3 pile = s.ship.bayStoragePoint();
        const glm::vec3 muster = s.ship.rampApproachPoint();
        ++tried;

        {
            Walker w;
            w.reset(s.ground, glm::ivec2(50, 30), 0);
            glm::vec3 crate = muster - s.f() * 8.0f;
            crate.y = s.terrain.heightAtWorld(crate.x, crate.z);
            w.assignFetch(s.ground, crate, pile);
            for (int i = 0; i < 40000 && w.hasTask(); ++i) w.update(s.ground, 1.0f / 60.0f);

            // Delivered and idle. Thirty seconds is many times what the trip takes.
            for (int i = 0; i < 60 * 30; ++i) w.update(s.ground, 1.0f / 60.0f);
            if (s.ground.enclosureAt(w.bodyCentre(s.ground)) < 0) ++leftWalker;
        }

        {
            Biped man;
            glm::vec3 crate = muster - s.f() * 6.0f;
            crate.y += 0.425f;
            man.reset(s.ground, glm::vec2(muster.x - s.f().x * 16.0f,
                                          muster.z - s.f().z * 16.0f), 0.0f, 1u);
            man.assignFetch(s.ground, crate, pile);
            for (int i = 0; i < 60 * 90 && man.hasTask(); ++i) man.update(s.ground, 1.0f / 60.0f);

            for (int i = 0; i < 60 * 30; ++i) man.update(s.ground, 1.0f / 60.0f);
            if (s.ground.enclosureAt(man.hipCentre()) < 0) ++leftBiped;
        }
    }

    char detail[128];
    std::snprintf(detail, sizeof detail, "walker %d of %d, biped %d of %d, within thirty seconds",
                  leftWalker, tried, leftBiped, tried);
    report("both walk back out once the job is done",
           leftWalker == tried && leftBiped == tried, detail);
}

// The walker needs none of this and is checked anyway, because "it happens to be
// correct" and "it is known to be correct" are different states. Its route is a
// breadth-first search over steps it can actually take, so a wall is not
// something it has to be told about -- there is simply no route through one.
void checkWalkerLeavesByTheRamp() {
    Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
    const glm::vec3 pile = s.ship.bayStoragePoint();
    const glm::vec3 foot = s.ship.rampFootPosition();
    const glm::vec3 crate = s.ship.origin() + s.r() * 40.0f;

    // reset() drops onto the terrain by design, so he cannot be teleported into
    // the bay -- he has to walk in. Send him in with a crate first.
    Walker w;
    w.reset(s.ground, glm::ivec2(50, 30), 0);
    glm::vec3 first = s.ship.rampApproachPoint() - s.f() * 8.0f;
    first.y = s.terrain.heightAtWorld(first.x, first.z);
    w.assignFetch(s.ground, first, pile);
    for (int i = 0; i < 40000 && w.hasTask(); ++i) w.update(s.ground, 1.0f / 60.0f);

    const bool aboard = s.ship.isAboard(w.bodyCentre(s.ground));

    w.assignFetch(s.ground, crate, pile);
    float nearest = 1e9f;
    for (const glm::ivec2& n : w.path()) {
        const glm::vec3 p = s.terrain.worldAt(n);
        nearest = std::min(nearest, glm::length(glm::vec2(p.x - foot.x, p.z - foot.z)));
    }

    char detail[128];
    std::snprintf(detail, sizeof detail, "route out passes %.1f from the ramp foot", nearest);
    report("the walker routes out through the ramp",
           aboard && w.hasTask() && nearest < 6.0f, detail);
}

// ---------------------------------------------------------------------------
// 4c. The door is a moving floor, and a moving floor is its own kind of trouble.
// ---------------------------------------------------------------------------
void checkTheDoorMoving() {
    // Shutting it on somebody scooped them: two and a half units of lift and
    // seventeen units of travel in a second and a half, none of it walked. It
    // reverses instead, which also has to be checked FOR NOT DEADLOCKING -- the
    // first attempt held the ramp where it was, and a creature standing on a ramp
    // frozen halfway has no reason to go anywhere, so it stayed at eighty per cent
    // for as long as anyone watched.
    {
        Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
        Walker w;
        w.reset(s.ground, glm::ivec2(50, 30), 0);
        const glm::vec3 pile = s.ship.bayStoragePoint();
        w.assignFetch(s.ground, pile, pile);

        // Catch him partway up it.
        for (int i = 0; i < 40000 && w.hasTask(); ++i) {
            w.update(s.ground, 1.0f / 60.0f);
            const glm::vec3 p = w.bodyCentre(s.ground);
            if (s.ship.isOnRamp(p) && p.y > s.ship.origin().y + 1.0f) break;
        }
        const bool caught = s.ship.isOnRamp(w.bodyCentre(s.ground));

        s.ship.closeRamp();                   // shut it on him
        float lowest = 1e9f;
        for (int i = 0; i < 60 * 6; ++i) {
            s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(w.bodyCentre(s.ground)));
            s.republish();
            w.update(s.ground, 1.0f / 60.0f);
            if (s.ship.isOnRamp(w.bodyCentre(s.ground))) {
                lowest = std::min(lowest, s.ship.rampProgress());
            }
        }

        char detail[128];
        std::snprintf(detail, sizeof detail,
                      "told to shut with him on it, it fell to %.0f%% and came back to %.0f%%",
                      lowest * 100.0f, s.ship.rampProgress() * 100.0f);
        report("the ramp will not shut on anybody", caught && s.ship.rampProgress() > 0.9f, detail);
    }

    // Where you WAIT for a door must not be somewhere the door goes.
    //
    // The muster point used to be measured from the ramp's live foot, so it moved
    // with the door -- six units further forward with the ramp shut, which put it
    // squarely under the arc the ramp sweeps on its way open. Anything that waited
    // there for the door to open was standing exactly where the door was about to
    // land, by construction, and the more properly it waited the more reliably it
    // got pinned.
    {
        Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
        const glm::vec3 whenOpen = s.ship.rampApproachPoint();
        s.closeRamp();
        const glm::vec3 whenShut = s.ship.rampApproachPoint();

        const float moved = glm::length(whenOpen - whenShut);
        // And it has to be clear of where the tip comes to rest, not merely fixed.
        s.ship.toggleRamp();
        for (int i = 0; i < 400; ++i) { s.ship.update(1.0f / 60.0f); }
        const glm::vec3 tip = s.ship.rampFootPosition();
        const float clearance = glm::length(glm::vec2(whenOpen.x - tip.x, whenOpen.z - tip.z));

        char detail[128];
        std::snprintf(detail, sizeof detail,
                      "moves %.2f as the door works, and sits %.1f clear of the tip",
                      moved, clearance);
        report("the muster point does not move with the door",
               moved < 0.01f && clearance > 2.0f, detail);
    }

    // And the walker climbing out from under one that is coming down on him. He
    // cannot be shoved -- his feet are on lattice nodes two units apart, and being
    // moved half a node is not a thing that can happen to him -- so he steps out a
    // whole block at a time instead.
    {
        Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
        s.closeRamp();

        Walker w;
        w.reset(s.ground, glm::ivec2(50, 30), 0);
        const glm::vec3 pile = s.ship.bayStoragePoint();

        // Park him under the arc: the shut ramp's tip sweeps down through here.
        glm::vec3 under = s.ship.origin() - s.f() * 21.0f;
        under.y = s.terrain.heightAtWorld(under.x, under.z);
        w.assignFetch(s.ground, under, pile);
        for (int i = 0; i < 40000 && !w.hasCargo(); ++i) w.update(s.ground, 1.0f / 60.0f);

        const glm::vec3 before = w.bodyCentre(s.ground);

        s.ship.toggleRamp();                  // open it onto him
        for (int i = 0; i < 60 * 8; ++i) {
            s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(w.bodyCentre(s.ground)));
            s.republish();
            w.update(s.ground, 1.0f / 60.0f);
        }

        const glm::vec3 after = w.bodyCentre(s.ground);
        const float surface = s.ground.heightAt(after.x, after.z, 9.0f, 9.0f);
        const float feet = w.footHeight(0);

        // Free means: not standing on the ground with the slab over his head.
        const bool pinned = surface > feet + 0.5f;

        char detail[128];
        std::snprintf(detail, sizeof detail,
                      "moved %.1f while it came down, ended %s",
                      glm::length(after - before), pinned ? "PINNED" : "in the clear");
        report("an opening ramp does not pin the walker", !pinned, detail);
    }

    // A shut door, and the two creatures answering it completely differently --
    // which is the point, and is a property of their bodies rather than of their
    // programming. One has hands.
    {
        Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
        s.closeRamp();

        const glm::vec3 pile = s.ship.bayStoragePoint();
        const glm::vec3 muster = s.ship.rampApproachPoint();

        // ---- the walker: fetch it anyway, then wait, and wait SOMEWHERE SAFE --
        Walker w;
        w.reset(s.ground, glm::ivec2(50, 30), 0);
        glm::vec3 crate = muster - s.f() * 12.0f;
        crate.y = s.terrain.heightAtWorld(crate.x, crate.z);
        w.assignFetch(s.ground, crate, pile);

        for (int i = 0; i < 60 * 60; ++i) {
            s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(w.bodyCentre(s.ground)));
            s.republish();
            w.update(s.ground, 1.0f / 60.0f);
        }

        const glm::vec3 held = w.bodyCentre(s.ground);
        const bool stillHasIt = w.hasCargo();
        const bool atTheSpot = glm::length(glm::vec2(held.x - muster.x, held.z - muster.z)) < 5.0f;

        // And clear of where the door is about to land.
        s.ship.toggleRamp();
        for (int i = 0; i < 400; ++i) s.ship.update(1.0f / 60.0f);
        const glm::vec3 tip = s.ship.rampFootPosition();
        const bool clear = glm::length(glm::vec2(held.x - tip.x, held.z - tip.z)) > 2.0f;

        char detail[160];
        std::snprintf(detail, sizeof detail,
                      "after a minute: %sholding it, %s the spot, %.1f from where the tip lands",
                      stillHasIt ? "" : "NOT ", atTheSpot ? "at" : "NOT at",
                      glm::length(glm::vec2(held.x - tip.x, held.z - tip.z)));
        report("the walker waits out a shut ramp, safely",
               stillHasIt && atTheSpot && clear, detail);

        // Opened, he takes it in -- and does not drop it during the half second
        // where the door calls itself open but the route does not exist yet.
        bool delivered = false;
        for (int i = 0; i < 60 * 30 && !delivered; ++i) {
            s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(w.bodyCentre(s.ground)));
            s.republish();
            w.update(s.ground, 1.0f / 60.0f);
            const glm::vec3 p = w.bodyCentre(s.ground);
            if (!w.hasCargo() && s.ground.enclosureAt(p) >= 0) delivered = true;
        }
        report("and takes it in when the ramp opens", delivered,
               "kept hold of it through the moment the door reports open but is not walkable");
    }

    {
        // ---- the biped: go and work the control -----------------------------
        Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
        s.closeRamp();

        const glm::vec3 pile = s.ship.bayStoragePoint();
        const glm::vec3 muster = s.ship.rampApproachPoint();

        Biped man;
        glm::vec3 crate = muster - s.f() * 8.0f;
        crate.y += 0.425f;
        man.reset(s.ground, glm::vec2(muster.x - s.f().x * 18.0f,
                                      muster.z - s.f().z * 18.0f), 0.0f, 1u);
        man.assignFetch(s.ground, crate, pile);

        bool delivered = false;
        for (int i = 0; i < 60 * 90 && !delivered; ++i) {
            // The scene notices him at the panel, exactly as it notices the
            // player there. He does not reach across the field and open a ship.
            if (man.wayShut() && !s.ship.isOpening() &&
                glm::length(man.hipCentre() - s.ship.controlPosition()) < 9.0f) {
                s.ship.toggleRamp();
            }
            s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(man.hipCentre()));
            s.republish();
            man.update(s.ground, 1.0f / 60.0f);
            if (!man.hasCargo() && s.ground.enclosureAt(man.hipCentre()) >= 0) delivered = true;
        }

        char detail[128];
        std::snprintf(detail, sizeof detail, "ramp finished at %.0f%%, crate %s",
                      s.ship.rampProgress() * 100.0f, delivered ? "delivered" : "NOT delivered");
        report("the biped opens the ramp himself", delivered, detail);
    }

    // The control does what the ramp visibly needs, from any state it can be in.
    //
    // It used to flip a bool, and the bool was not only the player's: the
    // interlock sets it to opening whenever something stands on a closing ramp,
    // and the biped sets it when he wants in. So a press could arrive with the
    // intent already inverted and do the exact opposite of the obvious thing --
    // press to open a shut ramp, and shut it again. From the outside that looks
    // like the walker waiting at a door that will not open, forever, which is
    // precisely what he should do and precisely what you cannot debug.
    {
        Scene s(0.0f, {0.0f, 0.0f}, 0.0f);

        // Shut, pressed: opens.
        s.closeRamp();
        s.ship.toggleRamp();
        for (int i = 0; i < 400; ++i) s.ship.update(1.0f / 60.0f);
        const bool openedFromShut = s.ship.rampProgress() > 0.9f;

        // Open, pressed: shuts.
        s.ship.toggleRamp();
        for (int i = 0; i < 400; ++i) s.ship.update(1.0f / 60.0f);
        const bool shutFromOpen = s.ship.rampProgress() < 0.1f;

        // And shut, pressed, AFTER the interlock has been flipping the intent
        // about behind the player's back. This is the one that failed.
        s.ship.openRamp();
        for (int i = 0; i < 60; ++i) s.ship.update(1.0f / 60.0f, true);   // held by somebody
        s.ship.closeRamp();
        for (int i = 0; i < 400; ++i) s.ship.update(1.0f / 60.0f);
        s.ship.toggleRamp();
        for (int i = 0; i < 400; ++i) s.ship.update(1.0f / 60.0f);
        const bool openedAfterMeddling = s.ship.rampProgress() > 0.9f;

        char detail[128];
        std::snprintf(detail, sizeof detail, "shut->open %d, open->shut %d, and %d after the interlock meddled",
                      (int)openedFromShut, (int)shutFromOpen, (int)openedAfterMeddling);
        report("the ramp control is never inverted",
               openedFromShut && shutFromOpen && openedAfterMeddling, detail);
    }

    // The inner control: reachable from the deck, and enough to let a creature
    // shut inside let itself out again.
    {
        Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
        Biped man;
        const float H = man.standHeight(), R = man.params.bodyRadius;
        const Enclosure e = s.ship.enclosure();

        // Somewhere you can actually stand, inside the room, clear of the wall
        // the panel is bolted to. A button in a solid is not a button.
        const bool standable =
            !s.ground.blocked(e.insideControl.x, e.insideControl.z, s.deckY(), H, R) &&
            s.ground.enclosureAt(glm::vec3(e.insideControl.x, s.deckY() + 0.1f,
                                           e.insideControl.z)) >= 0;

        // And within arm's reach of the panel it works.
        const float reach = glm::length(e.insideControl - s.ship.innerControlPosition());

        char detail[128];
        std::snprintf(detail, sizeof detail,
                      "standing spot is %s, %.1f from the panel",
                      standable ? "clear and inside the hold" : "NOT USABLE", reach);
        report("the bay has a control you can stand at", standable && reach < 3.0f, detail);
    }

    // Shut in with a job outside, he lets himself out. Until there was a panel on
    // the inside this was not something he could do at all -- he could only stand
    // at the door and wait for somebody else, which is still the walker's lot.
    {
        Scene s(0.0f, {0.0f, 0.0f}, 0.0f);
        const glm::vec3 pile = s.ship.bayStoragePoint();
        const glm::vec3 muster = s.ship.rampApproachPoint();

        // Get him aboard first, by delivering something.
        Biped man;
        glm::vec3 first = muster - s.f() * 6.0f;
        first.y += 0.425f;
        man.reset(s.ground, glm::vec2(muster.x - s.f().x * 16.0f,
                                      muster.z - s.f().z * 16.0f), 0.0f, 1u);
        man.assignFetch(s.ground, first, pile);
        for (int i = 0; i < 60 * 90 && man.hasTask(); ++i) man.update(s.ground, 1.0f / 60.0f);

        const bool aboard = s.ground.enclosureAt(man.hipCentre()) >= 0;
        s.closeRamp();                       // shut him in

        // Now send him for something outside.
        glm::vec3 out = s.ship.origin() + s.r() * 30.0f;
        out.y = s.terrain.heightAtWorld(out.x, out.z) + 0.425f;
        man.assignFetch(s.ground, out, pile);

        bool escaped = false;
        for (int i = 0; i < 60 * 120 && !escaped; ++i) {
            if (man.wayShut() && !s.ship.isOpening()) {
                const glm::vec3 at = man.hipCentre();
                if (glm::length(at - s.ship.controlPosition()) < 9.0f ||
                    glm::length(at - s.ship.innerControlPosition()) < 9.0f) {
                    s.ship.openRamp();
                }
            }
            s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(man.hipCentre()));
            s.republish();
            man.update(s.ground, 1.0f / 60.0f);
            if (s.ground.enclosureAt(man.hipCentre()) < 0 && s.ship.rampProgress() > 0.9f) {
                escaped = true;
            }
        }

        char detail[128];
        std::snprintf(detail, sizeof detail, "aboard %d, then out under his own power %d",
                      (int)aboard, (int)escaped);
        report("shut in, the biped lets himself out", aboard && escaped, detail);
    }

    // And swinging OPEN through somebody stood at the foot of it. That one is
    // allowed to shove -- it is a slab coming down and there is nowhere else for
    // him to be -- but it has to shove all of him. Moving a body without its
    // planted feet strands them, the legs stretch to reach ground he is no longer
    // over, the solver clamps, and the hips are dragged down to what the legs can
    // still touch. He ended at ground level with his feet somewhere behind him.
    {
        Scene closed(0.0f, {0.0f, 0.0f}, 0.0f);
        closed.closeRamp();

        const glm::vec3 muster = closed.ship.rampApproachPoint();
        Biped man;
        man.reset(closed.ground, glm::vec2(muster.x, muster.z), 0.0f, 1u);
        man.setHold(true);                    // stand there and take it

        const float standing = man.hipCentre().y;
        float lowestHip = 1e9f;

        closed.ship.toggleRamp();
        for (int i = 0; i < 60 * 6; ++i) {
            closed.ship.update(1.0f / 60.0f, closed.ship.isOnRamp(man.hipCentre()));
            closed.republish();
            man.update(closed.ground, 1.0f / 60.0f);
            lowestHip = std::min(lowestHip, man.hipCentre().y);
        }

        char detail[128];
        std::snprintf(detail, sizeof detail,
                      "hips stayed at %.2f, never below %.2f", standing, lowestHip);
        report("opening it shoves people clear, not into the dirt",
               lowestHip > standing - 0.6f, detail);
    }
}

// ---------------------------------------------------------------------------
// 5. He can be SENT aboard, not merely get there.
//
// His routes were planned on the terrain once, which put the cargo deck inside a
// solid and made every route to the pile come back NO ROUTE -- he would fetch a
// crate perfectly well and have nowhere to take it. Driven end to end rather than
// asked whether a route exists, because "can a route be planned" is a query and
// the bugs live in the loop.
// ---------------------------------------------------------------------------
void checkRoutesAndDeliveries() {
    {
        Scene s(11.0f, {34.0f, -18.0f}, 34.0f);
        const glm::vec3 pile = s.ship.bayStoragePoint();

        auto routes = [&](const glm::vec3& to) {
            Walker w;
            w.reset(s.ground, glm::ivec2(40, 40), 0);
            w.assignFetch(s.ground, to, pile);
            return w.hasTask();
        };

        const bool all = routes(glm::vec3(-20.0f, 0.0f, 10.0f)) && routes(pile)
                      && routes(s.ship.rampApproachPoint());
        report("routes plan to the field AND the deck", all,
               "a crate, the pile up on the deck, and the muster point");
    }

    int routable = 0, delivered = 0, stranded = 0;
    for (int trial = 0; trial < 12; ++trial) {
        Scene s(11.0f, {34.0f, -18.0f}, 34.0f);
        const glm::vec3 pile = s.ship.bayStoragePoint();

        Walker w;
        w.setSeed(1u + trial * 7919u);
        w.reset(s.ground, glm::ivec2(18 + (trial * 5) % 40, 20 + (trial * 11) % 40), trial % 4);

        // Sited on ground flat enough to set a box down on, the way the game
        // sites them. A crate dropped across the ridge is unreachable BY DESIGN
        // -- that is what pathFailed is for -- so it is not counted against the
        // pathfinder here.
        const glm::vec3 here = w.bodyCentre(s.ground);
        glm::vec3 crate = here + (s.ship.origin() - here) * 0.45f;
        crate.y = s.terrain.heightAtWorld(crate.x, crate.z);

        w.assignFetch(s.ground, crate, pile);
        if (!w.hasTask()) continue;
        ++routable;

        bool hadCargo = false;
        for (int step = 0; step < 40000 && w.hasTask(); ++step) {
            w.update(s.ground, 1.0f / 60.0f);
            if (w.hasCargo()) hadCargo = true;
        }

        const glm::vec3 end = w.bodyCentre(s.ground);
        const float toPile = glm::length(glm::vec2(end.x - pile.x, end.z - pile.z));
        if (hadCargo && toPile < 6.0f && end.y > s.deckY() - 1.0f) ++delivered;
        else ++stranded;
    }

    char detail[128];
    std::snprintf(detail, sizeof detail, "%d of %d routable runs ended at the pile, %d stranded",
                  delivered, routable, stranded);
    report("whole deliveries finish on the deck",
           routable > 0 && delivered == routable, detail);
}

} // namespace

// ---------------------------------------------------------------------------
// 5b. The rally: everybody in, to their OWN station, and the hold sealed.
//
// Stations are numbered rather than "somewhere in the bay" because a crew of
// five has to line up without treading on each other, and because a spot a unit
// was SENT to is one the scene can ask whether it reached. "Near the ship" is
// not a state anything can be sure of, which is exactly the property a launch
// sequence needs before it shuts a door.
// ---------------------------------------------------------------------------
void checkTheRally() {
    Scene s(0.0f, {0.0f, 0.0f}, 34.0f);
    const glm::vec3 o = s.ship.origin();

    // Well out on the field, in different directions, so neither of them is
    // simply already there.
    Walker w;
    w.reset(s.ground, s.ground.nodeNear(o + glm::vec3(60.0f, 0.0f, 45.0f)) - glm::ivec2(1), 0);
    Biped man;
    man.reset(s.ground, glm::vec2(o.x - 55.0f, o.z + 35.0f), 0.0f, 3u);

    const glm::vec3 stationB = s.ship.stationPosition(0);
    const glm::vec3 stationW = s.ship.stationPosition(1);
    const float apart = glm::length(glm::vec2(stationB.x - stationW.x, stationB.z - stationW.z));

    man.orderTo(s.ground, stationB);
    w.orderTo(s.ground, stationW);

    bool closing = false;
    int sealedAt = -1;
    for (int i = 0; i < 60 * 180; ++i) {
        const bool busy = s.ship.isOnRamp(man.hipCentre()) ||
                          s.ship.isOnRamp(w.bodyCentre(s.ground));
        s.ship.update(1.0f / 60.0f, busy);
        s.republish();
        w.update(s.ground, 1.0f / 60.0f);
        man.update(s.ground, 1.0f / 60.0f);

        const bool bothIn = w.onStation() && man.onStation() &&
                            s.ground.enclosureAt(w.bodyCentre(s.ground)) >= 0 &&
                            s.ground.enclosureAt(man.hipCentre()) >= 0;
        if (bothIn && !closing) { s.ship.closeRamp(); closing = true; }
        if (closing && s.ship.rampProgress() <= 0.001f) { sealedAt = i; break; }
    }

    const glm::vec3 bp = man.hipCentre();
    const glm::vec3 wp = w.bodyCentre(s.ground);
    const float crewApart = glm::length(glm::vec2(bp.x - wp.x, bp.z - wp.z));

    char detail[176];
    std::snprintf(detail, sizeof detail,
                  "stations %.1f apart, crew ended %.1f apart, sealed after %.0fs",
                  apart, crewApart, sealedAt >= 0 ? sealedAt / 60.0f : -1.0f);
    report("a rally brings them in and seals the hold",
           sealedAt >= 0 && man.onStation() && w.onStation() && crewApart > 2.5f, detail);
}

// ---------------------------------------------------------------------------
// 5c. Flight: it lifts, it follows the ground, and everything aboard comes too.
//
// Carrying passengers is the whole of the difficulty. Everything standing in the
// hold is at a WORLD position, and a moving deck makes every one of those wrong
// at once -- the biped's planted feet most of all, since a foot staying put
// while the hips travel over it is the basis of the entire walk and exactly the
// wrong instinct on a floor that is going somewhere.
// ---------------------------------------------------------------------------
void checkFlight() {
    Scene s(0.0f, {0.0f, 0.0f}, 34.0f);

    Biped rider;
    const glm::vec3 st = s.ship.stationPosition(0);
    rider.reset(s.ground, glm::vec2(st.x, st.z), 0.0f, 5u);
    glm::vec3 crate = s.ship.stationPosition(3);

    // Measured in the SHIP's axes. Comparing world offsets across a flight that
    // turns compares two different frames and calls the difference drift.
    auto localTo = [](const Ship& sh, const glm::vec3& p) {
        const glm::vec3 d = p - sh.origin();
        return glm::vec3(glm::dot(d, sh.right()), d.y, glm::dot(d, sh.forward()));
    };
    const glm::vec3 riderWas = localTo(s.ship, rider.hipCentre());
    const glm::vec3 crateWas = localTo(s.ship, crate);
    const glm::vec3 from = s.ship.origin();

    s.closeRamp();
    s.ship.setAirborne(true);

    float lowest = 1e9f;
    float airborneAt = -1.0f;
    for (int i = 0; i < 60 * 45; ++i) {
        s.ship.fly(1.0f / 60.0f, 1.0f, 0.35f, i < 60 * 4 ? 1.0f : 0.0f, s.terrain);

        const glm::vec3 move = s.ship.lastMove();
        const float spun = s.ship.lastTurn();
        const glm::vec3 about = s.ship.origin() - move;
        const float a = glm::radians(spun);
        const float sn = std::sin(a), cs = std::cos(a);
        const glm::vec3 d = crate - about;
        crate = about + glm::vec3(d.x * cs + d.z * sn, d.y, -d.x * sn + d.z * cs) + move;
        rider.carry(move, spun, about);
        s.republish();

        // Clearance is what it HOLDS, not what it has while leaving the ground.
        // It lifts off at its climb rate rather than appearing at altitude, so
        // the first half-second is legitimately low; when it gets there is the
        // separate thing worth knowing.
        const float above = s.ship.heightAboveGround(s.terrain);
        if (airborneAt < 0.0f && above >= s.ship.flight.clearance - 0.05f) {
            airborneAt = i / 60.0f;
        }
        if (airborneAt >= 0.0f) lowest = std::min(lowest, above);
    }

    const float riderDrift = glm::length(glm::vec2(
        localTo(s.ship, rider.hipCentre()).x - riderWas.x,
        localTo(s.ship, rider.hipCentre()).z - riderWas.z));
    const float crateDrift = glm::length(glm::vec2(
        localTo(s.ship, crate).x - crateWas.x,
        localTo(s.ship, crate).z - crateWas.z));
    // Net displacement, not distance flown -- it is turning throughout, so it
    // flies a circuit and comes most of the way back. Asserting on path length
    // would be the honest measure; asserting it ended a long way from where it
    // started asserts that it flew in a straight line, which it was never asked
    // to do.
    const float went = glm::length(glm::vec2(s.ship.origin().x - from.x,
                                             s.ship.origin().z - from.z));

    // And the player. He is the one thing the module cannot move itself -- the
    // host owns where he is -- so what is checked here is that the module would
    // ASK to move him, and that it only asks while he is actually aboard. A
    // player left standing on the pad watching his ship leave is what happens
    // when nobody asks.
    {
        Scene p(0.0f, {0.0f, 0.0f}, 0.0f);
        const glm::vec3 inHold = p.ship.stationPosition(2);
        const glm::vec3 onField = p.ship.origin() - p.f() * 60.0f;

        const bool aboardCounts = p.ground.enclosureAt(inHold) >= 0;
        const bool ashoreDoesNot = p.ground.enclosureAt(
            glm::vec3(onField.x, p.terrain.heightAtWorld(onField.x, onField.z), onField.z)) < 0;

        report("the ship knows who is standing in it", aboardCounts && ashoreDoesNot,
               "a spot on the deck is aboard, a spot on the field is not");
    }

    char detail[176];
    std::snprintf(detail, sizeof detail,
                  "off the ground in %.2fs, circuit put it %.0f from the pad, "
                  "cleared %.1f at worst, rider drifted %.2f, crate %.2f",
                  airborneAt, went, lowest, riderDrift, crateDrift);
    report("it flies, and the hold comes with it",
           went > 40.0f && airborneAt >= 0.0f && airborneAt < 2.0f &&
           lowest >= s.ship.flight.clearance - 0.5f &&
           riderDrift < 1.0f && crateDrift < 1.0f &&
           s.ground.enclosureAt(rider.hipCentre()) >= 0, detail);
}

// ---------------------------------------------------------------------------
// 5c-i. The walker can board however the ship is pointing.
//
// Every other check about getting aboard puts the ship at one of a handful of
// convenient headings, and the module lands it at thirty-four degrees, so this
// went unnoticed: on twenty of seventy-two headings the walker could not board at
// ALL. Not slowly, not sometimes -- there was no route, and there never would be.
// The biped managed every one of them.
//
// It mattered the moment the ship could fly, because a ship that flies comes down
// facing wherever it was last pointing, and a heading nobody chose is exactly the
// kind nobody had tried.
//
// The cause was in goodStep, and the ramp was innocent. A step's midpoint was
// tested with the body standing at the height of the higher END of the step --
// which is below the floor when the floor between them is higher than both, and
// the ramp's top face is deliberately set a sixteenth of a unit proud of the deck
// so the two do not z-fight. So the last step of the climb, ramp to deck, put his
// soles inside the slab he was standing on and was refused. It only bit when the
// ramp lay oblique to his lattice, because only then are his two front feet at
// different heights on it and only then does the geometry come that close.
//
// Cheap enough to ask at every heading: it is one route plan each, no simulation.
// The diagonals are walked properly as well, since a route existing and a machine
// following it are different claims.
// ---------------------------------------------------------------------------
void checkEveryHeading() {
    int headings = 0, noRoute = 0;
    float worstYaw = -1.0f;

    for (int yaw = 0; yaw < 360; yaw += 5) {
        Scene s(0.0f, {0.0f, 0.0f}, static_cast<float>(yaw));
        const glm::vec3 from = s.ship.rampApproachPoint() - s.f() * 14.0f;
        ++headings;

        for (int st = 0; st < s.ship.stationCount(); ++st) {
            Walker w;
            w.reset(s.ground, s.ground.nodeNear(from), 0);
            w.orderTo(s.ground, s.ship.stationPosition(st));
            if (w.pathFailed()) {
                ++noRoute;
                if (worstYaw < 0.0f) worstYaw = static_cast<float>(yaw);
                break;
            }
        }
    }

    char detail[176];
    std::snprintf(detail, sizeof detail, "%d headings, %d with no route aboard%s",
                  headings, noRoute, noRoute ? " (first at some yaw)" : "");
    report("the walker can board at any ship heading", noRoute == 0, detail);

    // And the diagonals actually walked, not merely planned.
    int walked = 0, arrived = 0;
    for (float yaw : {45.0f, 135.0f, 225.0f, 315.0f}) {
        Scene s(0.0f, {0.0f, 0.0f}, yaw);
        const glm::vec3 station = s.ship.stationPosition(1);
        Walker w;
        w.reset(s.ground, s.ground.nodeNear(s.ship.rampApproachPoint() - s.f() * 14.0f), 0);
        w.setHome(s.ship.origin(), 250.0f);
        w.orderTo(s.ground, station);
        ++walked;

        for (int i = 0; i < 60 * 120; ++i) {
            s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(w.bodyCentre(s.ground)));
            s.republish();
            w.update(s.ground, 1.0f / 60.0f);
            if (w.onStation() && s.ground.enclosureAt(w.bodyCentre(s.ground)) >= 0) {
                ++arrived;
                break;
            }
        }
    }

    char walkDetail[128];
    std::snprintf(walkDetail, sizeof walkDetail,
                  "%d of %d diagonals walked aboard and mustered", arrived, walked);
    report("and walks aboard on the diagonals", arrived == walked, walkDetail);
}

// ---------------------------------------------------------------------------
// 5c-ii. A rally works from every way he can be standing when it is called.
//
// Reported as "sometimes the walker gets stuck at the foot of the ramp, three or
// four times now". Sometimes is not a case you can sit down and reproduce, so it
// is not tested as one: this runs every approach on a grid around the ramp foot,
// at several ship headings and every walker heading, and counts the ones that
// stop. Before the fix, 28 of these stalled with a perfectly good route in hand.
//
// Two things were wrong, and both are about a route being a list of HEADINGS
// rather than of places -- "north, north, east" is only true from the block it
// was planned at:
//
//   - escapeIfBuried moves him out from under the closing ramp, two nodes
//     sideways, and left the plan alone. He then walked a heading list counted
//     from a block he was no longer standing on, which near a ship means into
//     the hull. It throws the plan away now.
//   - a routed step that had become impossible was counted and returned from,
//     forever. It gets a few ticks' grace and then the route goes.
// ---------------------------------------------------------------------------
void checkRallyApproaches() {
    int tried = 0, stalled = 0, worstSteps = 0;
    float worstDistance = 0.0f;

    for (float yaw : {0.0f, 34.0f, 90.0f}) {
        for (float back : {4.0f, 12.0f}) {
            for (float across = -6.0f; across <= 6.0f; across += 2.0f) {
                for (int heading = 0; heading < 4; heading += 2) {
                    Scene s(0.0f, {0.0f, 0.0f}, yaw);
                    const glm::vec3 station = s.ship.stationPosition(1);
                    const glm::vec3 from = s.ship.rampApproachPoint()
                                         - s.f() * (back - 6.0f) + s.r() * across;

                    Walker w;
                    w.reset(s.ground, s.ground.nodeNear(from), heading);
                    w.setHome(s.ship.origin(), 250.0f);
                    w.orderTo(s.ground, station);

                    ++tried;
                    bool aboard = false;
                    for (int i = 0; i < 60 * 90; ++i) {
                        s.ship.update(1.0f / 60.0f,
                                      s.ship.isOnRamp(w.bodyCentre(s.ground)));
                        s.republish();
                        w.update(s.ground, 1.0f / 60.0f);
                        if (w.onStation() &&
                            s.ground.enclosureAt(w.bodyCentre(s.ground)) >= 0) {
                            aboard = true;
                            break;
                        }
                    }

                    if (aboard) continue;
                    ++stalled;
                    const glm::vec3 at = w.bodyCentre(s.ground);
                    const float d = glm::length(glm::vec2(at.x - station.x,
                                                          at.z - station.z));
                    if (d > worstDistance) { worstDistance = d; worstSteps = w.steps(); }
                }
            }
        }
    }

    char detail[176];
    std::snprintf(detail, sizeof detail,
                  "%d approaches, %d stalled%s", tried, stalled,
                  stalled ? " -- worst stopped after some steps far from the deck" : "");
    (void)worstSteps;
    report("a rally works from wherever he is standing", stalled == 0, detail);
}

// ---------------------------------------------------------------------------
// 5c-iii. And he never claims to be somewhere he is not.
//
// Giving up is allowed; reporting success is not. The give-up branch used to
// latch onStation() whatever the distance, so a walker who could not plan a
// route stood out on the dirt saying he was aboard, and the launch sequence
// waited on a hold he was never going to be in. That failure is indistinguish-
// able from a slow walk, which is the worst kind: the thing looks like it is
// still working.
// ---------------------------------------------------------------------------
void checkStationHonesty() {
    // Forty-five degrees, where the ramp lies diagonally across his lattice and
    // he genuinely cannot get up it -- see the note on checkTheDiagonalRamp. A
    // station he cannot reach is exactly the case the give-up branch handles, so
    // it is the case worth asking him about.
    Scene s(0.0f, {0.0f, 0.0f}, 45.0f);

    Walker w;
    const glm::vec3 o = s.ship.origin();
    const glm::vec3 station = s.ship.stationPosition(1);
    w.reset(s.ground, s.ground.nodeNear(o - s.f() * 40.0f), 0);
    w.setHome(o, 250.0f);
    w.orderTo(s.ground, station);

    for (int i = 0; i < 60 * 60; ++i) {
        s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(w.bodyCentre(s.ground)));
        s.republish();
        w.update(s.ground, 1.0f / 60.0f);
    }

    const glm::vec3 at = w.bodyCentre(s.ground);
    const float away = glm::length(glm::vec2(at.x - station.x, at.z - station.z));
    const bool lying = w.onStation() && away > 3.0f;

    char detail[176];
    std::snprintf(detail, sizeof detail,
                  "sent to a station he cannot reach: ended %.1f away, "
                  "says on station %d, says stalled %d",
                  away, (int)w.onStation(), (int)w.stationStalled());
    report("he does not report a station he never reached", !lying, detail);
}

// ---------------------------------------------------------------------------
// 5d. The player rides too -- driven through the MODULE, in the host's order.
//
// Everything above tests the content. This tests the thing the content is bolted
// into, and it is a different kind of check: it stands a pretend player on the
// deck and runs TessaraModule::update / setPlayerPosition / carriedPlayer in the
// exact order terrain_editor calls them, including the snap-to-ground the
// scripted controller does every frame.
//
// It exists because the bug it catches was invisible from every other angle. The
// content was right -- ship, deck, enclosure, the aboard test, the carry maths,
// all correct and all covered. What was wrong was WHEN two of them ran: the
// passenger list was read after the ship had moved, and Ground still described
// the pad for one frame after take-off. The player was snapped back down onto a
// deck that had left, and from the next frame he was too far below it to count as
// aboard. Reasoning about it produced three wrong answers; running it produced
// the number in one go.
// ---------------------------------------------------------------------------
void checkThePlayerRides() {
    // Flat, and with the fixed bounds a saved level has -- a terrain that reports
    // no extent gives the module a two-node lattice and a crew that cannot walk.
    eden::TerrainConfig cfg;
    cfg.heightScale = 0.0f;
    cfg.useFixedBounds = true;
    eden::Terrain terrain(cfg);

    TessaraModule mod;
    mod.initialize();
    mod.setTerrain(&terrain);
    mod.onEnterPlayMode();

    const Ship& ship = mod.ship();
    constexpr float kEye = 1.7f;   // the host reports the CAMERA, not the feet

    // Standing on a muster station, which is the middle of the hold.
    glm::vec3 eye = ship.stationPosition(0) + glm::vec3(0.0f, kEye, 0.0f);
    mod.setPlayerPosition(eye);
    const bool aboardOnDeck = mod.playerAboard();

    // One frame of terrain_editor's play-mode loop.
    auto frame = [&] {
        constexpr float dt = 1.0f / 60.0f;
        mod.update(dt);
        mod.setPlayerPosition(eye);

        glm::vec3 move(0.0f), about(0.0f);
        float spun = 0.0f;
        const bool carried = mod.carriedPlayer(move, spun, about);
        if (carried) {
            const float a = glm::radians(spun);
            const glm::vec3 d = eye - about;
            eye = about + glm::vec3(d.x * std::cos(a) + d.z * std::sin(a), d.y,
                                    -d.x * std::sin(a) + d.z * std::cos(a)) + move;
        }

        // ...and the scripted controller's snap-to-ground, which is what sets his
        // height every frame and which pinned him to the old deck.
        float h = 0.0f;
        if (mod.groundHeight(eye.x, eye.z, eye.y - kEye, h)) eye.y = h + kEye;
        return carried;
    };

    mod.callRally();
    int sealedAt = -1;
    for (int i = 0; i < 60 * 120; ++i) {
        frame();
        if (mod.launchState() == TessaraModule::Launch::Ready) { sealedAt = i; break; }
    }

    const float deckWas = ship.origin().y + ship.params.deckHeight;
    const float stoodAt = (eye.y - kEye) - deckWas;

    int carriedFrames = 0, total = 60 * 10;
    float rose = 0.0f;
    if (sealedAt >= 0) {
        mod.launch();
        for (int i = 0; i < total; ++i) if (frame()) ++carriedFrames;
        rose = ship.origin().y - (deckWas - ship.params.deckHeight);
    }

    const float deckNow = ship.origin().y + ship.params.deckHeight;
    const float standsAt = (eye.y - kEye) - deckNow;

    char detail[192];
    std::snprintf(detail, sizeof detail,
                  "sealed after %.0fs, rose %.1f, carried %d/%d frames, "
                  "stood %+.2f above the deck and still does at %+.2f",
                  sealedAt >= 0 ? sealedAt / 60.0f : -1.0f, rose,
                  carriedFrames, total, stoodAt, standsAt);

    report("it takes the player with it",
           aboardOnDeck && sealedAt >= 0 && rose > 1.0f &&
           carriedFrames >= total - 2 && std::fabs(standsAt - stoodAt) < 0.25f,
           detail);

    // ...and brings him back down. Same seam, opposite direction, and exactly the
    // kind of thing that works one way round and not the other -- the launch left
    // him on the pad for two orderings that only bite while the deck is rising.
    if (sealedAt < 0) return;

    int downFrames = 0;
    mod.land();
    for (int i = 0; i < 60 * 60; ++i) {
        // Nobody at the helm and no keys held, so this is the ugliest descent
        // available: hands off, all the way down. It is the carry that is on trial
        // here, not the airmanship, and an unflown drop is the harshest version of
        // it -- the deck moves fastest, so anything that fails to keep up fails
        // here first.
        if (frame()) ++downFrames;
        if (mod.launchState() != TessaraModule::Launch::Flying) break;
    }

    const float rested = ship.origin().y;
    const float deckDown = rested + ship.params.deckHeight;
    const float onDeck = (eye.y - kEye) - deckDown;

    char downDetail[192];
    std::snprintf(downDetail, sizeof downDetail,
                  "came down over %d carried frames, touched at %.1f/s, "
                  "stands %+.2f above the deck, aboard %d",
                  downFrames, ship.touchdownSpeed(), onDeck, (int)mod.playerAboard());

    // AND the walker is still on his station, in the hold, after all that.
    //
    // Reported twice: "the walker still does not land with the ship". The check that
    // drives the raw Walker passes, so if this one fails the fault is in the module's
    // wiring rather than in the handover -- which is exactly the seam a check built
    // out of the parts cannot see.
    {
        const Walker& w = mod.walker();
        const glm::vec3 him = w.bodyCentre(*mod.ground());
        const float out = glm::length(glm::vec2(him.x - ship.origin().x,
                                                him.z - ship.origin().z));
        const glm::vec3 station = ship.stationPosition(1);
        const float offStation = glm::length(glm::vec2(him.x - station.x,
                                                       him.z - station.z));

        // ALL FOUR FEET on the deck, and level with each other.
        //
        // "Near the ship and in the hold" was not enough to ask. A block straddling
        // the deck edge puts two feet on the deck and two through it onto the terrain
        // two units below, which skews the body frame enough to draw him upside down
        // and strands him under his own hull -- and it passes every test that only
        // looks at where his middle is. The spread between his feet is the number
        // that would have caught it.
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 4; ++i) {
            lo = std::min(lo, w.footHeight(i));
            hi = std::max(hi, w.footHeight(i));
        }
        const float spread = hi - lo;
        const float deck = ship.origin().y + ship.params.deckHeight;

        char wdet[224];
        std::snprintf(wdet, sizeof wdet,
                      "after landing: %.1f from the ship, %.1f from his station, "
                      "room %d, parked %d, on station %d | feet spread %.2f, "
                      "%.2f off the deck",
                      out, offStation, mod.ground()->enclosureAt(him),
                      (int)w.parked(), (int)w.onStation(), spread, lo - deck);
        report("the walker lands with the ship, on his station",
               out < 14.0f && offStation < 4.0f && !w.parked() &&
               mod.ground()->enclosureAt(him) >= 0 && w.onStation() &&
               spread < 0.3f && std::fabs(lo - deck) < 0.5f, wdet);
    }

    report("and brings the player back down",
           !ship.airborne() && downFrames > 30 && std::fabs(onDeck) < 0.25f &&
           mod.playerAboard(),
           downDetail);
}

// ---------------------------------------------------------------------------
// 5e. The captain turns with the bow.
//
// Two yaws, counted in opposite directions, and nothing in either file says so:
// Camera::updateVectors builds its front as (cos yaw, ., sin yaw), and Ship
// builds its forward as (sin yaw, ., cos yaw). One is the other reflected, so a
// camera yaw is ninety degrees minus a ship yaw and their DELTAS have opposite
// signs. The host applies the ship's turn to the camera, which meant pressing A
// swung the bow to port and the captain's head to starboard.
//
// This runs the host's rule against a real eden::Camera rather than restating
// it: turn the ship, apply the correction, and ask whether the man at the helm
// is still looking where the ship is going.
// ---------------------------------------------------------------------------
void checkTheHelmView() {
    Scene s(0.0f, {0.0f, 0.0f}, 34.0f);

    // Facing the way the ship is. atan2(x, z) is the ship's convention; the
    // camera's own yaw is what makes its front come out that way.
    eden::Camera cam;
    const glm::vec3 bowWas = s.ship.forward();
    cam.setYaw(90.0f - s.ship.yawDegrees());

    const float aligned = glm::dot(glm::normalize(glm::vec3(cam.getFront().x, 0.0f,
                                                            cam.getFront().z)), bowWas);

    s.ship.setAirborne(true);
    float swung = 0.0f;
    glm::vec3 bowEarly = bowWas;
    for (int i = 0; i < 60 * 6; ++i) {
        s.ship.fly(1.0f / 60.0f, 0.6f, -1.0f, 0.0f, s.terrain);   // hard to port, as A does
        const float spun = s.ship.lastTurn();
        swung += spun;
        if (std::fabs(spun) > 1e-5f) cam.setYaw(cam.getYaw() - spun);   // the host's rule

        // Which WAY it went is only answerable inside half a turn -- six seconds
        // of hard rudder is most of a circle, and a bearing that has come back
        // round says nothing about which side it left on.
        if (i == 29) bowEarly = s.ship.forward();
    }

    const glm::vec3 bowNow = s.ship.forward();
    const glm::vec3 look = glm::normalize(glm::vec3(cam.getFront().x, 0.0f, cam.getFront().z));
    const float still = glm::dot(look, bowNow);

    // ...and that it went to PORT, which is the half the sign error would keep
    // getting right by accident if we only checked that they agreed.
    const float toPort = glm::dot(glm::cross(glm::vec3(0, 1, 0), bowWas), bowEarly);

    char detail[176];
    std::snprintf(detail, sizeof detail,
                  "started aligned %.3f, swung %.0f to port, still aligned %.3f",
                  aligned, -swung, still);
    report("the captain turns with the bow",
           aligned > 0.999f && std::fabs(swung) > 20.0f && toPort < 0.0f && still > 0.999f,
           detail);
}

// ---------------------------------------------------------------------------
// 5f. It comes down, and coming down is something you can do badly.
//
// The hover is a clearance the ship is not allowed through, so landing is that
// floor being let go of -- and once it is, the only thing holding it up is the
// lift being asked for. Which means the descent has to be a real fall with a real
// rate, and the test of that is not that it lands: it is that a landing flown
// badly arrives hard and one flown well does not. A "physics" landing that cannot
// be botched is an animation with extra steps.
//
// Three descents from the same height, differing only in what the pilot does:
// nothing at all, a late grab, and a proper flare.
// ---------------------------------------------------------------------------
void checkTheLanding() {
    // The pilot, as a descent rate he is willing to accept. Nobody flares at a
    // height -- they flare when the ground is coming up too fast -- and a
    // fixed-height trigger is a bang-bang controller that can hold a hover
    // instead of landing, which tests the controller rather than the ship.
    struct Run { const char* name; float wants; float from; };
    const Run runs[] = {
        { "hands off",  -1.0f, 999.0f },   // never touch it
        { "late grab",   4.0f,   6.0f },   // right rate, noticed far too late
        { "flared",      4.0f, 999.0f },   // flown all the way down
    };

    float speeds[3] = {0.0f, 0.0f, 0.0f};
    bool  landed[3] = {false, false, false};
    float rode[3]   = {0.0f, 0.0f, 0.0f};
    int   which = 0;

    for (const Run& run : runs) {
        Scene s(0.0f, {0.0f, 0.0f}, 34.0f);

        // Somebody in the hold, so the descent is also asked whether it brings
        // people down as well as taking them up.
        Biped rider;
        const glm::vec3 st = s.ship.stationPosition(0);
        rider.reset(s.ground, glm::vec2(st.x, st.z), 0.0f, 5u);

        auto localTo = [](const Ship& sh, const glm::vec3& p) {
            const glm::vec3 d = p - sh.origin();
            return glm::vec3(glm::dot(d, sh.right()), d.y, glm::dot(d, sh.forward()));
        };
        const glm::vec3 riderWas = localTo(s.ship, rider.hipCentre());

        s.closeRamp();
        s.ship.setAirborne(true);

        // Up to a working height first, so there is a descent to fly. Carrying
        // him on the way up too, or the drift measured at the end is the climb.
        for (int i = 0; i < 60 * 8; ++i) {
            s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, 1.0f, s.terrain);
            rider.carry(s.ship.lastMove(), s.ship.lastTurn(),
                        s.ship.origin() - s.ship.lastMove());
            s.republish();
        }
        const float from = s.ship.heightAboveGround(s.terrain);

        s.ship.beginLanding();
        for (int i = 0; i < 60 * 60 && s.ship.airborne(); ++i) {
            const float agl  = s.ship.heightAboveGround(s.terrain);
            const float rate = -s.ship.verticalSpeed();
            const float lift = (run.wants > 0.0f && agl < run.from && rate > run.wants)
                             ? 1.0f : 0.0f;

            s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, lift, s.terrain);
            rider.carry(s.ship.lastMove(), s.ship.lastTurn(),
                        s.ship.origin() - s.ship.lastMove());
            s.republish();
        }

        landed[which] = !s.ship.airborne();
        speeds[which] = s.ship.touchdownSpeed();
        rode[which] = glm::length(localTo(s.ship, rider.hipCentre()) - riderWas);
        if (g_verbose) {
            std::printf("      %-10s from %.0f up: touched down at %.1f/s (%s), "
                        "rider off by %.2f\n",
                        run.name, from, speeds[which],
                        s.ship.landedHard() ? "hard" : "clean", rode[which]);
        }
        ++which;
    }

    Scene ref(0.0f, {0.0f, 0.0f}, 34.0f);
    const float hardAt = ref.ship.flight.hardAt;

    char detail[192];
    std::snprintf(detail, sizeof detail,
                  "hands off %.1f/s, late grab %.1f/s, flared %.1f/s (hard above %.0f)",
                  speeds[0], speeds[1], speeds[2], hardAt);

    report("it lands, and a bad landing is a bad landing",
           landed[0] && landed[1] && landed[2] &&
           speeds[0] > hardAt &&              // untouched, it arrives hard
           speeds[2] < hardAt &&              // flown down, it does not
           speeds[2] < speeds[1] &&           // and flying it earlier is better
           rode[0] < 0.5f && rode[2] < 0.5f,  // and the hold came down too
           detail);

    // The ramp has to reach the ground it landed ON, not the pad it left.
    {
        Scene s(0.0f, {0.0f, 0.0f}, 34.0f);
        s.closeRamp();
        s.ship.setAirborne(true);
        for (int i = 0; i < 60 * 6; ++i) s.ship.fly(1.0f / 60.0f, 1.0f, 0.0f, 1.0f, s.terrain);
        s.ship.beginLanding();
        for (int i = 0; i < 60 * 60 && s.ship.airborne(); ++i) {
            const float rate = -s.ship.verticalSpeed();
            s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, rate > 4.0f ? 1.0f : 0.0f, s.terrain);
        }
        s.ship.openRamp();
        for (int i = 0; i < 300; ++i) s.ship.update(1.0f / 60.0f);
        s.republish();

        const glm::vec3 tip = s.ship.rampFootPosition();
        const float under = s.terrain.heightAtWorld(tip.x, tip.z);
        const float gap = tip.y - under;

        char rampDetail[160];
        std::snprintf(rampDetail, sizeof rampDetail,
                      "tip finished %+.2f from the ground it landed on", gap);
        report("the ramp reaches the ground it landed on",
               !s.ship.airborne() && gap < 0.4f && gap > -1.0f, rampDetail);
    }
}

// ---------------------------------------------------------------------------
// 5e-ii. A crate only reaches the pile if the CARRIER reached the pile.
//
// Reported as "the walker is delivering packages without even going inside, he
// just gets close to the ship and they teleport there". Two causes, both mine.
//
// Three activities read `m_pathIndex >= m_path.size()` as HAVING ARRIVED, because
// an empty route used to only ever mean a walked one. Then the stall recovery
// started throwing stale routes away -- so discarding a route told the hauling code
// he was standing at the pile, and the crate was filed there from wherever he was.
// It replans to the same goal now instead of discarding.
//
// And the module trusted the creature's word: task over and carrying, therefore
// delivered. A task can end for reasons that are not arrival. Now the crate goes
// where the CARRIER is, and only the pile counts as the pile.
//
// Checked by watching every crate for a teleport: a stored crate whose carrier was
// never near the pile is the bug, however it got there.
// ---------------------------------------------------------------------------
void checkNoTeleportedCrates() {
    Scene s(0.0f, {0.0f, 0.0f}, 34.0f);

    Walker w;
    const glm::vec3 o = s.ship.origin();
    w.reset(s.ground, s.ground.nodeNear(o - s.f() * 46.0f), 0);
    w.setHome(o, 250.0f);

    const glm::vec3 pile = s.ship.bayStoragePoint();
    glm::vec3 crate = o - s.f() * 52.0f + s.r() * 8.0f;
    crate.y = s.terrain.heightAtWorld(crate.x, crate.z) + 0.42f;

    w.assignFetch(s.ground, crate, pile);

    int teleports = 0, delivered = 0;
    float nearestWhenFiled = 1e9f;
    bool wasCarrying = false;

    for (int i = 0; i < 60 * 180; ++i) {
        s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(w.bodyCentre(s.ground)));
        s.republish();
        w.update(s.ground, 1.0f / 60.0f);

        const glm::vec3 at = w.bodyCentre(s.ground);
        if (w.hasCargo()) crate = w.cargoPosition(s.ground);

        // The moment he stops carrying, where was he? That is the only question.
        if (wasCarrying && !w.hasCargo()) {
            const float toPile = glm::length(glm::vec2(at.x - pile.x, at.z - pile.z));
            nearestWhenFiled = std::min(nearestWhenFiled, toPile);
            if (toPile < 6.0f) ++delivered; else ++teleports;
            break;
        }
        wasCarrying = w.hasCargo();
    }

    char detail[176];
    std::snprintf(detail, sizeof detail,
                  "let go of the crate %.1f from the pile: %d delivered, %d filed "
                  "from nowhere near it",
                  nearestWhenFiled, delivered, teleports);
    report("a crate only reaches the pile if he does", teleports == 0, detail);
}

// ---------------------------------------------------------------------------
// 5e-iv. The pre-landing warning tells the truth.
//
// The ship already says whether the GEAR can take a site. It could not say whether
// the RAMP could, and that is the question that strands people: flat enough on top
// to stand on, a cliff at the back, and you only learn it once you are parked.
//
// A warning is only worth having if it agrees with what happens. So this hovers
// over a site, asks what the ramp WOULD manage, then lands and measures what it
// actually managed. The two have to be the same number -- a prediction computed
// differently from the thing it predicts is worse than no prediction, which is why
// both go through Ship::fitRamp.
// ---------------------------------------------------------------------------
void checkTheLandingForecast() {
    int sites = 0, wrong = 0, warned = 0;
    float worstError = 0.0f;

    for (float relief : {8.0f, 22.0f}) {
        for (float yaw : {0.0f, 70.0f, 155.0f, 250.0f}) {
            Scene s(relief, {0.0f, 0.0f}, yaw);

            s.closeRamp();
            s.ship.setAirborne(true);
            for (int i = 0; i < 60 * 2; ++i)
                s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, 1.0f, s.terrain);
            for (int i = 0; i < 60 * 1; ++i)
                s.ship.fly(1.0f / 60.0f, 1.0f, 0.0f, 0.0f, s.terrain);

            const glm::vec3 o = s.ship.origin();
            if (std::fabs(o.x) > 82.0f || std::fabs(o.z) > 82.0f) continue;

            // Asked while still in the air.
            const float forecast = s.ship.rampGapIfLandedHere(s.terrain);
            if (forecast > 0.9f) ++warned;

            s.ship.beginLanding();
            for (int i = 0; i < 60 * 60 && s.ship.airborne(); ++i) {
                const float rate = -s.ship.verticalSpeed();
                s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, rate > 4.0f ? 1.0f : 0.0f, s.terrain);
            }
            if (s.ship.airborne()) continue;
            ++sites;

            s.ship.openRamp();
            s.ship.autoRampExtension();
            for (int i = 0; i < 500; ++i) s.ship.update(1.0f / 60.0f);
            const float actual = s.ship.rampGap(s.terrain);

            const float err = std::fabs(actual - forecast);
            worstError = std::max(worstError, err);
            if (err > 0.5f) ++wrong;
        }
    }

    char detail[176];
    std::snprintf(detail, sizeof detail,
                  "%d sites, %d of them warned about: forecast off by at most %.2f, "
                  "%d disagreed with what happened",
                  sites, warned, worstError, wrong);
    report("the pre-landing warning matches the landing", sites > 0 && wrong == 0,
           detail);
}

// ---------------------------------------------------------------------------
// 5e-iii. There is a way back aboard, and it is not next to the ramp.
//
// A landing site can pass every test the ship makes -- flat enough on top to stand
// on -- and still be a cliff at the back, so the ramp opens onto air. Get out of a
// ship parked like that and there is no way back into the ship you need in order to
// move it. That is a dead end, not a difficulty, and it is the one failure the
// player cannot work around.
//
// So there is a ladder, and where it is matters as much as that it exists: at the
// BRIDGE end, the far end from the ramp, so it is a way back aboard rather than a
// second door. What is checked here is that you can stand at its foot -- a ladder
// inside the hull, or up a cliff, is no ladder -- and that it reaches the deck.
// ---------------------------------------------------------------------------
void checkTheBoardingLadder() {
    int placements = 0, unreachable = 0, tooCloseToRamp = 0, wrongHeight = 0;
    float nearestToRamp = 1e9f;

    for (float yaw : {0.0f, 34.0f, 115.0f, 200.0f, 300.0f}) {
        for (float relief : {0.0f, 14.0f}) {
            Scene s(relief, {0.0f, 0.0f}, yaw);
            ++placements;

            const glm::vec3 foot = s.ship.ladderFoot();

            // Standable: on the ground outside the hull, not inside anything.
            const float ground = s.terrain.heightAtWorld(foot.x, foot.z);
            if (s.ground.blocked(foot.x, foot.z, ground, 1.9f, 0.40f)) ++unreachable;

            // Far from the ramp, or it is just another door.
            const glm::vec3 tip = s.ship.rampFootPosition();
            const float toRamp = glm::length(glm::vec2(foot.x - tip.x, foot.z - tip.z));
            nearestToRamp = std::min(nearestToRamp, toRamp);
            if (toRamp < 14.0f) ++tooCloseToRamp;

            // And it goes to the deck, which is where the bridge is.
            if (std::fabs(s.ship.ladderTopY() - s.deckY()) > 0.01f) ++wrongHeight;
        }
    }

    char detail[192];
    std::snprintf(detail, sizeof detail,
                  "%d placements: %d with no room to stand at the foot, %d too near "
                  "the ramp (nearest %.1f), %d not reaching the deck",
                  placements, unreachable, tooCloseToRamp, wrongHeight, nearestToRamp);
    report("there is a way back aboard, away from the ramp",
           unreachable == 0 && tooCloseToRamp == 0 && wrongHeight == 0, detail);
}

// ---------------------------------------------------------------------------
// 5f-i. The hull stops a body, from every side and at every height.
//
// Reported as "I often fall through the wall of the ship", and it was not a
// collision bug: GameModule::resolvePosition existed, TessaraModule implemented
// it, and NOTHING EVER CALLED IT. The hull has never been solid to the player.
//
// The old checks asked whether Ground::blocked answers correctly at points, and it
// always did -- which is why none of them caught this. What was missing is the
// thing a player does: keep pushing. So this walks a body INTO the hull a step at a
// time, applying the push-out every step exactly as the host now does, and asks
// whether it ends up inside. A query cannot see this; only the loop can.
// ---------------------------------------------------------------------------
void checkTheHullStopsYou() {
    Scene s(0.0f, {0.0f, 0.0f}, 34.0f);

    // A player's body: soles on the deck or the dirt, a bit under two units tall,
    // shoulder's width across -- the numbers the host passes.
    const float height = 1.90f, radius = 0.40f;

    int tried = 0, gotIn = 0;
    float deepest = 0.0f;

    // From all round the ship, at deck height and at ground height, walked straight
    // at the centre. Two heights because the hull is a belly below the deck and a
    // bay above it, and they are different solids.
    for (int a = 0; a < 24; ++a) {
        const float ang = a * (6.2831853f / 24.0f);
        const glm::vec3 dir(std::cos(ang), 0.0f, std::sin(ang));

        for (int level = 0; level < 2; ++level) {
            const float footY = level ? s.deckY() : s.ship.origin().y;
            glm::vec2 at(s.ship.origin().x - dir.x * 40.0f,
                         s.ship.origin().z - dir.z * 40.0f);
            ++tried;

            // Walk in, half a unit at a time, resolving every step. The ramp is shut
            // so there is no legitimate way in.
            s.closeRamp();
            for (int step = 0; step < 120; ++step) {
                at += glm::vec2(dir.x, dir.z) * 0.5f;
                glm::vec2 fixed = s.ground.resolve(at, footY, height, radius);
                at = fixed;
            }

            // Inside the bay, or inside the belly? Either is through the hull.
            const glm::vec3 here(at.x, footY, at.y);
            if (s.ground.blocked(at.x, at.y, footY, height, radius)) {
                ++gotIn;
            } else if (level == 1 && s.ground.enclosureAt(here + glm::vec3(0, 0.5f, 0)) >= 0) {
                ++gotIn;   // standing in the sealed hold, having walked in from outside
            }

            const float into = glm::length(glm::vec2(at.x - s.ship.origin().x,
                                                     at.y - s.ship.origin().z));
            deepest = std::max(deepest, s.ship.params.length * 0.5f - into);
        }
    }

    char detail[176];
    std::snprintf(detail, sizeof detail,
                  "%d approaches from all round, at deck and ground height: %d got "
                  "inside", tried, gotIn);
    report("the hull stops a body pushing into it", gotIn == 0, detail);
}

// ---------------------------------------------------------------------------
// 5f-ii. Fly somewhere and land, and everything that rode along is still there.
//
// Reported: "the walker goes along for the whole ride but when we land where does
// he disappear to? also the cargo tray doesn't come with us -- the boxes do but
// the pallet is left behind."
//
// One cause twice. Both were positions RECORDED once instead of derived from the
// ship:
//
//   The walker is parked for the flight -- his feet are lattice indices and a
//   moving deck is not on the lattice, so the drawing follows the ship while the
//   feet stop being updated. unpark() dropped the flag and nothing else, which
//   put him back on the nodes he boarded from. Fly a mile, land a mile from him.
//
//   The pile's spot was read from the ship once at placement. The crates ride
//   because carryPassengers moves anything inside the enclosure; the tray they sit
//   on was not in the enclosure's list of anything, so it stayed on the old pad.
// ---------------------------------------------------------------------------
void checkEverythingLands() {
    Scene s(0.0f, {0.0f, 0.0f}, 34.0f);

    // A walker standing in the hold, and a crate stacked on the pile.
    // Out on the field, and he walks in. reset() drops him on the TERRAIN by
    // design -- "being set down inside the ship is not a thing anyone asks for" --
    // so resetting him at the station puts him under the hull, boxed in, and he
    // never boards. The first version of this check did that and then measured a
    // walker who had never been aboard to carry.
    Walker w;
    const glm::vec3 station = s.ship.stationPosition(1);
    w.reset(s.ground, s.ground.nodeNear(s.ship.origin() - s.f() * 40.0f), 0);
    w.setHome(s.ship.origin(), 250.0f);
    w.orderTo(s.ground, station);
    for (int i = 0; i < 60 * 120; ++i) {
        s.ship.update(1.0f / 60.0f, s.ship.isOnRamp(w.bodyCentre(s.ground)));
        s.republish();
        w.update(s.ground, 1.0f / 60.0f);
        if (w.onStation() && s.ground.enclosureAt(w.bodyCentre(s.ground)) >= 0) break;
    }
    const bool startedAboard = s.ground.enclosureAt(w.bodyCentre(s.ground)) >= 0;

    // And the biped, who rides a different way: he is continuous rather than
    // lattice-locked, so he is not parked -- carryPassengers moves his hips and his
    // planted feet together every frame. Never tested across a LANDING though, and
    // landing is where the ground under him changes: his feet are at world
    // positions and the new terrain is not the old terrain.
    Biped man;
    const glm::vec3 st = s.ship.stationPosition(0);
    man.reset(s.ground, glm::vec2(st.x, st.z), 0.0f, 5u);
    man.setHome(s.ship.origin(), 250.0f);
    const bool bipedStartedAboard = s.ground.enclosureAt(man.hipCentre()) >= 0;

    auto localTo = [](const Ship& sh, const glm::vec3& p) {
        const glm::vec3 d = p - sh.origin();
        return glm::vec3(glm::dot(d, sh.right()), d.y, glm::dot(d, sh.forward()));
    };
    const glm::vec3 bipedWas = localTo(s.ship, man.hipCentre());

    glm::vec3 pallet = s.ship.bayStoragePoint();
    glm::vec3 crate = pallet + glm::vec3(0.0f, 0.42f, 0.0f);
    const glm::vec3 from = s.ship.origin();

    // Up, a long way off, and down again -- carrying everything the module carries.
    s.closeRamp();
    s.ship.setAirborne(true);
    w.parkIn(s.ground, s.ship.origin(), s.ship.right(), s.ship.forward());

    auto flyOne = [&](float forward, float turn, float lift) {
        s.ship.fly(1.0f / 60.0f, forward, turn, lift, s.terrain);
        const glm::vec3 move = s.ship.lastMove();
        const float spun = s.ship.lastTurn();
        const glm::vec3 about = s.ship.origin() - move;
        const float a = glm::radians(spun);
        const float sn = std::sin(a), cs = std::cos(a);
        auto shift = [&](glm::vec3 p) {
            const glm::vec3 d = p - about;
            return about + glm::vec3(d.x * cs + d.z * sn, d.y, -d.x * sn + d.z * cs) + move;
        };
        crate = shift(crate);
        w.parkFollow(s.ship.origin(), s.ship.right(), s.ship.forward());
        man.carry(move, spun, about);

        // The leash travels with the ship, exactly as TessaraModule's
        // carryPassengers does. Without this the check itself reproduces the third
        // instance of the same bug: home is a position recorded once, so after
        // flying a hundred and twenty units both creatures are outside their range
        // and set off back to where the ship USED to be. The module already gets
        // this right; it was the check that did not.
        w.setHome(s.ship.origin(), 250.0f);
        man.setHome(s.ship.origin(), 250.0f);
        s.republish();
    };

    // Far enough to prove the point, and no further: this field is 256 units
    // across, so a flight of 245 puts the ship at the edge and the walker's block
    // gets clamped to the boundary rather than landing under the hull. The first
    // version of this check did exactly that and blamed the code.
    for (int i = 0; i < 60 * 2; ++i)  flyOne(1.0f, 0.0f, 1.0f);
    for (int i = 0; i < 60 * 3; ++i)  flyOne(1.0f, 0.5f, 0.0f);

    const float flew = glm::length(glm::vec2(s.ship.origin().x - from.x,
                                             s.ship.origin().z - from.z));

    s.ship.beginLanding();
    for (int i = 0; i < 60 * 60 && s.ship.airborne(); ++i) {
        const float rate = -s.ship.verticalSpeed();
        flyOne(0.0f, 0.0f, rate > 4.0f ? 1.0f : 0.0f);
    }

    // Down. Hand the walker back to the lattice where he actually is.
    w.unpark(s.ground, s.ship.origin(), s.ship.right(), s.ship.forward());
    s.republish();

    // Then let them both STAND there for a while. Arriving in the hold and staying
    // in it are different claims: the ground under them is new, the biped solves
    // his legs against whatever he can now reach, and a creature that lands
    // correctly and then falls through the deck or gets shoved off it has still
    // been left behind, just more slowly.
    const glm::vec3 bipedOnTouchdown = localTo(s.ship, man.hipCentre());
    const float carriedDrift = glm::length(glm::vec2(bipedOnTouchdown.x - bipedWas.x,
                                                     bipedOnTouchdown.z - bipedWas.z));
    const bool inAtTouchdown = s.ground.enclosureAt(man.hipCentre()) >= 0;
    const float bipedAfterLanding = bipedOnTouchdown.y;

    // The ramp comes down before they are left to their own devices, because that
    // is what the module does -- the biped works the panel when the way is shut and
    // the module opens it. Sealed in with nobody to open it he does something else
    // entirely, and that is a separate bug rather than this one: see the note under
    // "a sealed hold is not a wall" below.
    s.ship.openRamp();
    for (int i = 0; i < 60 * 3; ++i) {
        s.ship.update(1.0f / 60.0f, false);
        s.republish();
        w.update(s.ground, 1.0f / 60.0f);
        man.update(s.ground, 1.0f / 60.0f, nullptr);
    }

    const glm::vec3 bipedNow = localTo(s.ship, man.hipCentre());
    const float bipedDrift = glm::length(glm::vec2(bipedNow.x - bipedWas.x,
                                                   bipedNow.z - bipedWas.z));
    const float bipedSank = bipedAfterLanding - bipedNow.y;

    // NEAR the ship, not aboard it. He has no orders once he is down, so he
    // wanders, and with the ramp open wandering off it is correct -- the ship has
    // landed and there is a planet to look at. What matters is that he is HERE
    // rather than a hundred and twenty units back at the launch pad, which is the
    // thing that was broken.
    const bool bipedStillIn =
        glm::length(glm::vec2(man.hipCentre().x - s.ship.origin().x,
                              man.hipCentre().z - s.ship.origin().z)) < 40.0f;
    const bool walkerStillIn =
        glm::length(glm::vec2(w.bodyCentre(s.ground).x - s.ship.origin().x,
                              w.bodyCentre(s.ground).z - s.ship.origin().z)) < 40.0f;

    // The same question after a real flight: are all four feet on the deck and
    // level with each other, or is he straddling its edge with two through it?
    float wlo = 1e9f, whi = -1e9f;
    for (int i = 0; i < 4; ++i) {
        wlo = std::min(wlo, w.footHeight(i));
        whi = std::max(whi, w.footHeight(i));
    }
    const float walkerSpread = whi - wlo;

    const glm::vec3 landedPallet = s.ship.bayStoragePoint();
    const glm::vec3 him = w.bodyCentre(s.ground);
    const float walkerFromShip = glm::length(glm::vec2(him.x - s.ship.origin().x,
                                                       him.z - s.ship.origin().z));
    const float palletFromShip = glm::length(glm::vec2(landedPallet.x - s.ship.origin().x,
                                                       landedPallet.z - s.ship.origin().z));
    const float crateOffPallet = glm::length(glm::vec2(crate.x - landedPallet.x,
                                                       crate.z - landedPallet.z));

    char detail[224];
    std::snprintf(detail, sizeof detail,
                  "flew %.0f | walker %.1f out, feet spread %.2f, still in %d | "
                  "biped: carried drift "
                  "%.2f (in the hold %d at touchdown), wandered %.2f in 3s (still here %d), "
                  "sank %.2f | pallet %.1f, crate %.1f off",
                  flew, walkerFromShip, walkerSpread, (int)walkerStillIn,
                  carriedDrift, (int)inAtTouchdown, bipedDrift, (int)bipedStillIn,
                  bipedSank, palletFromShip, crateOffPallet);
    std::printf("      biped local: was (%.1f,%.1f,%.1f) -> now (%.1f,%.1f,%.1f), "
                "ramp %.2f, hull is %.0f long x %.0f wide\n",
                bipedWas.x, bipedWas.y, bipedWas.z, bipedNow.x, bipedNow.y, bipedNow.z,
                s.ship.rampProgress(), s.ship.params.length, s.ship.params.width);

    report("nothing is left behind when it lands",
           startedAboard && bipedStartedAboard && flew > 40.0f &&
           s.ground.enclosureAt(him) >= 0 && walkerFromShip < 14.0f &&
           walkerSpread < 0.3f && walkerStillIn && bipedStillIn &&
           carriedDrift < 0.5f && inAtTouchdown &&
           std::fabs(bipedSank) < 0.6f &&
           palletFromShip < 14.0f && crateOffPallet < 1.0f, detail);
}

// ---------------------------------------------------------------------------
// 5f-iii. The ramp extension reaches the ground the fixed ramp could not.
//
// The fixed ramp stops short on a downhill site and cannot be steepened out of it:
// reach is length x sin(angle), and past about 24 degrees the biped's router will
// not cross the slope, so a steeper ramp is one nothing walks up -- he simply stops
// delivering. Two attempts to solve the angle proved that the hard way.
//
// So the LENGTH grows instead. At the nominal 21 degrees another eight units of ramp
// is nearly three more units of drop at exactly the same grade underfoot, which is
// the one axis that was still free.
//
// It ROTATES now as well, and that needed the biped's step-up raised from 0.90 to
// 1.5 first: his router is given it as the rise it may climb per two-unit node, so
// it capped any ramp he would cross at 24 degrees while the nominal is already 21.
// That one number is why every earlier attempt at this ended with him refusing to
// deliver. At 1.5 he crosses 37, so the ramp may lay itself to 33.
//
// Two things have to be true together, and the second is the one worth guarding:
// the gap closes, AND the ramp never lays itself steeper than a route will cross.
// A ramp that reaches by becoming a wall has solved nothing.
// ---------------------------------------------------------------------------
void checkTheRampExtension() {
    int sites = 0, helped = 0, stillShort = 0, neverLanded = 0, offField = 0;
    float worstBefore = 0.0f, worstAfter = 0.0f, steepest = 0.0f;

    for (float relief : {10.0f, 24.0f}) {
        for (glm::vec2 at : {glm::vec2(0.0f, 0.0f), glm::vec2(46.0f, -34.0f)}) {
            for (float yaw : {0.0f, 95.0f, 190.0f, 285.0f}) {
                Scene s(relief, at, yaw);

                // Landed away from the pad it levelled for itself, which is the only
                // place the ground behind the ship is not flat.
                s.closeRamp();
                s.ship.setAirborne(true);
                // Far enough off the levelled pad to be on real ground, and no
                // further: seven seconds at twenty-six units a second is a hundred
                // and eighty, and this field is two hundred and fifty-six across.
                // Fly off the edge and the ship never lands, which reads as a
                // thirty-unit ramp gap and is nothing of the kind.
                for (int i = 0; i < 60 * 2; ++i)
                    s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, 1.0f, s.terrain);
                for (int i = 0; i < 60 * 1; ++i)
                    s.ship.fly(1.0f / 60.0f, 1.0f, 0.0f, 0.0f, s.terrain);
                s.ship.beginLanding();
                for (int i = 0; i < 60 * 60 && s.ship.airborne(); ++i) {
                    const float rate = -s.ship.verticalSpeed();
                    s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, rate > 4.0f ? 1.0f : 0.0f, s.terrain);
                }
                ++sites;

                // It has to have LANDED for any of the rest to mean anything. One
                // site did not, and the check went green anyway with a worst-case
                // "ramp gap" of 105 units in its own summary -- which is a ship
                // hovering, not a ramp falling short. A number that absurd sitting
                // inside a passing test is the test failing to notice.
                if (s.ship.airborne()) { ++neverLanded; continue; }

                // ...and the ramp tip has to be ON the field. Off the edge,
                // heightAtWorld answers for ground that does not exist and rampGap
                // comes back as a hundred units -- which is what left a 105 in a
                // passing summary twice. Measuring off the end of the world is not
                // a failure of the ramp.
                //
                // The whole SHIP, not just the tip: the hull reaches nineteen units
                // past its centre and groundUnderHull samples along all of it. Sample
                // outside the heightfield and it answers with something that is not
                // a height -- one site "landed" at y 97.59 on terrain spanning about
                // twelve units, on phantom ground, and reported a 105-unit ramp gap
                // that survived two attempts to guard against it.
                {
                    const glm::vec3 o = s.ship.origin();
                    const float edge = 128.0f - 46.0f;   // half the field, less the hull
                    if (std::fabs(o.x) > edge || std::fabs(o.z) > edge) {
                        ++offField;
                        continue;
                    }
                }

                // Ramp down, extension stowed: the old behaviour.
                s.ship.setRampExtension(0.0f);
                s.ship.openRamp();
                for (int i = 0; i < 400; ++i) s.ship.update(1.0f / 60.0f);
                s.republish();
                const float before = s.ship.rampGap(s.terrain);
                const float angleBefore = s.ship.openAngleDegrees();

                // Now roll it out as far as the ground asks.
                s.ship.autoRampExtension();
                for (int i = 0; i < 400; ++i) s.ship.update(1.0f / 60.0f);
                s.republish();
                const float after = s.ship.rampGap(s.terrain);

                // Angle and extension are chosen together, so it is already final
                // here; what matters is that it stayed inside what a route crosses.
                (void)angleBefore;
                steepest = std::max(steepest, s.ship.openAngleDegrees());
                if (before > 12.0f && g_verbose) {
                    const glm::vec3 tip = s.ship.rampFootPosition();
                    std::printf("      ODD site relief %.0f yaw %.0f: ship y %.2f, tip "
                                "(%.1f,%.2f,%.1f), terrain there %.2f, gap %.2f\n",
                                relief, yaw, s.ship.origin().y, tip.x, tip.y, tip.z,
                                s.terrain.heightAtWorld(tip.x, tip.z), before);
                }
                worstBefore = std::max(worstBefore, before);
                worstAfter = std::max(worstAfter, after);

                // Only sites the fixed ramp actually failed on count as helped;
                // somewhere it already reached, the extension has nothing to do.
                if (before > 0.5f) {
                    if (after < before - 0.3f) ++helped; else ++stillShort;
                }
            }
        }
    }

    char detail[224];
    std::snprintf(detail, sizeof detail,
                  "%d sites (%d unlanded, %d off-field): worst gap %.2f stowed -> "
                  "%.2f out, %d of %d short ones closed, steepest %.1f deg and a "
                  "route crosses %.1f",
                  sites, neverLanded, offField, worstBefore, worstAfter, helped,
                  helped + stillShort, steepest,
                  glm::degrees(std::atan(Biped().params.stepUp / 2.0f)));

    report("the ramp reaches what it could not, and stays walkable",
           neverLanded == 0 && helped > 2 && stillShort == 0 &&
           worstAfter < worstBefore - 0.3f &&
           steepest <= glm::degrees(std::atan(Biped().params.stepUp / 2.0f)) + 0.01f,
           detail);
}

// ---------------------------------------------------------------------------
// 5g. Set down on a slope, all four feet find the ground.
//
// Reported from a screenshot: the ship resting on a hillside with daylight under
// its two downhill struts. The hull rests on the HIGHEST ground beneath it -- it
// has to, or the uphill end is buried -- so on any slope the other legs have
// ground to find below that, and they were simply stopping at the hull's plane.
//
// The hull deliberately does NOT tilt. Every Blocker in it is an upright box with
// a floor and a ceiling, the deck is a level patch, and the walker stands on a
// world-aligned lattice; pitching the ship invalidates all three at once. The gear
// compensates instead, which is what gear is for -- and it keeps the deck walkable
// and the crates where they were put.
//
// Tested on real relief, because a flat field cannot fail this and every other
// landing check runs on one.
// ---------------------------------------------------------------------------
void checkTheLandingGear() {
    int sites = 0, allDown = 0, reached = 0, tooSteep = 0, phantom = 0;
    float worstFoot = 0.0f, worstGap = -1e9f, mostSpread = 0.0f;

    // Several places on a rolling field, and several headings at each, so the ramp
    // points up some slopes and down others.
    for (float relief : {8.0f, 22.0f}) {
        for (glm::vec2 at : {glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, -30.0f),
                             glm::vec2(-52.0f, 44.0f)}) {
            for (float yaw : {0.0f, 70.0f, 155.0f, 250.0f}) {
                Scene s(relief, at, yaw);
                ++sites;

                // As LANDED, and landed SOMEWHERE ELSE. place() levels a pad
                // under itself, so a ship that takes off and comes straight back
                // down lands on flat ground and this check passes without testing
                // anything -- which it did, reporting a leg spread of 0.00 across
                // twenty-four slopes.
                s.ship.setAirborne(true);
                // Far enough off the levelled pad to be on real ground, and no
                // further: seven seconds at twenty-six units a second is a hundred
                // and eighty, and this field is two hundred and fifty-six across.
                // Fly off the edge and the ship never lands, which reads as a
                // thirty-unit ramp gap and is nothing of the kind.
                for (int i = 0; i < 60 * 2; ++i)
                    s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, 1.0f, s.terrain);
                for (int i = 0; i < 60 * 1; ++i)
                    s.ship.fly(1.0f / 60.0f, 1.0f, 0.0f, 0.0f, s.terrain);

                s.ship.beginLanding();
                for (int i = 0; i < 60 * 60 && s.ship.airborne(); ++i) {
                    const float rate = -s.ship.verticalSpeed();
                    s.ship.fly(1.0f / 60.0f, 0.0f, 0.0f, rate > 4.0f ? 1.0f : 0.0f, s.terrain);
                }
                s.ship.openRamp();
                for (int i = 0; i < 300; ++i) s.ship.update(1.0f / 60.0f);
                s.republish();

                // On the field, whole hull. groundUnderHull samples along all
                // thirty-eight units of it, and outside the heightfield those
                // samples are not heights -- a ship "landed" at y 97 on terrain
                // spanning twelve. This is the third check to be bitten by it, so:
                // if the flight left the field, the site tells you nothing.
                //
                // The site has to be one the ship is genuinely STANDING on. Its
                // resting height is the max over the centreline AND the legs, so a
                // single bad sample -- and outside the heightfield the samples are
                // not heights -- lifts the hull twenty units while the four legs
                // still agree with each other perfectly. That reads as "no site was
                // too steep, and seventeen of them had feet twenty units in the
                // air", which is a contradiction and was the clue.
                {
                    float highestUnderLeg = -1e30f;
                    for (int i = 0; i < Ship::kLegs; ++i) {
                        const glm::vec3 b = s.ship.legBase(i);
                        highestUnderLeg = std::max(highestUnderLeg,
                                                   s.terrain.heightAtWorld(b.x, b.z));
                    }
                    if (s.ship.origin().y - highestUnderLeg > 1.0f) { ++phantom; continue; }
                }

                // Every foot within a hand's width of the dirt under it -- but
                // only asked of sites the gear can actually absorb. A hull
                // thirty-eight units long on a steep hillside has ten units of drop
                // end to end, and no landing leg is ten units long; four of these
                // twenty-four sites are that, and demanding it anyway would be
                // demanding the impossible and calling working code broken.
                if (!s.ship.standsLevel(s.terrain)) { ++tooSteep; continue; }

                float worstHere = 0.0f;
                for (int i = 0; i < Ship::kLegs; ++i) {
                    const glm::vec3 foot = s.ship.legFoot(i);
                    const float under = s.terrain.heightAtWorld(foot.x, foot.z);
                    worstHere = std::max(worstHere, std::fabs(foot.y - under));
                }
                if (worstHere < 0.5f) ++allDown;
                worstFoot = std::max(worstFoot, worstHere);
                mostSpread = std::max(mostSpread, s.ship.legSpread());

                // A gap under 0.9 is one both of them can still step across: it is
                // the biped's own ledge limit. Bigger than that on a steep downhill
                // site is a real outcome rather than a bug -- the alternative is a
                // ramp too steep to route over -- so it is counted and reported.
                const float gap = s.ship.rampGap(s.terrain);
                if (gap < 0.9f) ++reached;
                worstGap = std::max(worstGap, gap);
            }
        }
    }

    const int standable = sites - tooSteep - phantom;

    char detail[224];
    std::snprintf(detail, sizeof detail,
                  "%d slopes (%d too steep, %d not really landed): %d of %d had all "
                  "four feet down (worst %.2f off, legs differed by up to %.2f), "
                  "ramp reached on %d (worst gap %.2f)",
                  sites, tooSteep, phantom, allDown, standable, worstFoot,
                  mostSpread, reached, worstGap);

    report("it stands on all four legs on a slope",
           standable > 0 && allDown == standable && mostSpread > 0.3f, detail);
}

// ---------------------------------------------------------------------------
// 6. And all of it on an AUTHORED planet, not the field this example generates.
//
// The creatures were tuned against 11 units of relief on 256 units of ground.
// red_planet is 4032 units across with 207 units of relief -- a different order
// of country entirely -- and it is the ground this is actually meant to run on.
// Skipped, not failed, when the level is not present: it lives in build output
// and a fresh clone has not got one.
// ---------------------------------------------------------------------------
void checkTheRealPlanet() {
    const std::string level =
        "build/examples/terrain_editor/levels/red_planet.eden";
    const char* tries[] = {
        "build/examples/terrain_editor/levels/red_planet.eden",
        "../../build/examples/terrain_editor/levels/red_planet.eden",
        "../../../levels/red_planet.eden",
        "levels/red_planet.eden",
    };

    eden::LevelData data;
    bool loaded = false;
    for (const char* p : tries) {
        if (eden::LevelSerializer::load(p, data) && !data.chunks.empty()) { loaded = true; break; }
    }
    if (!loaded) {
        if (g_verbose) std::printf("  %-34s %-4s %s\n", "the authored planet", "--",
                                   "red_planet.eden not found, skipped");
        return;
    }

    glm::ivec2 lo(1 << 30), hi(-(1 << 30));
    for (const auto& ch : data.chunks) { lo = glm::min(lo, ch.coord); hi = glm::max(hi, ch.coord); }
    const int res = static_cast<int>(std::lround(std::sqrt((double)data.chunks[0].heightmap.size())));
    const float tile = 2.0f;
    const int nodes = (hi.x - lo.x + 1) * (res - 1);
    const float half = nodes * 0.5f * tile;

    std::unordered_map<long long, const eden::LevelData::ChunkData*> byCoord;
    for (const auto& ch : data.chunks) byCoord[(long long)ch.coord.x * 100000 + ch.coord.y] = &ch;

    auto heightAt = [=](float wx, float wz) -> float {
        const int gx = (int)std::floor((wx + half) / tile);
        const int gz = (int)std::floor((wz + half) / tile);
        auto it = byCoord.find((long long)(gx / (res - 1) + lo.x) * 100000 + (gz / (res - 1) + lo.y));
        if (it == byCoord.end()) return 0.0f;
        const size_t idx = (size_t)(gz % (res - 1)) * res + (gx % (res - 1));
        return idx < it->second->heightmap.size() ? it->second->heightmap[idx] : 0.0f;
    };

    BorrowedTerrain planet(nodes, tile, heightAt);
    Ground ground(planet);

    Ship ship;
    ship.place(planet, glm::vec2(0.0f, 0.0f), 34.0f);
    ship.openRamp();
    for (int i = 0; i < 300; ++i) ship.update(1.0f / 60.0f);

    ground.addPatch(ship.deckPatch());
    ground.addPatch(ship.rampPatch());
    std::vector<Blocker> solids; ship.appendBlockers(solids);
    for (const Blocker& b : solids) ground.addBlocker(b);
    ground.addEnclosure(ship.enclosure());

    const glm::vec3 o = ship.origin();
    const glm::vec3 pile = ship.bayStoragePoint();
    const glm::vec3 foot = ship.rampFootPosition();

    Biped man;
    const float maxRise = tile * std::tan(glm::radians(man.params.maxSlopeDeg));

    int planned = 0, viaRamp = 0;
    for (int i = 0; i < 8; ++i) {
        const float a = i * 0.785f;
        glm::vec3 from = o + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 60.0f;
        from.y = heightAt(from.x, from.z);

        std::vector<glm::ivec2> route;
        if (!ground.findRoute(ground.nodeNear(from), from.y, ground.nodeNear(pile),
                              maxRise, man.params.bodyRadius, man.standHeight(), route)) continue;
        ++planned;

        float nearest = 1e9f;
        for (const glm::ivec2& nd : route) {
            const glm::vec3 p = planet.worldAt(nd);
            nearest = std::min(nearest, glm::length(glm::vec2(p.x - foot.x, p.z - foot.z)));
        }
        if (nearest < 8.0f) ++viaRamp;
    }

    Walker w;
    w.reset(ground, ground.nodeNear(glm::vec3(o.x + 40.0f, 0.0f, o.z + 40.0f)) - glm::ivec2(1), 0);
    glm::vec3 crate = o + glm::vec3(35.0f, 0.0f, 22.0f);
    crate.y = heightAt(crate.x, crate.z);
    w.assignFetch(ground, crate, pile);

    bool hadCargo = false;
    for (int i = 0; i < 60 * 200 && w.hasTask(); ++i) {
        w.update(ground, 1.0f / 60.0f);
        if (w.hasCargo()) hadCargo = true;
    }
    const glm::vec3 end = w.bodyCentre(ground);
    const float toPile = glm::length(glm::vec2(end.x - pile.x, end.z - pile.z));
    const bool delivered = hadCargo && toPile < 6.0f;

    char detail[192];
    std::snprintf(detail, sizeof detail,
                  "%d units across, ship down at y=%.0f, %d of 8 routes via the ramp, crate %s",
                  (int)(nodes * tile), o.y, viaRamp, delivered ? "delivered" : "NOT delivered");
    report("it all works on the authored planet", planned == 8 && viaRamp == 8 && delivered, detail);
}

int runShipChecks(bool verbose) {
    g_verbose = verbose;
    g_failed = 0;

    std::printf("ship checks\n");

    checkShipSolids();
    checkSeam();
    checkBipedCarriesAboard();
    checkWayInForPlayer();
    checkNoOtherWayIn();
    checkNoOtherWayInForWalker();
    checkWalkerClimbs();
    checkRoomsAndDoors();
    checkBipedRoundTrip();
    checkWalkerLeavesByTheRamp();
    checkTheyLeaveWhenDone();
    checkTheDoorMoving();
    checkRoutesAndDeliveries();
    checkTheRally();
    checkFlight();
    checkEveryHeading();
    checkRallyApproaches();
    checkStationHonesty();
    checkThePlayerRides();
    checkTheHelmView();
    checkTheLanding();
    checkNoTeleportedCrates();
    checkTheLandingForecast();
    checkTheBoardingLadder();
    checkTheHullStopsYou();
    checkEverythingLands();
    checkTheRampExtension();
    checkTheLandingGear();
    checkTheRealPlanet();

    std::printf("  %s\n\n", g_failed ? "SOMETHING IS BROKEN" : "all ok");
    return g_failed;
}

} // namespace tessara
