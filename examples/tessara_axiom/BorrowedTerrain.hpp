#pragma once

#include "TerrainSource.hpp"

#include <functional>

namespace tessara {

// Somebody else's terrain, seen through this example's eight questions.
//
// The point of it is that the ground under the ship stops being something this
// example generates and becomes something the engine already had. In the editor
// that is eden::Terrain -- an authored, chunked, splat-painted planet four
// thousand units across. Headless, it is the same heights read straight out of a
// .terrain file. One class covers both, because neither of them is asked
// anything more interesting than "how high is it here".
//
// Deliberately a borrowed pointer's worth of behaviour rather than a copy: the
// heights stay wherever they already live, and a level four thousand units
// across is sixteen megabytes of them that nobody needs a second copy of.
class BorrowedTerrain : public TerrainSource {
public:
    using HeightAt = std::function<float(float worldX, float worldZ)>;
    using SetHeight = std::function<void(float worldX, float worldZ, float height)>;

    // `nodes` and `spacing` are the lattice the creatures walk. They are the
    // terrain's own tile grid wherever possible -- red_planet's tileSize is 2.0
    // and so is this example's node spacing, so the two line up exactly and
    // nothing has to be resampled.
    BorrowedTerrain(int nodes, float spacing, HeightAt height, SetHeight write = nullptr)
        : m_n(nodes), m_spacing(spacing), m_height(std::move(height)), m_write(std::move(write)) {}

    int   n() const override { return m_n; }
    float spacing() const override { return m_spacing; }

    float heightAtWorld(float worldX, float worldZ) const override {
        return m_height(worldX, worldZ);
    }

    // By difference, because a borrowed terrain is not obliged to have thought
    // about normals. Sampled a whole tile out either way rather than an epsilon:
    // a real terrain's heights are a sampled surface, and asking it about a
    // hair's breadth returns the noise in the sampling rather than the slope.
    glm::vec3 normalAtWorld(float worldX, float worldZ) const override {
        const float d = m_spacing;
        const float hx = m_height(worldX + d, worldZ) - m_height(worldX - d, worldZ);
        const float hz = m_height(worldX, worldZ + d) - m_height(worldX, worldZ - d);
        return glm::normalize(glm::vec3(-hx, 2.0f * d, -hz));
    }

    // A landing pad flattened into somebody else's planet is a real edit to that
    // planet, so it goes through whatever the owner gave us to write with. Given
    // nothing to write with it does nothing, and the ship simply sets down on the
    // flattest ground it can find -- which on four thousand units of planet is a
    // good deal flatter than on two hundred and fifty of generated field.
    void levelPatch(glm::vec2 centre, float radius, float blend, float height) override {
        if (!m_write) return;

        const float outer = radius + blend;
        const int reach = static_cast<int>(std::ceil(outer / m_spacing));
        const glm::ivec2 mid = nodeNear(centre);

        for (int dz = -reach; dz <= reach; ++dz) {
            for (int dx = -reach; dx <= reach; ++dx) {
                const glm::ivec2 node = mid + glm::ivec2(dx, dz);
                if (!inBounds(node)) continue;

                const glm::vec3 at = worldAt(node);
                const float d = glm::length(glm::vec2(at.x - centre.x, at.z - centre.y));
                if (d > outer) continue;

                // Flat inside the radius, easing back out to whatever was there.
                const float t = (d <= radius) ? 1.0f
                                              : 1.0f - (d - radius) / std::max(0.001f, blend);
                const float eased = t * t * (3.0f - 2.0f * t);
                m_write(at.x, at.z, at.y + (height - at.y) * eased);
            }
        }
    }

    glm::ivec2 nodeNear(glm::vec2 world) const {
        const float half = m_n * 0.5f * m_spacing;
        return glm::ivec2(static_cast<int>(std::round((world.x + half) / m_spacing)),
                          static_cast<int>(std::round((world.y + half) / m_spacing)));
    }

private:
    int   m_n;
    float m_spacing;
    HeightAt  m_height;
    SetHeight m_write;
};

} // namespace tessara
