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

void Ship::place(Heightfield& hf, glm::vec2 near, float yawDegrees) {
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
    float radius = std::max(params.length, params.width) * 0.62f + m_rampLength;
    hf.levelPatch(best, radius, 14.0f, height);

    m_origin = glm::vec3(best.x, height, best.y);

    // Length is fixed by the doorway; the angle is then whatever gets the tip to
    // the ground. Clamped, so a silly deck height gives a steep ramp rather than
    // an impossible one.
    m_rampLength = params.bayHeight;

    // Aimed a little BELOW the ground for the same reason: a ramp tip landing
    // exactly level with a flat pad is a coplanar face, and it flickers.
    m_openAngle = glm::degrees(std::asin(
        std::clamp((params.deckHeight + params.groundBite) / m_rampLength, 0.0f, 0.95f)));
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

glm::vec3 Ship::rampFootPosition() const {
    // Shut, it stands straight up sealing the doorway; down, it lies back at the
    // ramp angle. One sweep between the two.
    float shut = 90.0f;
    float down = -m_openAngle;
    float angle = glm::radians(shut + (down - shut) * smoothStep(m_ramp));

    glm::vec3 hinge = m_origin + up() * params.deckHeight - forward() * (params.length * 0.5f);
    glm::vec3 dir = -forward() * std::cos(angle) + up() * std::sin(angle);
    return hinge + dir * m_rampLength;
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
    const float aft  = -halfL - 0.6f;
    const float fore = -halfL * 0.1f + halfL * 0.9f - 0.15f;

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
                    out.push_back(rail);
                }
            }
        }
    }

    // ---- everything forward of the bulkhead --------------------------------
    // Where the hold ends and the bridge begins. Solid all the way to the nose
    // rather than a thin partition at the bulkhead's own thickness, for two
    // reasons. The bridge is not somewhere anyone walks -- there is no door to
    // it and no floor drawn in it -- so a wall with a hollow behind it would be
    // modelling a room nobody can reach. And a thin wall is a wall you can walk
    // through: the biped checks his path at four points along it, which at
    // walking pace is a sample every couple of units, and the bulkhead drawn in
    // the mesh is barely half a unit thick. He would straddle it. A solid seven
    // units deep cannot be stepped over by anything that takes steps.
    {
        const float face = halfL * 0.62f;

        Blocker b = base;
        b.origin     = m_origin + forward() * ((face + halfL) * 0.5f);
        b.halfWidth  = halfB + 0.3f;
        b.halfLength = (halfL - face) * 0.5f;
        b.floorY     = m_origin.y + split;
        b.ceilingY   = m_origin.y + roof;
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
    return e;
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
    const glm::vec3 rest = hinge - forward() * (m_rampLength * std::cos(a));

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
    for (int i = 0; i < 4; ++i) {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sz = (i & 2) ? 1.0f : -1.0f;
        glm::vec3 foot = at(sx * halfW * 0.72f, 0.0f, sz * halfL * 0.58f);

        // Sunk in, not sat on. Bottom face below the terrain, top face above.
        appendBox(verts, indices, foot + u * (0.18f - p.groundBite), r, u, f,
                  glm::vec3(0.85f, 0.18f + p.groundBite, 1.15f), kHullDark);
        appendTaperedStrut(verts, indices, foot + u * 0.30f,
                           foot + u * (p.deckHeight + 0.2f), 0.34f, 0.26f, kTrim);
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
        appendBox(verts, indices,
                  at(sx * (p.bayWidth * 0.5f + wallT), (wallTop + wallBot) * 0.5f, 0.0f),
                  r, u, f,
                  glm::vec3(wallT, (wallTop - wallBot) * 0.5f, halfL), kHull);
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

    // Front bulkhead -- the bay stops here and the bridge begins. Oversized on
    // every axis so it buries into the walls, the deck and the roof.
    const float bulkTop = p.deckHeight + p.bayHeight + eps;
    const float bulkBot = p.deckHeight - 0.30f;                          // 1.70
    appendBox(verts, indices,
              at(0.0f, (bulkTop + bulkBot) * 0.5f, halfL * 0.62f), r, u, f,
              glm::vec3(p.bayWidth * 0.5f + eps * 2.0f,
                        (bulkTop - bulkBot) * 0.5f, 0.25f), kHullDark);

    // ---- bridge -----------------------------------------------------------
    const float bridgeY = roofY + 1.35f;
    appendBox(verts, indices, at(0.0f, bridgeY, halfL * 0.66f), r, u, f,
              glm::vec3(halfW * 0.62f, 1.35f, halfL * 0.30f), kHull);

    // Canopy: three faces so it reads as a windscreen rather than a slab.
    appendBox(verts, indices, at(0.0f, bridgeY + 0.35f, halfL * 0.95f), r, u, f,
              glm::vec3(halfW * 0.50f, 0.70f, 0.16f), kGlass);
    for (int side = 0; side < 2; ++side) {
        float sx = side ? 1.0f : -1.0f;
        appendBox(verts, indices,
                  at(sx * halfW * 0.52f, bridgeY + 0.35f, halfL * 0.80f), r, u, f,
                  glm::vec3(0.14f, 0.60f, halfL * 0.14f), kGlass);
    }

    // Nose, pushed out PAST the hull front rather than finishing flush with it
    // -- that shared vertical plane was the second seam.
    appendBox(verts, indices, at(0.0f, p.deckHeight + 1.2f, halfL * 0.86f + eps), r, u, f,
              glm::vec3(halfW * 0.80f, 1.5f, halfL * 0.14f + eps), kHull);

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

    // ---- ramp control -----------------------------------------------------
    const glm::vec3 control = ship.controlPosition();
    appendBox(verts, indices, control, r, u, f,
              glm::vec3(0.10f, 0.42f, 0.34f), kHullDark);
    appendBox(verts, indices, control + r * 0.09f, r, u, f,
              glm::vec3(0.05f, 0.22f, 0.18f),
              ship.isOpening() ? kPanelOn : kPanelOff);
}

} // namespace tessara
