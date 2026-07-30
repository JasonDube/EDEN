#include "Ship.hpp"
#include "MeshBuild.hpp"

#include <algorithm>
#include <cmath>

namespace tessara {

namespace {

const glm::vec3 kHull    {0.30f, 0.33f, 0.37f};
const glm::vec3 kHullDark{0.19f, 0.21f, 0.24f};
const glm::vec3 kBayFloor{0.24f, 0.25f, 0.27f};
const glm::vec3 kTrim    {0.46f, 0.49f, 0.53f};
const glm::vec3 kGlass   {0.30f, 0.72f, 0.86f};
const glm::vec3 kRamp    {0.26f, 0.28f, 0.31f};
const glm::vec3 kHazard  {0.95f, 0.66f, 0.10f};
const glm::vec3 kPanelOff{0.55f, 0.20f, 0.16f};
const glm::vec3 kPanelOn {0.35f, 0.85f, 0.45f};

float smoothStep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

glm::vec3 Ship::forward() const {
    float y = glm::radians(m_yaw);
    return glm::vec3(std::sin(y), 0.0f, std::cos(y));
}

glm::vec3 Ship::right() const {
    float y = glm::radians(m_yaw);
    return glm::vec3(std::cos(y), 0.0f, -std::sin(y));
}

void Ship::place(TerrainSource& hf, glm::vec2 near, float yawDegrees) {
    m_yaw = yawDegrees;

    // Something this size sitting on a hillside looks wrong however carefully it
    // is placed, so find the flattest patch nearby rather than trusting a point
    // sample. Cost is a few hundred height lookups, once.
    const float half = hf.n() * 0.5f * hf.spacing() - 24.0f;
    const float probe = std::max(params.length, params.width) * 0.5f;

    glm::vec2 best = near;
    float bestSpread = 1e9f;

    for (int ring = 0; ring < 10; ++ring) {
        for (int step = 0; step < 12; ++step) {
            float angle = step * (6.2831853f / 12.0f);
            glm::vec2 at = near + glm::vec2(std::cos(angle), std::sin(angle)) * (ring * 5.0f);
            if (std::fabs(at.x) > half || std::fabs(at.y) > half) continue;

            float lo = 1e9f, hi = -1e9f;
            for (int sx = -1; sx <= 1; ++sx) {
                for (int sz = -1; sz <= 1; ++sz) {
                    float h = hf.heightAtWorld(at.x + sx * probe, at.y + sz * probe);
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                }
            }
            float spread = hi - lo;
            if (spread < bestSpread) { bestSpread = spread; best = at; }
            if (spread < 0.35f) { ring = 99; break; }   // flat enough, stop looking
        }
    }

    // Level the site, then sit on it. Searching alone was not enough: the
    // flattest patch this terrain offers still varied nearly two units under the
    // hull, and a ramp swung down onto sloping ground finished up to 3.7 units
    // underneath it.
    float height = hf.heightAtWorld(best.x, best.y);
    float radius = std::max(params.length, params.width) * 0.62f
                 + m_rampLength + params.rampExtend;
    hf.levelPatch(best, radius, 14.0f, height);

    m_origin = glm::vec3(best.x, height, best.y);

    // Length is fixed by the doorway; the angle is then whatever gets the tip to
    // the ground. Clamped, so a silly deck height gives a steep ramp rather than
    // an impossible one.
    m_rampLength = params.bayHeight;

    // Aimed a little BELOW the ground for the same reason: a ramp tip landing
    // exactly level with a flat pad is a coplanar face, and it flickers.
    solveRampAngle(hf);
    solveLegs(hf);
    solveRampExtension(hf);
    autoRampExtension();
}

void Ship::update(float dt, bool obstructed) {
    // Closing on somebody is the only direction that hurts, so that is the only
    // one that is refused. Opening sweeps the ramp down onto ground it is about
    // to become part of, and can be let run.
    //
    // It REVERSES rather than stopping where it is. Stopping deadlocks: the ramp
    // waits for whoever is on it to leave, and a creature standing on a ramp
    // frozen halfway has no particular reason to go anywhere -- the walker just
    // wanders on the spot, so it is held at eighty per cent forever and neither
    // of them will move first. Reopening ends the standoff, gives him a floor
    // that is level and stops moving, and lets him walk off it in his own time.
    // It is also what every lift door does, and reads immediately: the ramp came
    // back down, so something is on it.
    m_rampHeld = obstructed && !m_opening;
    if (m_rampHeld) m_opening = true;

    float rate = dt / std::max(0.05f, params.rampSeconds);
    m_ramp = std::clamp(m_ramp + (m_opening ? rate : -rate), 0.0f, 1.0f);

    // The extension only travels once the ramp is down, and comes in before it
    // shuts. A section sliding out of a slab that is still swinging would sweep it
    // through the hull, and a ramp trying to stow with its extension out cannot.
    const float want = (m_ramp > 0.92f) ? std::clamp(m_rampExtWant, 0.0f, 1.0f) : 0.0f;
    const float extRate = dt / std::max(0.05f, params.rampExtendSeconds);
    if (m_rampExt < want)      m_rampExt = std::min(want, m_rampExt + extRate);
    else if (m_rampExt > want) m_rampExt = std::max(want, m_rampExt - extRate);
}

// Terrain following, which is most of what makes a surface aircraft pleasant and
// costs a handful of height queries. The floor is the ground UNDER THE HULL rather
// than under its centre -- a ship thirty-eight units long crossing a ridge meets
// it with its nose, and a clearance measured amidships would have the bow buried
// while the middle was comfortably clear. Landing on it uses the same number, so
// setting down across a rise rests on the rise.
float Ship::groundUnderHull(const TerrainSource& ground) const {
    float below = ground.heightAtWorld(m_origin.x, m_origin.z);
    const float halfL = params.length * 0.5f;
    for (float along : {-halfL, -halfL * 0.5f, halfL * 0.5f, halfL}) {
        const glm::vec3 at = m_origin + forward() * along;
        below = std::max(below, ground.heightAtWorld(at.x, at.z));
    }

    // And under the FEET, which is where it actually touches.
    //
    // The samples above run down the centreline, and the legs are off to the sides.
    // A rise under one foot that the centreline never crossed left the hull resting
    // lower than that foot's ground, so the leg had a NEGATIVE extension to make --
    // which is not a thing a leg does -- and the foot finished a unit and a quarter
    // buried in the dirt.
    for (int i = 0; i < kLegs; ++i) {
        const glm::vec3 base = legBase(i);
        below = std::max(below, ground.heightAtWorld(base.x, base.z));
    }
    return below;
}

// The four feet, at the plane the hull rests on -- before the gear extends.
glm::vec3 Ship::legBase(int i) const {
    const float sx = (i & 1) ? 1.0f : -1.0f;
    const float sz = (i & 2) ? 1.0f : -1.0f;
    return m_origin + right()   * (sx * params.width  * 0.5f * 0.72f)
                    + forward() * (sz * params.length * 0.5f * 0.58f);
}

// Each leg as long as the ground under IT requires.
//
// The hull rests on the highest ground beneath it, so on a slope every other leg
// has ground to find below that. Without this they simply stopped at the hull's
// plane and the downhill pair hung in the air with daylight under them.
void Ship::solveLegs(const TerrainSource& ground) {
    for (int i = 0; i < kLegs; ++i) {
        const glm::vec3 base = legBase(i);
        const float under = ground.heightAtWorld(base.x, base.z);
        m_legDrop[i] = std::clamp(m_origin.y - under + params.groundBite,
                                  0.0f, params.legTravel);
    }
}

float Ship::legSpread() const {
    float lo = m_legDrop[0], hi = m_legDrop[0];
    for (int i = 1; i < kLegs; ++i) {
        lo = std::min(lo, m_legDrop[i]);
        hi = std::max(hi, m_legDrop[i]);
    }
    return hi - lo;
}

// The ramp's angle, solved so the TIP lands on the ground it is actually reaching
// for -- not on a nominal deck height.
//
// Those are the same number only on a level pad. Set down facing downhill and the
// ground at the tip is well below the ground under the hull, so an angle solved
// from the deck height leaves the ramp hanging; facing uphill it drives the tip
// into the slope. And the tip's position depends on the angle, which depends on
// the ground at the tip, so it is solved by iterating -- four passes, which is
// three more than it needs on anything walkable.
// The ramp's angle: whatever reaches the ground from the deck height, and nothing
// cleverer than that.
//
// Two more ambitious versions were tried and both were worse. Solving for the
// ground under the TIP -- so a ramp set down facing downhill lays out further to
// reach -- is right in principle and wrong in every particular: the angle it
// wants is steeper than 24 degrees, and 24 is where the biped's router gives up
// (0.90 of rise per two-unit node), so he simply stopped delivering. Capping it at
// something walkable leaves nothing to solve, because the nominal angle is already
// 21. And sweeping the angle against real terrain broke the rally as well.
//
// So it reaches for the deck height it has, and when the ground behind the ship is
// lower than the ground under it the tip stops short. Ship::rampGap reports by how
// much, which is the honest version: a ramp that stops short is a step the crew can
// still take, and a ramp steep enough to always touch is one nothing can climb.
// Angle AND extension, solved together, because both move the tip.
//
// Solving them apart cannot work: pick the angle with the extension stowed and it
// comes out steep, then the extension slides out and drives the tip through the
// dirt. They are one geometry problem with two levers.
//
// The preference is deliberate. Among settings that reach, the SHALLOWEST angle
// wins -- length is free to walk on and steepness is not -- so it uses the
// extension first and only tips further when reach runs out. And stopping short is
// weighted better than overshooting: short is a step down, long is a spike through
// the ground.
//
// Swept, never iterated. Feeding a height function its own output back through a
// guess oscillates on real terrain; four passes of that put the ramp somewhere
// arbitrary and the biped stopped delivering, which is how it was caught. Five
// hundred height lookups once per landing is nothing, and a table cannot diverge.
Ship::RampFit Ship::fitRamp(const TerrainSource& ground, float restY) const {
    const float halfL = params.length * 0.5f;

    // Never shallower than the nominal, or the ramp lifts off a level pad.
    const float nominal = glm::degrees(std::asin(
        std::clamp((params.deckHeight + params.groundBite) / m_rampLength, 0.0f, 0.95f)));

    const glm::vec3 base(m_origin.x, restY, m_origin.z);

    auto gapAt = [&](float degrees, float ext) {
        const float rad = glm::radians(degrees);
        const float span = m_rampLength + params.rampExtend * ext;
        const glm::vec3 tip = base + up() * params.deckHeight
                            - forward() * (halfL + span * std::cos(rad))
                            - up() * (span * std::sin(rad));
        return tip.y - ground.heightAtWorld(tip.x, tip.z) + params.groundBite;
    };

    RampFit best{nominal, 0.0f, gapAt(nominal, 0.0f)};
    float bestScore = 1e9f;
    for (float d = nominal; d <= params.rampMaxDegrees + 0.01f; d += 0.5f) {
        for (float e = 0.0f; e <= 1.0f + 0.001f; e += 0.05f) {
            const float gap = gapAt(d, e);
            // Stopping short is a step to climb down; driving the tip through the
            // dirt is a spike out of the ground. Weighted so it prefers the former.
            const float score = gap < 0.0f ? -gap * 3.0f : gap;
            // Strictly better, so the shallowest angle reached first keeps a tie:
            // length is free to walk on and steepness is not.
            if (score < bestScore - 0.01f) {
                bestScore = score;
                best = RampFit{d, e, gap};
            }
        }
    }
    return best;
}

float Ship::rampGapIfLandedHere(const TerrainSource& ground) const {
    return fitRamp(ground, groundUnderHull(ground)).gap;
}

// Angle AND extension, solved together, because both move the tip.
//
// Solving them apart cannot work: pick the angle with the extension stowed and it
// comes out steep, then the extension slides out and drives the tip through the
// dirt. They are one geometry problem with two levers.
//
// Swept, never iterated. Feeding a height function its own output back through a
// guess oscillates on real terrain; four passes of that put the ramp somewhere
// arbitrary and the biped stopped delivering, which is how it was caught.
void Ship::solveRampAngle(const TerrainSource& ground) {
    const RampFit fit = fitRamp(ground, m_origin.y);
    m_openAngle = fit.degrees;
    m_rampExtAuto = fit.extension;
}

// Kept as its own name because the call sites read better for it, and because the
// extension is a thing the player can also work by hand.
void Ship::solveRampExtension(const TerrainSource& ground) {
    solveRampAngle(ground);
}

float Ship::siteDrop(const TerrainSource& ground) const {
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < kLegs; ++i) {
        const glm::vec3 base = legBase(i);
        const float under = ground.heightAtWorld(base.x, base.z);
        lo = std::min(lo, under);
        hi = std::max(hi, under);
    }
    return hi - lo;
}

float Ship::rampGap(const TerrainSource& ground) const {
    const glm::vec3 tip = rampFootPosition();
    return tip.y - ground.heightAtWorld(tip.x, tip.z);
}

// Put it on the ground where it stands, in one move, and hand back the
// displacement -- because everything aboard has to be moved by exactly it, and a
// ship that arrives without its crew is the bug this whole seam exists to avoid.
glm::vec3 Ship::settle(const TerrainSource& ground) {
    const glm::vec3 was = m_origin;

    m_origin.y = groundUnderHull(ground);
    m_airborne = false;
    m_landing  = false;
    m_vy = 0.0f;
    m_touchdownSpeed = 0.0f;   // it was set down, not landed
    solveRampAngle(ground);
    solveLegs(ground);
    solveRampExtension(ground);
    autoRampExtension();

    m_lastMove = m_origin - was;
    m_lastTurn = 0.0f;
    return m_lastMove;
}

void Ship::fly(float dt, float forward, float turn, float lift,
               const TerrainSource& ground) {
    const glm::vec3 was = m_origin;
    const float wasYaw = m_yaw;

    m_yaw += glm::clamp(turn, -1.0f, 1.0f) * flight.turnRate * dt;
    m_origin += this->forward() * (glm::clamp(forward, -1.0f, 1.0f) * flight.speed * dt);
    if (!m_landing) m_origin.y += glm::clamp(lift, -1.0f, 1.0f) * flight.climbRate * dt;

    const float below = groundUnderHull(ground);

    if (m_landing) {
        // Flown down, not lowered.
        //
        // Everything above this point is a hover: the clearance is a floor the
        // ship is simply not allowed through, and while it holds, height is
        // whatever the pilot last asked for. Landing is letting go of that floor,
        // and the moment it is gone the only thing holding the ship up is lift.
        // So the descent carries a real vertical speed that gravity builds and
        // lift spends, and arriving is a thing you can do badly.
        m_vy += (glm::clamp(lift, 0.0f, 1.0f) * flight.thrust - flight.gravity) * dt;
        m_vy = std::clamp(m_vy, -flight.maxDrop, flight.climbRate);
        m_origin.y += m_vy * dt;

        // The gear meets the ground where the HULL does, which is the highest of
        // the samples taken above rather than the one under the middle. Set down
        // across a rise and it rests on the rise -- the ship does not tilt, so the
        // alternative is a nose buried in a slope.
        if (m_origin.y <= below) {
            m_origin.y = below;
            m_touchdownSpeed = -m_vy;
            m_vy = 0.0f;
            m_landing = false;
            m_airborne = false;

            // And the ramp and the gear are both re-solved for the ground it is
            // actually standing on.
            solveRampAngle(ground);
            solveLegs(ground);
            solveRampExtension(ground);
            autoRampExtension();
        }
    } else {
        // Held off the ground, but at a RATE rather than by decree. Clamping it
        // outright means take-off is a single frame in which the ship, and the
        // deck, and everybody standing on the deck, are seven units higher than
        // they were -- and anything that reads a position once a frame sees them
        // teleport. A ship that rises is also simply what leaving the ground looks
        // like.
        const float floor = below + flight.clearance;
        if (m_origin.y < floor) {
            m_origin.y = std::min(floor, m_origin.y + flight.climbRate * dt);
        }
        m_origin.y = std::min(m_origin.y, below + flight.ceiling);
        m_vy = 0.0f;
    }

    m_lastMove = m_origin - was;
    m_lastTurn = m_yaw - wasYaw;
}

bool Ship::isOnRamp(const glm::vec3& p) const {
    const SurfacePatch ramp = rampPatch();
    if (!ramp.enabled) return false;

    const glm::vec3 d = p - ramp.origin;
    const float u = d.x * ramp.right.x + d.z * ramp.right.z;

    // `along` is tilted, so its ground shadow is what a horizontal offset has to
    // be measured against -- the same solve the surface query does.
    const glm::vec2 flat(ramp.along.x, ramp.along.z);
    const float run = glm::length(flat);
    if (run < 1e-4f) return false;
    const float v = (d.x * flat.x + d.z * flat.y) / (run * run);

    if (std::fabs(u) > ramp.halfWidth + 0.9f) return false;
    if (std::fabs(v) > ramp.halfLength + 0.9f) return false;

    // And actually on it, rather than passing beneath or standing on the deck
    // over its top end. Generous upward, because a body's reported position is
    // its hips or its shell, not its soles.
    const float surface = ramp.origin.y + ramp.along.y * v;
    return p.y > surface - 1.2f && p.y < surface + 5.5f;
}

glm::vec3 Ship::controlPosition() const {
    // On the outside of the hull by the rear quarter, where somebody walking up
    // to the back of the ship would find it.
    return m_origin
         + right() * (params.width * 0.5f + 0.25f)
         - forward() * (params.length * 0.22f)
         + up() * params.controlRise;
}

glm::vec3 Ship::ladderFoot() const {
    // Starboard side, well forward, level with the bridge -- the opposite end of
    // the ship from the ramp.
    // A body's width clear of the hull, because this is where somebody STANDS to
    // use it. At 0.30 the rails were against the plating and the player's own
    // radius put him inside it -- a ladder you cannot reach is not a ladder, and
    // all ten test placements failed on exactly that.
    return m_origin
         + right() * (params.width * 0.5f + 1.00f)
         + forward() * (params.length * kHatchStation);
}

void Ship::updateHatch(float dt, bool somebodyOnThePlatform) {
    const float rate = dt / 0.7f;
    m_hatch = std::clamp(m_hatch + (somebodyOnThePlatform ? rate : -rate), 0.0f, 1.0f);
}

glm::vec3 Ship::hatchCentre() const {
    return m_origin
         + right() * (params.width * 0.5f)
         + forward() * (params.length * kHatchStation)
         + up() * params.deckHeight;
}

SurfacePatch Ship::ladderPlatformPatch() const {
    // From the hull face out past the ladder, at deck height, so stepping off the
    // top rung puts you on something and one pace inboard puts you through the
    // hatch.
    const glm::vec3 foot = ladderFoot();
    const float out = params.width * 0.5f;

    SurfacePatch patch;
    patch.right  = forward();                       // its own long axis runs fore-aft
    patch.along  = right();
    patch.origin = m_origin
                 + right() * ((out + 1.35f) * 0.5f + out * 0.5f)
                 + forward() * (params.length * kHatchStation)
                 + up() * params.deckHeight;
    patch.halfWidth  = 1.60f;                       // fore and aft of the hatch
    patch.halfLength = 1.10f;                       // hull face out past the rungs
    patch.solidUnder = true;
    patch.enabled = true;
    (void)foot;
    return patch;
}

glm::vec3 Ship::innerControlPosition() const {
    // On the starboard bay wall by the doorway, at the same height above the
    // floor as the one outside is above the ground. Somebody who has used one
    // knows where to look for the other, which is most of what a control panel
    // has to do.
    return m_origin
         + up() * (params.deckHeight + params.controlRise)
         + right() * (params.bayWidth * 0.5f - 0.12f)
         - forward() * (params.length * 0.30f);
}

glm::vec3 Ship::rampFootPosition() const {
    // Shut, it stands straight up sealing the doorway; down, it lies back at the
    // ramp angle. One sweep between the two.
    float shut = 90.0f;
    float down = -m_openAngle;
    float angle = glm::radians(shut + (down - shut) * smoothStep(m_ramp));

    glm::vec3 hinge = m_origin + up() * params.deckHeight - forward() * (params.length * 0.5f);
    glm::vec3 dir = -forward() * std::cos(angle) + up() * std::sin(angle);
    return hinge + dir * rampSpan();
}

SurfacePatch Ship::deckPatch() const {
    const float halfL = params.length * 0.5f;

    // The deck runs from under the ramp's hinge to the front of the bay.
    //
    // That aft end is the important number and it is NOT where the deck is drawn.
    // Matched to the mesh it stopped 0.15 short of the hull's rear edge, and the
    // ramp hangs from a hinge exactly ON that edge -- so between the two there
    // was a strip of nothing 0.15 wide, and a strip of nothing reads as terrain,
    // two units down.
    //
    // A body is not a point but a FOOT is, and it only has to come down once in
    // the gap to be told the floor is on the ground. What it looks like is a
    // creature that walks all the way up the ramp and then stands in the doorway
    // refusing to go in, because from where he is the bay is a 2.2-unit cliff and
    // his slope rule is doing exactly what it should.
    //
    // So they OVERLAP rather than meet. Abutting surfaces are the same mistake as
    // coplanar faces one file over -- exact adjacency is not something float
    // arithmetic will hold for you, and there is no reason to ask it to.
    // Forward to the nose, not just to the bulkhead. The bridge is a place you
    // stand, so it needs a floor; before this the deck stopped at the bulkhead
    // and everything beyond it was solid, which is why the front of the ship was
    // a wall rather than a room.
    const float aft  = -halfL - 0.6f;
    const float fore = halfL * 0.95f;

    SurfacePatch patch;
    patch.right  = right();
    patch.along  = forward();
    patch.origin = m_origin + up() * params.deckHeight
                 + forward() * ((aft + fore) * 0.5f);
    // A hand's width in at the sides, so you cannot stand on the join with the
    // wall. Only the sides: pulling the ENDS in is what caused the gap.
    patch.halfWidth  = params.bayWidth * 0.5f - 0.15f;
    patch.halfLength = (fore - aft) * 0.5f;
    return patch;
}

SurfacePatch Ship::rampPatch() const {
    const glm::vec3 hinge = m_origin + up() * params.deckHeight
                          - forward() * (params.length * 0.5f);
    const glm::vec3 foot  = rampFootPosition();

    glm::vec3 span = hinge - foot;          // up the ramp
    float length = glm::length(span);

    SurfacePatch patch;
    patch.right  = right();
    patch.along  = length > 1e-4f ? span / length : forward();
    patch.origin = (hinge + foot) * 0.5f + up() * 0.16f;   // the top face, not the middle
    patch.halfWidth  = params.bayWidth * 0.46f;
    patch.halfLength = length * 0.5f;

    // You cannot walk through it side-on. The way up is the foot of it, which is
    // the only place along its length where its surface is at your feet rather
    // than through your chest.
    patch.solidUnder = true;

    // Shut, it is a vertical door; there is nothing to walk on until it has come
    // down far enough to be a slope rather than a wall. No angle test needed --
    // the step-up rule in Ground refuses anything too steep on its own -- but a
    // door standing straight up is not a floor in any sense and should not be
    // offered as one.
    patch.enabled = m_ramp > 0.15f;
    return patch;
}

void Ship::appendBlockers(std::vector<Blocker>& out) const {
    const float halfL = params.length * 0.5f;
    const float halfW = params.width * 0.5f;
    const float halfB = params.bayWidth * 0.5f;

    // The one seam everything else is measured from. Below it the hull is solid
    // right across; above it there is a bay with walls either side. Put the
    // belly's top and the walls' bottoms at the SAME height and there is no
    // sliver between them for a foot to find.
    const float split = params.deckHeight - 0.25f;
    const float roof  = params.deckHeight + params.bayHeight + 0.6f;

    Blocker base;
    base.right = right();
    base.along = forward();

    // ---- everything below the deck ----------------------------------------
    // Belly, struts and all, as one solid. Its floor is well under the terrain
    // rather than level with it: a solid that stops exactly at the ground has a
    // bottom edge, and something walking downhill into the hull would find it.
    {
        Blocker b = base;
        b.origin     = m_origin;
        b.halfWidth  = halfW;
        b.halfLength = halfL;
        b.floorY     = m_origin.y - 6.0f;
        b.ceilingY   = m_origin.y + split;
        out.push_back(b);
    }

    // ---- the two hull walls the bay runs between ---------------------------
    const float wallT = (halfW - halfB) * 0.5f;
    for (int side = 0; side < 2; ++side) {
        const float sx = side ? 1.0f : -1.0f;

        Blocker b = base;
        b.origin     = m_origin + right() * (sx * (halfB + wallT));
        b.halfWidth  = wallT;
        b.halfLength = halfL;
        b.floorY     = m_origin.y + split;
        b.ceilingY   = m_origin.y + roof;

        // Starboard has a HATCH in it, level with the deck, where the ladder comes
        // up. Cut by emitting the wall as two lengths rather than one, which is the
        // same trick the bulkhead uses for its doorway -- there is no such thing
        // here as a solid with a hole in it, so a gap has to be a gap.
        if (sx > 0.0f) {
            const float at = params.length * kHatchStation;   // the ladder's station
            const float half = kHatchHalf;

            // Shut, the hatch is part of the wall. Same rule as the ramp: a door is
            // solid exactly while it is not a way through, and the two conditions
            // are written from one number so they cannot contradict each other.
            if (m_hatch < 0.75f) {
                Blocker shut = b;
                shut.halfLength = half;
                shut.origin = b.origin + forward() * at;
                out.push_back(shut);
            }
            const float aftEnd  = at - half;
            const float foreEnd = at + half;

            Blocker aft = b;
            aft.halfLength = (aftEnd + halfL) * 0.5f;
            aft.origin = b.origin + forward() * ((aftEnd - halfL) * 0.5f);
            out.push_back(aft);

            Blocker fore = b;
            fore.halfLength = (halfL - foreEnd) * 0.5f;
            fore.origin = b.origin + forward() * ((halfL + foreEnd) * 0.5f);
            out.push_back(fore);
            continue;
        }

        out.push_back(b);
    }

    // ---- the ramp's side rails ---------------------------------------------
    // Why a ramp in a game about walking needs rails.
    //
    // This slope rises 0.38 a unit. Two units ALONG it is 0.76 up. Two units
    // ACROSS onto its flank is also 0.76 up. They are the same number, so no rule
    // about how high a surface is can tell walking up the ramp from climbing onto
    // the side of it -- and every attempt to separate them by tightening the
    // height either let him mount the flank or stopped him climbing the slope,
    // because they are the same measurement.
    //
    // What actually distinguishes them is DIRECTION, and the honest way to say a
    // direction in a world made of solids is to put something in the way of all
    // the others. So the flank gets a kerb and the low end does not. Nothing has
    // to be told to approach from behind: behind is the only gap left.
    //
    // It is also the only version that works for the walker, who is a low machine
    // and has no business stepping up onto anything.
    if (m_ramp > 0.15f) {
        const glm::vec3 hinge = m_origin + up() * params.deckHeight
                              - forward() * halfL;
        const glm::vec3 span = hinge - rampFootPosition();
        const glm::vec3 flat(span.x, 0.0f, span.z);
        const float run = glm::length(flat);

        if (run > 1e-3f) {
            const int   kSlices  = 6;
            const float railRise = 0.85f;   // how far it stands proud of the slab
            const float rampHalf = params.bayWidth * 0.46f;

            // A kerb rather than a strip of paint, and the width is not taste.
            //
            // The walker's feet are POINTS on a lattice two units apart, so a
            // rail thinner than a step is one he strides straight over without
            // ever sampling it -- the same tunnelling that let the biped straddle
            // the bulkhead. A wide kerb is a kerb a point-sampled foot cannot
            // miss, and it still leaves eight units of ramp to walk up, which is
            // four times what either of them needs.
            const float railT = 0.6f;

            // Upright slices up a tilted rail. Each one's ceiling is taken from
            // its HIGH end, so the rail is proud of the slab everywhere inside
            // it -- nobody is meant to stand on a rail, so there is no reason to
            // follow its top face exactly, and every reason not to leave a notch
            // between slices where a foot would fit.
            for (int s = 0; s < kSlices; ++s) {
                const glm::vec3 a = rampFootPosition() + span * (float(s)     / kSlices);
                const glm::vec3 b = rampFootPosition() + span * (float(s + 1) / kSlices);

                for (int side = 0; side < 2; ++side) {
                    const float sx = side ? 1.0f : -1.0f;

                    Blocker rail = base;
                    rail.along      = flat / run;
                    rail.origin     = (a + b) * 0.5f + right() * (sx * (rampHalf - railT));
                    rail.halfWidth  = railT;
                    rail.halfLength = run / (2.0f * kSlices);
                    rail.floorY     = m_origin.y - 6.0f;
                    rail.ceilingY   = std::max(a.y, b.y) + railRise;

                    // Kept solid, and the attempt to retire them is worth recording.
                    //
                    // Asked for: let the crew hop on and off the ramp's sides. A body
                    // here is a COLUMN, so anything standing on the ramp beside a kerb
                    // overlaps it at any height -- nothing can step over one however
                    // well it jumps. So the only way to allow it is to stop pushing
                    // these, and that was tried.
                    //
                    // It broke the ramp as a ROUTE, which is a thing the kerbs turned
                    // out to be doing as well as forcing entry at the foot. Measured
                    // over the check suite with them gone: the rally never sealed and
                    // the crew ended twenty units apart, twenty-six of eighty-four
                    // approaches stalled, four of eleven deliveries finished with
                    // seven stranded, none of the four diagonals boarded. Without a
                    // wall the planners find shortcuts up the flank and the gait
                    // cannot finish them.
                    //
                    // Hopping on and off is still worth having. It needs the routes to
                    // work without a wall to lean on, which is a piece of work rather
                    // than a line -- not a kerb that is quietly deleted underneath a
                    // suite that then reports the game as broken in eight places.
                    out.push_back(rail);
                }
            }
        }
    }

    // ---- the boarding ladder ------------------------------------------------
    // Not a solid: it is climbed, not walked into, and a blocker here would be a
    // pillar standing in the doorway of nothing.

    // ---- the shut ramp IS a door, so make it solid -------------------------
    //
    // The ramp is a SurfacePatch: down, it is a floor you walk up; shut, the patch
    // simply switches off. Nothing ever stood in the doorway. So a sealed hold was
    // sealed to the routing -- the enclosure reports its door shut, the walker waits
    // outside for it -- and wide open to anything that just walked at it, which is
    // what a player does. Three of forty-eight approaches from around the hull got
    // inside, all of them from astern.
    //
    // The threshold is the PATCH'S OWN, not a number chosen here.
    //
    // rampPatch() enables itself above 0.15, so anything else makes the ramp a floor
    // and a door at once: at 0.85 the biped walking up a half-open ramp was standing
    // inside the door blocker, and he stopped delivering. Complementary by
    // construction is the only version that cannot contradict itself.
    if (m_ramp <= 0.15f) {
        Blocker door = base;
        door.origin     = m_origin + up() * params.deckHeight - forward() * (halfL - 0.3f);
        door.halfWidth  = halfB;
        door.halfLength = 0.55f;
        door.floorY     = m_origin.y + split;
        door.ceilingY   = m_origin.y + params.deckHeight + params.bayHeight;
        out.push_back(door);
    }

    // ---- the bulkhead, with a way through it -------------------------------
    // It used to be solid from here to the nose, on the grounds that the bridge
    // was somewhere nobody could walk -- no door, no floor. Both of those are now
    // false, so what is left is a partition with a doorway in it, built the same
    // way the cargo doorway is: two posts, and the gap between them is not a
    // feature anybody implemented.
    //
    // The posts are DEEP rather than thin, for the reason the solid version was.
    // A thin wall is a wall you can walk through -- the biped checks his path at
    // four points, which at walking pace is a sample every couple of units, and a
    // partition drawn half a unit thick is one he straddles.
    {
        const float face = halfL * 0.62f;
        const float doorHalf = 2.2f;              // the gap you walk through

        for (int side = 0; side < 2; ++side) {
            const float sx = side ? 1.0f : -1.0f;
            const float inner = doorHalf;
            const float outer = halfB + 0.3f;

            Blocker b = base;
            b.origin     = m_origin + forward() * face
                         + right() * (sx * (inner + outer) * 0.5f);
            b.halfWidth  = (outer - inner) * 0.5f;
            b.halfLength = 1.1f;
            b.floorY     = m_origin.y + split;
            b.ceilingY   = m_origin.y + roof;
            out.push_back(b);
        }
    }

    // ---- the bridge door ---------------------------------------------------
    // Solid across the doorway until it is most of the way open. Most, not fully:
    // a door you have to wait to finish opening reads as a door that is stuck,
    // and the last hand's width of travel is not what was stopping you.
    if (m_bridgeDoor < 0.75f) {
        Blocker b = base;
        b.origin     = bridgeDoorCentre();
        b.halfWidth  = 2.5f;
        b.halfLength = 0.9f;
        b.floorY     = m_origin.y + split;
        b.ceilingY   = m_origin.y + roof;
        out.push_back(b);
    }

    // ---- the bulwark across the nose ---------------------------------------
    // Waist high, and that is the whole point of it. The nose used to be solid
    // from below the deck to well above head height, so standing at the front of
    // the ship you were looking at a wall -- which is no way to fly anything.
    // Low enough to see over, high enough to stop you walking out of the front.
    {
        // Thin, and right at the bow. A bulwark nearly four units deep ate the
        // bridge: with a body's width added it reached back past where the
        // captain stands, so the room the door had just been cut into had no
        // floor left to stand on. It only has to be deep enough that nothing
        // steps over it, and nothing walks at the bow in a hurry.
        Blocker b = base;
        b.origin     = m_origin + forward() * (halfL * 0.95f);
        b.halfWidth  = halfB + 0.3f;
        b.halfLength = 1.2f;
        b.floorY     = m_origin.y + split;
        b.ceilingY   = m_origin.y + params.deckHeight + 1.15f;
        out.push_back(b);
    }
}

Enclosure Ship::enclosure() const {
    const float halfL = params.length * 0.5f;

    Enclosure e;
    e.right  = right();
    e.along  = forward();
    e.origin = m_origin;

    // The hull's own footprint, and the deck as its floor. Under the belly is
    // therefore NOT inside: it is a bad place to stand, but a creature there
    // wants to walk out from under the hull, not out through the door.
    e.halfWidth  = params.width * 0.5f;
    e.halfLength = halfL;
    e.floorY     = m_origin.y + params.deckHeight - 0.6f;

    e.outside = rampApproachPoint();

    // A few units in from the doorway, on the deck. Far enough in that reaching
    // it means genuinely being through the door rather than hovering in it.
    e.inside = m_origin + up() * params.deckHeight - forward() * (halfL - 4.0f);

    // Shut is not the same as part-open: the ramp only reaches the ground at the
    // very end of its travel, and a way through you cannot yet step onto is a way
    // through that is closed. Asked of the ramp's actual tip rather than of its
    // progress, because that is the thing that has to touch the dirt.
    e.open = rampFootPosition().y <= m_origin.y + 0.75f;

    // The walkable width of the ramp, less the kerbs and a body's width, so
    // "still in the doorway" and "clear of the doorway" agree with what the
    // solids will actually let him do.
    e.corridorHalf = params.bayWidth * 0.46f - 1.6f;

    // Standing room at the ramp control, out from the hull far enough that a body
    // is not inside the belly while it reaches for the button.
    e.hasControl = true;
    e.control = m_origin
              + right() * (params.width * 0.5f + 2.0f)
              - forward() * (params.length * 0.22f);

    // And standing room at the inner panel, far enough off the wall that a body
    // is not inside it while reaching.
    e.hasInsideControl = true;
    e.insideControl = m_origin
                    + up() * params.deckHeight
                    + right() * (params.bayWidth * 0.5f - 1.8f)
                    - forward() * (params.length * 0.30f);
    return e;
}

glm::vec3 Ship::bridgeDoorCentre() const {
    return m_origin + up() * params.deckHeight + forward() * (params.length * 0.31f);
}

void Ship::updateBridgeDoor(float dt, bool somebodyNear) {
    // Quick, because it is a door in a corridor and nobody should have to wait
    // for it. Half the ramp's travel, and the ramp is a cargo lift.
    const float rate = dt / 0.55f;
    m_bridgeDoor = std::clamp(m_bridgeDoor + (somebodyNear ? rate : -rate), 0.0f, 1.0f);
}

glm::vec3 Ship::helmPosition() const {
    // Desk height, not chest-and-then-some. At 1.10 its top came to 3.85 with a
    // standing eye at 3.90 -- so having just lowered the bow to see over, the
    // console was the next thing in the way. A helm you look AT instead of over
    // is the same mistake one object further in.
    return m_origin + up() * (params.deckHeight + 0.62f)
                    + forward() * (params.length * 0.42f);
}

glm::vec3 Ship::helmStation() const {
    return m_origin + up() * params.deckHeight
                    + forward() * (params.length * 0.36f);
}

glm::vec3 Ship::stationPosition(int index) const {
    const int i = std::max(0, index);
    const int row = i / 2;
    const float side = (i % 2) ? 1.0f : -1.0f;

    // Far enough apart that a walker's two-by-two footprint and a biped's
    // shoulders do not overlap, and far enough forward that the pile at the rear
    // third of the bay is behind everybody.
    const float across = 2.6f;
    const float spacing = 5.0f;
    const float firstRow = -params.length * 0.10f;

    return m_origin
         + up() * params.deckHeight
         + right() * (side * across)
         + forward() * (firstRow + row * spacing);
}

glm::vec3 Ship::bayStoragePoint() const {
    // Rear third of the bay, so a stack does not block the way in.
    return m_origin + up() * params.deckHeight
         - forward() * (params.length * 0.24f);
}

glm::vec3 Ship::rampApproachPoint() const {
    // Just beyond where the foot of the ramp COMES TO REST, on the ground, lined
    // up with it. Walking at the bay from anywhere else means walking into the
    // hull.
    //
    // Measured from the ramp lying fully down, not from wherever it happens to be
    // -- and that is not a detail. Taken from the live foot the muster point moves
    // with the door: six units further forward with the ramp shut, which puts it
    // directly under the arc the ramp sweeps on its way open. So a creature that
    // waited there for the door to open was standing exactly where the door was
    // about to land, every time, by construction. It came down on his head and
    // pinned him, and the better he behaved -- the more properly he waited -- the
    // more reliably it happened.
    //
    // Where you wait for a door is not somewhere the door goes.
    const float a = glm::radians(m_openAngle);
    const glm::vec3 hinge = m_origin + up() * params.deckHeight
                          - forward() * (params.length * 0.5f);
    const glm::vec3 rest = hinge - forward() * (rampSpan() * std::cos(a));

    return glm::vec3(rest.x, m_origin.y, rest.z) - forward() * 3.0f;
}

bool Ship::isAboard(const glm::vec3& p) const {
    glm::vec3 d = p - m_origin;
    float along  = glm::dot(d, forward());
    float across = glm::dot(d, right());
    return std::fabs(along) < params.length * 0.5f
        && std::fabs(across) < params.width * 0.5f
        && p.y > m_origin.y + params.deckHeight - 1.0f;
}

void appendShipMesh(const Ship& ship,
                    std::vector<SceneVertex>& verts,
                    std::vector<uint32_t>& indices)
{
    const Ship::Params& p = ship.params;

    const glm::vec3 o  = ship.origin();
    const glm::vec3 f  = ship.forward();
    const glm::vec3 r  = ship.right();
    const glm::vec3 u  = Ship::up();

    const float halfL = p.length * 0.5f;
    const float halfW = p.width * 0.5f;

    // Anything positioned in ship space goes through here, so the whole thing
    // moves and turns as one.
    auto at = [&](float x, float y, float z) { return o + r * x + u * y + f * z; };

    // ---- landing struts ---------------------------------------------------
    // Each one drawn to the length the ground under it asked for. The shoulder
    // stays on the hull and only the foot moves, which is what a leg looks like --
    // the alternative is four legs of one length and daylight under the downhill
    // pair, which is what this looked like on a slope.
    for (int i = 0; i < Ship::kLegs; ++i) {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sz = (i & 2) ? 1.0f : -1.0f;
        const float drop = ship.legDrop(i);

        const glm::vec3 shoulder = at(sx * halfW * 0.72f, p.deckHeight + 0.2f,
                                     sz * halfL * 0.58f);
        const glm::vec3 foot = at(sx * halfW * 0.72f, -drop, sz * halfL * 0.58f);

        // Sunk in, not sat on. Bottom face below the terrain, top face above.
        appendBox(verts, indices, foot + u * (0.18f - p.groundBite), r, u, f,
                  glm::vec3(0.85f, 0.18f + p.groundBite, 1.15f), kHullDark);
        appendTaperedStrut(verts, indices, foot + u * 0.30f, shoulder, 0.34f, 0.26f, kTrim);
    }

    // Every piece below is nudged so that NO TWO SHARE A FACE PLANE.
    //
    // Boxes built from the same handful of dimensions land on the same planes by
    // default -- the belly's top at exactly the deck's top, the nose's front at
    // exactly the hull's front -- and two coplanar faces have no depth between
    // them to resolve, so the depth test picks a winner per pixel and the seam
    // crawls. Overlap them instead and there is always an answer.
    const float eps = 0.09f;

    // ---- belly and hull sides --------------------------------------------
    // Top tucked UNDER the cargo deck, ends pulled in from the hull's.
    appendBox(verts, indices, at(0.0f, p.deckHeight - 0.57f, 0.0f), r, u, f,
              glm::vec3(halfW, 0.35f, halfL - eps), kHullDark);          // top 1.78

    const float wallY = p.deckHeight + p.bayHeight * 0.5f;
    const float wallT = (halfW - p.bayWidth * 0.5f) * 0.5f;

    // Walls run from just BELOW the deck's top surface, not level with it.
    const float wallTop = p.deckHeight + p.bayHeight;
    const float wallBot = p.deckHeight - 0.10f;                          // 1.90
    for (int side = 0; side < 2; ++side) {
        float sx = side ? 1.0f : -1.0f;
        const float wx = sx * (p.bayWidth * 0.5f + wallT);
        const float wy = (wallTop + wallBot) * 0.5f;
        const float wh = (wallTop - wallBot) * 0.5f;

        // Starboard is drawn as two lengths with a HOLE between them, because the
        // collision has a hole there and a wall you can walk through while looking
        // solid is worse than either. The gap is the hatch.
        if (sx > 0.0f) {
            const float hz = p.length * Ship::kHatchStation;
            const float hh = Ship::kHatchHalf;

            const float aftMid  = (-halfL + (hz - hh)) * 0.5f;
            const float aftHalf = ((hz - hh) + halfL) * 0.5f;
            appendBox(verts, indices, at(wx, wy, aftMid), r, u, f,
                      glm::vec3(wallT, wh, aftHalf), kHull);

            const float foreMid  = ((hz + hh) + halfL) * 0.5f;
            const float foreHalf = (halfL - (hz + hh)) * 0.5f;
            appendBox(verts, indices, at(wx, wy, foreMid), r, u, f,
                      glm::vec3(wallT, wh, foreHalf), kHull);

            // A header over the opening, so the hull does not read as sliced.
            const float headBot = p.deckHeight + p.bayHeight * 0.62f;
            appendBox(verts, indices, at(wx, (headBot + wallTop) * 0.5f, hz), r, u, f,
                      glm::vec3(wallT, (wallTop - headBot) * 0.5f, hh), kHull);

            // And the door: a panel filling the opening, sliding aft into the wall
            // as it opens. Drawn from hatchProgress, so what you see is the number
            // the collision is using.
            const float open = ship.hatchProgress();
            const float doorH = (headBot - wallBot) * 0.5f;
            appendBox(verts, indices,
                      at(wx, wallBot + doorH, hz - open * (hh * 2.0f + 0.05f)),
                      r, u, f, glm::vec3(wallT * 0.55f, doorH, hh), kTrim);
            continue;
        }

        appendBox(verts, indices, at(wx, wy, 0.0f), r, u, f,
                  glm::vec3(wallT, wh, halfL), kHull);
    }

    // Roof over the bay: slightly wider than the walls and dropped a little
    // into them, so neither its sides nor its underside line up with theirs.
    const float roofY = p.deckHeight + p.bayHeight;
    appendBox(verts, indices, at(0.0f, roofY + 0.3f - eps, -halfL * 0.15f + eps), r, u, f,
              glm::vec3(halfW + eps, 0.3f, halfL * 0.85f - eps), kHull);

    // ---- cargo bay --------------------------------------------------------
    // The deck is inset from the walls so it reads as a floor you could stand
    // on rather than as the underside of the roof.
    // Wider than the doorway so its edges tuck INSIDE the walls rather than
    // stopping flush against them, and pulled in from the stern.
    appendBox(verts, indices,
              at(0.0f, p.deckHeight - 0.19f, -halfL * 0.1f + eps * 0.5f), r, u, f,
              glm::vec3(p.bayWidth * 0.5f + eps, 0.19f, halfL * 0.9f - eps),
              kBayFloor);                                                // 1.62 - 2.00

    // Rib frames down the bay, which is most of what makes an interior read.
    for (int i = -2; i <= 2; ++i) {
        float z = i * (halfL * 0.34f);
        for (int side = 0; side < 2; ++side) {
            float sx = side ? 1.0f : -1.0f;
            // Kept clear of the deck below and the roof above: decorative
            // framing has no business sharing a plane with structure.
            appendBox(verts, indices, at(sx * p.bayWidth * 0.5f, wallY, z), r, u, f,
                      glm::vec3(0.10f, p.bayHeight * 0.5f - 0.20f, 0.22f), kTrim);
        }
        appendBox(verts, indices, at(0.0f, roofY - 0.06f, z), r, u, f,
                  glm::vec3(p.bayWidth * 0.5f - 0.19f, 0.15f, 0.22f), kTrim);
    }

    // Front bulkhead -- the bay stops here and the bridge begins. Two posts with
    // a doorway between them, because the bridge is somewhere you walk to now.
    // Oversized on every axis so they bury into the walls, the deck and the roof.
    const float bulkTop = p.deckHeight + p.bayHeight + eps;
    const float bulkBot = p.deckHeight - 0.30f;                          // 1.70
    const float doorHalf = 2.2f;
    for (int side = 0; side < 2; ++side) {
        const float sx = side ? 1.0f : -1.0f;
        const float outer = p.bayWidth * 0.5f + eps * 2.0f;
        appendBox(verts, indices,
                  at(sx * (doorHalf + outer) * 0.5f,
                     (bulkTop + bulkBot) * 0.5f, halfL * 0.62f), r, u, f,
                  glm::vec3((outer - doorHalf) * 0.5f,
                            (bulkTop - bulkBot) * 0.5f, 0.25f), kHullDark);
    }
    // A lintel over the doorway, so the gap reads as a door rather than as a
    // piece of missing wall.
    appendBox(verts, indices,
              at(0.0f, p.deckHeight + p.bayHeight - 0.35f, halfL * 0.62f), r, u, f,
              glm::vec3(doorHalf + 0.3f, 0.35f, 0.22f), kTrim);

    // The two panels of the bridge door, slid apart by however open it is. They
    // retract INTO the posts rather than vanishing, which is why the travel is
    // the door's own half-width -- at full open each panel is exactly hidden.
    {
        const glm::vec3 doorAt = ship.bridgeDoorCentre();
        const float panelHalf = 1.25f;
        const float travel = ship.bridgeDoorProgress() * (panelHalf * 2.0f);
        const float panelH = (p.bayHeight - 0.7f) * 0.5f;

        for (int side = 0; side < 2; ++side) {
            const float sx = side ? 1.0f : -1.0f;
            appendBox(verts, indices,
                      doorAt + u * panelH + r * (sx * (panelHalf + travel)),
                      r, u, f,
                      glm::vec3(panelHalf, panelH, 0.14f), kHull);
            // A rib down the leading edge, so you can see which way it moves.
            appendBox(verts, indices,
                      doorAt + u * panelH + r * (sx * (travel + 0.06f)),
                      r, u, f,
                      glm::vec3(0.10f, panelH * 0.96f, 0.17f), kTrim);
        }
    }

    // ---- bridge -----------------------------------------------------------
    const float bridgeY = roofY + 1.35f;
    appendBox(verts, indices, at(0.0f, bridgeY, halfL * 0.66f), r, u, f,
              glm::vec3(halfW * 0.62f, 1.35f, halfL * 0.30f), kHull);

    // Nose. A bulwark you can see over rather than a wall you cannot -- it used
    // to run from below the deck to a metre above standing eye height, so the
    // view forward from the front of the ship was hull. Pushed out PAST the hull
    // front rather than finishing flush with it; that shared vertical plane was
    // the second seam.
    const float bulwarkTop = p.deckHeight + 1.15f;
    appendBox(verts, indices,
              at(0.0f, (p.deckHeight - 0.3f + bulwarkTop) * 0.5f, halfL * 0.95f + eps),
              r, u, f,
              glm::vec3(halfW * 0.80f, (bulwarkTop - p.deckHeight + 0.3f) * 0.5f,
                        1.2f + eps), kHull);

    // Corner posts, so the windscreen has something to be held in.
    for (int side = 0; side < 2; ++side) {
        const float sx = side ? 1.0f : -1.0f;
        appendBox(verts, indices,
                  at(sx * halfW * 0.78f, bulwarkTop + 1.5f, halfL * 0.95f + eps), r, u, f,
                  glm::vec3(0.16f, 1.55f, 0.16f), kTrim);
    }

    // ---- the helm ----------------------------------------------------------
    // A console at the front of the bridge with a lit panel on it, and a mark on
    // the deck behind it where the captain stands. Both at deck level: the cabin
    // on the roof reads as a bridge from outside and is six units above anybody's
    // head, so this is the one that is actually flown from.
    {
        const glm::vec3 helm = ship.helmPosition();
        appendBox(verts, indices, helm, r, u, f,
                  glm::vec3(1.9f, 0.34f, 0.42f), kHullDark);

        // The panel lies BACK toward the captain rather than standing up facing
        // the bow, so it is a thing you read by looking down at it.
        appendBox(verts, indices, helm + u * 0.30f - f * 0.12f, r, u, f,
                  glm::vec3(1.55f, 0.07f, 0.32f), kPanelOn);

        // Legs, so it stands on the deck rather than floating over it.
        for (int side = 0; side < 2; ++side) {
            const float sx = side ? 1.0f : -1.0f;
            appendBox(verts, indices,
                      helm + r * (sx * 1.55f) - u * 0.46f, r, u, f,
                      glm::vec3(0.16f, 0.32f, 0.16f), kTrim);
        }
    }

    // ---- ramp -------------------------------------------------------------
    const float shut = 90.0f;
    const float down = -ship.openAngleDegrees();
    const float angle = glm::radians(shut + (down - shut) * smoothStep(ship.rampProgress()));

    const glm::vec3 hinge = at(0.0f, p.deckHeight, -halfL);
    const glm::vec3 dir   = -f * std::cos(angle) + u * std::sin(angle);
    const glm::vec3 foot  = ship.rampFootPosition();

    // Its own frame: `dir` is along the slab, and the axis across it is the
    // ship's right, so the ramp turns with the ship for free.
    const glm::vec3 rampUp = glm::normalize(glm::cross(r, dir));
    appendBox(verts, indices, (hinge + foot) * 0.5f, r, rampUp, dir,
              glm::vec3(p.bayWidth * 0.46f, 0.16f, glm::length(foot - hinge) * 0.5f),
              kRamp);

    // ---- the platform at the top of the ladder, and the hatch beside it ----
    {
        const glm::vec3 hatch = ship.hatchCentre();
        const float outw = p.width * 0.5f;

        // The landing itself: a grating from the hull face out past the rungs.
        appendBox(verts, indices,
                  o + r * (outw + 0.68f) + f * (p.length * Ship::kHatchStation)
                    + u * (p.deckHeight - 0.09f),
                  r, u, f, glm::vec3(0.78f, 0.09f, 1.35f), kTrim);

        // A rail along its outer edge, so it reads as somewhere to stand rather
        // than a shelf, and a frame round the opening so the hatch reads as a way in.
        appendBox(verts, indices,
                  o + r * (outw + 1.42f) + f * (p.length * Ship::kHatchStation)
                    + u * (p.deckHeight + 0.45f),
                  r, u, f, glm::vec3(0.06f, 0.45f, 1.35f), kHazard);
        for (int e = 0; e < 2; ++e) {
            const float sz = e ? 1.0f : -1.0f;
            appendBox(verts, indices,
                      hatch + f * (sz * 1.38f) + u * (p.bayHeight * 0.32f),
                      r, u, f, glm::vec3(0.14f, p.bayHeight * 0.32f, 0.10f), kTrim);
        }
        appendBox(verts, indices, hatch + u * (p.bayHeight * 0.64f),
                  r, u, f, glm::vec3(0.14f, 0.10f, 1.38f), kTrim);
    }

    // ---- the boarding ladder ----------------------------------------------
    // Two rails and a set of rungs up the hull by the bridge. Drawn from the same
    // numbers ladderFoot() is computed from, so what you climb is what you see.
    {
        // Drawn against the plating, inboard of the spot you stand on to use it.
        const glm::vec3 base = ship.ladderFoot() - r * 0.72f;
        const float top = ship.ladderTopY();
        const float rise = top - o.y + 0.5f;      // a little proud of the deck lip
        const float railGap = 0.55f;

        for (int side = 0; side < 2; ++side) {
            const float sx = side ? 1.0f : -1.0f;
            appendBox(verts, indices,
                      base + f * (sx * railGap) + u * (rise * 0.5f),
                      r, u, f, glm::vec3(0.07f, rise * 0.5f, 0.07f), kTrim);
        }
        const int rungs = std::max(3, static_cast<int>(rise / 0.42f));
        for (int i = 1; i <= rungs; ++i) {
            const float t = static_cast<float>(i) / (rungs + 1);
            appendBox(verts, indices, base + u * (rise * t),
                      r, u, f, glm::vec3(0.06f, 0.05f, railGap), kHazard);
        }
    }

    // The extended section, drawn as a plate lying over the outer end of the slab.
    //
    // A telescoping ramp that is simply LONGER reads as a design change rather than
    // a mechanism; a visible second plate reads as a thing that came out of the
    // first, which is what it is. Narrower and a shade proud, so the seam shows.
    if (ship.rampExtension() > 0.01f) {
        const float out = p.rampExtend * ship.rampExtension();
        const glm::vec3 mid = foot - dir * (out * 0.5f);
        appendBox(verts, indices, mid + rampUp * 0.04f, r, rampUp, dir,
                  glm::vec3(p.bayWidth * 0.42f, 0.15f, out * 0.5f), kTrim);
    }

    // Hazard stripes across it, so which way is up is never in doubt.
    for (int i = 0; i < 4; ++i) {
        float t = 0.18f + i * 0.21f;
        appendBox(verts, indices, hinge + dir * (glm::length(foot - hinge) * t)
                                       + rampUp * 0.17f,
                  r, rampUp, dir,
                  glm::vec3(p.bayWidth * 0.44f, 0.03f, 0.22f), kHazard);
    }

    // Side rails. These are collision before they are decoration -- they are what
    // makes the foot of the ramp the only way onto it -- and a thing that stops
    // you has no business being invisible.
    const float rampHalfW = p.bayWidth * 0.46f;
    for (int side = 0; side < 2; ++side) {
        const float sx = side ? 1.0f : -1.0f;
        appendBox(verts, indices,
                  (hinge + foot) * 0.5f + r * (sx * (rampHalfW - 0.6f)) + rampUp * 0.42f,
                  r, rampUp, dir,
                  glm::vec3(0.6f, 0.42f, glm::length(foot - hinge) * 0.5f),
                  kTrim);
    }

    // Hinge housing, so the ramp does not appear to pivot around nothing.
    appendBox(verts, indices, hinge, r, u, f,
              glm::vec3(p.bayWidth * 0.48f, 0.26f, 0.26f), kTrim);

    // ---- ramp controls ----------------------------------------------------
    // One outside on the hull, one inside on the bay wall, same panel drawn twice
    // and facing opposite ways -- so it reads as the same control wherever you
    // meet it.
    const glm::vec3 control = ship.controlPosition();
    appendBox(verts, indices, control, r, u, f,
              glm::vec3(0.10f, 0.42f, 0.34f), kHullDark);
    appendBox(verts, indices, control + r * 0.09f, r, u, f,
              glm::vec3(0.05f, 0.22f, 0.18f),
              ship.isOpening() ? kPanelOn : kPanelOff);

    const glm::vec3 inner = ship.innerControlPosition();
    appendBox(verts, indices, inner, r, u, f,
              glm::vec3(0.10f, 0.42f, 0.34f), kHullDark);
    appendBox(verts, indices, inner - r * 0.09f, r, u, f,
              glm::vec3(0.05f, 0.22f, 0.18f),
              ship.isOpening() ? kPanelOn : kPanelOff);
}

void appendShipGlass(const Ship& ship,
                     std::vector<SceneVertex>& verts,
                     std::vector<uint32_t>& indices)
{
    const Ship::Params& p = ship.params;

    const glm::vec3 o = ship.origin();
    const glm::vec3 f = ship.forward();
    const glm::vec3 r = ship.right();
    const glm::vec3 u = Ship::up();

    const float halfL = p.length * 0.5f;
    const float halfW = p.width * 0.5f;
    auto at = [&](float x, float y, float z) { return o + r * x + u * y + f * z; };

    // The windscreen the bridge looks out of.
    const float bulwarkTop = p.deckHeight + 1.15f;
    appendBox(verts, indices,
              at(0.0f, bulwarkTop + 1.5f, halfL * 0.95f + 0.09f), r, u, f,
              glm::vec3(halfW * 0.78f, 1.5f, 0.06f), kGlass);

    // And the raised cabin's canopy, which is styling seen from outside.
    const float bridgeY = p.deckHeight + p.bayHeight + 0.3f + 1.35f;
    appendBox(verts, indices, at(0.0f, bridgeY + 0.35f, halfL * 0.95f), r, u, f,
              glm::vec3(halfW * 0.50f, 0.70f, 0.10f), kGlass);
    for (int side = 0; side < 2; ++side) {
        const float sx = side ? 1.0f : -1.0f;
        appendBox(verts, indices,
                  at(sx * halfW * 0.52f, bridgeY + 0.35f, halfL * 0.80f), r, u, f,
                  glm::vec3(0.10f, 0.60f, halfL * 0.14f), kGlass);
    }
}

} // namespace tessara
