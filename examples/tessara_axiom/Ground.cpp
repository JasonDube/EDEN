#include "Ground.hpp"

#include <algorithm>
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

glm::ivec2 Ground::nodeNear(const glm::vec3& world) const {
    const float half = n() * 0.5f * spacing();
    const int x = static_cast<int>(std::round((world.x + half) / spacing()));
    const int z = static_cast<int>(std::round((world.z + half) / spacing()));
    return glm::clamp(glm::ivec2(x, z), glm::ivec2(0), glm::ivec2(n() - 1));
}

bool Ground::findRoute(const glm::ivec2& from, float fromY, const glm::ivec2& to,
                       float maxRise, float radius, float height,
                       std::vector<glm::ivec2>& outNodes) const
{
    outNodes.clear();

    const int side = n();
    auto index = [side](const glm::ivec2& p) { return p.y * side + p.x; };
    auto valid = [side](const glm::ivec2& p) {
        return p.x >= 0 && p.y >= 0 && p.x < side && p.y < side;
    };
    if (!valid(from) || !valid(to)) return false;

    std::vector<int> cameFrom(static_cast<size_t>(side) * side, -2);
    std::vector<float> arrivedAt(static_cast<size_t>(side) * side, 0.0f);

    std::vector<glm::ivec2> frontier{from}, next;
    cameFrom[index(from)] = -1;
    arrivedAt[index(from)] = fromY;

    static const glm::ivec2 kStep[4] = { {1,0}, {0,1}, {-1,0}, {0,-1} };

    bool found = (from == to);
    while (!found && !frontier.empty()) {
        next.clear();
        for (const glm::ivec2& b : frontier) {
            const float here = arrivedAt[index(b)];

            for (const glm::ivec2& d : kStep) {
                const glm::ivec2 at = b + d;
                if (!valid(at) || cameFrom[index(at)] != -2) continue;

                const glm::vec3 landing = worldAt(at, here, maxRise);
                if (std::fabs(landing.y - here) > maxRise) continue;
                if (blocked(landing.x, landing.z, landing.y, height, radius)) continue;

                cameFrom[index(at)]  = index(b);
                arrivedAt[index(at)] = landing.y;
                if (at == to) { found = true; break; }
                next.push_back(at);
            }
            if (found) break;
        }
        frontier.swap(next);
    }

    if (!found) return false;

    for (glm::ivec2 at = to; at != from; ) {
        outNodes.push_back(at);
        const int parent = cameFrom[index(at)];
        at = glm::ivec2(parent % side, parent / side);
    }
    std::reverse(outNodes.begin(), outNodes.end());
    return true;
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

    // Nothing to say unless the way is SHUT.
    //
    // This function used to steer -- hand back the near mark, then the far one,
    // walk you through endwise, with a corridor to stop you stepping off the side
    // of a ramp. All of that is gone, and the reason is that the creature it
    // steered now plans routes. A route cannot cross a wall: it finds the door by
    // itself, because the door is the only way its search gets through, and it
    // comes off a ramp endwise because the kerbs are not ways off.
    //
    // Two things steering one creature is one thing too many. Left in, the marks
    // and the route disagreed exactly where they overlapped -- the route led up
    // the ramp as the way toward something beyond it, the marks saw him up there
    // with a goal outside and sent him back down, and he paced between them at
    // the ring by the door for as long as anyone watched. Three separate attempts
    // to arbitrate produced three new boundaries for them to argue on, which is
    // the tell: it was not a boundary that was wrong.
    //
    // A shut door is the one thing a route genuinely cannot see. There is no way
    // through, so the search simply fails -- and failing is not the same answer
    // as being told to go and press something. That is what is left here.
    const int here = enclosureAt(from);
    const int there = enclosureAt(to);
    if (here == there) return false;

    const Enclosure& e = m_enclosures[here >= 0 ? here : there];
    if (e.open) return false;

    outShut = true;

    // Shut, the way through is whatever will open it -- so for anything on the
    // outside with hands, it is the button. Anything shut IN gets the door and no
    // more, because there is nothing on that side to work.
    outWaypoint = (here < 0 && e.hasControl) ? e.control : (here >= 0 ? e.inside : e.outside);
    return true;
}

bool Ground::wayOut(const glm::vec3& from, glm::vec3& outDestination, bool& outShut) const {
    const int here = enclosureAt(from);
    if (here < 0) return false;

    // The far side of the way through -- a destination, for something that plans
    // a route and will find its own way there. Handing a planner the near mark
    // instead makes it arrive two units later, decide it has finished, and start
    // again, flickering in the middle of the room without ever nearing the door.
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
