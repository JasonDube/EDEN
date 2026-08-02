#pragma once

// THE SHIP FILE (.ship) -- a whole vessel as one portable artifact.
//
// WHY (user design, 2026-08-02): the test capital should be built once, not
// every session; enemy and alien fleets will be authored complete -- hull,
// fittings, wiring, and someday crew -- before the game ever starts; and
// starter ships should be pickable without building. One format serves all
// of it, and it lives in its own TU because nothing goes in the god file.
//
// WHAT IT HOLDS: every object the flight manifest would claim (the manifest
// IS the ship's identity -- same five rules as takeoff), positions relative
// to the hull's keel-centre, rotations, scales, metadata (roles, ratings,
// power state -- the full rail), ports, textures (deduplicated blobs, so a
// hull painted in four textures stores four, not four hundred), the wire
// runs between members by index, and a reserved "crew" list for the day the
// robots come aboard.
//
// Files live in assets/ships/<name>.ship, JSON, human-diffable.

#include "Editor/SceneObject.hpp"
#include "Renderer/ModelRenderer.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace shipfile {

struct Host {
    std::vector<std::unique_ptr<eden::SceneObject>>* sceneObjects = nullptr;

    // The manifest of the ship nearest a point -- the five rules, borrowed
    // from flight. Empty result = no ship there.
    std::function<std::vector<std::string>(const glm::vec3&)> surveyShip;

    // Create a GPU model from mesh data; returns the buffer handle.
    std::function<uint32_t(const std::vector<eden::ModelVertex>&,
                           const std::vector<uint32_t>&)> createModel;
    // Upload a texture for a handle.
    std::function<void(uint32_t, const unsigned char*, int, int)> uploadTexture;
    // Load a model file (.lime/.glb) into mesh data + texture; returns false
    // if the file cannot be read. Ports and metadata ride along when the
    // format carries them.
    struct LoadedModel {
        std::vector<eden::ModelVertex> verts;
        std::vector<uint32_t> indices;
        std::vector<unsigned char> texture;
        int texW = 0, texH = 0;
        std::vector<eden::SceneObject::StoredPort> ports;
    };
    std::function<bool(const std::string&, LoadedModel&)> loadModelFile;

    // Add a wire between two placed objects at named ports/CPs.
    std::function<void(eden::SceneObject*, const std::string&,
                       eden::SceneObject*, const std::string&)> addWire;
    // Collect the wires among a set of objects: (fromObj, fromPort, toObj, toPort).
    struct WireRec { eden::SceneObject* a; std::string ap; eden::SceneObject* b; std::string bp; };
    std::function<std::vector<WireRec>(const std::vector<eden::SceneObject*>&)> wiresAmong;

    // A fresh name prefix for a spawned ship ("ship7_").
    std::function<std::string()> nextPrefix;
    // Called after objects were added.
    std::function<void()> objectsChanged;
};

// Save the ship nearest `nearPos` to `path`. Returns an error string, or a
// human report ("saved 214 pieces, 12 wires, 3 textures") on success.
std::string save(Host& host, const glm::vec3& nearPos, const std::string& path);

// Spawn a ship file with its keel-centre at `dropPos` (caller has already
// decided ground height and clearance). Same report-or-error contract.
std::string spawn(Host& host, const glm::vec3& dropPos, const std::string& path);

// List the .ship files in a directory (bare names, no extension).
std::vector<std::string> list(const std::string& dir);

}  // namespace shipfile
