#include "Heightfield.hpp"

#include <algorithm>
#include <cmath>

namespace tessara {

namespace {

constexpr float kTau = 6.2831853f;

// Deterministic, so a run can be repeated exactly -- the gait numbers in the
// tuning panel only mean anything if the terrain is the same every time.
float generate(int x, int y, int n) {
    float fx = static_cast<float>(x) / n;
    float fy = static_cast<float>(y) / n;

    float h = 0.0f;
    h += 0.9f  * std::sin(fx * kTau * 1.0f) * std::cos(fy * kTau * 0.75f);
    h += 0.35f * std::sin(fx * kTau * 2.5f + 1.3f) * std::sin(fy * kTau * 2.0f);

    // A ridge across the middle, steep enough to be a wall in places. This is
    // what the slope rule refuses, and refusing is what makes him look like he
    // is searching rather than wandering.
    float ridge = std::exp(-std::pow((fy - 0.55f) * 14.0f, 2.0f));
    h += 3.2f * ridge * (0.5f + 0.5f * std::sin(fx * kTau * 1.5f));

    // A pit he cannot climb back out of the steep side of.
    float px = fx - 0.28f, py = fy - 0.22f;
    h -= 2.6f * std::exp(-(px * px + py * py) * 130.0f);

    return h;
}

glm::vec3 lerpColor(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

} // namespace

Heightfield::Heightfield(int n, float spacing, float relief)
    : m_n(std::max(2, n)), m_spacing(spacing), m_relief(relief)
{
    regenerate(relief);
}

void Heightfield::regenerate(float relief) {
    m_relief = relief;
    m_heights.assign(static_cast<size_t>(m_n) * m_n, 0.0f);
    for (int y = 0; y < m_n; ++y) {
        for (int x = 0; x < m_n; ++x) {
            m_heights[static_cast<size_t>(y) * m_n + x] = generate(x, y, m_n) * m_relief;
        }
    }
}

float Heightfield::heightAtWorld(float worldX, float worldZ) const {
    float half = m_n * 0.5f * m_spacing;
    float gx = (worldX + half) / m_spacing;
    float gz = (worldZ + half) / m_spacing;

    int x0 = static_cast<int>(std::floor(gx));
    int z0 = static_cast<int>(std::floor(gz));
    float tx = gx - x0;
    float tz = gz - z0;

    float h00 = heightAt({x0,     z0});
    float h10 = heightAt({x0 + 1, z0});
    float h01 = heightAt({x0,     z0 + 1});
    float h11 = heightAt({x0 + 1, z0 + 1});

    return (h00 * (1 - tx) + h10 * tx) * (1 - tz)
         + (h01 * (1 - tx) + h11 * tx) * tz;
}

// Central differences. At the border the one-sided neighbour is reused, which
// leans the edge normals slightly outward -- invisible, and cheaper than a
// special case per side.
glm::vec3 Heightfield::normalAt(const glm::ivec2& node) const {
    int xm = std::max(node.x - 1, 0),        zm = std::max(node.y - 1, 0);
    int xp = std::min(node.x + 1, m_n - 1),  zp = std::min(node.y + 1, m_n - 1);

    float dx = heightAt({xp, node.y}) - heightAt({xm, node.y});
    float dz = heightAt({node.x, zp}) - heightAt({node.x, zm});
    float runX = (xp - xm) * m_spacing;
    float runZ = (zp - zm) * m_spacing;

    return glm::normalize(glm::vec3(-dx * runZ, runX * runZ, -dz * runX));
}

glm::vec3 Heightfield::normalAtWorld(float worldX, float worldZ) const {
    // Central differences over half a cell. Sampling wider than the foot would
    // smooth away the very slope the ankle is meant to sit on.
    const float e = m_spacing * 0.5f;

    float dx = heightAtWorld(worldX + e, worldZ) - heightAtWorld(worldX - e, worldZ);
    float dz = heightAtWorld(worldX, worldZ + e) - heightAtWorld(worldX, worldZ - e);

    return glm::normalize(glm::vec3(-dx, 2.0f * e, -dz));
}

void Heightfield::levelPatch(glm::vec2 centre, float radius, float blend, float height) {
    float half = m_n * 0.5f * m_spacing;
    float outer = radius + std::max(0.01f, blend);

    int minX = std::max(0, static_cast<int>((centre.x - outer + half) / m_spacing) - 1);
    int maxX = std::min(m_n - 1, static_cast<int>((centre.x + outer + half) / m_spacing) + 1);
    int minZ = std::max(0, static_cast<int>((centre.y - outer + half) / m_spacing) - 1);
    int maxZ = std::min(m_n - 1, static_cast<int>((centre.y + outer + half) / m_spacing) + 1);

    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            glm::vec3 world = worldAt({x, z});
            float d = glm::length(glm::vec2(world.x, world.z) - centre);
            if (d > outer) continue;

            // Flat inside the pad, then a smooth shoulder out to the original
            // ground -- a hard edge would read as a hole cut in the world.
            float t = d <= radius ? 0.0f
                                  : std::clamp((d - radius) / blend, 0.0f, 1.0f);
            float w = t * t * (3.0f - 2.0f * t);

            float& h = m_heights[static_cast<size_t>(z) * m_n + x];
            h = height + (h - height) * w;
        }
    }
}

void Heightfield::buildMesh(std::vector<SceneVertex>& outVertices,
                            std::vector<uint32_t>& outIndices) const
{
    outVertices.clear();
    outIndices.clear();
    outVertices.reserve(static_cast<size_t>(m_n) * m_n);
    outIndices.reserve(static_cast<size_t>(m_n - 1) * (m_n - 1) * 6);

    // Colour by height first, then darken by slope, so the ridge and the pit read
    // as distinct places rather than as one shaded blanket.
    const glm::vec3 pitCol   {0.09f, 0.11f, 0.16f};
    const glm::vec3 lowCol   {0.16f, 0.21f, 0.26f};
    const glm::vec3 midCol   {0.28f, 0.33f, 0.35f};
    const glm::vec3 ridgeCol {0.52f, 0.50f, 0.42f};

    float span = std::max(0.001f, 4.0f * m_relief);

    for (int y = 0; y < m_n; ++y) {
        for (int x = 0; x < m_n; ++x) {
            glm::ivec2 node{x, y};
            glm::vec3 n = normalAt(node);
            float h = heightAt(node);

            float t = (h + span * 0.45f) / span;   // roughly 0 at the pit, 1 at the ridge
            glm::vec3 c = t < 0.34f ? lerpColor(pitCol, lowCol, t / 0.34f)
                        : t < 0.67f ? lerpColor(lowCol, midCol, (t - 0.34f) / 0.33f)
                                    : lerpColor(midCol, ridgeCol, (t - 0.67f) / 0.33f);

            float flatness = std::clamp(n.y, 0.0f, 1.0f);
            c *= 0.55f + 0.45f * flatness;

            outVertices.push_back({worldAt(node), n, c});
        }
    }

    for (int y = 0; y + 1 < m_n; ++y) {
        for (int x = 0; x + 1 < m_n; ++x) {
            uint32_t i00 = static_cast<uint32_t>(y) * m_n + x;
            uint32_t i10 = i00 + 1;
            uint32_t i01 = i00 + m_n;
            uint32_t i11 = i01 + 1;

            outIndices.push_back(i00); outIndices.push_back(i01); outIndices.push_back(i11);
            outIndices.push_back(i00); outIndices.push_back(i11); outIndices.push_back(i10);
        }
    }
}

void Heightfield::buildGridLines(int stride, std::vector<glm::vec3>& outSegments) const {
    outSegments.clear();
    int s = std::max(1, stride);

    for (int y = 0; y < m_n; y += s) {
        for (int x = 0; x + s < m_n; x += s) {
            outSegments.push_back(worldAt({x, y}));
            outSegments.push_back(worldAt({x + s, y}));
        }
    }
    for (int x = 0; x < m_n; x += s) {
        for (int y = 0; y + s < m_n; y += s) {
            outSegments.push_back(worldAt({x, y}));
            outSegments.push_back(worldAt({x, y + s}));
        }
    }
}

} // namespace tessara
