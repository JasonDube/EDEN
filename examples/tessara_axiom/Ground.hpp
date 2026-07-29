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

    // Whether walking INTO this surface is prevented, as opposed to only walking
    // on it being allowed.
    //
    // A deck does not need it -- it has hull walls round it and you cannot get
    // beside it to try. A ramp does: it stands out in the open with its whole
    // flank exposed, and the shortest line to the bay from almost anywhere runs
    // straight across it. Without this a creature aims at the doorway, meets the
    // ramp side-on, and walks through it as though it were a picture of a ramp.
    bool  solidUnder = false;
};

// An upright box you cannot walk into.
//
// The same kind of thing as a SurfacePatch, and there for the same reason: a
// patch says where a surface IS, a blocker says where a body may not be, and
// neither is the mesh it was cut from. A hull wall is one box whatever it is
// drawn out of, so the collision does not have to be rebuilt every time the ship
// is restyled -- and a doorway is not a feature anyone implements, it is the gap
// where nobody put a blocker.
//
// Upright on purpose. A tilted solid would want a real narrow-phase; everything
// here that has to be walked around is a wall, and a wall stands up.
struct Blocker {
    glm::vec3 origin{0.0f};        // centre of the footprint, in the horizontal plane
    glm::vec3 right{1, 0, 0};      // unit, horizontal
    glm::vec3 along{0, 0, 1};      // unit, horizontal
    float halfWidth  = 1.0f;
    float halfLength = 1.0f;
    float floorY     = 0.0f;       // underside; a body below this walks beneath it
    float ceilingY   = 1.0f;       // top; a body standing above this walks over it
    bool  enabled = true;
};

// Somewhere with a way in, and only the one.
//
// The point of naming this is that a creature standing in the cargo bay and a
// crate lying on the field are not simply far apart -- they are separated by a
// WALL, and the distance between them says nothing about that. Greedy steering
// asks only which way the goal lies, so it walks at the hull, is refused, sidles
// along, tries again, and never arrives. It is not stupid; it has never been told
// there is such a thing as a room.
//
// So this is the telling. `inside` and `outside` are where you stand to use the
// way through, one on each side of it, and `open` is whether it can be used at
// all. Anything holding those three can route itself in or out without knowing
// what the room is -- the same bargain as the patches, where nothing that walks
// knows the floor it is on belongs to a ship.
struct Enclosure {
    glm::vec3 origin{0.0f};        // centre of the footprint
    glm::vec3 right{1, 0, 0};      // unit, horizontal
    glm::vec3 along{0, 0, 1};      // unit, horizontal
    float halfWidth  = 1.0f;
    float halfLength = 1.0f;

    // You are only inside if you are also at or above this. Under the hull is
    // outdoors: it is a bad place to be, but it is not the hold.
    float floorY = 0.0f;

    glm::vec3 outside{0.0f};       // where you stand to go in
    glm::vec3 inside{0.0f};        // where you stand to come out
    bool      open = true;         // whether the way through is usable

    // How wide the way through is. The two marks give its length; this gives it
    // a body, and it needs one because a doorway is somewhere you can BE. Halfway
    // down the ramp a creature is out of the hold already -- so by region alone
    // it is on the same side as a crate lying off the beam, aims straight at it,
    // and walks into the kerb. Being in the way through has to outrank being on
    // the same side of it, or the last stride of every exit goes over the edge.
    float corridorHalf = 2.0f;
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

    void clearBlockers() { m_blockers.clear(); }
    void addBlocker(const Blocker& blocker) { m_blockers.push_back(blocker); }

    void clearEnclosures() { m_enclosures.clear(); }
    void addEnclosure(const Enclosure& e) { m_enclosures.push_back(e); }

    // Which enclosure a point is in, or -1 for the open world.
    int enclosureAt(const glm::vec3& p) const;

    // ---- getting from one to the other -------------------------------------
    // Where to head for NEXT, when `to` is not in the same place as `from`.
    //
    // False means they are already on the same side of everything and the goal
    // can simply be walked at. True means it cannot, and `outWaypoint` is the
    // nearer end of the way through -- the muster point from outside, the head of
    // the ramp from inside. Reaching that end hands back the far one, and stepping
    // through puts both in the same region, at which point this stops answering
    // and ordinary steering takes over. Nobody has to track which leg they are on.
    //
    // `outShut` says the way exists but is closed, which is a different answer
    // from "no way" and wants a different response: not a detour, a wait.
    bool wayThrough(const glm::vec3& from, const glm::vec3& to,
                    glm::vec3& outWaypoint, bool& outShut) const;

    // Where to get to in order to be OUT of whatever you are in, with nowhere
    // particular to be. False if you are already outdoors.
    //
    // The far side of the way through, unlike wayThrough -- this one is for
    // things that PLAN a route and want a destination, not for things that steer
    // and want the next mark. Handing a planner the near mark makes it arrive
    // almost at once, decide it has finished, and start again.
    //
    // Wanting this at all is a lesson about wandering. A creature with no job
    // picks its heading by how much unwalked ground each one opens -- which is a
    // fine rule outdoors and says nothing at all inside a room it has just walked
    // every node of. Every heading scores zero, the tie-break picks at random, and
    // it paces the hold indefinitely: hundreds of steps, never leaving, looking
    // for all the world like it is doing something.
    bool wayOut(const glm::vec3& from, glm::vec3& outDestination, bool& outShut) const;

    // Pass-through, so a Ground can stand in wherever a Heightfield was.
    int   n() const { return m_terrain->n(); }
    float spacing() const { return m_terrain->spacing(); }
    const Heightfield& terrain() const { return *m_terrain; }

    // Terrain only -- for anything that genuinely means the ground, like siting
    // a crate or a landing pad.
    float terrainHeight(float x, float z) const { return m_terrain->heightAtWorld(x, z); }

    // ---- the lattice -------------------------------------------------------
    // For whatever stands on nodes rather than between them. Same rule as the
    // continuous version and the same reasons, asked at a node's world position
    // -- so a creature whose whole world is a graph of nodes gets to walk onto
    // the ship without its graph having to know the ship exists.
    bool inBounds(const glm::ivec2& node) const { return m_terrain->inBounds(node); }
    float     heightAt(const glm::ivec2& node, float fromY, float stepUp) const;
    glm::vec3 worldAt(const glm::ivec2& node, float fromY, float stepUp) const;

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

    // ---- solids ------------------------------------------------------------
    // A body is a column: `radius` across, standing with its soles at `footY` and
    // its crown `height` above them. It is inside a solid if the two overlap in
    // all three axes -- which is what lets the same test say "you cannot walk
    // through that wall" and "you can stand on the deck above it" without either
    // caller knowing there is a difference.
    bool blocked(float x, float z, float footY, float height, float radius = 0.0f) const;

    // The nearest place a blocked body could legally stand, pushed out through
    // whichever face it is least far inside. Returns `xz` untouched if it was
    // already free, so it is safe to run every frame on everything.
    glm::vec2 resolve(glm::vec2 xz, float footY, float height, float radius) const;

private:
    // Returns false if (x,z) is off the patch. `outU` and `outV` are where the
    // point sits in the patch's own frame, which only the push-out wants.
    static bool sample(const SurfacePatch& patch, float x, float z, float& outHeight,
                       float* outU = nullptr, float* outV = nullptr);

    // How far inside the footprint, along each of the box's own axes. Negative on
    // either axis means outside. Also reports where the point sits in box space,
    // because the push-out needs it and recomputing it is the same work again.
    static bool overlap(const Blocker& b, float x, float z, float footY, float height,
                        float radius, float& outU, float& outV,
                        float& outDepthU, float& outDepthV);

    const Heightfield* m_terrain;
    std::vector<SurfacePatch> m_patches;
    std::vector<Blocker> m_blockers;
    std::vector<Enclosure> m_enclosures;
};

} // namespace tessara
