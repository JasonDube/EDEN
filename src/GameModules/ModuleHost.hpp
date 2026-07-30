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
#include <vector>

#include <glm/glm.hpp>

namespace eden {

// One of a module's own objects, as it currently stands in the world.
//
// A snapshot rather than a handle, because the alternative is lending out
// pointers into a vector the host reorders and destroys. Cheap to refill once a
// frame, which is what a sim that moves everything every frame does anyway.
struct ModuleObjectInfo {
    std::string name;
    std::string tag;              // whatever the module called it: "inhabitant", "water"
    glm::vec3   position{0.0f};
    glm::vec3   scale{1.0f};
    bool        skinned = false;
};

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
    virtual bool setObjectScale(const std::string& name, const glm::vec3& scale) = 0;

    // What kind of thing this is, in the module's own vocabulary. The host does
    // not interpret it; it only stores it and hands it back with the object.
    virtual bool setObjectTag(const std::string& name, const std::string& tag) = 0;

    // Everything this module owns, refilled into `out`.
    //
    // OWNED rather than every object in the level, and that distinction was
    // discovered rather than designed: the tribe sim scans the whole scene list
    // in seventeen places, and every one of those scans is looking for something
    // the sim itself spawned. A module has no business walking the host's world.
    virtual void ownedObjects(std::vector<ModuleObjectInfo>& out) const = 0;

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

    // Is there ground to live on at all?
    //
    // This replaces modules asking "is this an EDEN OS level", which is the host
    // telling a module which of the host's OTHER games is running -- a question
    // no module should be able to ask. What the tribe sim actually wants to know
    // is whether there is terrain, and that is a fact about the world.
    virtual bool hasTerrain() const = 0;

    // ---- drawing over the world --------------------------------------------
    // renderUI gives a module the screen size but no way to find where a thing
    // in the world lands on it. Handing over the Camera would work and would
    // also hand over far more than projecting a point.

    // False when the point is behind the eye, which is not the same as being off
    // the edge of the screen -- a caller that ignores this draws its labels
    // mirrored behind the camera, which is exactly what it looks like.
    virtual bool worldToScreen(const glm::vec3& world, glm::vec2& outPixel) const = 0;

    // The other way, for picking: a ray from the eye through a pixel.
    virtual bool screenRay(const glm::vec2& pixel, glm::vec3& outOrigin,
                           glm::vec3& outDirection) const = 0;

    // ---- the clock ---------------------------------------------------------
    // Minutes into the game day, 0..1440.
    virtual float gameTimeMinutes() const = 0;

    // Ask for the day to turn, in game-minutes per real second. 0 hands it back.
    //
    // A REQUEST and not a setting: levels park the clock at noon deliberately for
    // stable lighting, and the tribe sim used to just wind it and put it back,
    // which meant whichever system touched it last won. The host arbitrates.
    virtual void requestTimeScale(float minutesPerRealSecond) = 0;

    // How many objects this module currently owns. For the host's own checks --
    // see the empty-level check, which drives this seam the way a module would.
    virtual std::size_t ownedCount() const = 0;
};

} // namespace eden
