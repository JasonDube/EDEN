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
        ship.toggleRamp();
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
        solid(at(0.0f, 11.8f), deck) && solid(at(0.0f, 17.0f), deck);
    report("hull walls and nose are solid", wallsSolid,
           "sides, bulkhead, and everything forward of it");

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
        man.assignFetch(crate, pile, nullptr, &muster);
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
    std::snprintf(detail, sizeof detail, "%d ship placements across a lattice cell", placements);
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
    checkRoutesAndDeliveries();

    std::printf("  %s\n\n", g_failed ? "SOMETHING IS BROKEN" : "all ok");
    return g_failed;
}

} // namespace tessara
