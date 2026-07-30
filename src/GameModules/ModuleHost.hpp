#pragma once

// What a module can DO to the world, as opposed to what it can be asked about.
//
// WHY THIS EXISTS. GameModule was, until now, entirely one-directional. Every
// call on it either hands the module a pointer to something the host owns or
// asks it a question -- "is there ground of yours here", "did you move the floor
// the player was standing on", "should I push this body out of something solid".
// A module could answer for the world. It could not put anything in it.
//
// Tessara gets away with that by being self-sufficient: its own vertex buffer,
// its own pipeline, its own shaders, drawn in renderWorld, and it answers
// groundHeight/resolvePosition for what it drew. That works for one hand-built
// ship and it does not generalise:
//
//   - the tribe sim spawns skinned GLB characters as real scene objects, plays
//     animation clips on them, and deletes one when a lion catches it. None of
//     that had a route through the seam, which is the only reason it is a struct
//     of borrowed pointers in the terrain editor rather than a module;
//   - the prefab catalogue has the player BUY a helm and place it -- a scene
//     object with metadata and ports, saved in the level, selectable.
//
// So the seam grows one direction, and no more than that. Every method here is
// something one of those two demonstrably needs; nothing is here because it
// seemed likely to be wanted.
//
// OBJECTS ARE NAMED, NOT POINTED AT. A raw SceneObject* held across a frame is a
// dangling pointer waiting for a level load, and the host reorders and destroys
// its own objects freely. Names are also what everything else in this codebase
// already uses as an object handle. The host guarantees the name it RETURNS is
// unique and is the one to keep -- the one passed in is a suggestion.
//
// OWNERSHIP IS TRACKED. Everything spawned through here is remembered as this
// module's, so the host can take it all away again when the module unloads or
// the level is wiped. A seam that let a module litter objects it could not be
// made to clean up would just be a new way to leak into a fresh level, which is
// the exact bug that made all of this worth doing.

#include <cstddef>
#include <string>

#include <glm/glm.hpp>

namespace eden {

class ModuleHost {
public:
    virtual ~ModuleHost() = default;

    // ---- putting things in the world ---------------------------------------

    // A model file (.lime or .glb, skinned or not), placed and grounded by the
    // caller. Returns the object's name, or empty on failure -- and it fails
    // quietly rather than throwing, because a missing asset should not take the
    // editor down. `nameHint` is a suggestion; the returned name is the truth.
    virtual std::string spawnModel(const std::string& path,
                                   const glm::vec3& position,
                                   float yawDegrees,
                                   const std::string& nameHint) = 0;

    // A coloured box, for a module that wants a marker or a placeholder without
    // shipping an asset for it.
    virtual std::string spawnBox(const glm::vec3& position,
                                 const glm::vec3& size,
                                 const glm::vec4& color,
                                 const std::string& nameHint) = 0;

    // Take one away. Silently does nothing if it is already gone, because the
    // caller racing its own deferred deletion is normal.
    virtual void destroyObject(const std::string& name) = 0;

    // Everything this module put here. Called by the host on unload and on New
    // Level; a module may also call it itself.
    virtual void destroyAllOwned() = 0;

    // ---- moving and reading them back --------------------------------------

    virtual bool objectPosition(const std::string& name, glm::vec3& outPosition) const = 0;
    virtual bool setObjectPosition(const std::string& name, const glm::vec3& position) = 0;
    virtual bool setObjectYaw(const std::string& name, float yawDegrees) = 0;

    // Only meaningful for a spawnModel result that turned out to be skinned;
    // false for anything else, which is information rather than an error.
    virtual bool playAnimation(const std::string& name,
                               const std::string& clip,
                               bool loop) = 0;

    // ---- asking about the ground -------------------------------------------
    // GameModule::setTerrain already hands over the Terrain itself, but a module
    // that only wants to stand something on the floor should not have to include
    // the terrain headers to do it.
    virtual float terrainHeight(float x, float z) const = 0;

    // How many objects this module currently owns. For the host's own checks --
    // see the empty-level check, which drives this seam the way a module would.
    virtual std::size_t ownedCount() const = 0;
};

} // namespace eden
