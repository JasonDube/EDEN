#include <eden/LevelInstantiator.hpp>

#include <eden/LevelSerializer.hpp>   // LevelData
#include <eden/Terrain.hpp>           // Terrain, TerrainConfig
#include <eden/Action.hpp>            // Behavior, Action, ActionType, TriggerType
#include <eden/PhysicsWorld.hpp>      // PhysicsWorld, BulletCollisionType
#include "Editor/ChunkManager.hpp"
#include "Editor/SceneObject.hpp"     // SceneObject, AABB, BeingType
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Editor/GLBLoader.hpp"
#include "Editor/LimeLoader.hpp"
#include "Editor/SkinnedGLBLoader.hpp"
#include "Editor/BinaryLevelReader.hpp"  // BinaryLevelReader, BinaryLevelData
#include "Renderer/VulkanContext.hpp"
#include "Renderer/ModelRenderer.hpp"          // ModelRenderer, ModelVertex
#include "Renderer/SkinnedModelRenderer.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <climits>
#include <cfloat>
#include <iostream>

namespace eden {

void LevelInstantiator::applyTerrain(const LevelData& data,
                                     Terrain& terrain,
                                     ChunkManager& chunkManager,
                                     VulkanContext& context) {
    // Restore the terrain's world scale + bounds so the level reloads at the
    // size it was authored — instead of dropping its height pixels into
    // whatever (possibly giant default) terrain is currently active. Only
    // levels saved with this field reconfigure; older levels keep old behavior.
    if (data.hasTerrainConfig) {
        TerrainConfig tcfg = terrain.getConfig();
        tcfg.tileSize        = data.terrainTileSize;
        tcfg.chunkResolution = data.terrainChunkResolution;
        tcfg.heightScale     = data.terrainHeightScale;
        tcfg.useFixedBounds  = data.terrainUseFixedBounds;
        tcfg.minChunk        = data.terrainMinChunk;
        tcfg.maxChunk        = data.terrainMaxChunk;
        tcfg.wrapWorld       = data.terrainWrapWorld;
        tcfg.stretchTexToBounds = data.terrainStretchTex;
        context.waitIdle();
        chunkManager.releaseAllChunkBuffers(terrain);
        terrain.reconfigure(tcfg);
        chunkManager.preloadAllChunks(terrain, nullptr);
    } else if (!data.chunks.empty()) {
        // Old levels (saved before hasTerrainConfig existed) carry no bounds.
        // Derive them from the chunk coords in the file, otherwise loading an
        // archive while the tiny worktable terrain (1x1 chunk) is active
        // applies 1 of N chunks and the level comes up as a postage stamp.
        glm::ivec2 mn(INT_MAX), mx(INT_MIN);
        for (const auto& c : data.chunks) {
            mn = glm::min(mn, glm::ivec2(c.coord));
            mx = glm::max(mx, glm::ivec2(c.coord));
        }
        const TerrainConfig& cur = terrain.getConfig();
        bool covers = cur.useFixedBounds &&
                      cur.minChunk.x <= mn.x && cur.minChunk.y <= mn.y &&
                      cur.maxChunk.x >= mx.x && cur.maxChunk.y >= mx.y;
        if (!covers) {
            TerrainConfig tcfg = cur;
            tcfg.useFixedBounds = true;
            tcfg.minChunk = mn;
            tcfg.maxChunk = mx;
            // Classic full-world levels (32x32 at -16..15) were wrap-worlds.
            tcfg.wrapWorld = (mn == glm::ivec2(-16, -16) && mx == glm::ivec2(15, 15));
            // Old archives carry no terrain scale of their own, so do NOT inherit
            // it from `cur` (the previously-loaded level) — that made an archive's
            // world size depend on load order. Loading akelba after a tileSize=3.97
            // level stretched it ~2x and shifted terrain features hundreds of feet;
            // loading it fresh (worktable tileSize 2.0) looked correct. Reset to the
            // classic old-world scale — the LevelData defaults (2.0 / 64 / 200),
            // which is exactly what a fresh-booted editor's worktable uses — so old
            // archives load identically regardless of what was open before.
            tcfg.tileSize        = data.terrainTileSize;        // 2.0 for old archives
            tcfg.chunkResolution = data.terrainChunkResolution; // 64
            tcfg.heightScale     = data.terrainHeightScale;     // 200
            tcfg.stretchTexToBounds = false;
            std::cout << "[LevelInstantiator] Old level without terrain config — derived bounds ("
                      << mn.x << "," << mn.y << ")..(" << mx.x << "," << mx.y << ")" << std::endl;
            context.waitIdle();
            chunkManager.releaseAllChunkBuffers(terrain);
            terrain.reconfigure(tcfg);
            chunkManager.preloadAllChunks(terrain, nullptr);
        }
    }

    LevelSerializer::applyToTerrain(data, terrain);
}

void LevelInstantiator::spawnObjects(const LevelData& data, const SpawnContext& ctx) {
    // NOTE: PrimitiveType here is eden::PrimitiveType (SceneObject.hpp) — the
    // renderer enum that includes Door/Wedge — matching the original loop, NOT
    // the smaller LevelData::PrimitiveType. objData.primitiveType is a raw int.
    for (const auto& objData : data.objects) {
        // Skip objects with no model path AND no primitive type
        if (objData.modelPath.empty() && objData.primitiveType == 0) continue;

        std::unique_ptr<SceneObject> obj;

        // Check if this is a primitive object
        if (objData.primitiveType != 0) {
            PrimitiveType primType = static_cast<PrimitiveType>(objData.primitiveType);
            PrimitiveMeshBuilder::MeshData meshData;

            switch (primType) {
                case PrimitiveType::Cube:
                    meshData = PrimitiveMeshBuilder::createCube(objData.primitiveSize, objData.primitiveColor);
                    break;
                case PrimitiveType::Cylinder:
                    meshData = PrimitiveMeshBuilder::createCylinder(
                        objData.primitiveRadius, objData.primitiveHeight,
                        objData.primitiveSegments, objData.primitiveColor);
                    break;
                case PrimitiveType::SpawnMarker:
                    meshData = PrimitiveMeshBuilder::createSpawnMarker(objData.primitiveSize);
                    break;
                case PrimitiveType::Wedge:
                    meshData = PrimitiveMeshBuilder::createWedge(objData.primitiveSize, objData.slopeRatio, objData.primitiveColor);
                    break;
                case PrimitiveType::Door:
                    meshData = PrimitiveMeshBuilder::createCube(objData.primitiveSize, objData.primitiveColor);
                    break;
                default:
                    std::cerr << "Unknown primitive type: " << objData.primitiveType << std::endl;
                    continue;
            }

            std::string name = objData.name.empty() ? "Primitive" : objData.name;
            obj = std::make_unique<SceneObject>(name);
            uint32_t handle = ctx.modelRenderer.createModel(meshData.vertices, meshData.indices);
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
            obj->setLocalBounds(meshData.bounds);

            // Store primitive info for re-saving
            obj->setPrimitiveType(primType);
            obj->setPrimitiveSize(objData.primitiveSize);
            obj->setPrimitiveRadius(objData.primitiveRadius);
            obj->setPrimitiveHeight(objData.primitiveHeight);
            obj->setPrimitiveSegments(objData.primitiveSegments);
            obj->setPrimitiveColor(objData.primitiveColor);
            obj->setSlopeRatio(objData.slopeRatio);

            // Apply door properties if this is a door
            if (primType == PrimitiveType::Door) {
                obj->setDoorId(objData.doorId);
                obj->setTargetLevel(objData.targetLevel);
                obj->setTargetDoorId(objData.targetDoorId);
            }

            std::cout << "Loaded primitive: " << name << std::endl;
        }
        // Check if this is a skinned model
        else if (objData.isSkinned) {
            auto result = SkinnedGLBLoader::load(objData.modelPath);
            if (!result.success || result.meshes.empty()) {
                std::cerr << "Failed to load skinned model: " << objData.modelPath << std::endl;
                continue;
            }

            const auto& mesh = result.meshes[0];

            // Create GPU resources for skinned model
            uint32_t handle = ctx.skinnedRenderer.createModel(
                mesh.vertices,
                mesh.indices,
                std::make_unique<Skeleton>(*result.skeleton),
                result.animations,
                mesh.hasTexture ? mesh.textureData.data() : nullptr,
                mesh.textureWidth,
                mesh.textureHeight
            );

            obj = std::make_unique<SceneObject>(mesh.name);
            obj->setSkinnedModelHandle(handle);
            obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));

            // Store animation info and play
            auto animNames = ctx.skinnedRenderer.getAnimationNames(handle);
            obj->setAnimationNames(animNames);
            if (!objData.currentAnimation.empty()) {
                ctx.skinnedRenderer.playAnimation(handle, objData.currentAnimation, true);
                obj->setCurrentAnimation(objData.currentAnimation);
            } else if (!animNames.empty()) {
                ctx.skinnedRenderer.playAnimation(handle, animNames[0], true);
                obj->setCurrentAnimation(animNames[0]);
            }

            std::cout << "Loaded skinned model: " << objData.modelPath << std::endl;
        } else if (objData.modelPath.size() >= 5 &&
                   objData.modelPath.substr(objData.modelPath.size() - 5) == ".lime") {
            // LIME format model (deduplicate GPU buffers by path)
            auto result = LimeLoader::load(objData.modelPath);
            if (!result.success) {
                std::cerr << "Failed to load LIME model: " << objData.modelPath << std::endl;
                continue;
            }
            obj = LimeLoader::createSceneObject(result.mesh, ctx.modelRenderer);
            if (!obj) {
                std::cerr << "Failed to create scene object for LIME: " << objData.modelPath << std::endl;
                continue;
            }
        } else {
            // Static GLB model
            auto result = GLBLoader::load(objData.modelPath);
            if (!result.success || result.meshes.empty()) {
                std::cerr << "Failed to load model: " << objData.modelPath << std::endl;
                continue;
            }

            const auto& mesh = result.meshes[0];
            obj = GLBLoader::createSceneObject(mesh, ctx.modelRenderer);
            if (!obj) {
                std::cerr << "Failed to create scene object for: " << objData.modelPath << std::endl;
                continue;
            }
        }

        obj->setModelPath(objData.modelPath);
        obj->getTransform().setPosition(objData.position);
        obj->setEulerRotation(objData.rotation);
        obj->getTransform().setScale(objData.scale);
        obj->setHueShift(objData.hueShift);
        obj->setSaturation(objData.saturation);
        obj->setBrightness(objData.brightness);
        obj->setVisible(objData.visible);
        obj->setAABBCollision(objData.aabbCollision);
        obj->setPolygonCollision(objData.polygonCollision);
        obj->setBulletCollisionType(static_cast<BulletCollisionType>(objData.bulletCollisionType));
        obj->setKinematicPlatform(objData.kinematicPlatform);
        obj->setIndoor(objData.indoor);
        obj->setBeingType(static_cast<BeingType>(objData.beingType));
        if (!objData.groveScript.empty()) {
            obj->setGroveScriptPath(objData.groveScript);
        }
        if (!objData.entityScript.empty()) {
            obj->setEntityScript(objData.entityScript);
        }

        // Apply frozen transform if saved (re-bake rotation/scale into vertices)
        if (objData.frozenTransform && obj->hasMeshData()) {
            // Convert euler angles to quaternion
            glm::vec3 radians = glm::radians(objData.frozenRotation);
            glm::quat rotation = glm::quat(radians);
            glm::vec3 scale = objData.frozenScale;

            // Transform vertices
            std::vector<ModelVertex> vertices = obj->getVertices();
            glm::mat4 rotMat = glm::mat4_cast(rotation);
            glm::mat3 normalMat = glm::mat3(rotMat);

            glm::vec3 minBounds(FLT_MAX);
            glm::vec3 maxBounds(-FLT_MAX);

            for (auto& v : vertices) {
                glm::vec3 scaledPos = v.position * scale;
                v.position = glm::vec3(rotMat * glm::vec4(scaledPos, 1.0f));
                v.normal = glm::normalize(normalMat * v.normal);
                minBounds = glm::min(minBounds, v.position);
                maxBounds = glm::max(maxBounds, v.position);
            }

            // Update mesh data and bounds
            obj->setMeshData(vertices, obj->getIndices());
            AABB newBounds;
            newBounds.min = minBounds;
            newBounds.max = maxBounds;
            obj->setLocalBounds(newBounds);

            // Update GPU buffer
            if (obj->getBufferHandle() != UINT32_MAX) {
                ctx.modelRenderer.updateModelBuffer(obj->getBufferHandle(), vertices);
            }

            // Store frozen transform for future saves
            obj->setFrozenTransform(objData.frozenRotation, objData.frozenScale);

            std::cout << "Applied frozen transform for " << objData.name << std::endl;
        }

        // Add to Bullet physics world if it has Bullet collision
        if (obj->hasBulletCollision() && ctx.physicsWorld) {
            ctx.physicsWorld->addObject(obj.get(), obj->getBulletCollisionType());
        }
        obj->setDailySchedule(objData.dailySchedule);
        obj->setPatrolSpeed(objData.patrolSpeed);
        if (!objData.description.empty()) {
            obj->setDescription(objData.description);
        }
        if (!objData.buildingType.empty()) {
            obj->setBuildingType(objData.buildingType);
        }

        // Restore wall holes
        for (const auto& [hMin, hMax] : objData.wallHoles) {
            obj->addWallHole(hMin, hMax);
        }

        // Restore behaviors
        for (const auto& behData : objData.behaviors) {
            Behavior behavior;
            behavior.name = behData.name;
            behavior.trigger = static_cast<TriggerType>(behData.trigger);
            behavior.triggerParam = behData.triggerParam;
            behavior.triggerRadius = behData.triggerRadius;
            behavior.loop = behData.loop;
            behavior.enabled = behData.enabled;

            for (const auto& actData : behData.actions) {
                Action action;
                action.type = static_cast<ActionType>(actData.type);
                action.vec3Param = actData.vec3Param;
                action.floatParam = actData.floatParam;
                action.stringParam = actData.stringParam;
                action.animationParam = actData.animationParam;
                action.boolParam = actData.boolParam;
                action.easing = static_cast<Action::Easing>(actData.easing);
                action.duration = actData.duration;
                behavior.actions.push_back(action);
            }
            obj->addBehavior(behavior);
        }

        ctx.sceneObjects.push_back(std::move(obj));
    }
}

bool LevelInstantiator::spawnObjectsBinary(const std::string& levelPath,
                                           const LevelData& data,
                                           const SpawnContext& ctx) {
    std::string binPath = BinaryLevelReader::getBinaryPath(levelPath);

    if (!BinaryLevelReader::exists(binPath)) {
        return false;
    }

    BinaryLevelReader reader;
    BinaryLevelData binData = reader.load(binPath);

    if (!binData.success) {
        std::cerr << "Binary level load failed: " << binData.error << std::endl;
        return false;
    }

    // Verify object counts match
    if (binData.objects.size() != data.objects.size()) {
        std::cerr << "Binary/JSON object count mismatch, falling back to JSON" << std::endl;
        return false;
    }

    std::cout << "Loading from binary format (" << binPath << ")" << std::endl;

    // Load each object from binary data
    for (size_t i = 0; i < binData.objects.size(); ++i) {
        const auto& binObj = binData.objects[i];
        const auto& jsonObj = data.objects[i];

        std::unique_ptr<SceneObject> obj;

        // Skinned models still need GLB loading
        if (binObj.isSkinned) {
            auto result = SkinnedGLBLoader::load(binObj.modelPath);
            if (!result.success || result.meshes.empty()) {
                std::cerr << "Failed to load skinned model: " << binObj.modelPath << std::endl;
                continue;
            }

            const auto& mesh = result.meshes[0];
            uint32_t handle = ctx.skinnedRenderer.createModel(
                mesh.vertices,
                mesh.indices,
                std::make_unique<Skeleton>(*result.skeleton),
                result.animations,
                mesh.hasTexture ? mesh.textureData.data() : nullptr,
                mesh.textureWidth,
                mesh.textureHeight
            );

            obj = std::make_unique<SceneObject>(mesh.name);
            obj->setSkinnedModelHandle(handle);
            obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));

            auto animNames = ctx.skinnedRenderer.getAnimationNames(handle);
            obj->setAnimationNames(animNames);
            if (!jsonObj.currentAnimation.empty()) {
                ctx.skinnedRenderer.playAnimation(handle, jsonObj.currentAnimation, true);
                obj->setCurrentAnimation(jsonObj.currentAnimation);
            } else if (!animNames.empty()) {
                ctx.skinnedRenderer.playAnimation(handle, animNames[0], true);
                obj->setCurrentAnimation(animNames[0]);
            }
        }
        // Objects with mesh data in binary
        else if (binObj.meshId >= 0 && binObj.meshId < static_cast<int32_t>(binData.meshes.size())) {
            const auto& meshData = binData.meshes[binObj.meshId];

            obj = std::make_unique<SceneObject>(binObj.name);

            // Upload mesh to GPU with optional texture
            const unsigned char* texData = nullptr;
            int texW = 0, texH = 0;
            if (meshData.textureId >= 0 && meshData.textureId < static_cast<int32_t>(binData.textures.size())) {
                const auto& tex = binData.textures[meshData.textureId];
                texData = tex.pixels.data();
                texW = tex.width;
                texH = tex.height;
            }

            uint32_t handle = ctx.modelRenderer.createModel(
                meshData.vertices,
                meshData.indices,
                texData, texW, texH
            );

            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
            obj->setLocalBounds(meshData.bounds);

            // Store mesh data for raycasting
            obj->setMeshData(meshData.vertices, meshData.indices);

            // Store texture data for painting
            if (texData && texW > 0 && texH > 0) {
                const auto& tex = binData.textures[meshData.textureId];
                obj->setTextureData(tex.pixels, texW, texH);
            } else {
                // Create default white texture
                std::vector<unsigned char> defaultTex(256 * 256 * 4, 255);
                obj->setTextureData(defaultTex, 256, 256);
            }
        }
        // Fallback: no mesh in binary, try loading from model path or generate primitive
        else if (binObj.isPrimitive) {
            PrimitiveType primType = static_cast<PrimitiveType>(binObj.primitiveType);
            PrimitiveMeshBuilder::MeshData meshData;

            switch (primType) {
                case PrimitiveType::Cube:
                    meshData = PrimitiveMeshBuilder::createCube(binObj.primitiveSize, binObj.primitiveColor);
                    break;
                case PrimitiveType::Cylinder:
                    meshData = PrimitiveMeshBuilder::createCylinder(
                        binObj.primitiveRadius, binObj.primitiveHeight,
                        binObj.primitiveSegments, binObj.primitiveColor);
                    break;
                case PrimitiveType::SpawnMarker:
                    meshData = PrimitiveMeshBuilder::createSpawnMarker(binObj.primitiveSize);
                    break;
                case PrimitiveType::Wedge:
                    meshData = PrimitiveMeshBuilder::createWedge(binObj.primitiveSize, binObj.slopeRatio, binObj.primitiveColor);
                    break;
                case PrimitiveType::Door:
                    meshData = PrimitiveMeshBuilder::createCube(binObj.primitiveSize, binObj.primitiveColor);
                    break;
                case PrimitiveType::NPC:
                    meshData = PrimitiveMeshBuilder::createCube(binObj.primitiveSize, binObj.primitiveColor);
                    break;
                default:
                    std::cerr << "Unknown primitive type in binary: " << binObj.primitiveType << std::endl;
                    continue;
            }

            obj = std::make_unique<SceneObject>(binObj.name);
            uint32_t handle = ctx.modelRenderer.createModel(meshData.vertices, meshData.indices);
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
            obj->setLocalBounds(meshData.bounds);
            obj->setMeshData(meshData.vertices, meshData.indices);
        }
        else if (!binObj.modelPath.empty()) {
            // Load from file as fallback (supports both GLB and LIME)
            bool isLime = binObj.modelPath.size() >= 5 &&
                binObj.modelPath.substr(binObj.modelPath.size() - 5) == ".lime";
            if (isLime) {
                auto result = LimeLoader::load(binObj.modelPath);
                if (!result.success) {
                    std::cerr << "Failed to load LIME model: " << binObj.modelPath << std::endl;
                    continue;
                }
                obj = LimeLoader::createSceneObject(result.mesh, ctx.modelRenderer);
                if (!obj) continue;
            } else {
                auto result = GLBLoader::load(binObj.modelPath);
                if (!result.success || result.meshes.empty()) {
                    std::cerr << "Failed to load model: " << binObj.modelPath << std::endl;
                    continue;
                }
                const auto& mesh = result.meshes[0];
                obj = GLBLoader::createSceneObject(mesh, ctx.modelRenderer);
                if (!obj) continue;
            }
        }
        else {
            continue;  // Skip invalid objects
        }

        // Apply properties from binary
        obj->setModelPath(binObj.modelPath);

        // Restore ports and CPs from .lime file (binary cache doesn't store these)
        if (binObj.modelPath.size() >= 5 &&
            binObj.modelPath.substr(binObj.modelPath.size() - 5) == ".lime") {
            auto limeResult = LimeLoader::load(binObj.modelPath);
            if (limeResult.success) {
                if (!limeResult.mesh.controlPoints.empty()) {
                    std::vector<SceneObject::StoredControlPoint> cps;
                    for (const auto& cp : limeResult.mesh.controlPoints)
                        cps.push_back({cp.vertexIndex, cp.name});
                    obj->setControlPoints(cps);
                }
                if (!limeResult.mesh.ports.empty()) {
                    std::vector<SceneObject::StoredPort> ports;
                    for (const auto& p : limeResult.mesh.ports)
                        ports.push_back({p.name, p.position, p.forward, p.up});
                    obj->setPorts(ports);
                }
                if (!limeResult.mesh.metadata.empty()) {
                    obj->setModelMetadata(limeResult.mesh.metadata);
                }
            }
        }

        obj->getTransform().setPosition(binObj.position);
        obj->setEulerRotation(binObj.rotation);
        obj->getTransform().setScale(binObj.scale);
        obj->setHueShift(binObj.hueShift);
        obj->setSaturation(binObj.saturation);
        obj->setBrightness(binObj.brightness);
        obj->setVisible(binObj.visible);
        obj->setAABBCollision(binObj.aabbCollision);
        obj->setPolygonCollision(binObj.polygonCollision);
        obj->setBulletCollisionType(static_cast<BulletCollisionType>(binObj.bulletCollisionType));
        obj->setKinematicPlatform(binObj.kinematicPlatform);
        obj->setBeingType(static_cast<BeingType>(binObj.beingType));
        obj->setDailySchedule(binObj.dailySchedule);
        obj->setPatrolSpeed(binObj.patrolSpeed);
        if (!binObj.description.empty()) {
            obj->setDescription(binObj.description);
        }
        if (!binObj.buildingType.empty()) {
            obj->setBuildingType(binObj.buildingType);
        }
        obj->setTransparent(binObj.transparent);
        obj->setIndoor(binObj.indoor);

        // Primitive properties
        if (binObj.isPrimitive) {
            obj->setPrimitiveType(static_cast<PrimitiveType>(binObj.primitiveType));
            obj->setPrimitiveSize(binObj.primitiveSize);
            obj->setPrimitiveRadius(binObj.primitiveRadius);
            obj->setPrimitiveHeight(binObj.primitiveHeight);
            obj->setPrimitiveSegments(binObj.primitiveSegments);
            obj->setPrimitiveColor(binObj.primitiveColor);
            obj->setSlopeRatio(binObj.slopeRatio);
        }

        // Door properties
        if (binObj.isDoor) {
            obj->setDoorId(binObj.doorId);
            obj->setTargetLevel(binObj.targetLevel);
            obj->setTargetDoorId(binObj.targetDoorId);
        }

        // Frozen transform (already baked in binary, just record it)
        if (binObj.hasFrozenTransform) {
            obj->setFrozenTransform(binObj.frozenRotation, binObj.frozenScale);
        }

        // Add to physics world
        if (obj->hasBulletCollision() && ctx.physicsWorld) {
            ctx.physicsWorld->addObject(obj.get(), obj->getBulletCollisionType());
        }

        // Restore assigned @entity script from JSON (not in binary cache).
        if (!jsonObj.entityScript.empty()) {
            obj->setEntityScript(jsonObj.entityScript);
        }

        // Restore wall holes from JSON (not in binary)
        for (const auto& [hMin, hMax] : jsonObj.wallHoles) {
            obj->addWallHole(hMin, hMax);
        }

        // Restore behaviors from JSON (not in binary)
        for (const auto& behData : jsonObj.behaviors) {
            Behavior behavior;
            behavior.name = behData.name;
            behavior.trigger = static_cast<TriggerType>(behData.trigger);
            behavior.triggerParam = behData.triggerParam;
            behavior.triggerRadius = behData.triggerRadius;
            behavior.loop = behData.loop;
            behavior.enabled = behData.enabled;

            for (const auto& actData : behData.actions) {
                Action action;
                action.type = static_cast<ActionType>(actData.type);
                action.vec3Param = actData.vec3Param;
                action.floatParam = actData.floatParam;
                action.stringParam = actData.stringParam;
                action.animationParam = actData.animationParam;
                action.boolParam = actData.boolParam;
                action.easing = static_cast<Action::Easing>(actData.easing);
                action.duration = actData.duration;
                behavior.actions.push_back(action);
            }
            obj->addBehavior(behavior);
        }

        ctx.sceneObjects.push_back(std::move(obj));
    }

    return true;
}

} // namespace eden
