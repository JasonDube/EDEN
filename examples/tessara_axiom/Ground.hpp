#pragma once

#include "Heightfield.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace tessara {

// A flat or tilted rectangle you can stand on: a cargo deck, a ramp, later a
// floor pulled out of a GLB.
//
// Deliberately just a plane and an extent rather than a mesh. When the ship's
// interior becomes real geometry the patches can be generated from it -- one per
// walkable face, or one per room -- and nothing that CONSUMES this has to change,
// because none of them know what a patch is made of.
struct SurfacePatch {
    glm::vec3 origin{0.0f};        // centre of the rectangle, on its surface
    glm::vec3 right{1, 0, 0};      // unit, horizontal
    glm::vec3 along{0, 0, 1};      // unit, tilted for a ramp
    float halfWidth  = 1.0f;
    float halfLength = 1.0f;
    bool  enabled = true;
};

// What the world's inhabitants stand on.
//
// Everything that walks -- the player, the biped -- asks this instead of asking
// the terrain, and gets back the surface actually under its feet. That is the
// whole mechanism by which anything can walk into the ship: not one line of the
// walking code knows the ship is there.
class Ground {
public:
    explicit Ground(const Heightfield& terrain) : m_terrain(&terrain) {}

    void clearPatches() { m_patches.clear(); }
    void addPatch(const SurfacePatch& patch) { m_patches.push_back(patch); }

    // Pass-through, so a Ground can stand in wherever a Heightfield was.
    int   n() const { return m_terrain->n(); }
    float spacing() const { return m_terrain->spacing(); }
    const Heightfield& terrain() const { return *m_terrain; }

    // Terrain only -- for anything that genuinely means the ground, like siting
    // a crate or a landing pad.
    float terrainHeight(float x, float z) const { return m_terrain->heightAtWorld(x, z); }

    // The surface under a foot that is currently at `fromY`.
    //
    // Highest candidate that is not more than `stepUp` above where the foot
    // already is. That single rule is what stops you teleporting onto the cargo
    // deck by walking into the side of the hull, and what makes the ramp the way
    // in -- without anything anywhere naming the ramp.
    float heightAt(float x, float z, float fromY, float stepUp = 0.75f) const;
    glm::vec3 normalAt(float x, float z, float fromY, float stepUp = 0.75f) const;

    // True if the chosen surface was a patch rather than the terrain.
    bool onPatch(float x, float z, float fromY, float stepUp = 0.75f) const;

private:
    // Returns false if (x,z) is off the patch.
    static bool sample(const SurfacePatch& patch, float x, float z, float& outHeight);

    const Heightfield* m_terrain;
    std::vector<SurfacePatch> m_patches;
};

} // namespace tessara
