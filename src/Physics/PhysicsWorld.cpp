#include <eden/PhysicsWorld.hpp>
#include <eden/Terrain.hpp>
#include "Editor/SceneObject.hpp"
#include "Renderer/ModelRenderer.hpp"

// Bullet headers
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>

#include <iostream>

namespace eden {

// Helper to convert glm to bullet
static btVector3 toBt(const glm::vec3& v) {
    return btVector3(v.x, v.y, v.z);
}

// Helper to convert bullet to glm
static glm::vec3 toGlm(const btVector3& v) {
    return glm::vec3(v.x(), v.y(), v.z());
}

// Helper to convert glm matrix to bullet transform (extracts only rotation and translation, ignoring scale)
static btTransform toBtTransform(const glm::mat4& mat) {
    // Extract translation
    btVector3 origin(mat[3][0], mat[3][1], mat[3][2]);

    // Extract rotation by normalizing the basis vectors (removes scale)
    glm::vec3 col0(mat[0][0], mat[0][1], mat[0][2]);
    glm::vec3 col1(mat[1][0], mat[1][1], mat[1][2]);
    glm::vec3 col2(mat[2][0], mat[2][1], mat[2][2]);

    float len0 = glm::length(col0);
    float len1 = glm::length(col1);
    float len2 = glm::length(col2);

    // Avoid division by zero
    if (len0 > 0.0001f) col0 /= len0;
    if (len1 > 0.0001f) col1 /= len1;
    if (len2 > 0.0001f) col2 /= len2;

    btMatrix3x3 basis(
        col0.x, col1.x, col2.x,
        col0.y, col1.y, col2.y,
        col0.z, col1.z, col2.z
    );

    btTransform transform;
    transform.setBasis(basis);
    transform.setOrigin(origin);
    return transform;
}

PhysicsWorld::PhysicsWorld() {
    // Create collision configuration
    m_collisionConfig = std::make_unique<btDefaultCollisionConfiguration>();

    // Create dispatcher
    m_dispatcher = std::make_unique<btCollisionDispatcher>(m_collisionConfig.get());

    // Create broadphase (AABB tree for fast broad-phase collision detection)
    m_broadphase = std::make_unique<btDbvtBroadphase>();

    // Add ghost pair callback for character controller
    m_broadphase->getOverlappingPairCache()->setInternalGhostPairCallback(new btGhostPairCallback());

    // Create solver (needed for dynamics world)
    m_solver = std::make_unique<btSequentialImpulseConstraintSolver>();

    // Create dynamics world (supports character controller)
    m_dynamicsWorld = std::unique_ptr<btDynamicsWorld>(new btDiscreteDynamicsWorld(
        m_dispatcher.get(),
        m_broadphase.get(),
        m_solver.get(),
        m_collisionConfig.get()
    ));

    // Set gravity
    m_dynamicsWorld->setGravity(btVector3(0, -9.81f, 0));

    // Point collision world to dynamics world for compatibility
    m_collisionWorld = m_dynamicsWorld.get();

    std::cout << "[Physics] Bullet dynamics world initialized" << std::endl;
}

PhysicsWorld::~PhysicsWorld() {
    destroyCharacterController();
    clear();
}

void PhysicsWorld::addObject(SceneObject* obj, BulletCollisionType type) {
    if (!obj || type == BulletCollisionType::NONE) return;

    // Remove existing collision data if any
    removeObject(obj);

    // Create collision shape based on type
    btCollisionShape* shape = nullptr;
    switch (type) {
        case BulletCollisionType::BOX:
            shape = createBoxShape(obj);
            break;
        case BulletCollisionType::CONVEX_HULL:
            shape = createConvexHullShape(obj);
            break;
        case BulletCollisionType::MESH:
            shape = createMeshShape(obj);
            break;
        default:
            return;
    }

    if (!shape) {
        std::cerr << "[Physics] Failed to create collision shape for " << obj->getName() << std::endl;
        return;
    }

    // Create collision object
    btCollisionObject* collisionObj = new btCollisionObject();
    collisionObj->setCollisionShape(shape);
    collisionObj->setUserPointer(obj);  // Store reference to SceneObject

    // Set transform
    glm::mat4 modelMatrix = obj->getTransform().getMatrix();
    collisionObj->setWorldTransform(toBtTransform(modelMatrix));

    // Add to world
    m_collisionWorld->addCollisionObject(collisionObj);

    // Store in our map
    CollisionData data;
    data.collisionObject = collisionObj;
    data.shape = shape;
    data.type = type;
    data.creationScale = obj->getTransform().getScale();  // Remember scale at creation
    m_objects[obj] = data;

    std::cout << "[Physics] Added collision object: " << obj->getName()
              << " (type=" << static_cast<int>(type) << ")" << std::endl;
}

void PhysicsWorld::removeObject(SceneObject* obj) {
    auto it = m_objects.find(obj);
    if (it == m_objects.end()) return;

    CollisionData& data = it->second;

    if (data.collisionObject) {
        m_collisionWorld->removeCollisionObject(data.collisionObject);
        delete data.collisionObject;
    }

    if (data.shape) {
        // For mesh shapes, we need to clean up the mesh interface
        if (data.type == BulletCollisionType::MESH) {
            btBvhTriangleMeshShape* meshShape = static_cast<btBvhTriangleMeshShape*>(data.shape);
            btStridingMeshInterface* meshInterface = meshShape->getMeshInterface();
            delete meshInterface;
        }
        delete data.shape;
    }

    m_objects.erase(it);
}

void PhysicsWorld::updateObjectTransform(SceneObject* obj) {
    auto it = m_objects.find(obj);
    if (it == m_objects.end()) return;

    // Check if scale changed - if so, need to recreate the collision shape
    glm::vec3 currentScale = obj->getTransform().getScale();
    glm::vec3 storedScale = it->second.creationScale;

    // Use a small epsilon for float comparison
    bool scaleChanged = glm::length(currentScale - storedScale) > 0.001f;

    if (scaleChanged) {
        // Scale changed - recreate the collision shape with new scale
        BulletCollisionType type = it->second.type;
        std::cout << "[Physics] Scale changed for " << obj->getName()
                  << " from (" << storedScale.x << "," << storedScale.y << "," << storedScale.z << ")"
                  << " to (" << currentScale.x << "," << currentScale.y << "," << currentScale.z << ")"
                  << " - recreating collision" << std::endl;
        removeObject(obj);
        addObject(obj, type);
        return;
    }

    glm::mat4 modelMatrix = obj->getTransform().getMatrix();
    it->second.collisionObject->setWorldTransform(toBtTransform(modelMatrix));

    // Update AABB in broadphase
    m_collisionWorld->updateSingleAabb(it->second.collisionObject);
}

RaycastResult PhysicsWorld::raycast(const glm::vec3& from, const glm::vec3& to) const {
    RaycastResult result;

    btVector3 btFrom = toBt(from);
    btVector3 btTo = toBt(to);

    btCollisionWorld::ClosestRayResultCallback callback(btFrom, btTo);
    m_collisionWorld->rayTest(btFrom, btTo, callback);

    if (callback.hasHit()) {
        result.hit = true;
        result.hitPoint = toGlm(callback.m_hitPointWorld);
        result.hitNormal = toGlm(callback.m_hitNormalWorld);
        result.distance = glm::distance(from, result.hitPoint);

        if (callback.m_collisionObject) {
            result.hitObject = static_cast<SceneObject*>(
                callback.m_collisionObject->getUserPointer()
            );
        }
    }

    return result;
}

std::vector<CollisionResult> PhysicsWorld::checkCollision(SceneObject* obj) const {
    std::vector<CollisionResult> results;

    auto it = m_objects.find(obj);
    if (it == m_objects.end()) return results;

    // Perform collision detection
    m_collisionWorld->performDiscreteCollisionDetection();

    int numManifolds = m_dispatcher->getNumManifolds();
    for (int i = 0; i < numManifolds; i++) {
        btPersistentManifold* manifold = m_dispatcher->getManifoldByIndexInternal(i);

        const btCollisionObject* objA = manifold->getBody0();
        const btCollisionObject* objB = manifold->getBody1();

        // Check if this manifold involves our object
        SceneObject* otherObj = nullptr;
        if (objA == it->second.collisionObject) {
            otherObj = static_cast<SceneObject*>(objB->getUserPointer());
        } else if (objB == it->second.collisionObject) {
            otherObj = static_cast<SceneObject*>(objA->getUserPointer());
        } else {
            continue;
        }

        int numContacts = manifold->getNumContacts();
        for (int j = 0; j < numContacts; j++) {
            btManifoldPoint& pt = manifold->getContactPoint(j);
            if (pt.getDistance() < 0.0f) {
                CollisionResult cr;
                cr.colliding = true;
                cr.normal = toGlm(pt.m_normalWorldOnB);
                cr.penetration = -pt.getDistance();
                cr.otherObject = otherObj;
                results.push_back(cr);
                break;  // One contact per pair is enough
            }
        }
    }

    return results;
}

bool PhysicsWorld::checkSphereCollision(const glm::vec3& center, float radius,
                                         glm::vec3* outNormal,
                                         SceneObject** outHitObject) const {
    // Create a temporary sphere shape for the query
    btSphereShape sphereShape(radius);
    btCollisionObject sphereObj;
    sphereObj.setCollisionShape(&sphereShape);

    btTransform sphereTransform;
    sphereTransform.setIdentity();
    sphereTransform.setOrigin(toBt(center));
    sphereObj.setWorldTransform(sphereTransform);

    // Contact test callback
    struct ContactCallback : public btCollisionWorld::ContactResultCallback {
        bool hasContact = false;
        btVector3 normal{0, 0, 0};
        SceneObject* hitObject = nullptr;

        btScalar addSingleResult(btManifoldPoint& cp,
                                 const btCollisionObjectWrapper* colObj0Wrap, int, int,
                                 const btCollisionObjectWrapper* colObj1Wrap, int, int) override {
            if (cp.getDistance() < 0.0f) {
                hasContact = true;
                normal = cp.m_normalWorldOnB;
                // Get the other object (not our sphere)
                const btCollisionObject* other = colObj1Wrap->getCollisionObject();
                hitObject = static_cast<SceneObject*>(other->getUserPointer());
            }
            return 0;
        }
    };

    ContactCallback callback;
    m_collisionWorld->contactTest(&sphereObj, callback);

    if (callback.hasContact) {
        if (outNormal) *outNormal = toGlm(callback.normal);
        if (outHitObject) *outHitObject = callback.hitObject;
        return true;
    }

    return false;
}

float PhysicsWorld::getHeightAt(float x, float z, float currentY) const {
    // Cast ray downward from above the current position
    glm::vec3 from(x, currentY + 10.0f, z);
    glm::vec3 to(x, currentY - 100.0f, z);

    RaycastResult result = raycast(from, to);
    if (result.hit && result.hitPoint.y <= currentY) {
        // Only count as walkable ground if surface is mostly horizontal
        // Normal Y > 0.5 means slope is less than ~60 degrees
        if (result.hitNormal.y > 0.5f) {
            return result.hitPoint.y;
        }
    }

    return -1000.0f;  // No walkable ground found
}

std::vector<glm::vec3> PhysicsWorld::getCollisionShapeVertices(SceneObject* obj) const {
    std::vector<glm::vec3> vertices;

    auto it = m_objects.find(obj);
    if (it == m_objects.end()) return vertices;

    const CollisionData& data = it->second;
    if (!data.shape) return vertices;

    // Get the world transform
    btTransform worldTransform = data.collisionObject->getWorldTransform();

    // Handle different shape types
    if (data.type == BulletCollisionType::BOX) {
        btBoxShape* box = static_cast<btBoxShape*>(data.shape);
        btVector3 halfExtents = box->getHalfExtentsWithMargin();

        // 8 corners of the box
        btVector3 corners[8] = {
            btVector3(-halfExtents.x(), -halfExtents.y(), -halfExtents.z()),
            btVector3( halfExtents.x(), -halfExtents.y(), -halfExtents.z()),
            btVector3( halfExtents.x(),  halfExtents.y(), -halfExtents.z()),
            btVector3(-halfExtents.x(),  halfExtents.y(), -halfExtents.z()),
            btVector3(-halfExtents.x(), -halfExtents.y(),  halfExtents.z()),
            btVector3( halfExtents.x(), -halfExtents.y(),  halfExtents.z()),
            btVector3( halfExtents.x(),  halfExtents.y(),  halfExtents.z()),
            btVector3(-halfExtents.x(),  halfExtents.y(),  halfExtents.z()),
        };

        // Transform corners to world space and create edges
        // Bottom face
        vertices.push_back(toGlm(worldTransform * corners[0]));
        vertices.push_back(toGlm(worldTransform * corners[1]));
        vertices.push_back(toGlm(worldTransform * corners[1]));
        vertices.push_back(toGlm(worldTransform * corners[2]));
        vertices.push_back(toGlm(worldTransform * corners[2]));
        vertices.push_back(toGlm(worldTransform * corners[3]));
        vertices.push_back(toGlm(worldTransform * corners[3]));
        vertices.push_back(toGlm(worldTransform * corners[0]));
        // Top face
        vertices.push_back(toGlm(worldTransform * corners[4]));
        vertices.push_back(toGlm(worldTransform * corners[5]));
        vertices.push_back(toGlm(worldTransform * corners[5]));
        vertices.push_back(toGlm(worldTransform * corners[6]));
        vertices.push_back(toGlm(worldTransform * corners[6]));
        vertices.push_back(toGlm(worldTransform * corners[7]));
        vertices.push_back(toGlm(worldTransform * corners[7]));
        vertices.push_back(toGlm(worldTransform * corners[4]));
        // Vertical edges
        vertices.push_back(toGlm(worldTransform * corners[0]));
        vertices.push_back(toGlm(worldTransform * corners[4]));
        vertices.push_back(toGlm(worldTransform * corners[1]));
        vertices.push_back(toGlm(worldTransform * corners[5]));
        vertices.push_back(toGlm(worldTransform * corners[2]));
        vertices.push_back(toGlm(worldTransform * corners[6]));
        vertices.push_back(toGlm(worldTransform * corners[3]));
        vertices.push_back(toGlm(worldTransform * corners[7]));

    } else if (data.type == BulletCollisionType::CONVEX_HULL) {
        btConvexHullShape* hull = static_cast<btConvexHullShape*>(data.shape);
        int numPoints = hull->getNumPoints();
        const btVector3* points = hull->getUnscaledPoints();

        // For convex hull, we'll draw edges between nearby points (simplified)
        // A proper implementation would compute the actual hull edges
        for (int i = 0; i < numPoints; i++) {
            btVector3 p1 = worldTransform * points[i];
            // Connect to a few nearby points
            for (int j = i + 1; j < numPoints && j < i + 4; j++) {
                btVector3 p2 = worldTransform * points[j];
                vertices.push_back(toGlm(p1));
                vertices.push_back(toGlm(p2));
            }
        }
    } else if (data.type == BulletCollisionType::MESH) {
        btBvhTriangleMeshShape* meshShape = static_cast<btBvhTriangleMeshShape*>(data.shape);
        btStridingMeshInterface* meshInterface = meshShape->getMeshInterface();

        // Calculate scale ratio: current scale / creation scale
        // Mesh vertices are stored at creationScale, need to show at current scale
        glm::vec3 currentScale = obj->getTransform().getScale();
        glm::vec3 scaleRatio = currentScale / data.creationScale;

        // Extract triangles from mesh interface
        const unsigned char* vertexBase;
        int numVerts;
        PHY_ScalarType vertexType;
        int vertexStride;
        const unsigned char* indexBase;
        int indexStride;
        int numFaces;
        PHY_ScalarType indexType;

        meshInterface->getLockedReadOnlyVertexIndexBase(
            &vertexBase, numVerts, vertexType, vertexStride,
            &indexBase, indexStride, numFaces, indexType, 0);

        // Get position from world transform (rotation handled separately)
        btVector3 origin = worldTransform.getOrigin();
        btMatrix3x3 rotation = worldTransform.getBasis();

        // Draw triangle edges (limit to avoid too many lines)
        int maxTriangles = std::min(numFaces, 2000);  // Limit for performance
        for (int i = 0; i < maxTriangles; i++) {
            unsigned int idx0, idx1, idx2;
            if (indexType == PHY_INTEGER) {
                const int* indices = reinterpret_cast<const int*>(indexBase + i * indexStride);
                idx0 = indices[0]; idx1 = indices[1]; idx2 = indices[2];
            } else {
                const short* indices = reinterpret_cast<const short*>(indexBase + i * indexStride);
                idx0 = indices[0]; idx1 = indices[1]; idx2 = indices[2];
            }

            btVector3 v0, v1, v2;
            if (vertexType == PHY_FLOAT) {
                const float* verts0 = reinterpret_cast<const float*>(vertexBase + idx0 * vertexStride);
                const float* verts1 = reinterpret_cast<const float*>(vertexBase + idx1 * vertexStride);
                const float* verts2 = reinterpret_cast<const float*>(vertexBase + idx2 * vertexStride);
                v0 = btVector3(verts0[0], verts0[1], verts0[2]);
                v1 = btVector3(verts1[0], verts1[1], verts1[2]);
                v2 = btVector3(verts2[0], verts2[1], verts2[2]);
            } else {
                const double* verts0 = reinterpret_cast<const double*>(vertexBase + idx0 * vertexStride);
                const double* verts1 = reinterpret_cast<const double*>(vertexBase + idx1 * vertexStride);
                const double* verts2 = reinterpret_cast<const double*>(vertexBase + idx2 * vertexStride);
                v0 = btVector3(static_cast<btScalar>(verts0[0]), static_cast<btScalar>(verts0[1]), static_cast<btScalar>(verts0[2]));
                v1 = btVector3(static_cast<btScalar>(verts1[0]), static_cast<btScalar>(verts1[1]), static_cast<btScalar>(verts1[2]));
                v2 = btVector3(static_cast<btScalar>(verts2[0]), static_cast<btScalar>(verts2[1]), static_cast<btScalar>(verts2[2]));
            }

            // Apply scale ratio to adjust from creation scale to current scale
            v0 = btVector3(v0.x() * scaleRatio.x, v0.y() * scaleRatio.y, v0.z() * scaleRatio.z);
            v1 = btVector3(v1.x() * scaleRatio.x, v1.y() * scaleRatio.y, v1.z() * scaleRatio.z);
            v2 = btVector3(v2.x() * scaleRatio.x, v2.y() * scaleRatio.y, v2.z() * scaleRatio.z);

            // Apply rotation and translation to get world space
            btVector3 w0 = rotation * v0 + origin;
            btVector3 w1 = rotation * v1 + origin;
            btVector3 w2 = rotation * v2 + origin;

            vertices.push_back(toGlm(w0)); vertices.push_back(toGlm(w1));
            vertices.push_back(toGlm(w1)); vertices.push_back(toGlm(w2));
            vertices.push_back(toGlm(w2)); vertices.push_back(toGlm(w0));
        }

        meshInterface->unLockReadOnlyVertexBase(0);
    }

    return vertices;
}

void PhysicsWorld::clear() {
    for (auto& pair : m_objects) {
        CollisionData& data = pair.second;

        if (data.collisionObject) {
            m_collisionWorld->removeCollisionObject(data.collisionObject);
            delete data.collisionObject;
        }

        if (data.shape) {
            if (data.type == BulletCollisionType::MESH) {
                btBvhTriangleMeshShape* meshShape = static_cast<btBvhTriangleMeshShape*>(data.shape);
                btStridingMeshInterface* meshInterface = meshShape->getMeshInterface();
                delete meshInterface;
            }
            delete data.shape;
        }
    }
    m_objects.clear();
}

void PhysicsWorld::addTerrain(Terrain* terrain) {
    if (!terrain) return;

    removeTerrain();  // Remove any existing terrain collision

    const auto& config = terrain->getConfig();
    const auto& chunks = terrain->getAllChunks();

    if (chunks.empty()) {
        std::cerr << "[Physics] No terrain chunks to add" << std::endl;
        return;
    }

    // Find terrain bounds
    int minChunkX = INT_MAX, maxChunkX = INT_MIN;
    int minChunkZ = INT_MAX, maxChunkZ = INT_MIN;

    for (const auto& pair : chunks) {
        glm::ivec2 coord = pair.first;
        minChunkX = std::min(minChunkX, coord.x);
        maxChunkX = std::max(maxChunkX, coord.x);
        minChunkZ = std::min(minChunkZ, coord.y);
        maxChunkZ = std::max(maxChunkZ, coord.y);
    }

    int chunkCountX = maxChunkX - minChunkX + 1;
    int chunkCountZ = maxChunkZ - minChunkZ + 1;
    int chunkRes = config.chunkResolution;

    // Total heightfield dimensions (vertices, not cells)
    int totalWidth = chunkCountX * (chunkRes - 1) + 1;
    int totalHeight = chunkCountZ * (chunkRes - 1) + 1;

    std::cout << "[Physics] Building terrain heightfield: " << totalWidth << "x" << totalHeight
              << " from " << chunks.size() << " chunks" << std::endl;

    // Allocate heightfield data
    m_terrainHeights.resize(totalWidth * totalHeight, 0.0f);
    m_terrainMinHeight = FLT_MAX;
    m_terrainMaxHeight = -FLT_MAX;

    // Fill heightfield from chunks
    for (const auto& pair : chunks) {
        glm::ivec2 coord = pair.first;
        const auto& chunk = pair.second;

        int chunkOffsetX = (coord.x - minChunkX) * (chunkRes - 1);
        int chunkOffsetZ = (coord.y - minChunkZ) * (chunkRes - 1);

        for (int z = 0; z < chunkRes; z++) {
            for (int x = 0; x < chunkRes; x++) {
                float height = chunk->getHeightAtLocal(x, z);

                int globalX = chunkOffsetX + x;
                int globalZ = chunkOffsetZ + z;

                if (globalX < totalWidth && globalZ < totalHeight) {
                    m_terrainHeights[globalZ * totalWidth + globalX] = height;
                    m_terrainMinHeight = std::min(m_terrainMinHeight, height);
                    m_terrainMaxHeight = std::max(m_terrainMaxHeight, height);
                }
            }
        }
    }

    // Add some margin to height range
    float heightRange = m_terrainMaxHeight - m_terrainMinHeight;
    if (heightRange < 1.0f) heightRange = 1.0f;

    // Create heightfield shape
    // Bullet heightfield is centered, so we need to offset it
    // Use diamond subdivision for better collision on steep terrain
    m_terrainShape = std::make_unique<btHeightfieldTerrainShape>(
        totalWidth,                    // width (X)
        totalHeight,                   // height (Z)
        m_terrainHeights.data(),       // height data
        1.0f,                          // height scale (already in world units)
        m_terrainMinHeight,            // min height
        m_terrainMaxHeight,            // max height
        1,                             // up axis (Y)
        PHY_FLOAT,                     // data type
        true                           // use diamond subdivision for better collision
    );

    // Set local scaling (tile size)
    m_terrainShape->setLocalScaling(btVector3(config.tileSize, 1.0f, config.tileSize));

    // Set collision margin for better detection
    m_terrainShape->setMargin(0.1f);

    // Position the terrain
    // Bullet's heightfield is centered at its local origin
    // The heightfield spans from local (-halfWidth, minY, -halfHeight) to (halfWidth, maxY, halfHeight)
    // We need to position it so the world corner matches the terrain's world corner

    // World position of the min corner of the terrain
    float minCornerX = minChunkX * (chunkRes - 1) * config.tileSize;
    float minCornerZ = minChunkZ * (chunkRes - 1) * config.tileSize;

    // Half extents of the heightfield (after scaling)
    float halfWidthWorld = (totalWidth - 1) * 0.5f * config.tileSize;
    float halfHeightWorld = (totalHeight - 1) * 0.5f * config.tileSize;

    // Center position = min corner + half extents
    float centerX = minCornerX + halfWidthWorld;
    float centerZ = minCornerZ + halfHeightWorld;
    float centerY = (m_terrainMinHeight + m_terrainMaxHeight) * 0.5f;

    btTransform terrainTransform;
    terrainTransform.setIdentity();
    terrainTransform.setOrigin(btVector3(centerX, centerY, centerZ));

    // Create as a static rigid body (mass = 0) for better character controller interaction
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, nullptr, m_terrainShape.get());
    m_terrainRigidBody = std::make_unique<btRigidBody>(rbInfo);
    m_terrainRigidBody->setWorldTransform(terrainTransform);
    m_terrainRigidBody->setCollisionFlags(m_terrainRigidBody->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
    m_terrainRigidBody->setFriction(1.0f);

    // Add to world
    m_dynamicsWorld->addRigidBody(m_terrainRigidBody.get());

    std::cout << "[Physics] Terrain collision: chunks (" << minChunkX << "," << minChunkZ
              << ") to (" << maxChunkX << "," << maxChunkZ << ")" << std::endl;
    std::cout << "[Physics] Terrain center: (" << centerX << ", " << centerY << ", " << centerZ << ")"
              << " size: " << (halfWidthWorld * 2) << "x" << (halfHeightWorld * 2)
              << " height: " << m_terrainMinHeight << " to " << m_terrainMaxHeight << std::endl;
}

void PhysicsWorld::removeTerrain() {
    if (m_terrainRigidBody) {
        m_dynamicsWorld->removeRigidBody(m_terrainRigidBody.get());
        m_terrainRigidBody.reset();
    }
    m_terrainShape.reset();
    m_terrainHeights.clear();
}

btCollisionShape* PhysicsWorld::createBoxShape(SceneObject* obj) {
    AABB bounds = obj->getLocalBounds();
    glm::vec3 halfExtents = bounds.getSize() * 0.5f;

    // Apply scale
    glm::vec3 scale = obj->getTransform().getScale();
    halfExtents *= scale;

    return new btBoxShape(toBt(halfExtents));
}

btCollisionShape* PhysicsWorld::createConvexHullShape(SceneObject* obj) {
    const auto& vertices = obj->getVertices();
    if (vertices.empty()) {
        std::cerr << "[Physics] No vertices for convex hull: " << obj->getName() << std::endl;
        return createBoxShape(obj);  // Fallback to box
    }

    btConvexHullShape* hull = new btConvexHullShape();

    // Get object scale
    glm::vec3 scale = obj->getTransform().getScale();

    // Add vertices to hull (Bullet will compute the convex hull)
    for (const auto& v : vertices) {
        glm::vec3 scaledPos = v.position * scale;
        hull->addPoint(toBt(scaledPos), false);  // false = don't recalc AABB yet
    }

    hull->recalcLocalAabb();
    hull->optimizeConvexHull();  // Reduce vertex count for performance

    std::cout << "[Physics] Created convex hull with "
              << hull->getNumPoints() << " points (from " << vertices.size() << " vertices)"
              << std::endl;

    return hull;
}

btCollisionShape* PhysicsWorld::createMeshShape(SceneObject* obj) {
    const auto& vertices = obj->getVertices();
    const auto& indices = obj->getIndices();

    if (vertices.empty() || indices.empty()) {
        std::cerr << "[Physics] No mesh data for triangle mesh: " << obj->getName() << std::endl;
        return createBoxShape(obj);  // Fallback to box
    }

    // Get object scale
    glm::vec3 scale = obj->getTransform().getScale();

    // Create triangle mesh
    btTriangleMesh* mesh = new btTriangleMesh();

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        glm::vec3 v0 = vertices[indices[i]].position * scale;
        glm::vec3 v1 = vertices[indices[i + 1]].position * scale;
        glm::vec3 v2 = vertices[indices[i + 2]].position * scale;

        mesh->addTriangle(toBt(v0), toBt(v1), toBt(v2));
    }

    btBvhTriangleMeshShape* meshShape = new btBvhTriangleMeshShape(mesh, true);

    std::cout << "[Physics] Created triangle mesh with "
              << indices.size() / 3 << " triangles" << std::endl;

    return meshShape;
}

void PhysicsWorld::createCharacterController(const glm::vec3& startPos, float height, float radius) {
    destroyCharacterController();  // Clean up any existing controller

    // Create capsule shape for the character (height is total, including hemispheres)
    float capsuleHeight = height - 2.0f * radius;  // Cylinder part height
    if (capsuleHeight < 0.1f) capsuleHeight = 0.1f;
    m_characterShape = std::make_unique<btCapsuleShape>(radius, capsuleHeight);

    // Create ghost object for collision detection
    m_ghostObject = std::make_unique<btPairCachingGhostObject>();
    m_ghostObject->setWorldTransform(btTransform(btQuaternion::getIdentity(), toBt(startPos)));
    m_ghostObject->setCollisionShape(m_characterShape.get());
    m_ghostObject->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

    // Create character controller
    btScalar stepHeight = 0.35f;  // Max height the character can step up
    m_characterController = std::make_unique<btKinematicCharacterController>(
        m_ghostObject.get(),
        m_characterShape.get(),
        stepHeight,
        btVector3(0, 1, 0)  // Up vector
    );

    // Configure character controller
    m_characterController->setGravity(btVector3(0, -20.0f, 0));  // Moderate gravity
    m_characterController->setMaxSlope(btRadians(50.0f));  // Max walkable slope
    m_characterController->setJumpSpeed(7.0f);
    m_characterController->setFallSpeed(30.0f);  // Slower fall to reduce tunneling
    m_characterController->setMaxPenetrationDepth(0.2f);  // Allow more penetration recovery
    m_characterController->setStepHeight(0.5f);  // Can step over obstacles

    // Add to world - character collides with everything
    m_dynamicsWorld->addCollisionObject(m_ghostObject.get(),
        btBroadphaseProxy::CharacterFilter,
        btBroadphaseProxy::AllFilter);
    m_dynamicsWorld->addAction(m_characterController.get());

    std::cout << "[Physics] Character controller created at ("
              << startPos.x << ", " << startPos.y << ", " << startPos.z << ")" << std::endl;
}

void PhysicsWorld::destroyCharacterController() {
    if (m_characterController) {
        m_dynamicsWorld->removeAction(m_characterController.get());
        m_characterController.reset();
    }
    if (m_ghostObject) {
        m_dynamicsWorld->removeCollisionObject(m_ghostObject.get());
        m_ghostObject.reset();
    }
    m_characterShape.reset();
}

void PhysicsWorld::updateCharacter(float deltaTime) {
    if (!m_characterController) return;

    // Step the physics simulation with more substeps for better accuracy
    // maxSubSteps=10, fixedTimeStep=1/240 for smoother terrain collision
    m_dynamicsWorld->stepSimulation(deltaTime, 10, 1.0f / 240.0f);
}

void PhysicsWorld::setCharacterWalkDirection(const glm::vec3& direction) {
    if (!m_characterController) return;
    m_characterController->setWalkDirection(toBt(direction));
}

void PhysicsWorld::characterJump() {
    if (!m_characterController) return;
    if (m_characterController->onGround()) {
        m_characterController->jump();
    }
}

void PhysicsWorld::characterForceJump() {
    if (!m_characterController) return;
    m_characterController->jump();
}

glm::vec3 PhysicsWorld::getCharacterPosition() const {
    if (!m_ghostObject) return glm::vec3(0);
    btTransform transform = m_ghostObject->getWorldTransform();
    return toGlm(transform.getOrigin());
}

void PhysicsWorld::setCharacterPosition(const glm::vec3& pos) {
    if (!m_ghostObject || !m_characterController) return;
    btTransform transform = m_ghostObject->getWorldTransform();
    transform.setOrigin(toBt(pos));
    m_ghostObject->setWorldTransform(transform);
    m_characterController->warp(toBt(pos));
}

bool PhysicsWorld::isCharacterOnGround() const {
    if (!m_characterController) return false;
    return m_characterController->onGround();
}

void PhysicsWorld::setCharacterCollisionEnabled(bool enabled) {
    if (!m_ghostObject) return;

    if (enabled) {
        // Re-enable collision
        m_ghostObject->setCollisionFlags(
            m_ghostObject->getCollisionFlags() & ~btCollisionObject::CF_NO_CONTACT_RESPONSE);
    } else {
        // Disable collision (ghost passes through everything)
        m_ghostObject->setCollisionFlags(
            m_ghostObject->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }
}

} // namespace eden
