#pragma once

#include <glm/glm.hpp>

#include <cmath>

namespace tessara {

// The ground under everything, whatever is producing it.
//
// Two things produce it so far: the generated field this example grew up on, and
// a region lifted out of an authored .eden level. Everything above this line --
// the ship, both creatures, the collision, the routes, every check -- reads the
// world through the handful of calls below and cannot tell which it is talking
// to. That was already nearly true: the whole of Ground, Walker, Biped and Ship
// only ever asked the terrain eight questions, which is why this interface is
// eight questions long rather than an act of design.
//
// The node-flavoured three are virtual with workable defaults rather than pure,
// because a source that stores a lattice can answer them exactly and one that
// samples a surface should not have to pretend it does.
class TerrainSource {
public:
    virtual ~TerrainSource() = default;

    // ---- the shape of the world -------------------------------------------
    virtual int   n() const = 0;         // lattice nodes per side
    virtual float spacing() const = 0;   // world units between them

    // ---- what is under a point --------------------------------------------
    virtual float heightAtWorld(float worldX, float worldZ) const = 0;
    virtual glm::vec3 normalAtWorld(float worldX, float worldZ) const = 0;

    // ---- and what a thing the size of a ship does to it --------------------
    // Levelling a landing site is a real edit to the world, not a rendering
    // trick: the creatures walk what the ship flattened.
    virtual void levelPatch(glm::vec2 centre, float radius, float blend, float height) = 0;

    // ---- the lattice ------------------------------------------------------
    virtual bool inBounds(const glm::ivec2& node) const {
        return node.x >= 0 && node.y >= 0 && node.x < n() && node.y < n();
    }

    virtual glm::vec3 worldAt(const glm::ivec2& node) const {
        const float half = n() * 0.5f * spacing();
        const float x = node.x * spacing() - half;
        const float z = node.y * spacing() - half;
        return glm::vec3(x, heightAtWorld(x, z), z);
    }

    virtual float heightAt(const glm::ivec2& node) const {
        return worldAt(node).y;
    }
};

} // namespace tessara
