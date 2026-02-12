#include "grove_host.hpp"
#include "Editor/SceneObject.hpp"
#include "Editor/GLBLoader.hpp"
#include "Editor/LimeLoader.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Zone/ZoneSystem.hpp"
#include <eden/Camera.hpp>
#include <eden/Terrain.hpp>
#include <eden/Action.hpp>
#include <grove.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <cstring>

using namespace eden;

// ── Grove scripting host functions ──────────────

static int32_t groveLogFn(const GroveValue* args, uint32_t argc, GroveValue* /*result*/, void* ud) {
    auto* accum = static_cast<std::string*>(ud);
    for (uint32_t i = 0; i < argc; i++) {
        if (i > 0) accum->append("\t");
        switch (args[i].tag) {
            case GROVE_NIL:    accum->append("nil"); break;
            case GROVE_BOOL:   accum->append(args[i].data.bool_val ? "true" : "false"); break;
            case GROVE_NUMBER: {
                double n = args[i].data.number_val;
                if (n == static_cast<double>(static_cast<int64_t>(n)) && std::isfinite(n))
                    accum->append(std::to_string(static_cast<int64_t>(n)));
                else
                    accum->append(std::to_string(n));
                break;
            }
            case GROVE_STRING: {
                auto& sv = args[i].data.string_val;
                if (sv.ptr && sv.len > 0) accum->append(sv.ptr, sv.len);
                break;
            }
            case GROVE_VEC3: {
                auto& v = args[i].data.vec3_val;
                accum->append("vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")");
                break;
            }
            case GROVE_OBJECT:
                accum->append("<object:" + std::to_string(args[i].data.object_handle) + ">");
                break;
        }
    }
    accum->append("\n");
    return 0;
}

// ─── Math functions ───

static int32_t groveSinFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* /*ud*/) {
    result->tag = GROVE_NUMBER;
    if (argc < 1 || args[0].tag != GROVE_NUMBER) { result->data.number_val = 0.0; return 0; }
    result->data.number_val = std::sin(args[0].data.number_val);
    return 0;
}

static int32_t groveCosFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* /*ud*/) {
    result->tag = GROVE_NUMBER;
    if (argc < 1 || args[0].tag != GROVE_NUMBER) { result->data.number_val = 0.0; return 0; }
    result->data.number_val = std::cos(args[0].data.number_val);
    return 0;
}

static int32_t groveAtan2Fn(const GroveValue* args, uint32_t argc, GroveValue* result, void* /*ud*/) {
    result->tag = GROVE_NUMBER;
    if (argc < 2 || args[0].tag != GROVE_NUMBER || args[1].tag != GROVE_NUMBER) { result->data.number_val = 0.0; return 0; }
    result->data.number_val = std::atan2(args[0].data.number_val, args[1].data.number_val);
    return 0;
}

static int32_t groveSqrtFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* /*ud*/) {
    result->tag = GROVE_NUMBER;
    if (argc < 1 || args[0].tag != GROVE_NUMBER) { result->data.number_val = 0.0; return 0; }
    result->data.number_val = std::sqrt(args[0].data.number_val);
    return 0;
}

static int32_t groveAbsFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* /*ud*/) {
    result->tag = GROVE_NUMBER;
    if (argc < 1 || args[0].tag != GROVE_NUMBER) { result->data.number_val = 0.0; return 0; }
    result->data.number_val = std::abs(args[0].data.number_val);
    return 0;
}

static int32_t groveTerrainHeightFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    if (argc < 1 || args[0].tag != GROVE_VEC3) {
        result->tag = GROVE_NUMBER;
        result->data.number_val = 0.0;
        return 0;
    }
    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);
    float h = ctx->terrain->getHeightAt(x, z);
    result->tag = GROVE_NUMBER;
    result->data.number_val = static_cast<double>(h);
    return 0;
}

// ─── Construction primitives for Grove scripts ───

// get_player_pos() → vec3
static int32_t groveGetPlayerPos(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    glm::vec3 pos = ctx->camera->getPosition();
    result->tag = GROVE_VEC3;
    result->data.vec3_val.x = static_cast<double>(pos.x);
    result->data.vec3_val.y = static_cast<double>(pos.y);
    result->data.vec3_val.z = static_cast<double>(pos.z);
    return 0;
}

// spawn_cube(name, pos, size, r, g, b) → bool
static int32_t groveSpawnCubeFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;

    if (argc < 6 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
        args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER ||
        args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "grove_cube";
    float size = static_cast<float>(args[2].data.number_val);
    float r = static_cast<float>(args[3].data.number_val);
    float g = static_cast<float>(args[4].data.number_val);
    float b = static_cast<float>(args[5].data.number_val);
    glm::vec4 color(r, g, b, 1.0f);

    auto meshData = PrimitiveMeshBuilder::createCube(size, color);
    auto obj = std::make_unique<SceneObject>(name);
    uint32_t handle = ctx->modelRenderer->createModel(meshData.vertices, meshData.indices);
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
    obj->setLocalBounds(meshData.bounds);
    obj->setModelPath("");
    obj->setMeshData(meshData.vertices, meshData.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveSize(size);
    obj->setPrimitiveColor(color);

    // Position bottom on terrain
    float posX = static_cast<float>(args[1].data.vec3_val.x);
    float posZ = static_cast<float>(args[1].data.vec3_val.z);
    float terrainY = ctx->terrain->getHeightAt(posX, posZ);
    float halfSize = size * 0.5f;
    obj->getTransform().setPosition(glm::vec3(posX, terrainY + halfSize, posZ));

    ctx->sceneObjects->push_back(std::move(obj));
    std::cout << "[Grove] Spawned cube '" << name << "' at (" << posX << ", " << terrainY + halfSize << ", " << posZ << ")" << std::endl;
    result->data.bool_val = 1;
    return 0;
}

// spawn_cylinder(name, pos, radius, height, r, g, b) → bool
static int32_t groveSpawnCylinderFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;

    if (argc < 7 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
        args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER ||
        args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER ||
        args[6].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "grove_cylinder";
    float radius = static_cast<float>(args[2].data.number_val);
    float height = static_cast<float>(args[3].data.number_val);
    float r = static_cast<float>(args[4].data.number_val);
    float g = static_cast<float>(args[5].data.number_val);
    float b = static_cast<float>(args[6].data.number_val);
    glm::vec4 color(r, g, b, 1.0f);

    auto meshData = PrimitiveMeshBuilder::createCylinder(radius, height, 12, color);
    auto obj = std::make_unique<SceneObject>(name);
    uint32_t handle = ctx->modelRenderer->createModel(meshData.vertices, meshData.indices);
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
    obj->setLocalBounds(meshData.bounds);
    obj->setModelPath("");
    obj->setMeshData(meshData.vertices, meshData.indices);
    obj->setPrimitiveType(PrimitiveType::Cylinder);
    obj->setPrimitiveRadius(radius);
    obj->setPrimitiveHeight(height);
    obj->setPrimitiveSegments(12);
    obj->setPrimitiveColor(color);

    // Position bottom on terrain (mesh origin is at bottom, y=0 to y=height)
    float posX = static_cast<float>(args[1].data.vec3_val.x);
    float posZ = static_cast<float>(args[1].data.vec3_val.z);
    float terrainY = ctx->terrain->getHeightAt(posX, posZ);
    obj->getTransform().setPosition(glm::vec3(posX, terrainY, posZ));

    ctx->sceneObjects->push_back(std::move(obj));
    std::cout << "[Grove] Spawned cylinder '" << name << "' at (" << posX << ", " << terrainY << ", " << posZ << ")" << std::endl;
    result->data.bool_val = 1;
    return 0;
}

// spawn_beam(name, pos1, pos2, thickness, r, g, b) → bool
// Creates a beam (stretched cube) between two world positions.
// pos.y values are height above terrain at that X,Z.
static int32_t groveSpawnBeamFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;

    if (argc < 7 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
        args[2].tag != GROVE_VEC3 || args[3].tag != GROVE_NUMBER ||
        args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER || args[6].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "grove_beam";
    float thickness = static_cast<float>(args[3].data.number_val);
    float r = static_cast<float>(args[4].data.number_val);
    float g = static_cast<float>(args[5].data.number_val);
    float b = static_cast<float>(args[6].data.number_val);
    glm::vec4 color(r, g, b, 1.0f);

    // Compute endpoint positions (Y = terrain height + pos.y offset)
    float x1 = static_cast<float>(args[1].data.vec3_val.x);
    float z1 = static_cast<float>(args[1].data.vec3_val.z);
    float y1 = ctx->terrain->getHeightAt(x1, z1) + static_cast<float>(args[1].data.vec3_val.y);

    float x2 = static_cast<float>(args[2].data.vec3_val.x);
    float z2 = static_cast<float>(args[2].data.vec3_val.z);
    float y2 = ctx->terrain->getHeightAt(x2, z2) + static_cast<float>(args[2].data.vec3_val.y);

    // Compute beam geometry
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length < 0.001f) return 0;

    float midX = (x1 + x2) * 0.5f;
    float midY = (y1 + y2) * 0.5f;
    float midZ = (z1 + z2) * 0.5f;

    // Y rotation aligns local Z with horizontal direction
    float rotY = std::atan2(dx, dz) * 180.0f / 3.14159265f;
    // X rotation (pitch) for height differences
    float horizDist = std::sqrt(dx * dx + dz * dz);
    float rotX = -std::atan2(dy, horizDist) * 180.0f / 3.14159265f;

    // Create unit cube, scale to beam dimensions (Z = length axis)
    auto meshData = PrimitiveMeshBuilder::createCube(1.0f, color);
    auto obj = std::make_unique<SceneObject>(name);
    uint32_t handle = ctx->modelRenderer->createModel(meshData.vertices, meshData.indices);
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
    obj->setLocalBounds(meshData.bounds);
    obj->setModelPath("");
    obj->setMeshData(meshData.vertices, meshData.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveSize(1.0f);
    obj->setPrimitiveColor(color);

    obj->getTransform().setPosition(glm::vec3(midX, midY, midZ));
    obj->getTransform().setScale(glm::vec3(thickness, thickness, length));
    obj->setEulerRotation(glm::vec3(rotX, rotY, 0.0f));

    ctx->sceneObjects->push_back(std::move(obj));
    std::cout << "[Grove] Spawned beam '" << name << "' from (" << x1 << "," << y1 << "," << z1
              << ") to (" << x2 << "," << y2 << "," << z2 << "), length=" << length << std::endl;
    result->data.bool_val = 1;
    return 0;
}

// spawn_model(name, path, pos) → bool
static int32_t groveSpawnModelFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;

    if (argc < 3 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_STRING ||
        args[2].tag != GROVE_VEC3) return 0;

    auto& nameSv = args[0].data.string_val;
    std::string name = (nameSv.ptr && nameSv.len > 0) ? std::string(nameSv.ptr, nameSv.len) : "grove_model";
    auto& pathSv = args[1].data.string_val;
    std::string modelPath = (pathSv.ptr && pathSv.len > 0) ? std::string(pathSv.ptr, pathSv.len) : "";

    if (modelPath.empty()) return 0;

    // Resolve relative paths — search multiple locations
    if (modelPath[0] != '/') {
        std::string resolved;
        std::vector<std::string> searchPaths;

        // 1. Same directory as current level file
        if (!ctx->currentLevelPath->empty()) {
            size_t lastSlash = ctx->currentLevelPath->find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                searchPaths.push_back(ctx->currentLevelPath->substr(0, lastSlash + 1) + modelPath);
            }
        }
        // 2. levels/ subdirectory
        searchPaths.push_back("levels/" + modelPath);
        // 3. CWD itself
        searchPaths.push_back(modelPath);

        for (const auto& candidate : searchPaths) {
            std::ifstream test(candidate);
            if (test.good()) {
                resolved = candidate;
                break;
            }
        }

        if (!resolved.empty()) {
            modelPath = resolved;
        } else {
            std::cout << "[Grove] Model not found in any search path for: " << modelPath << std::endl;
        }
    }

    bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";
    std::unique_ptr<SceneObject> obj;

    if (isLime) {
        auto loadResult = LimeLoader::load(modelPath);
        if (loadResult.success) {
            obj = LimeLoader::createSceneObject(loadResult.mesh, *ctx->modelRenderer);
        }
    } else {
        auto loadResult = GLBLoader::load(modelPath);
        if (loadResult.success && !loadResult.meshes.empty()) {
            obj = GLBLoader::createSceneObject(loadResult.meshes[0], *ctx->modelRenderer);
        }
    }

    if (!obj) {
        std::cout << "[Grove] Failed to load model: " << modelPath << std::endl;
        return 0;
    }

    obj->setName(name);
    obj->setModelPath(modelPath);

    // Position bottom on terrain using min-vertex-Y offset
    float posX = static_cast<float>(args[2].data.vec3_val.x);
    float posZ = static_cast<float>(args[2].data.vec3_val.z);
    float terrainY = ctx->terrain->getHeightAt(posX, posZ);

    glm::vec3 scale = obj->getTransform().getScale();
    float minVertexY = 0.0f;
    if (obj->hasMeshData()) {
        const auto& verts = obj->getVertices();
        if (!verts.empty()) {
            minVertexY = verts[0].position.y;
            for (const auto& v : verts) {
                if (v.position.y < minVertexY) minVertexY = v.position.y;
            }
        }
    }
    float bottomOffset = -minVertexY * scale.y;
    obj->getTransform().setPosition(glm::vec3(posX, terrainY + bottomOffset, posZ));

    ctx->sceneObjects->push_back(std::move(obj));
    std::cout << "[Grove] Spawned model '" << name << "' at (" << posX << ", " << terrainY + bottomOffset << ", " << posZ << ")" << std::endl;
    result->data.bool_val = 1;
    return 0;
}

// clone(source_name, new_name, pos) → bool
static int32_t groveCloneFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;

    if (argc < 3 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_STRING ||
        args[2].tag != GROVE_VEC3) return 0;

    auto& srcSv = args[0].data.string_val;
    std::string srcName = (srcSv.ptr && srcSv.len > 0) ? std::string(srcSv.ptr, srcSv.len) : "";
    auto& newSv = args[1].data.string_val;
    std::string newName = (newSv.ptr && newSv.len > 0) ? std::string(newSv.ptr, newSv.len) : "";

    if (srcName.empty() || newName.empty()) return 0;

    // Find source object
    SceneObject* original = nullptr;
    for (auto& obj : *ctx->sceneObjects) {
        if (obj && obj->getName() == srcName) { original = obj.get(); break; }
    }
    if (!original) {
        std::cout << "[Grove] clone: source '" << srcName << "' not found" << std::endl;
        return 0;
    }

    std::unique_ptr<SceneObject> newObj;

    if (!original->getModelPath().empty()) {
        std::string modelPath = original->getModelPath();
        bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";
        if (isLime) {
            auto r = LimeLoader::load(modelPath);
            if (r.success) newObj = LimeLoader::createSceneObject(r.mesh, *ctx->modelRenderer);
        } else {
            auto r = GLBLoader::load(modelPath);
            if (r.success && !r.meshes.empty()) newObj = GLBLoader::createSceneObject(r.meshes[0], *ctx->modelRenderer);
        }
        if (newObj) newObj->setModelPath(modelPath);
    } else if (original->hasMeshData()) {
        const auto& verts = original->getVertices();
        const auto& inds = original->getIndices();
        uint32_t handle = ctx->modelRenderer->createModel(verts, inds);
        newObj = std::make_unique<SceneObject>(newName);
        newObj->setBufferHandle(handle);
        newObj->setVertexCount(static_cast<uint32_t>(verts.size()));
        newObj->setIndexCount(static_cast<uint32_t>(inds.size()));
        newObj->setLocalBounds(original->getLocalBounds());
        newObj->setMeshData(verts, inds);
    }

    if (!newObj) {
        std::cout << "[Grove] clone: failed to clone '" << srcName << "'" << std::endl;
        return 0;
    }

    newObj->setName(newName);
    newObj->setEulerRotation(original->getEulerRotation());
    newObj->getTransform().setScale(original->getTransform().getScale());
    newObj->setHueShift(original->getHueShift());
    newObj->setSaturation(original->getSaturation());
    newObj->setBrightness(original->getBrightness());

    // Position bottom on terrain
    float posX = static_cast<float>(args[2].data.vec3_val.x);
    float posZ = static_cast<float>(args[2].data.vec3_val.z);
    float terrainY = ctx->terrain->getHeightAt(posX, posZ);

    glm::vec3 scale = newObj->getTransform().getScale();
    float minVertexY = 0.0f;
    if (newObj->hasMeshData()) {
        const auto& verts = newObj->getVertices();
        if (!verts.empty()) {
            minVertexY = verts[0].position.y;
            for (const auto& v : verts) {
                if (v.position.y < minVertexY) minVertexY = v.position.y;
            }
        }
    }
    float bottomOffset = -minVertexY * scale.y;
    newObj->getTransform().setPosition(glm::vec3(posX, terrainY + bottomOffset, posZ));

    ctx->sceneObjects->push_back(std::move(newObj));
    std::cout << "[Grove] Cloned '" << srcName << "' as '" << newName << "'" << std::endl;
    result->data.bool_val = 1;
    return 0;
}

// object_pos(name) → vec3 or nil
// Returns the world position of a named scene object. Useful for loops in Phase 2+.
static int32_t groveObjectPos(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;

    if (argc < 1 || args[0].tag != GROVE_STRING) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

    for (auto& obj : *ctx->sceneObjects) {
        if (obj && obj->getName() == name) {
            glm::vec3 pos = obj->getTransform().getPosition();
            result->tag = GROVE_VEC3;
            result->data.vec3_val.x = static_cast<double>(pos.x);
            result->data.vec3_val.y = static_cast<double>(pos.y);
            result->data.vec3_val.z = static_cast<double>(pos.z);
            return 0;
        }
    }
    return 0;  // returns nil if not found
}

// set_object_rotation(name, rx, ry, rz) → bool
static int32_t groveSetObjectRotation(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;

    if (argc < 4 || args[0].tag != GROVE_STRING ||
        args[1].tag != GROVE_NUMBER || args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";
    float rx = static_cast<float>(args[1].data.number_val);
    float ry = static_cast<float>(args[2].data.number_val);
    float rz = static_cast<float>(args[3].data.number_val);

    for (auto& obj : *ctx->sceneObjects) {
        if (obj && obj->getName() == name) {
            obj->setEulerRotation(glm::vec3(rx, ry, rz));
            result->data.bool_val = 1;
            return 0;
        }
    }
    return 0;
}

// set_object_scale(name, sx, sy, sz) → bool
static int32_t groveSetObjectScale(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;

    if (argc < 4 || args[0].tag != GROVE_STRING ||
        args[1].tag != GROVE_NUMBER || args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";
    float sx = static_cast<float>(args[1].data.number_val);
    float sy = static_cast<float>(args[2].data.number_val);
    float sz = static_cast<float>(args[3].data.number_val);

    for (auto& obj : *ctx->sceneObjects) {
        if (obj && obj->getName() == name) {
            obj->getTransform().setScale(glm::vec3(sx, sy, sz));
            result->data.bool_val = 1;
            return 0;
        }
    }
    return 0;
}

// delete_object(name) → bool
static int32_t groveDeleteObject(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;

    if (argc < 1 || args[0].tag != GROVE_STRING) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

    for (auto it = ctx->sceneObjects->begin(); it != ctx->sceneObjects->end(); ++it) {
        if (*it && (*it)->getName() == name) {
            ctx->sceneObjects->erase(it);
            std::cout << "[Grove] Deleted object '" << name << "'" << std::endl;
            result->data.bool_val = 1;
            return 0;
        }
    }
    return 0;
}

// ─── Helper: get the current script's behavior INDEX on the current bot target ───
// Returns -1 if not found. Using index instead of pointer avoids dangling pointer
// if the behaviors vector reallocates (e.g. addBehavior from another script).

static int getBotScriptBehaviorIndex(GroveContext* ctx) {
    if (!*ctx->groveBotTarget) return -1;
    auto& behaviors = (*ctx->groveBotTarget)->getBehaviors();
    for (size_t i = 0; i < behaviors.size(); i++) {
        if (behaviors[i].name == *ctx->groveCurrentScriptName) return static_cast<int>(i);
    }
    return -1;
}

// Convenience: get pointer from index (call fresh each time, never cache the pointer)
static Behavior* getBotScriptBehavior(GroveContext* ctx) {
    int idx = getBotScriptBehaviorIndex(ctx);
    if (idx < 0) return nullptr;
    return &(*ctx->groveBotTarget)->getBehaviors()[idx];
}

// ─── Queued construction commands (execute during behavior sequence) ───

// queue_spawn_cube(name, pos, size, r, g, b) — queue a cube spawn
static int32_t groveQueueSpawnCube(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 6) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
        args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER ||
        args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "cube";

    glm::vec3 pos(static_cast<float>(args[1].data.vec3_val.x),
                  static_cast<float>(args[1].data.vec3_val.y),
                  static_cast<float>(args[1].data.vec3_val.z));

    // Encode: "cube|name|size|r|g|b"
    std::string cmd = "cube|" + name + "|" +
        std::to_string(args[2].data.number_val) + "|" +
        std::to_string(args[3].data.number_val) + "|" +
        std::to_string(args[4].data.number_val) + "|" +
        std::to_string(args[5].data.number_val);

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.vec3Param = pos;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// queue_spawn_cylinder(name, pos, radius, height, r, g, b) — queue a cylinder spawn
static int32_t groveQueueSpawnCylinder(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 7) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
        args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER ||
        args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER ||
        args[6].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "cylinder";

    glm::vec3 pos(static_cast<float>(args[1].data.vec3_val.x),
                  static_cast<float>(args[1].data.vec3_val.y),
                  static_cast<float>(args[1].data.vec3_val.z));

    // Encode: "cylinder|name|radius|height|r|g|b"
    std::string cmd = "cylinder|" + name + "|" +
        std::to_string(args[2].data.number_val) + "|" +
        std::to_string(args[3].data.number_val) + "|" +
        std::to_string(args[4].data.number_val) + "|" +
        std::to_string(args[5].data.number_val) + "|" +
        std::to_string(args[6].data.number_val);

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.vec3Param = pos;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// queue_spawn_beam(name, pos1, pos2, thickness, r, g, b) — queue a beam spawn
static int32_t groveQueueSpawnBeam(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 7) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
        args[2].tag != GROVE_VEC3 || args[3].tag != GROVE_NUMBER ||
        args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER ||
        args[6].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "beam";

    // pos1 goes in vec3Param
    glm::vec3 pos1(static_cast<float>(args[1].data.vec3_val.x),
                   static_cast<float>(args[1].data.vec3_val.y),
                   static_cast<float>(args[1].data.vec3_val.z));

    // Encode: "beam|name|p2x|p2y|p2z|thickness|r|g|b"
    std::string cmd = "beam|" + name + "|" +
        std::to_string(args[2].data.vec3_val.x) + "|" +
        std::to_string(args[2].data.vec3_val.y) + "|" +
        std::to_string(args[2].data.vec3_val.z) + "|" +
        std::to_string(args[3].data.number_val) + "|" +
        std::to_string(args[4].data.number_val) + "|" +
        std::to_string(args[5].data.number_val) + "|" +
        std::to_string(args[6].data.number_val);

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.vec3Param = pos1;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// queue_spawn_beam_model(name, path, pos1, pos2) — queue a model beam between two points
static int32_t groveQueueSpawnBeamModel(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 4) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_STRING ||
        args[2].tag != GROVE_VEC3 || args[3].tag != GROVE_VEC3) return 0;

    auto& nameSv = args[0].data.string_val;
    std::string name = (nameSv.ptr && nameSv.len > 0) ? std::string(nameSv.ptr, nameSv.len) : "beam";
    auto& pathSv = args[1].data.string_val;
    std::string path = (pathSv.ptr && pathSv.len > 0) ? std::string(pathSv.ptr, pathSv.len) : "";

    glm::vec3 pos1(static_cast<float>(args[2].data.vec3_val.x),
                   static_cast<float>(args[2].data.vec3_val.y),
                   static_cast<float>(args[2].data.vec3_val.z));

    // Encode: "beam_model|name|path|p2x|p2y|p2z"
    std::string cmd = "beam_model|" + name + "|" + path + "|" +
        std::to_string(args[3].data.vec3_val.x) + "|" +
        std::to_string(args[3].data.vec3_val.y) + "|" +
        std::to_string(args[3].data.vec3_val.z);

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.vec3Param = pos1;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// queue_spawn_wall_panel(name, path, pos1, pos2) — queue a wall panel between two posts
static int32_t groveQueueSpawnWallPanel(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 4) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_STRING ||
        args[2].tag != GROVE_VEC3 || args[3].tag != GROVE_VEC3) return 0;

    auto& nameSv = args[0].data.string_val;
    std::string name = (nameSv.ptr && nameSv.len > 0) ? std::string(nameSv.ptr, nameSv.len) : "wall";
    auto& pathSv = args[1].data.string_val;
    std::string path = (pathSv.ptr && pathSv.len > 0) ? std::string(pathSv.ptr, pathSv.len) : "";

    glm::vec3 pos1(static_cast<float>(args[2].data.vec3_val.x),
                   static_cast<float>(args[2].data.vec3_val.y),
                   static_cast<float>(args[2].data.vec3_val.z));

    // Encode: "wall_panel|name|path|p2x|p2y|p2z"
    std::string cmd = "wall_panel|" + name + "|" + path + "|" +
        std::to_string(args[3].data.vec3_val.x) + "|" +
        std::to_string(args[3].data.vec3_val.y) + "|" +
        std::to_string(args[3].data.vec3_val.z);

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.vec3Param = pos1;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// queue_spawn_model(name, path, pos) — queue a model spawn
static int32_t groveQueueSpawnModel(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 3) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_STRING ||
        args[2].tag != GROVE_VEC3) return 0;

    auto& nameSv = args[0].data.string_val;
    std::string name = (nameSv.ptr && nameSv.len > 0) ? std::string(nameSv.ptr, nameSv.len) : "model";
    auto& pathSv = args[1].data.string_val;
    std::string path = (pathSv.ptr && pathSv.len > 0) ? std::string(pathSv.ptr, pathSv.len) : "";

    glm::vec3 pos(static_cast<float>(args[2].data.vec3_val.x),
                  static_cast<float>(args[2].data.vec3_val.y),
                  static_cast<float>(args[2].data.vec3_val.z));

    // Encode: "model|name|path"
    std::string cmd = "model|" + name + "|" + path;

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.vec3Param = pos;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// queue_set_rotation(name, rx, ry, rz) — queue a rotation change
static int32_t groveQueueSetRotation(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 4) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_NUMBER ||
        args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

    // Encode: "set_rotation|name|rx|ry|rz"
    std::string cmd = "set_rotation|" + name + "|" +
        std::to_string(args[1].data.number_val) + "|" +
        std::to_string(args[2].data.number_val) + "|" +
        std::to_string(args[3].data.number_val);

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// queue_set_scale(name, sx, sy, sz) — queue a scale change
static int32_t groveQueueSetScale(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 4) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_NUMBER ||
        args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

    // Encode: "set_scale|name|sx|sy|sz"
    std::string cmd = "set_scale|" + name + "|" +
        std::to_string(args[1].data.number_val) + "|" +
        std::to_string(args[2].data.number_val) + "|" +
        std::to_string(args[3].data.number_val);

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// queue_delete(name) — queue an object deletion
static int32_t groveQueueDelete(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

    std::string cmd = "delete|" + name;

    Action a;
    a.type = ActionType::GROVE_COMMAND;
    a.stringParam = cmd;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// pickup(name) — queue walking to a named object and picking it up
static int32_t grovePickup(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

    // Find the target object to get its position for walking
    glm::vec3 targetPos(0.0f);
    for (auto& obj : *ctx->sceneObjects) {
        if (obj && obj->getName() == name && obj->isVisible()) {
            targetPos = obj->getTransform().getPosition();
            break;
        }
    }

    // Optional args: gravity (bool), speed (number)
    bool useGravity = false;
    float speed = 2.0f;
    if (argc >= 2 && args[1].tag == GROVE_BOOL) {
        useGravity = args[1].data.bool_val != 0;
    }
    if (argc >= 3 && args[2].tag == GROVE_NUMBER) {
        speed = static_cast<float>(args[2].data.number_val);
        if (speed <= 0.0f) speed = 2.0f;
    }

    Action a;
    a.type = ActionType::PICKUP;
    a.stringParam = name;
    a.vec3Param = targetPos;
    a.boolParam = useGravity;
    a.floatParam = speed;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// place_vertical(target_name) — queue walking to target and placing carried item vertically into it
static int32_t grovePlaceVertical(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    auto& sv = args[0].data.string_val;
    std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

    // Find the target object to get its position for walking
    glm::vec3 targetPos(0.0f);
    for (auto& obj : *ctx->sceneObjects) {
        if (obj && obj->getName() == name) {
            targetPos = obj->getTransform().getPosition();
            break;
        }
    }

    bool useGravity = false;
    float speed = 2.0f;
    if (argc >= 2 && args[1].tag == GROVE_BOOL) {
        useGravity = args[1].data.bool_val != 0;
    }
    if (argc >= 3 && args[2].tag == GROVE_NUMBER) {
        speed = static_cast<float>(args[2].data.number_val);
        if (speed <= 0.0f) speed = 2.0f;
    }

    Action a;
    a.type = ActionType::PLACE_VERTICAL;
    a.stringParam = name;
    a.vec3Param = targetPos;
    a.boolParam = useGravity;
    a.floatParam = speed;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// place_at(pos, [gravity], [speed]) — walk to position and place carried item on terrain
static int32_t grovePlaceAt(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    glm::vec3 pos;
    pos.x = static_cast<float>(args[0].data.vec3_val.x);
    pos.y = static_cast<float>(args[0].data.vec3_val.y);
    pos.z = static_cast<float>(args[0].data.vec3_val.z);

    bool useGravity = false;
    float speed = 2.0f;
    if (argc >= 2 && args[1].tag == GROVE_BOOL) {
        useGravity = args[1].data.bool_val != 0;
    }
    if (argc >= 3 && args[2].tag == GROVE_NUMBER) {
        speed = static_cast<float>(args[2].data.number_val);
        if (speed <= 0.0f) speed = 2.0f;
    }

    Action a;
    a.type = ActionType::PLACE_AT;
    a.vec3Param = pos;
    a.boolParam = useGravity;
    a.floatParam = speed;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// place_horizontal(target_a, target_b, [gravity], [speed])
static int32_t grovePlaceHorizontal(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 2 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_STRING) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    auto& svA = args[0].data.string_val;
    auto& svB = args[1].data.string_val;
    std::string nameA = (svA.ptr && svA.len > 0) ? std::string(svA.ptr, svA.len) : "";
    std::string nameB = (svB.ptr && svB.len > 0) ? std::string(svB.ptr, svB.len) : "";
    if (nameA.empty() || nameB.empty()) return 0;

    // Resolve midpoint from the two target objects
    glm::vec3 posA(0), posB(0);
    for (auto& obj : *ctx->sceneObjects) {
        if (obj && obj->getName() == nameA) posA = obj->getTransform().getPosition();
        if (obj && obj->getName() == nameB) posB = obj->getTransform().getPosition();
    }
    glm::vec3 midpoint = (posA + posB) * 0.5f;

    bool useGravity = false;
    float speed = 2.0f;
    if (argc >= 3 && args[2].tag == GROVE_BOOL) {
        useGravity = args[2].data.bool_val != 0;
    }
    if (argc >= 4 && args[3].tag == GROVE_NUMBER) {
        speed = static_cast<float>(args[3].data.number_val);
        if (speed <= 0.0f) speed = 2.0f;
    }

    Action a;
    a.type = ActionType::PLACE_HORIZONTAL;
    a.stringParam = nameA + "|" + nameB;  // Pipe-delimited target names
    a.vec3Param = midpoint;                // Walk target (midpoint)
    a.boolParam = useGravity;
    a.floatParam = speed;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// place_roof(corner1, corner2, corner3, corner4, [gravity], [speed])
static int32_t grovePlaceRoof(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 4) return 0;
    for (uint32_t i = 0; i < 4; i++) {
        if (args[i].tag != GROVE_STRING) return 0;
    }

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    // Extract 4 corner names
    std::string names[4];
    for (int i = 0; i < 4; i++) {
        auto& sv = args[i].data.string_val;
        names[i] = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";
        if (names[i].empty()) return 0;
    }

    // Resolve center from the 4 corner objects
    glm::vec3 positions[4] = {};
    for (auto& obj : *ctx->sceneObjects) {
        if (!obj) continue;
        for (int i = 0; i < 4; i++) {
            if (obj->getName() == names[i]) positions[i] = obj->getTransform().getPosition();
        }
    }
    glm::vec3 center = (positions[0] + positions[1] + positions[2] + positions[3]) * 0.25f;

    bool useGravity = false;
    float speed = 2.0f;
    if (argc >= 5 && args[4].tag == GROVE_BOOL) {
        useGravity = args[4].data.bool_val != 0;
    }
    if (argc >= 6 && args[5].tag == GROVE_NUMBER) {
        speed = static_cast<float>(args[5].data.number_val);
        if (speed <= 0.0f) speed = 2.0f;
    }

    Action a;
    a.type = ActionType::PLACE_ROOF;
    a.stringParam = names[0] + "|" + names[1] + "|" + names[2] + "|" + names[3];
    a.vec3Param = center;  // Walk target (will be re-resolved at runtime)
    a.boolParam = useGravity;
    a.floatParam = speed;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// place_wall(post_a, post_b, [gravity], [speed])
static int32_t grovePlaceWall(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget || argc < 2 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_STRING) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    auto& svA = args[0].data.string_val;
    auto& svB = args[1].data.string_val;
    std::string nameA = (svA.ptr && svA.len > 0) ? std::string(svA.ptr, svA.len) : "";
    std::string nameB = (svB.ptr && svB.len > 0) ? std::string(svB.ptr, svB.len) : "";
    if (nameA.empty() || nameB.empty()) return 0;

    // Resolve midpoint
    glm::vec3 posA(0), posB(0);
    for (auto& obj : *ctx->sceneObjects) {
        if (obj && obj->getName() == nameA) posA = obj->getTransform().getPosition();
        if (obj && obj->getName() == nameB) posB = obj->getTransform().getPosition();
    }
    glm::vec3 midpoint = (posA + posB) * 0.5f;

    bool useGravity = false;
    float speed = 2.0f;
    if (argc >= 3 && args[2].tag == GROVE_BOOL) {
        useGravity = args[2].data.bool_val != 0;
    }
    if (argc >= 4 && args[3].tag == GROVE_NUMBER) {
        speed = static_cast<float>(args[3].data.number_val);
        if (speed <= 0.0f) speed = 2.0f;
    }

    Action a;
    a.type = ActionType::PLACE_WALL;
    a.stringParam = nameA + "|" + nameB;
    a.vec3Param = midpoint;  // Will be re-resolved at runtime
    a.boolParam = useGravity;
    a.floatParam = speed;
    a.duration = 0.0f;
    b->actions.push_back(a);

    result->data.bool_val = 1;
    return 0;
}

// run_file(path) — load and execute a .grove script file
static int32_t groveRunFile(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (argc < 1 || args[0].tag != GROVE_STRING) return 0;

    auto& sv = args[0].data.string_val;
    std::string path = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";
    if (path.empty()) return 0;

    // Search for the script file in multiple locations
    std::string resolvedPath;
    std::vector<std::string> searchPaths = {
        path,
        "scripts/" + path,
    };
    // Search relative to the loaded level file
    if (ctx->currentLevelPath && !ctx->currentLevelPath->empty()) {
        size_t lastSlash = ctx->currentLevelPath->find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            searchPaths.push_back(ctx->currentLevelPath->substr(0, lastSlash + 1) + path);
        }
    }
    // Also search in the bot target's own scripts folder
    if (*ctx->groveBotTarget) {
        searchPaths.push_back("scripts/" + (*ctx->groveBotTarget)->getName() + "/" + path);
    }
    // Also search all subdirectories of scripts/
    if (std::filesystem::exists("scripts") && std::filesystem::is_directory("scripts")) {
        for (const auto& entry : std::filesystem::directory_iterator("scripts")) {
            if (entry.is_directory()) {
                searchPaths.push_back(entry.path().string() + "/" + path);
            }
        }
    }
    for (const auto& sp : searchPaths) {
        std::ifstream test(sp);
        if (test.good()) {
            resolvedPath = sp;
            break;
        }
    }

    if (resolvedPath.empty()) {
        std::cerr << "[Grove] run_file: Could not find '" << path << "'" << std::endl;
        return 0;
    }

    // Read the file
    std::ifstream file(resolvedPath);
    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    if (source.empty()) {
        std::cerr << "[Grove] run_file: Empty file '" << resolvedPath << "'" << std::endl;
        return 0;
    }

    std::cout << "[Grove] run_file: Executing '" << resolvedPath << "' (" << source.size() << " bytes)" << std::endl;

    // Set behavior name from script filename
    std::string prevScriptName = *ctx->groveCurrentScriptName;
    std::filesystem::path fp(resolvedPath);
    std::string baseName = fp.stem().string();
    if (!baseName.empty()) {
        *ctx->groveCurrentScriptName = baseName;
    }

    // Execute the script in the same VM
    int32_t ret = grove_eval(ctx->groveVm, source.c_str());
    result->data.bool_val = (ret == 0) ? 1 : 0;

    *ctx->groveCurrentScriptName = prevScriptName;
    return 0;
}

// ─── Zone system Grove bindings ───

static int32_t groveZoneTypeFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    if (!ctx->zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
        result->tag = GROVE_STRING;
        result->data.string_val.ptr = "unknown";
        result->data.string_val.len = 7;
        return 0;
    }
    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);
    const char* name = ZoneSystem::zoneTypeName(ctx->zoneSystem->getZoneType(x, z));
    result->tag = GROVE_STRING;
    result->data.string_val.ptr = name;
    result->data.string_val.len = static_cast<uint32_t>(strlen(name));
    return 0;
}

static int32_t groveZoneResourceFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    if (!ctx->zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
        result->tag = GROVE_STRING;
        result->data.string_val.ptr = "none";
        result->data.string_val.len = 4;
        return 0;
    }
    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);
    const char* name = ZoneSystem::resourceTypeName(ctx->zoneSystem->getResource(x, z));
    result->tag = GROVE_STRING;
    result->data.string_val.ptr = name;
    result->data.string_val.len = static_cast<uint32_t>(strlen(name));
    return 0;
}

static int32_t groveZoneOwnerFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    if (!ctx->zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
        result->tag = GROVE_NUMBER;
        result->data.number_val = 0.0;
        return 0;
    }
    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);
    result->tag = GROVE_NUMBER;
    result->data.number_val = static_cast<double>(ctx->zoneSystem->getOwner(x, z));
    return 0;
}

static int32_t groveCanBuildFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    if (!ctx->zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        return 0;
    }
    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);
    // Use player ID 1 as default for script queries
    result->tag = GROVE_BOOL;
    result->data.bool_val = ctx->zoneSystem->canBuild(x, z, 1) ? 1 : 0;
    return 0;
}

static int32_t grovePlotPriceFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    if (!ctx->zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
        result->tag = GROVE_NUMBER;
        result->data.number_val = 0.0;
        return 0;
    }
    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);
    auto g = ctx->zoneSystem->worldToGrid(x, z);
    result->tag = GROVE_NUMBER;
    result->data.number_val = static_cast<double>(ctx->zoneSystem->getPlotPrice(g.x, g.y));
    return 0;
}

// ─── AlgoBot behavior host functions ───

// bot_target(name_string) — select a scene object by name
static int32_t groveBotTargetFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (argc < 1 || args[0].tag != GROVE_STRING) return 0;

    auto& sv = args[0].data.string_val;
    std::string name(sv.ptr, sv.len);
    *ctx->groveBotTarget = nullptr;
    for (auto& obj : *ctx->sceneObjects) {
        if (obj->getName() == name) {
            *ctx->groveBotTarget = obj.get();
            // Ensure it has a behavior with current script name we can append to
            bool found = false;
            for (auto& b : obj->getBehaviors()) {
                if (b.name == *ctx->groveCurrentScriptName) { found = true; break; }
            }
            if (!found) {
                Behavior b;
                b.name = *ctx->groveCurrentScriptName;
                b.trigger = TriggerType::MANUAL;
                b.loop = false;
                b.enabled = true;
                obj->addBehavior(b);
            }
            result->data.bool_val = 1;
            return 0;
        }
    }
    return 0;
}

// move_to(vec3) — queue MOVE_TO action
static int32_t groveMoveTo(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    glm::vec3 target(
        static_cast<float>(args[0].data.vec3_val.x),
        static_cast<float>(args[0].data.vec3_val.y),
        static_cast<float>(args[0].data.vec3_val.z));

    float duration = (argc >= 2 && args[1].tag == GROVE_NUMBER) ? static_cast<float>(args[1].data.number_val) : 2.0f;

    Action a = Action::MoveTo(target, duration);
    // Optional animation param
    if (argc >= 3 && args[2].tag == GROVE_STRING) {
        auto& asv = args[2].data.string_val;
        a.animationParam = std::string(asv.ptr, asv.len);
    }
    b->actions.push_back(a);
    return 0;
}

// rotate_to(vec3) — queue ROTATE_TO action (euler degrees)
static int32_t groveRotateTo(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    glm::vec3 target(
        static_cast<float>(args[0].data.vec3_val.x),
        static_cast<float>(args[0].data.vec3_val.y),
        static_cast<float>(args[0].data.vec3_val.z));

    float duration = (argc >= 2 && args[1].tag == GROVE_NUMBER) ? static_cast<float>(args[1].data.number_val) : 1.0f;

    b->actions.push_back(Action::RotateTo(target, duration));
    return 0;
}

// turn_to(vec3) — queue TURN_TO action (face a world position, yaw only)
static int32_t groveTurnTo(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    glm::vec3 target(
        static_cast<float>(args[0].data.vec3_val.x),
        static_cast<float>(args[0].data.vec3_val.y),
        static_cast<float>(args[0].data.vec3_val.z));

    float duration = (argc >= 2 && args[1].tag == GROVE_NUMBER) ? static_cast<float>(args[1].data.number_val) : 0.5f;

    Action a;
    a.type = ActionType::TURN_TO;
    a.vec3Param = target;
    a.duration = duration;
    b->actions.push_back(a);
    return 0;
}

// wait(seconds) — queue WAIT action
static int32_t groveWait(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_NUMBER) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    float seconds = static_cast<float>(args[0].data.number_val);
    b->actions.push_back(Action::Wait(seconds));
    return 0;
}

// set_visible(bool) — queue SET_VISIBLE action
static int32_t groveSetVisible(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_BOOL) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    b->actions.push_back(Action::SetVisible(args[0].data.bool_val != 0));
    return 0;
}

// play_anim(name_string, duration?) — queue WAIT with animation param
static int32_t grovePlayAnim(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    float duration = (argc >= 2 && args[1].tag == GROVE_NUMBER) ? static_cast<float>(args[1].data.number_val) : 0.0f;

    Action a = Action::Wait(duration);
    auto& asv = args[0].data.string_val;
    a.animationParam = std::string(asv.ptr, asv.len);
    b->actions.push_back(a);
    return 0;
}

// send_signal(signal_name, target_entity?) — queue SEND_SIGNAL action
static int32_t groveSendSignal(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    auto& ssv = args[0].data.string_val;
    std::string signalName(ssv.ptr, ssv.len);
    std::string targetEntity;
    if (argc >= 2 && args[1].tag == GROVE_STRING) {
        auto& tsv = args[1].data.string_val;
        targetEntity = std::string(tsv.ptr, tsv.len);
    }

    b->actions.push_back(Action::SendSignal(signalName, targetEntity));
    return 0;
}

// follow_path(path_name) — queue FOLLOW_PATH action
static int32_t groveFollowPath(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    auto& fsv = args[0].data.string_val;
    b->actions.push_back(Action::FollowPath(std::string(fsv.ptr, fsv.len)));
    return 0;
}

// bot_loop(bool) — set whether the grove_script behavior loops
static int32_t groveBotLoop(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (!b) return 0;

    b->loop = (argc >= 1 && args[0].tag == GROVE_BOOL && args[0].data.bool_val != 0);
    return 0;
}

// bot_clear() — clear all queued actions on current target
static int32_t groveBotClear(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NIL;
    if (!*ctx->groveBotTarget) return 0;

    Behavior* b = getBotScriptBehavior(ctx);
    if (b) b->actions.clear();
    return 0;
}

// bot_run() — mark the grove_script behavior as ready and start it if in play mode
static int32_t groveBotRun(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!*ctx->groveBotTarget) return 0;

    // Use index to avoid pointer invalidation
    int bIdx = getBotScriptBehaviorIndex(ctx);
    if (bIdx < 0) return 0;

    auto& behaviors = (*ctx->groveBotTarget)->getBehaviors();
    if (bIdx >= static_cast<int>(behaviors.size())) return 0;  // bounds check

    Behavior& b = behaviors[bIdx];

    // Set trigger to ON_COMMAND — only runs when explicitly triggered
    b.trigger = TriggerType::ON_COMMAND;
    b.enabled = true;
    result->data.bool_val = 1;

    // If already in play mode, start the behavior immediately
    if (*ctx->isPlayMode && !b.actions.empty()) {
        (*ctx->groveBotTarget)->setActiveBehaviorIndex(bIdx);
        (*ctx->groveBotTarget)->setActiveActionIndex(0);
        (*ctx->groveBotTarget)->resetPathComplete();
        (*ctx->groveBotTarget)->clearPathWaypoints();

        if (b.actions[0].type == ActionType::FOLLOW_PATH) {
            ctx->loadPathForAction(*ctx->groveBotTarget, b.actions[0]);
        }
    }
    return 0;
}

// ─── Player economy host functions ───

// get_credits() → number
static int32_t groveGetCredits(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NUMBER;
    result->data.number_val = static_cast<double>(*ctx->playerCredits);
    return 0;
}

// add_credits(amount) → number (new balance)
static int32_t groveAddCredits(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_NUMBER;
    if (argc >= 1 && args[0].tag == GROVE_NUMBER) {
        float amount = static_cast<float>(args[0].data.number_val);
        if (amount > 0) *ctx->playerCredits += amount;
    }
    result->data.number_val = static_cast<double>(*ctx->playerCredits);
    return 0;
}

// deduct_credits(amount) → bool (true if sufficient funds, false if not)
static int32_t groveDeductCredits(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (argc >= 1 && args[0].tag == GROVE_NUMBER) {
        float amount = static_cast<float>(args[0].data.number_val);
        if (amount > 0 && *ctx->playerCredits >= amount) {
            *ctx->playerCredits -= amount;
            result->data.bool_val = 1;
        }
    }
    return 0;
}

// buy_plot(vec3) → bool (true if purchased, false if can't afford or already owned)
static int32_t groveBuyPlot(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!ctx->zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);

    // Check if plot is already owned
    uint32_t owner = ctx->zoneSystem->getOwner(x, z);
    if (owner != 0) return 0;  // Already owned

    // Check if zone allows building/purchasing
    ZoneType zt = ctx->zoneSystem->getZoneType(x, z);
    if (zt == ZoneType::Battlefield || zt == ZoneType::SpawnSafe) return 0;  // Can't buy these

    // Get price
    auto g = ctx->zoneSystem->worldToGrid(x, z);
    float price = ctx->zoneSystem->getPlotPrice(g.x, g.y);

    // Check funds
    if (*ctx->playerCredits < price) return 0;  // Can't afford

    // Purchase!
    *ctx->playerCredits -= price;
    ctx->zoneSystem->setOwner(g.x, g.y, 1);  // Player ID 1

    std::cout << "[Economy] Purchased plot (" << g.x << ", " << g.y << ") for "
              << static_cast<int>(price) << " CR. Balance: "
              << static_cast<int>(*ctx->playerCredits) << " CR" << std::endl;

    // Spawn corner boundary posts
    ctx->spawnPlotPosts(g.x, g.y);

    result->data.bool_val = 1;
    return 0;
}

// sell_plot(vec3) → bool (true if sold, refunds 50% of current price)
static int32_t groveSellPlot(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_BOOL;
    result->data.bool_val = 0;
    if (!ctx->zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);

    // Must own this plot
    uint32_t owner = ctx->zoneSystem->getOwner(x, z);
    if (owner != 1) return 0;  // Not our plot

    auto g = ctx->zoneSystem->worldToGrid(x, z);
    float price = ctx->zoneSystem->getPlotPrice(g.x, g.y);
    float refund = price * 0.5f;

    *ctx->playerCredits += refund;
    ctx->zoneSystem->setOwner(g.x, g.y, 0);  // Unown

    std::cout << "[Economy] Sold plot (" << g.x << ", " << g.y << ") for "
              << static_cast<int>(refund) << " CR. Balance: "
              << static_cast<int>(*ctx->playerCredits) << " CR" << std::endl;

    // Remove corner boundary posts
    ctx->removePlotPosts(g.x, g.y);

    result->data.bool_val = 1;
    return 0;
}

// plot_status(vec3) → string ("available", "owned", "spawn_zone", "battlefield", "too_expensive")
static int32_t grovePlotStatus(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
    auto* ctx = static_cast<GroveContext*>(ud);
    result->tag = GROVE_STRING;

    static const char* s_available = "available";
    static const char* s_owned = "owned";
    static const char* s_spawn = "spawn_zone";
    static const char* s_battlefield = "battlefield";
    static const char* s_expensive = "too_expensive";
    static const char* s_unknown = "unknown";

    if (!ctx->zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
        result->data.string_val.ptr = s_unknown;
        result->data.string_val.len = 7;
        return 0;
    }

    float x = static_cast<float>(args[0].data.vec3_val.x);
    float z = static_cast<float>(args[0].data.vec3_val.z);

    ZoneType zt = ctx->zoneSystem->getZoneType(x, z);
    if (zt == ZoneType::SpawnSafe) {
        result->data.string_val.ptr = s_spawn;
        result->data.string_val.len = static_cast<uint32_t>(strlen(s_spawn));
        return 0;
    }
    if (zt == ZoneType::Battlefield) {
        result->data.string_val.ptr = s_battlefield;
        result->data.string_val.len = static_cast<uint32_t>(strlen(s_battlefield));
        return 0;
    }

    uint32_t owner = ctx->zoneSystem->getOwner(x, z);
    if (owner != 0) {
        result->data.string_val.ptr = s_owned;
        result->data.string_val.len = static_cast<uint32_t>(strlen(s_owned));
        return 0;
    }

    auto g = ctx->zoneSystem->worldToGrid(x, z);
    float price = ctx->zoneSystem->getPlotPrice(g.x, g.y);
    if (*ctx->playerCredits < price) {
        result->data.string_val.ptr = s_expensive;
        result->data.string_val.len = static_cast<uint32_t>(strlen(s_expensive));
        return 0;
    }

    result->data.string_val.ptr = s_available;
    result->data.string_val.len = static_cast<uint32_t>(strlen(s_available));
    return 0;
}

// ─── Registration ───

void registerGroveHostFunctions(GroveVm* vm, GroveContext* ctx) {
    grove_register_fn(vm, "log", groveLogFn, ctx->groveOutputAccum);
    grove_register_fn(vm, "terrain_height", groveTerrainHeightFn, ctx);
    grove_register_fn(vm, "sin", groveSinFn, ctx);
    grove_register_fn(vm, "cos", groveCosFn, ctx);
    grove_register_fn(vm, "atan2", groveAtan2Fn, ctx);
    grove_register_fn(vm, "sqrt", groveSqrtFn, ctx);
    grove_register_fn(vm, "abs", groveAbsFn, ctx);

    // Construction primitives
    grove_register_fn(vm, "get_player_pos", groveGetPlayerPos, ctx);
    grove_register_fn(vm, "spawn_cube", groveSpawnCubeFn, ctx);
    grove_register_fn(vm, "spawn_cylinder", groveSpawnCylinderFn, ctx);
    grove_register_fn(vm, "spawn_beam", groveSpawnBeamFn, ctx);
    grove_register_fn(vm, "spawn_model", groveSpawnModelFn, ctx);
    grove_register_fn(vm, "clone", groveCloneFn, ctx);
    grove_register_fn(vm, "object_pos", groveObjectPos, ctx);
    grove_register_fn(vm, "set_object_rotation", groveSetObjectRotation, ctx);
    grove_register_fn(vm, "set_object_scale", groveSetObjectScale, ctx);
    grove_register_fn(vm, "delete_object", groveDeleteObject, ctx);

    // Queued construction commands (for behavior sequences)
    grove_register_fn(vm, "queue_spawn_cube", groveQueueSpawnCube, ctx);
    grove_register_fn(vm, "queue_spawn_cylinder", groveQueueSpawnCylinder, ctx);
    grove_register_fn(vm, "queue_spawn_beam", groveQueueSpawnBeam, ctx);
    grove_register_fn(vm, "queue_spawn_model", groveQueueSpawnModel, ctx);
    grove_register_fn(vm, "queue_spawn_beam_model", groveQueueSpawnBeamModel, ctx);
    grove_register_fn(vm, "queue_spawn_wall_panel", groveQueueSpawnWallPanel, ctx);
    grove_register_fn(vm, "queue_set_rotation", groveQueueSetRotation, ctx);
    grove_register_fn(vm, "queue_set_scale", groveQueueSetScale, ctx);
    grove_register_fn(vm, "queue_delete", groveQueueDelete, ctx);

    grove_register_fn(vm, "zone_type", groveZoneTypeFn, ctx);
    grove_register_fn(vm, "zone_resource", groveZoneResourceFn, ctx);
    grove_register_fn(vm, "zone_owner", groveZoneOwnerFn, ctx);
    grove_register_fn(vm, "can_build", groveCanBuildFn, ctx);
    grove_register_fn(vm, "plot_price", grovePlotPriceFn, ctx);

    // AlgoBot behavior functions
    grove_register_fn(vm, "bot_target", groveBotTargetFn, ctx);
    grove_register_fn(vm, "move_to", groveMoveTo, ctx);
    grove_register_fn(vm, "rotate_to", groveRotateTo, ctx);
    grove_register_fn(vm, "turn_to", groveTurnTo, ctx);
    grove_register_fn(vm, "wait", groveWait, ctx);
    grove_register_fn(vm, "set_visible", groveSetVisible, ctx);
    grove_register_fn(vm, "play_anim", grovePlayAnim, ctx);
    grove_register_fn(vm, "send_signal", groveSendSignal, ctx);
    grove_register_fn(vm, "follow_path", groveFollowPath, ctx);
    grove_register_fn(vm, "bot_loop", groveBotLoop, ctx);
    grove_register_fn(vm, "bot_clear", groveBotClear, ctx);
    grove_register_fn(vm, "bot_run", groveBotRun, ctx);
    grove_register_fn(vm, "pickup", grovePickup, ctx);
    grove_register_fn(vm, "place_vertical", grovePlaceVertical, ctx);
    grove_register_fn(vm, "place_at", grovePlaceAt, ctx);
    grove_register_fn(vm, "place_horizontal", grovePlaceHorizontal, ctx);
    grove_register_fn(vm, "place_roof", grovePlaceRoof, ctx);
    grove_register_fn(vm, "place_wall", grovePlaceWall, ctx);
    grove_register_fn(vm, "run_file", groveRunFile, ctx);

    // Player economy functions
    grove_register_fn(vm, "get_credits", groveGetCredits, ctx);
    grove_register_fn(vm, "add_credits", groveAddCredits, ctx);
    grove_register_fn(vm, "deduct_credits", groveDeductCredits, ctx);
    grove_register_fn(vm, "buy_plot", groveBuyPlot, ctx);
    grove_register_fn(vm, "sell_plot", groveSellPlot, ctx);
    grove_register_fn(vm, "plot_status", grovePlotStatus, ctx);
}
