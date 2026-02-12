#pragma once

#include <eden/ICharacterController.hpp>

// Jolt includes - use Jolt's compiled defaults (don't override defines)
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/AABoxCast.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>
#include <functional>
#include <iostream>

namespace eden {

// Forward declarations
class SceneObject;

// Jolt uses its own memory allocation - we need to provide implementations
class JoltAllocator {
public:
    static void* Allocate(size_t size) { return malloc(size); }
    static void Free(void* ptr) { free(ptr); }
    static void* AlignedAllocate(size_t size, size_t alignment) {
        return aligned_alloc(alignment, (size + alignment - 1) & ~(alignment - 1));
    }
    static void AlignedFree(void* ptr) { free(ptr); }
};

// Broad phase layer definitions
namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr uint32_t NUM_LAYERS = 2;
}

// Object layers
namespace ObjectLayers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr uint32_t NUM_LAYERS = 2;
}

// BroadPhaseLayerInterface implementation
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    virtual uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        static const JPH::BroadPhaseLayer table[] = {
            BroadPhaseLayers::NON_MOVING,
            BroadPhaseLayers::MOVING
        };
        return table[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
            default: return "UNKNOWN";
        }
    }
#endif
};

// ObjectVsBroadPhaseLayerFilter implementation
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        return true; // All layers collide with each other
    }
};

// ObjectLayerPairFilter implementation
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override {
        return true; // All object layers collide with each other
    }
};

// Contact listener for custom collision handling
class CharacterContactListener : public JPH::CharacterContactListener {
public:
    void SetPhysicsSystem(JPH::PhysicsSystem* system) { m_physicsSystem = system; }

    virtual void OnContactAdded(const JPH::CharacterVirtual* inCharacter,
                                const JPH::BodyID& inBodyID2,
                                const JPH::SubShapeID& inSubShapeID2,
                                JPH::RVec3Arg inContactPosition,
                                JPH::Vec3Arg inContactNormal,
                                JPH::CharacterContactSettings& ioSettings) override {
        // Check if this is a kinematic body (moving platform)
        if (m_physicsSystem) {
            JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), inBodyID2);
            if (lock.Succeeded()) {
                const JPH::Body& body = lock.GetBody();
                if (body.GetMotionType() == JPH::EMotionType::Kinematic) {
                    // Kinematic platforms should push the character
                    ioSettings.mCanPushCharacter = true;
                    // Character can't push kinematic platforms
                    ioSettings.mCanReceiveImpulses = false;

                    // Debug: Show contact normal
                    static int contactDebug = 0;
                    if (contactDebug++ % 30 == 0) {
                        std::cout << "Kinematic contact normal: ("
                                  << inContactNormal.GetX() << ","
                                  << inContactNormal.GetY() << ","
                                  << inContactNormal.GetZ() << ") canPush=" << ioSettings.mCanPushCharacter << std::endl;
                    }
                }
            }
        }
    }

    // Called to adjust a kinematic body's velocity before collision solving
    virtual void OnAdjustBodyVelocity(const JPH::CharacterVirtual* inCharacter,
                                      const JPH::Body& inBody2,
                                      JPH::Vec3& ioLinearVelocity,
                                      JPH::Vec3& ioAngularVelocity) override {
        // For kinematic bodies, make sure the velocity is available for collision solving
        if (inBody2.GetMotionType() == JPH::EMotionType::Kinematic) {
            static int velDebug = 0;
            if (velDebug++ % 60 == 0) {
                std::cout << "OnAdjustBodyVelocity kinematic: ("
                          << ioLinearVelocity.GetX() << ","
                          << ioLinearVelocity.GetY() << ","
                          << ioLinearVelocity.GetZ() << ")" << std::endl;
            }
        }
    }

    // Called when contacts are being solved
    // Note: Platform velocity is now handled in JoltCharacter::extendedUpdate via GetGroundVelocity
    virtual void OnContactSolve(const JPH::CharacterVirtual* inCharacter,
                                const JPH::BodyID& inBodyID2,
                                const JPH::SubShapeID& inSubShapeID2,
                                JPH::RVec3Arg inContactPosition,
                                JPH::Vec3Arg inContactNormal,
                                JPH::Vec3Arg inContactVelocity,
                                const JPH::PhysicsMaterial* inContactMaterial,
                                JPH::Vec3Arg inCharacterVelocity,
                                JPH::Vec3& ioNewCharacterVelocity) override {
        // Nothing special needed here - ground velocity is applied in extendedUpdate
    }

private:
    JPH::PhysicsSystem* m_physicsSystem = nullptr;
};

// Main Jolt character controller class
class JoltCharacter : public ICharacterController {
public:
    JoltCharacter();
    ~JoltCharacter() override;

    // Initialize the physics system
    bool initialize() override;
    void shutdown() override;

    // Add static collision bodies (terrain mesh, static objects)
    void addStaticMesh(const std::vector<glm::vec3>& vertices,
                       const std::vector<uint32_t>& indices,
                       const glm::mat4& transform = glm::mat4(1.0f)) override;

    void addStaticBox(const glm::vec3& halfExtents,
                      const glm::vec3& position,
                      const glm::quat& rotation = glm::quat(1, 0, 0, 0)) override;

    void addConvexHull(const std::vector<glm::vec3>& points,
                       const glm::vec3& position,
                       const glm::quat& rotation = glm::quat(1, 0, 0, 0)) override;

    // Add terrain heightfield (much faster than mesh for terrain)
    // heightData: row-major height samples, sampleCount x sampleCount
    // offset: world position of heightfield center
    // scale: (horizontal scale per sample, vertical scale, horizontal scale per sample)
    void addTerrainHeightfield(const std::vector<float>& heightData,
                               int sampleCount,
                               const glm::vec3& offset,
                               const glm::vec3& scale) override;

    // Add kinematic (moving) platform - returns body index
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
    // Returns the new position after collision resolution
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
    void setMaxSlopeAngle(float degrees) override;

    // Raycast against the physics world (uses ICharacterController::RaycastResult)
    RaycastResult raycast(const glm::vec3& from, const glm::vec3& to) const override;

    // Clear all bodies (for level reset)
    void clearBodies() override;

    // Get the backend type
    PhysicsBackend getBackendType() const override { return PhysicsBackend::Jolt; }

private:
    // Convert between GLM and Jolt types
    static JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    static JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
    static glm::vec3 toGlm(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
    // Note: In single precision mode, RVec3 = Vec3, so no separate overload needed
    static glm::quat toGlm(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

    // Jolt systems
    std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem;

    // Layer interfaces
    BPLayerInterfaceImpl m_broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl m_objectLayerPairFilter;

    // Character
    std::unique_ptr<JPH::CharacterVirtual> m_character;
    std::unique_ptr<CharacterContactListener> m_contactListener;

    // Settings
    float m_gravity = 20.0f;
    float m_characterHeight = 0.9f;
    float m_characterRadius = 0.15f;

    // Track bodies for cleanup
    std::vector<JPH::BodyID> m_staticBodies;
    std::vector<JPH::BodyID> m_kinematicBodies;

    // Our own platform tracking (like Homebrew) - don't rely on Jolt's ground detection
    struct TrackedPlatform {
        uint32_t id;
        glm::vec3 center;
        glm::vec3 halfExtents;
        glm::vec3 velocity;
    };
    std::vector<TrackedPlatform> m_trackedPlatforms;

    // Check if character is on a tracked platform (our own logic, not Jolt's)
    bool isOnTrackedPlatform(glm::vec3& outVelocity) const;

    bool m_initialized = false;
};

} // namespace eden
