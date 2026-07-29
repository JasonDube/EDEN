#include "Ground.hpp"

#include <cmath>

namespace tessara {

// Solve p = origin + right*u + along*v in the horizontal plane, then read the
// height off the tilt. `right` is horizontal and `along` may not be, so their
// ground shadows still span the plane and the 2x2 always inverts.
bool Ground::sample(const SurfacePatch& patch, float x, float z, float& outHeight) {
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

bool Ground::onPatch(float x, float z, float fromY, float stepUp) const {
    const float chosen = heightAt(x, z, fromY, stepUp);
    for (const SurfacePatch& patch : m_patches) {
        float h;
        if (sample(patch, x, z, h) && std::fabs(h - chosen) < 1e-3f) return true;
    }
    return false;
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
