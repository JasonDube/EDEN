#include "Ground.hpp"

#include <cmath>

namespace tessara {

// Solve p = origin + right*u + along*v in the horizontal plane, then read the
// height off the tilt. `right` is horizontal and `along` may not be, so their
// ground shadows still span the plane and the 2x2 always inverts.
bool Ground::sample(const SurfacePatch& patch, float x, float z, float& outHeight,
                    float* outU, float* outV) {
    if (!patch.enabled) return false;

    const float rx = patch.right.x, rz = patch.right.z;
    const float ax = patch.along.x, az = patch.along.z;

    const float det = rx * az - ax * rz;
    if (std::fabs(det) < 1e-5f) return false;      // patch seen edge-on; nothing to stand on

    const float dx = x - patch.origin.x;
    const float dz = z - patch.origin.z;

    const float u = (dx * az - ax * dz) / det;
    const float v = (rx * dz - dx * rz) / det;

    if (std::fabs(u) > patch.halfWidth || std::fabs(v) > patch.halfLength) return false;

    outHeight = patch.origin.y + patch.along.y * v;
    if (outU) *outU = u;
    if (outV) *outV = v;
    return true;
}

float Ground::heightAt(float x, float z, float fromY, float stepUp) const {
    float best = m_terrain->heightAtWorld(x, z);
    const float ceiling = fromY + stepUp;

    // The terrain is the floor of last resort: if even that is a big step up
    // (walking into a cliff) it still wins, because there is nothing below it.
    bool haveReachable = best <= ceiling;

    for (const SurfacePatch& patch : m_patches) {
        float h;
        if (!sample(patch, x, z, h)) continue;
        if (h > ceiling) continue;              // too high to step onto from here

        if (!haveReachable || h > best) {
            best = h;
            haveReachable = true;
        }
    }
    return best;
}

float Ground::heightAt(const glm::ivec2& node, float fromY, float stepUp) const {
    const glm::vec3 w = m_terrain->worldAt(node);
    return heightAt(w.x, w.z, fromY, stepUp);
}

glm::vec3 Ground::worldAt(const glm::ivec2& node, float fromY, float stepUp) const {
    glm::vec3 w = m_terrain->worldAt(node);
    w.y = heightAt(w.x, w.z, fromY, stepUp);
    return w;
}

bool Ground::onPatch(float x, float z, float fromY, float stepUp) const {
    const float chosen = heightAt(x, z, fromY, stepUp);
    for (const SurfacePatch& patch : m_patches) {
        float h;
        if (sample(patch, x, z, h) && std::fabs(h - chosen) < 1e-3f) return true;
    }
    return false;
}

// Both axes are unit and horizontal, so this is a dot product rather than the
// 2x2 solve the patches need -- a box does not tilt and cannot be seen edge-on.
bool Ground::overlap(const Blocker& b, float x, float z, float footY, float height,
                     float radius, float& outU, float& outV,
                     float& outDepthU, float& outDepthV)
{
    if (!b.enabled) return false;

    // Vertical first: it is the cheap test and it is the one that usually
    // answers, because most things are either well above a solid or well below it.
    if (footY >= b.ceilingY || footY + height <= b.floorY) return false;

    const float dx = x - b.origin.x;
    const float dz = z - b.origin.z;

    outU = dx * b.right.x + dz * b.right.z;
    outV = dx * b.along.x + dz * b.along.z;

    outDepthU = (b.halfWidth  + radius) - std::fabs(outU);
    outDepthV = (b.halfLength + radius) - std::fabs(outV);

    return outDepthU > 0.0f && outDepthV > 0.0f;
}

bool Ground::blocked(float x, float z, float footY, float height, float radius) const {
    float u, v, du, dv;
    for (const Blocker& b : m_blockers) {
        if (overlap(b, x, z, footY, height, radius, u, v, du, dv)) return true;
    }

    // A solid-under patch is solid all the way down: if your feet are below it,
    // you are inside it. Standing ON it puts them level rather than below, so the
    // surface stays walkable, and that is the whole rule.
    //
    // It did once allow ducking underneath, for a body short enough to clear the
    // surface with its head -- which sounds reasonable and was the bug. The
    // walker stands 1.6 tall and the ramp's high end is 1.98 up, so he FITTED
    // UNDER IT, walked in beneath the slab, and from there stepped up onto its
    // top surface in one move -- straight past the kerbs, which guard the flanks
    // and have nothing to say about the underside.
    //
    // There is no under a cargo ramp. It rests on the ground with its foot buried
    // in the dirt, and the allowance was modelling somewhere nobody can be.
    for (const SurfacePatch& patch : m_patches) {
        if (!patch.solidUnder) continue;

        float h;
        if (!sample(patch, x, z, h)) continue;
        if (footY < h - 0.06f) return true;
    }
    return false;
}

glm::vec2 Ground::resolve(glm::vec2 xz, float footY, float height, float radius) const {
    // Two passes, because pushing out of one wall can bury you in the next --
    // the inside corner where the hull side meets the bulkhead is exactly that.
    // Two is enough for a corner and stops short of chasing a solution around a
    // ring of boxes forever.
    for (int pass = 0; pass < 2; ++pass) {
        bool moved = false;

        for (const Blocker& b : m_blockers) {
            float u, v, du, dv;
            if (!overlap(b, xz.x, xz.y, footY, height, radius, u, v, du, dv)) continue;

            // Out through the nearest face. Pushing along the shallow axis is
            // what makes brushing a wall slide you along it rather than shoving
            // you back the length of the hull.
            glm::vec3 axis;
            float distance;
            if (du < dv) {
                axis = b.right;
                distance = (u < 0.0f) ? -du : du;
            } else {
                axis = b.along;
                distance = (v < 0.0f) ? -dv : dv;
            }

            // A hair past the face, so the next frame's test finds it outside
            // rather than exactly on the boundary and pushes it again.
            distance += (distance < 0.0f ? -1e-3f : 1e-3f);

            xz += glm::vec2(axis.x, axis.z) * distance;
            moved = true;
        }

        // And out of any surface that is passing through the body. The ramp is
        // the one that moves, so it is the one that can arrive around somebody
        // rather than being walked into -- and from inside it every direction is
        // refused, so without this he stands in it turning on the spot forever.
        for (const SurfacePatch& patch : m_patches) {
            if (!patch.solidUnder) continue;

            float h, u, v;
            if (!sample(patch, xz.x, xz.y, h, &u, &v)) continue;
            if (footY >= h - 0.06f) continue;

            const float du = (patch.halfWidth  + radius) - std::fabs(u);
            const float dv = (patch.halfLength + radius) - std::fabs(v);

            glm::vec3 axis;
            float distance;
            if (du < dv) {
                axis = patch.right;
                distance = (u < 0.0f) ? -du : du;
            } else {
                // Off the low end by preference, because that is the end that
                // reaches the ground. Shoved off the high end of a ramp you are
                // deposited in mid-air beside the hull.
                axis = patch.along;
                distance = (v < 0.0f) ? -dv : dv;
            }
            distance += (distance < 0.0f ? -1e-3f : 1e-3f);

            xz += glm::vec2(axis.x, axis.z) * distance;
            moved = true;
        }

        if (!moved) break;
    }
    return xz;
}

int Ground::enclosureAt(const glm::vec3& p) const {
    for (size_t i = 0; i < m_enclosures.size(); ++i) {
        const Enclosure& e = m_enclosures[i];
        if (p.y < e.floorY) continue;

        const float dx = p.x - e.origin.x;
        const float dz = p.z - e.origin.z;
        const float u = dx * e.right.x + dz * e.right.z;
        const float v = dx * e.along.x + dz * e.along.z;

        if (std::fabs(u) <= e.halfWidth && std::fabs(v) <= e.halfLength) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const Enclosure* Ground::between(const glm::vec3& from, const glm::vec3& to) const {
    const int here = enclosureAt(from);
    const int there = enclosureAt(to);
    if (here == there) return nullptr;
    return &m_enclosures[here >= 0 ? here : there];
}

bool Ground::wayThrough(const glm::vec3& from, const glm::vec3& to,
                        glm::vec3& outWaypoint, bool& outShut) const
{
    outShut = false;

    const int here = enclosureAt(from);
    const int there = enclosureAt(to);

    // How far along the way through, and how far off its centreline. Flat,
    // because the two marks are two units apart in height and a creature that
    // counted its own climb as progress would arrive early.
    auto measure = [&](const Enclosure& e, float& outAlong, float& outAcross, float& outSpan) {
        const glm::vec2 axis(e.inside.x - e.outside.x, e.inside.z - e.outside.z);
        outSpan = glm::length(axis);
        const glm::vec2 dir = (outSpan > 1e-4f) ? axis / outSpan : glm::vec2(0.0f, 1.0f);
        const glm::vec2 d(from.x - e.outside.x, from.z - e.outside.z);
        outAlong  = glm::dot(d, dir);
        outAcross = d.x * -dir.y + d.y * dir.x;
    };

    int use = (here >= 0) ? here : there;

    if (here == there) {
        // Same side of everything -- unless he is standing IN a way through, in
        // which case he has to come out of it endwise. This is the case that had
        // him halfway down the ramp, technically out of the hold, aiming at a
        // crate off the beam and walking into the kerb.
        use = -1;
        for (size_t i = 0; i < m_enclosures.size() && use < 0; ++i) {
            float along, across, span;
            measure(m_enclosures[i], along, across, span);
            // Stopping a stride short at BOTH ends is what lets him leave it.
            // A corridor that reaches its own exit mark tells a creature standing
            // on that mark to walk to where it already is, and he mills there
            // until the job times out -- the same shape of mistake as measuring
            // the door by proximity, one level down.
            if (along > 1.0f && along < span - 1.0f &&
                std::fabs(across) < m_enclosures[i].corridorHalf) {
                use = static_cast<int>(i);
            }
        }
        if (use < 0) return false;

        // He is in the doorway and his goal is not through it, so the way out is
        // the end he is not aiming past.
        outWaypoint = (there == use) ? m_enclosures[use].inside : m_enclosures[use].outside;
        return m_enclosures[use].open || (outShut = true, true);
    }

    // Whichever room is involved. If he is in one, it is the one he must leave --
    // getting out comes before getting in, and with one room in the world the
    // distinction is theoretical anyway.
    const Enclosure& e = m_enclosures[use];

    if (!e.open) {
        outShut = true;

        // Shut, the next place to go is whatever will open it -- so for anything
        // on the outside with hands, the way through IS the button. Anything shut
        // IN gets the door itself and no more, because there is nothing on this
        // side to work.
        outWaypoint = (here < 0 && e.hasControl) ? e.control
                    : (here >= 0 ? e.inside : e.outside);
        return true;
    }

    // Which end of the way through to head for, decided by whether he is PAST the
    // near one rather than by how close he is to it.
    //
    // That distinction is the whole of it. Proximity oscillates: he reaches the
    // muster point, is handed the head of the ramp, starts up it, and within three
    // strides is far enough from the muster point to be handed the muster point
    // again. He turns round, comes back, and does it forever -- a creature pacing
    // at the bottom of a ramp, which looks exactly like a collision bug and is
    // not one. Nor is it fixable by widening the radius; that only moves the
    // distance at which it happens.
    //
    // Projecting onto the line between the two marks is monotonic instead. Every
    // step up the ramp is a step further along that line, so the answer can only
    // move forwards, and no state has to be kept anywhere to remember which leg
    // of the journey this is.
    //
    float along, across, span;
    measure(e, along, across, span);


    if (here >= 0) {
        // Inside, getting out. Deeper in than the head of the ramp, go to it
        // first -- that is what lines him up with the doorway instead of cutting
        // the corner into its frame.
        outWaypoint = (along > span) ? e.inside : e.outside;
    } else {
        // Outside, getting in. The muster point first, unless he is already lined
        // up with the way through.
        //
        // Lined up, not merely level with it. Standing off the starboard beam he
        // is well past the muster point measured along the ramp's axis, and going
        // by that alone he gets sent to the head of the ramp -- which from there
        // is a line straight through the hull wall. Being abeam of a door is not
        // being in front of it, and the difference is the whole width of the ship.
        const bool linedUp = along > -0.5f && std::fabs(across) < e.corridorHalf;
        outWaypoint = linedUp ? e.inside : e.outside;
    }
    return true;
}

bool Ground::wayOut(const glm::vec3& from, glm::vec3& outDestination, bool& outShut) const {
    const int here = enclosureAt(from);
    if (here < 0) return false;

    // The far side of the way through, not the near one.
    //
    // wayThrough hands back the NEXT mark, which is what something steering
    // wants -- it re-asks every frame and the answer walks it along. Handing the
    // same thing to something that PLANS is a different mistake entirely: it
    // routes to the head of the ramp, arrives two units later, considers itself
    // outside, goes back to wandering, re-asks, routes to the head of the ramp
    // again. It flickers between the two states in the middle of the bay forever
    // and never gets near the door.
    //
    // A planner wants the destination and will find its own way to it -- and this
    // one can, because its search only ever crosses ground it could walk.
    outDestination = m_enclosures[here].outside;
    outShut = !m_enclosures[here].open;
    return true;
}

glm::vec3 Ground::normalAt(float x, float z, float fromY, float stepUp) const {
    const float chosen = heightAt(x, z, fromY, stepUp);

    for (const SurfacePatch& patch : m_patches) {
        float h;
        if (!sample(patch, x, z, h) || std::fabs(h - chosen) > 1e-3f) continue;

        // along x right, so a level patch gives straight up and a ramp leans
        // back the way it climbs.
        glm::vec3 normal = glm::cross(patch.along, patch.right);
        if (glm::dot(normal, normal) < 1e-8f) break;
        normal = glm::normalize(normal);
        return normal.y < 0.0f ? -normal : normal;
    }
    return m_terrain->normalAtWorld(x, z);
}

} // namespace tessara
