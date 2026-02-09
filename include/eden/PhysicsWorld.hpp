#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>

// Forward declarations for Bullet types
class btCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btCollisionWorld;
class btCollisionShape;
class btCollisionObject;
class btPairCachingGhostObject;
class btKinematicCharacterController;
class btCapsuleShape;
class btDynamicsWorld;
class btSequentialImpulseConstraintSolver;
class btHeightfieldTerrainShape;
class btRigidBody;

namespace eden {

class SceneObject;
class Terrain;
struct ModelVertex;

// Forward declare BulletCollisionType (defined in SceneObject.hpp)
enum class BulletCollisionType;

// Result of a raycast
struct RaycastResult {
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 hitPoint{0.0f};
    glm::vec3 hitNormal{0.0f};
    SceneObject* hitObject = nullptr;
};

// Result of a collision query
struct CollisionResult {
    bool colliding = false;
    glm::vec3 normal{0.0f};
    float penetration = 0.0f;
    SceneObject* otherObject = nullptr;
};

/**
 * Wrapper for Bullet Physics collision detection.
 * Uses btCollisionWorld (no dynamics, just collision detection).
 */
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Add/remove collision objects
    void addObject(SceneObject* obj, BulletCollisionType type);
    void removeObject(SceneObject* obj);
    void updateObjectTransform(SceneObject* obj);

    // Query methods
    RaycastResult raycast(const glm::vec3& from, const glm::vec3& to) const;
    std::vector<CollisionResult> checkCollision(SceneObject* obj) const;

    // Check if a point/sphere collides with any object
    bool checkSphereCollision(const glm::vec3& center, float radius,
                              glm::vec3* outNormal = nullptr,
                              SceneObject** outHitObject = nullptr) const;

    // Get height at position (for standing on objects)
    float getHeightAt(float x, float z, float currentY) const;

    // Debug: get collision shape vertices for visualization
    std::vector<glm::vec3> getCollisionShapeVertices(SceneObject* obj) const;

    // Clear all collision objects
    void clear();

    // Terrain collision
    void addTerrain(Terrain* terrain);
    void removeTerrain();

    // Character controller for player movement
    void createCharacterController(const glm::vec3& startPos, float height = 1.7f, float radius = 0.4f);
    void destroyCharacterController();
    void updateCharacter(float deltaTime);
    void setCharacterWalkDirection(const glm::vec3& direction);
    void characterJump();
    void characterForceJump();  // Jump without ground check (for custom terrain collision)
    glm::vec3 getCharacterPosition() const;
    void setCharacterPosition(const glm::vec3& pos);
    bool isCharacterOnGround() const;
    bool hasCharacterController() const { return m_characterController != nullptr; }
    void setCharacterCollisionEnabled(bool enabled);

private:
    // Create collision shapes
    btCollisionShape* createBoxShape(SceneObject* obj);
    btCollisionShape* createConvexHullShape(SceneObject* obj);
    btCollisionShape* createMeshShape(SceneObject* obj);

    // Bullet collision world components
    std::unique_ptr<btCollisionConfiguration> m_collisionConfig;
    std::unique_ptr<btCollisionDispatcher> m_dispatcher;
    std::unique_ptr<btBroadphaseInterface> m_broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver> m_solver;
    std::unique_ptr<btDynamicsWorld> m_dynamicsWorld;
    btCollisionWorld* m_collisionWorld = nullptr;  // Points to m_dynamicsWorld

    // Character controller
    std::unique_ptr<btPairCachingGhostObject> m_ghostObject;
    std::unique_ptr<btCapsuleShape> m_characterShape;
    std::unique_ptr<btKinematicCharacterController> m_characterController;

    // Terrain collision
    std::vector<float> m_terrainHeights;  // Flattened heightfield data
    std::unique_ptr<btHeightfieldTerrainShape> m_terrainShape;
    std::unique_ptr<btRigidBody> m_terrainRigidBody;
    float m_terrainMinHeight = 0.0f;
    float m_terrainMaxHeight = 0.0f;

    // Track collision objects and shapes
    struct CollisionData {
        btCollisionObject* collisionObject = nullptr;
        btCollisionShape* shape = nullptr;
        BulletCollisionType type;  // Initialized in addObject()
        glm::vec3 creationScale{1.0f};  // Scale when collision was created
    };
    std::unordered_map<SceneObject*, CollisionData> m_objects;
};

} // namespace eden
