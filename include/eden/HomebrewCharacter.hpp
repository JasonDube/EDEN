#pragma once

#include <eden/ICharacterController.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <functional>

namespace eden {

// Simple homebrew character controller without external physics library
// Uses basic collision detection and response
class HomebrewCharacter : public ICharacterController {
public:
    HomebrewCharacter();
    ~HomebrewCharacter() override;

    // Initialize the physics system
    bool initialize() override;
    void shutdown() override;

    // Add static collision bodies
    void addStaticMesh(const std::vector<glm::vec3>& vertices,
                       const std::vector<uint32_t>& indices,
                       const glm::mat4& transform = glm::mat4(1.0f)) override;

    void addStaticBox(const glm::vec3& halfExtents,
                      const glm::vec3& position,
                      const glm::quat& rotation = glm::quat(1, 0, 0, 0)) override;

    void addConvexHull(const std::vector<glm::vec3>& points,
                       const glm::vec3& position,
                       const glm::quat& rotation = glm::quat(1, 0, 0, 0)) override;

    // Add terrain heightfield
    void addTerrainHeightfield(const std::vector<float>& heightData,
                               int sampleCount,
                               const glm::vec3& offset,
                               const glm::vec3& scale) override;

    // Kinematic platforms
    uint32_t addKinematicPlatform(const glm::vec3& halfExtents,
                                  const glm::vec3& position,
                                  const glm::quat& rotation = glm::quat(1, 0, 0, 0)) override;
    void updatePlatformTransform(uint32_t platformId,
                                  const glm::vec3& position,
                                  const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                                  const glm::vec3& velocity = glm::vec3(0),
                                  float deltaTime = 1.0f/60.0f) override;

    // Create the character controller
    void createCharacter(const glm::vec3& position, float height = 1.8f, float radius = 0.3f) override;

    // Update character movement
    glm::vec3 update(float deltaTime,
                     const glm::vec3& desiredVelocity,
                     bool jump,
                     float jumpVelocity = 8.0f) override;

    // Extended update with stair stepping
    glm::vec3 extendedUpdate(float deltaTime,
                             const glm::vec3& desiredVelocity,
                             bool jump,
                             float jumpVelocity = 8.0f,
                             float maxStairHeight = 0.4f) override;

    // Getters
    glm::vec3 getPosition() const override;
    glm::vec3 getLinearVelocity() const override;
    bool isOnGround() const override;
    bool isOnSteepGround() const override;
    glm::vec3 getGroundNormal() const override;
    glm::vec3 getGroundVelocity() const override;

    // Setters
    void setPosition(const glm::vec3& position) override;
    void setLinearVelocity(const glm::vec3& velocity) override;
    void setGravity(float gravity) override { m_gravity = gravity; }
    void setMaxSlopeAngle(float degrees) override { m_maxSlopeAngle = degrees; }

    // Raycast
    RaycastResult raycast(const glm::vec3& from, const glm::vec3& to) const override;

    // Clear all bodies
    void clearBodies() override;

    // Get backend type
    PhysicsBackend getBackendType() const override { return PhysicsBackend::Homebrew; }

    // Set height query function (for terrain)
    using HeightQueryFunc = std::function<float(float x, float z)>;
    void setHeightQueryFunction(HeightQueryFunc func) { m_heightQuery = func; }

private:
    // Collision primitive types
    struct AABB {
        glm::vec3 min;
        glm::vec3 max;
        glm::vec3 velocity{0.0f};  // For kinematic platforms
        uint32_t id = 0;
    };

    struct Triangle {
        glm::vec3 v0, v1, v2;
        glm::vec3 normal;
    };

    struct Heightfield {
        std::vector<float> data;
        int sampleCount;
        glm::vec3 offset;
        glm::vec3 scale;
    };

    // Collision detection helpers
    bool raycastAABB(const glm::vec3& from, const glm::vec3& dir, float maxDist,
                     const AABB& box, float& outDist, glm::vec3& outNormal) const;
    bool raycastTriangle(const glm::vec3& from, const glm::vec3& dir,
                         const Triangle& tri, float& outDist) const;
    bool raycastHeightfield(const glm::vec3& from, const glm::vec3& dir, float maxDist,
                            float& outDist, glm::vec3& outNormal) const;

    float getHeightfieldHeight(float x, float z) const;
    glm::vec3 getHeightfieldNormal(float x, float z) const;

    // Capsule-AABB collision
    bool capsuleAABBCollision(const glm::vec3& capsulePos, float radius, float halfHeight,
                              const AABB& box, glm::vec3& pushOut) const;

    // Ground state check
    void checkGroundState();

    // Character state
    glm::vec3 m_position{0.0f};
    glm::vec3 m_velocity{0.0f};
    float m_characterHeight = 0.9f;
    float m_characterRadius = 0.15f;
    bool m_onGround = false;
    bool m_onSteepGround = false;
    glm::vec3 m_groundNormal{0, 1, 0};
    glm::vec3 m_groundVelocity{0.0f};

    // Settings
    float m_gravity = 20.0f;
    float m_maxSlopeAngle = 50.0f;

    // Collision data
    std::vector<AABB> m_staticBoxes;
    std::vector<AABB> m_kinematicBoxes;
    std::vector<Triangle> m_triangles;
    std::vector<Heightfield> m_heightfields;
    uint32_t m_nextPlatformId = 1;

    // Height query function (for terrain that's not in heightfield form)
    HeightQueryFunc m_heightQuery;

    bool m_initialized = false;
};

} // namespace eden
