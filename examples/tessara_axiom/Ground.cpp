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
