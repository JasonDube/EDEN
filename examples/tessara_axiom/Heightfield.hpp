#pragma once

#include <algorithm>

#include "SceneVertex.hpp"
#include "TerrainSource.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace tessara {

// A square lattice of heights, and the world positions of its nodes.
//
// This is the walker's world AND its graph. The nodes are not sample points on
// a surface that has some other true definition -- they ARE the surface, which
// is what lets a step be a move to a neighbouring index rather than a raycast
// against triangles. That property is the whole reason this scales to a colony.
//
// The shape is ported from the LIME walker experiment: rolling ground with a
// steep ridge across it and a pit cut into one quadrant, chosen so the slope
// rule has something it must genuinely refuse.
class Heightfield : public TerrainSource {
public:
    Heightfield(int n, float spacing, float relief);

    // Rebuild heights from the generator. Cheap; safe to call from a slider.
    void regenerate(float relief);

    int   n()       const override { return m_n; }
    float spacing() const override { return m_spacing; }
    float relief()  const { return m_relief; }

    bool inBounds(const glm::ivec2& node) const override {
        return node.x >= 0 && node.y >= 0 && node.x < m_n && node.y < m_n;
    }

    // Off the edge, the EDGE height rather than zero.
    //
    // Zero is a lie that reads as flat ground at sea level, and it is not a quiet
    // one: heightAtWorld interpolates four nodes, so anything within a node of the
    // boundary blends real terrain with a phantom plane, and heightAboveGround and
    // groundUnderHull take the MAX over several samples. A ship crossing the edge
    // therefore rests on whichever fiction is highest -- which is how three separate
    // checks came to report a ship landed twenty units in the air, and a fourth a
    // hundred-unit ramp gap, all of them believing the geometry was at fault.
    //
    // Clamping continues the terrain instead, which is wrong in a way that cannot
    // surprise anybody: past the edge the world simply stops changing.
    float heightAt(const glm::ivec2& node) const override {
        const int x = std::clamp(node.x, 0, m_n - 1);
        const int z = std::clamp(node.y, 0, m_n - 1);
        return m_heights[static_cast<size_t>(z) * m_n + x];
    }

    // World position of a node. X and Z come from the lattice, Y from the height,
    // and the field is centred on the origin so the camera has somewhere sane to
    // start.
    glm::vec3 worldAt(const glm::ivec2& node) const override {
        float half = m_n * 0.5f * m_spacing;
        return glm::vec3(node.x * m_spacing - half,
                         heightAt(node),
                         node.y * m_spacing - half);
    }

    // Bilinear height for anything that is not standing on a node -- the camera,
    // mostly. The walker never needs this.
    float heightAtWorld(float worldX, float worldZ) const override;

    glm::vec3 normalAt(const glm::ivec2& node) const;

    // Continuous version, for anything not standing on a node -- a biped's foot,
    // mostly. The walker never needs it; it only ever stands on nodes.
    glm::vec3 normalAtWorld(float worldX, float worldZ) const override;

    // Level a circular patch, easing back out to whatever was there. Nothing the
    // size of a ship looks right parked across a hillside, and the honest answer
    // is that a landing site gets levelled rather than that the terrain happens
    // to be flat where you need it.
    void levelPatch(glm::vec2 centre, float radius, float blend, float height) override;

    // Triangulated surface, coloured by height and slope. Rebuilt only when the
    // field itself changes.
    void buildMesh(std::vector<SceneVertex>& outVertices,
                   std::vector<uint32_t>& outIndices) const;

    // The lattice as line segments, for the diagnostic view. Thinned by `stride`
    // because a 128x128 field is 32k edges and only the shape reads at distance.
    void buildGridLines(int stride, std::vector<glm::vec3>& outSegments) const;

private:
    int   m_n;
    float m_spacing;
    float m_relief;
    std::vector<float> m_heights;
};

} // namespace tessara
