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

} // namespace eden
