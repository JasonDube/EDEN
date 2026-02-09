#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdint>

namespace eden {

// Physics backend types
enum class PhysicsBackend {
    Jolt = 0,
    Homebrew = 1
};

// Abstract character controller interface
// Allows switching between different physics implementations (Jolt, Homebrew, etc.)
class ICharacterController {
public:
    virtual ~ICharacterController() = default;

    // Initialize the physics system
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // Add static collision bodies
    virtual void addStaticMesh(const std::vector<glm::vec3>& vertices,
                               const std::vector<uint32_t>& indices,
                               const glm::mat4& transform = glm::mat4(1.0f)) = 0;

    virtual void addStaticBox(const glm::vec3& halfExtents,
                              const glm::vec3& position,
                              const glm::quat& rotation = glm::quat(1, 0, 0, 0)) = 0;

    virtual void addConvexHull(const std::vector<glm::vec3>& points,
                               const glm::vec3& position,
                               const glm::quat& rotation = glm::quat(1, 0, 0, 0)) = 0;

    // Add terrain heightfield
    virtual void addTerrainHeightfield(const std::vector<float>& heightData,
                                       int sampleCount,
                                       const glm::vec3& offset,
                                       const glm::vec3& scale) = 0;

    // Kinematic (moving) platforms
    // Returns a body ID that can be used to update the platform
    virtual uint32_t addKinematicPlatform(const glm::vec3& halfExtents,
                                          const glm::vec3& position,
                                          const glm::quat& rotation = glm::quat(1, 0, 0, 0)) = 0;

    // Update platform transform - velocity is provided directly from the behavior system
    // for smooth movement, rather than being calculated from position deltas
    virtual void updatePlatformTransform(uint32_t platformId,
                                         const glm::vec3& position,
                                         const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                                         const glm::vec3& velocity = glm::vec3(0),
                                         float deltaTime = 1.0f/60.0f) = 0;

    // Create the character controller
    virtual void createCharacter(const glm::vec3& position,
                                 float height = 1.8f,
                                 float radius = 0.3f) = 0;

    // Update character movement - returns new position after collision resolution
    virtual glm::vec3 update(float deltaTime,
                             const glm::vec3& desiredVelocity,
                             bool jump,
                             float jumpVelocity = 8.0f) = 0;

    // Extended update with stair stepping
    virtual glm::vec3 extendedUpdate(float deltaTime,
                                     const glm::vec3& desiredVelocity,
                                     bool jump,
                                     float jumpVelocity = 8.0f,
                                     float maxStairHeight = 0.4f) = 0;

    // Getters
    virtual glm::vec3 getPosition() const = 0;
    virtual glm::vec3 getLinearVelocity() const = 0;
    virtual bool isOnGround() const = 0;
    virtual bool isOnSteepGround() const = 0;
    virtual glm::vec3 getGroundNormal() const = 0;
    virtual glm::vec3 getGroundVelocity() const = 0;

    // Setters
    virtual void setPosition(const glm::vec3& position) = 0;
    virtual void setLinearVelocity(const glm::vec3& velocity) = 0;
    virtual void setGravity(float gravity) = 0;
    virtual void setMaxSlopeAngle(float degrees) = 0;

    // Raycast result
    struct RaycastResult {
        bool hit = false;
        glm::vec3 hitPoint{0};
        glm::vec3 hitNormal{0, 1, 0};
        float distance = 0.0f;
    };

    // Raycast against the physics world
    virtual RaycastResult raycast(const glm::vec3& from, const glm::vec3& to) const = 0;

    // Clear all bodies (for level reset)
    virtual void clearBodies() = 0;

    // Get the backend type
    virtual PhysicsBackend getBackendType() const = 0;
};

} // namespace eden
