#include <eden/JoltCharacter.hpp>
#include <iostream>
#include <cstdarg>
#include <algorithm>

// Jolt requires these callbacks
static void JoltTrace(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    std::cout << "[Jolt] " << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS
static bool JoltAssertFailed(const char* expression, const char* message, const char* file, uint32_t line) {
    std::cerr << "[Jolt Assert] " << file << ":" << line << ": " << expression;
    if (message) std::cerr << " - " << message;
    std::cerr << std::endl;
    return true; // Break into debugger
}
#endif

namespace eden {

JoltCharacter::JoltCharacter() {
}

JoltCharacter::~JoltCharacter() {
    shutdown();
}

bool JoltCharacter::initialize() {
    if (m_initialized) return true;

    // Install callbacks
    JPH::Trace = JoltTrace;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = JoltAssertFailed;
#endif

    // Register allocation hook (use default malloc/free)
    JPH::RegisterDefaultAllocator();

    // Create factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt types
    JPH::RegisterTypes();

    // Create temp allocator (10 MB)
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    // Create job system with max threads
    m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        std::thread::hardware_concurrency() - 1
    );

    // Physics system settings
    const uint32_t maxBodies = 10240;
    const uint32_t numBodyMutexes = 0;  // 0 = auto
    const uint32_t maxBodyPairs = 65536;
    const uint32_t maxContactConstraints = 10240;

    m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_physicsSystem->Init(
        maxBodies,
        numBodyMutexes,
        maxBodyPairs,
        maxContactConstraints,
        m_broadPhaseLayerInterface,
        m_objectVsBroadPhaseLayerFilter,
        m_objectLayerPairFilter
    );

    // Set gravity
    m_physicsSystem->SetGravity(JPH::Vec3(0, -m_gravity, 0));

    m_contactListener = std::make_unique<CharacterContactListener>();
    m_contactListener->SetPhysicsSystem(m_physicsSystem.get());

    m_initialized = true;
    std::cout << "Jolt Physics initialized successfully" << std::endl;
    return true;
}

void JoltCharacter::shutdown() {
    if (!m_initialized) return;

    // Remove all bodies
    clearBodies();

    // Destroy character
    m_character.reset();
    m_contactListener.reset();

    // Destroy physics system
    m_physicsSystem.reset();

    // Destroy job system
    m_jobSystem.reset();
    m_tempAllocator.reset();

    // Unregister types and destroy factory
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    m_initialized = false;
}

void JoltCharacter::clearBodies() {
    if (!m_physicsSystem) return;

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

    // Remove static bodies
    for (const auto& bodyId : m_staticBodies) {
        bodyInterface.RemoveBody(bodyId);
        bodyInterface.DestroyBody(bodyId);
    }
    m_staticBodies.clear();

    // Remove kinematic bodies
    for (const auto& bodyId : m_kinematicBodies) {
        bodyInterface.RemoveBody(bodyId);
        bodyInterface.DestroyBody(bodyId);
    }
    m_kinematicBodies.clear();

    // Clear our own platform tracking
    m_trackedPlatforms.clear();
}

void JoltCharacter::addStaticMesh(const std::vector<glm::vec3>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   const glm::mat4& transform) {
    if (!m_initialized || vertices.empty() || indices.empty()) return;

    // Convert vertices to Jolt format
    JPH::TriangleList triangles;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        glm::vec4 v0 = transform * glm::vec4(vertices[indices[i]], 1.0f);
        glm::vec4 v1 = transform * glm::vec4(vertices[indices[i + 1]], 1.0f);
        glm::vec4 v2 = transform * glm::vec4(vertices[indices[i + 2]], 1.0f);

        JPH::Triangle tri(
            JPH::Float3(v0.x, v0.y, v0.z),
            JPH::Float3(v1.x, v1.y, v1.z),
            JPH::Float3(v2.x, v2.y, v2.z)
        );
        triangles.push_back(tri);
    }

    // Create mesh shape
    JPH::MeshShapeSettings meshSettings(triangles);
    JPH::ShapeSettings::ShapeResult result = meshSettings.Create();
    if (result.HasError()) {
        std::cerr << "Failed to create mesh shape: " << result.GetError() << std::endl;
        return;
    }

    // Create body
    JPH::BodyCreationSettings bodySettings(
        result.Get(),
        JPH::RVec3::sZero(),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        ObjectLayers::NON_MOVING
    );

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
    m_staticBodies.push_back(bodyId);
}

void JoltCharacter::addStaticBox(const glm::vec3& halfExtents,
                                  const glm::vec3& position,
                                  const glm::quat& rotation) {
    if (!m_initialized) return;

    // Skip degenerate boxes
    float minExtent = std::min({halfExtents.x, halfExtents.y, halfExtents.z});
    if (minExtent <= 0.001f) return;

    // Adapt convex radius for thin boxes
    float convexRadius = std::min(JPH::cDefaultConvexRadius, minExtent * 0.5f);

    JPH::BoxShapeSettings boxSettings(toJolt(halfExtents), convexRadius);
    JPH::ShapeSettings::ShapeResult result = boxSettings.Create();
    if (result.HasError()) {
        return;  // Silently skip invalid boxes
    }

    JPH::BodyCreationSettings bodySettings(
        result.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        toJolt(rotation),
        JPH::EMotionType::Static,
        ObjectLayers::NON_MOVING
    );

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
    m_staticBodies.push_back(bodyId);
}

void JoltCharacter::addConvexHull(const std::vector<glm::vec3>& points,
                                   const glm::vec3& position,
                                   const glm::quat& rotation) {
    if (!m_initialized || points.empty()) return;

    // Convert points to Jolt format
    JPH::Array<JPH::Vec3> joltPoints;
    joltPoints.reserve(points.size());
    for (const auto& p : points) {
        joltPoints.push_back(toJolt(p));
    }

    JPH::ConvexHullShapeSettings hullSettings(joltPoints);
    JPH::ShapeSettings::ShapeResult result = hullSettings.Create();
    if (result.HasError()) {
        std::cerr << "Failed to create convex hull shape: " << result.GetError() << std::endl;
        return;
    }

    JPH::BodyCreationSettings bodySettings(
        result.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        toJolt(rotation),
        JPH::EMotionType::Static,
        ObjectLayers::NON_MOVING
    );

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
    m_staticBodies.push_back(bodyId);
}

void JoltCharacter::addTerrainHeightfield(const std::vector<float>& heightData,
                                           int sampleCount,
                                           const glm::vec3& offset,
                                           const glm::vec3& scale) {
    if (!m_initialized || heightData.empty()) return;

    // Jolt HeightFieldShape expects sample count to be power of 2 + 1
    // and height data in row-major format
    JPH::HeightFieldShapeSettings settings(
        heightData.data(),
        JPH::Vec3(offset.x, offset.y, offset.z),
        JPH::Vec3(scale.x, scale.y, scale.z),
        sampleCount
    );

    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        std::cerr << "Failed to create heightfield shape: " << result.GetError() << std::endl;
        return;
    }

    JPH::BodyCreationSettings bodySettings(
        result.Get(),
        JPH::RVec3::sZero(),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        ObjectLayers::NON_MOVING
    );

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
    m_staticBodies.push_back(bodyId);
}

uint32_t JoltCharacter::addKinematicPlatform(const glm::vec3& halfExtents,
                                              const glm::vec3& position,
                                              const glm::quat& rotation) {
    if (!m_initialized) return UINT32_MAX;

    // Adapt convex radius for thin platforms
    float minExtent = std::min({halfExtents.x, halfExtents.y, halfExtents.z});
    float convexRadius = std::min(JPH::cDefaultConvexRadius, minExtent * 0.5f);

    JPH::BoxShapeSettings boxSettings(toJolt(halfExtents), convexRadius);
    JPH::ShapeSettings::ShapeResult result = boxSettings.Create();
    if (result.HasError()) {
        std::cerr << "Failed to create kinematic platform: " << result.GetError() << std::endl;
        return UINT32_MAX;
    }

    JPH::BodyCreationSettings bodySettings(
        result.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        toJolt(rotation),
        JPH::EMotionType::Kinematic,
        ObjectLayers::MOVING
    );

    // Ensure kinematic body is in broadphase
    bodySettings.mAllowDynamicOrKinematic = true;

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::Activate);

    if (bodyId.IsInvalid()) {
        std::cerr << "Kinematic platform body ID is invalid!" << std::endl;
        return UINT32_MAX;
    }

    std::cout << "Kinematic body created: ID=" << bodyId.GetIndex()
              << " position=(" << position.x << "," << position.y << "," << position.z << ")"
              << " halfExtents=(" << halfExtents.x << "," << halfExtents.y << "," << halfExtents.z << ")" << std::endl;

    m_kinematicBodies.push_back(bodyId);

    // Track platform ourselves (don't rely solely on Jolt's ground detection)
    TrackedPlatform platform;
    platform.id = bodyId.GetIndexAndSequenceNumber();
    platform.center = position;
    platform.halfExtents = halfExtents;
    platform.velocity = glm::vec3(0.0f);
    m_trackedPlatforms.push_back(platform);

    return bodyId.GetIndexAndSequenceNumber();
}

void JoltCharacter::updatePlatformTransform(uint32_t platformId,
                                             const glm::vec3& position,
                                             const glm::quat& rotation,
                                             const glm::vec3& velocity,
                                             float deltaTime) {
    JPH::BodyID bodyId(platformId);
    if (!m_initialized || bodyId.IsInvalid()) return;

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

    // Directly set position (no physics simulation fighting)
    bodyInterface.SetPositionAndRotation(
        bodyId,
        JPH::RVec3(position.x, position.y, position.z),
        toJolt(rotation),
        JPH::EActivation::Activate
    );

    // Use the provided velocity directly from the behavior system
    bodyInterface.SetLinearVelocity(bodyId, toJolt(velocity));

    // Update our own tracked platform data (like Homebrew does)
    for (auto& platform : m_trackedPlatforms) {
        if (platform.id == platformId) {
            platform.center = position;
            platform.velocity = velocity;
            break;
        }
    }
}

bool JoltCharacter::isOnTrackedPlatform(glm::vec3& outVelocity) const {
    if (!m_character) return false;

    glm::vec3 charPos = getPosition();
    float feetY = charPos.y - m_characterHeight * 0.5f;

    for (const auto& platform : m_trackedPlatforms) {
        // Check if character is within platform bounds
        // Use tight vertical tolerance - only detect when actually standing on platform
        float minX = platform.center.x - platform.halfExtents.x - 0.1f;
        float maxX = platform.center.x + platform.halfExtents.x + 0.1f;
        float minZ = platform.center.z - platform.halfExtents.z - 0.1f;
        float maxZ = platform.center.z + platform.halfExtents.z + 0.1f;
        float platformTop = platform.center.y + platform.halfExtents.y;

        // Tight vertical check: feet must be close to platform top (within 0.15m)
        if (charPos.x >= minX && charPos.x <= maxX &&
            charPos.z >= minZ && charPos.z <= maxZ &&
            feetY >= platformTop - 0.15f && feetY <= platformTop + 0.15f) {
            outVelocity = platform.velocity;
            return true;
        }
    }
    return false;
}

void JoltCharacter::createCharacter(const glm::vec3& position, float height, float radius) {
    if (!m_initialized) return;

    m_characterHeight = height;
    m_characterRadius = radius;

    // Create capsule shape (standing character)
    float capsuleHalfHeight = (height - 2.0f * radius) / 2.0f;
    if (capsuleHalfHeight < 0.01f) capsuleHalfHeight = 0.01f;

    JPH::RefConst<JPH::Shape> standingShape = new JPH::CapsuleShape(capsuleHalfHeight, radius);

    // Character settings
    JPH::CharacterVirtualSettings settings;
    settings.mShape = standingShape;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(75.0f);
    settings.mMaxStrength = 100.0f;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mCharacterPadding = 0.02f;
    settings.mPenetrationRecoverySpeed = 1.0f;
    settings.mPredictiveContactDistance = 0.1f;
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);  // Support below feet
    settings.mUp = JPH::Vec3::sAxisY();

    m_character = std::make_unique<JPH::CharacterVirtual>(
        &settings,
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        0, // No user data
        m_physicsSystem.get()
    );

    m_character->SetListener(m_contactListener.get());

    // std::cout << "Created Jolt character at " << position.x << ", " << position.y << ", " << position.z << std::endl;
}

glm::vec3 JoltCharacter::update(float deltaTime, const glm::vec3& desiredVelocity, bool jump, float jumpVelocity) {
    if (!m_character) return glm::vec3(0);

    // Get current velocity
    JPH::Vec3 velocity = m_character->GetLinearVelocity();

    // Apply gravity
    bool onGround = isOnGround();
    if (!onGround) {
        velocity += JPH::Vec3(0, -m_gravity * deltaTime, 0);
    }

    // Apply desired horizontal velocity
    velocity.SetX(desiredVelocity.x);
    velocity.SetZ(desiredVelocity.z);

    // Handle jump
    if (jump && onGround) {
        velocity.SetY(jumpVelocity);
    }

    // Update character
    m_character->SetLinearVelocity(velocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;

    // Use filters that allow collision with all body types
    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    JPH::ShapeFilter shapeFilter;
    JPH::BroadPhaseLayerFilter broadPhaseFilter;  // Default allows all layers
    JPH::ObjectLayerFilter objectLayerFilter;      // Default allows all layers

    m_character->ExtendedUpdate(
        deltaTime,
        JPH::Vec3(0, -m_gravity, 0),
        updateSettings,
        broadPhaseFilter,
        objectLayerFilter,
        bodyFilter,
        shapeFilter,
        *m_tempAllocator
    );

    return getPosition();
}

glm::vec3 JoltCharacter::extendedUpdate(float deltaTime,
                                         const glm::vec3& desiredVelocity,
                                         bool jump,
                                         float jumpVelocity,
                                         float maxStairHeight) {
    if (!m_character) return glm::vec3(0);

    // Use OUR OWN platform tracking (like Homebrew) - don't rely on Jolt's ground detection
    glm::vec3 trackedPlatformVelocity;
    bool onTrackedPlatform = isOnTrackedPlatform(trackedPlatformVelocity);

    // Also check Jolt's ground state for static ground
    m_character->UpdateGroundVelocity();
    bool onGround = isOnGround();
    JPH::Vec3 currentVelocity = m_character->GetLinearVelocity();

    JPH::Vec3 newVelocity;

    if (onTrackedPlatform) {
        // ON KINEMATIC PLATFORM: Use our tracked velocity directly, bypass Jolt
        // This is the same approach that works perfectly in Homebrew
        newVelocity = toJolt(trackedPlatformVelocity);
        newVelocity.SetX(newVelocity.GetX() + desiredVelocity.x);
        newVelocity.SetZ(newVelocity.GetZ() + desiredVelocity.z);

        if (jump) {
            newVelocity.SetY(newVelocity.GetY() + jumpVelocity);
        }
    } else if (onGround) {
        // ON STATIC GROUND: No vertical velocity needed
        newVelocity = JPH::Vec3(desiredVelocity.x, 0, desiredVelocity.z);

        if (jump) {
            newVelocity.SetY(jumpVelocity);
        }
    } else {
        // IN AIR: Apply gravity
        float currentVerticalVel = currentVelocity.GetY();
        newVelocity = JPH::Vec3(desiredVelocity.x, currentVerticalVel, desiredVelocity.z);
        newVelocity += JPH::Vec3(0, -m_gravity * deltaTime, 0);
    }

    m_character->SetLinearVelocity(newVelocity);

    // Extended update settings with stair stepping
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    // Large step down to follow descending platforms
    // At 2m/s descent and 30fps, that's 0.067m per frame - use 0.5m for safety margin
    updateSettings.mStickToFloorStepDown = JPH::Vec3(0, -0.5f, 0);
    updateSettings.mWalkStairsStepUp = JPH::Vec3(0, maxStairHeight, 0);
    updateSettings.mWalkStairsMinStepForward = 0.02f;
    updateSettings.mWalkStairsStepForwardTest = 0.15f;
    updateSettings.mWalkStairsCosAngleForwardContact = JPH::Cos(JPH::DegreesToRadians(75.0f));
    updateSettings.mWalkStairsStepDownExtra = JPH::Vec3::sZero();

    JPH::IgnoreMultipleBodiesFilter bodyFilter;
    JPH::ShapeFilter shapeFilter;

    // PhysicsSystem::Update() is called later to process kinematic body movement
    // (MoveKinematic sets velocity, Update moves the body to target position)

    // Physics debug prints disabled (were spamming console every 2s)

    // Use default filters that allow all body interactions
    // The character is in ObjectLayers::MOVING and should collide with all layers
    JPH::BroadPhaseLayerFilter broadPhaseFilter;  // Default allows all layers
    JPH::ObjectLayerFilter objectLayerFilter;      // Default allows all layers

    // IMPORTANT: Disable gravity completely when on ground or on a tracked platform
    // When on a moving platform, gravity fights against platform velocity causing jitter
    JPH::Vec3 effectiveGravity = (onGround || onTrackedPlatform) ? JPH::Vec3::sZero() : JPH::Vec3(0, -m_gravity, 0);

    m_character->ExtendedUpdate(
        deltaTime,
        effectiveGravity,
        updateSettings,
        broadPhaseFilter,
        objectLayerFilter,
        bodyFilter,
        shapeFilter,
        *m_tempAllocator
    );

    // Contact/ground debug prints disabled (were spamming console every 1s)

    return getPosition();
}

glm::vec3 JoltCharacter::getPosition() const {
    if (!m_character) return glm::vec3(0);
    return toGlm(m_character->GetPosition());
}

glm::vec3 JoltCharacter::getLinearVelocity() const {
    if (!m_character) return glm::vec3(0);
    return toGlm(m_character->GetLinearVelocity());
}

bool JoltCharacter::isOnGround() const {
    if (!m_character) return false;
    return m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

bool JoltCharacter::isOnSteepGround() const {
    if (!m_character) return false;
    return m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnSteepGround;
}

glm::vec3 JoltCharacter::getGroundNormal() const {
    if (!m_character) return glm::vec3(0, 1, 0);
    return toGlm(m_character->GetGroundNormal());
}

glm::vec3 JoltCharacter::getGroundVelocity() const {
    if (!m_character) return glm::vec3(0);
    return toGlm(m_character->GetGroundVelocity());
}

void JoltCharacter::setPosition(const glm::vec3& position) {
    if (!m_character) return;
    m_character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
}

void JoltCharacter::setLinearVelocity(const glm::vec3& velocity) {
    if (!m_character) return;
    m_character->SetLinearVelocity(toJolt(velocity));
}

void JoltCharacter::setMaxSlopeAngle(float degrees) {
    if (!m_character) return;
    m_character->SetMaxSlopeAngle(JPH::DegreesToRadians(degrees));
}

JoltCharacter::RaycastResult JoltCharacter::raycast(const glm::vec3& from, const glm::vec3& to) const {
    RaycastResult result;
    if (!m_physicsSystem) return result;

    glm::vec3 direction = to - from;
    float length = glm::length(direction);
    if (length < 0.0001f) return result;

    JPH::RRayCast ray(
        JPH::Vec3(from.x, from.y, from.z),
        JPH::Vec3(direction.x, direction.y, direction.z)
    );

    JPH::RayCastResult hit;

    // Use broad phase filter that allows all layers
    // This ensures kinematic bodies are included in the query
    JPH::BroadPhaseLayerFilter broadPhaseFilter;  // Default: no filtering
    JPH::ObjectLayerFilter objectLayerFilter;      // Default: no filtering

    if (m_physicsSystem->GetNarrowPhaseQuery().CastRay(
            ray,
            hit,
            broadPhaseFilter,
            objectLayerFilter)) {
        result.hit = true;
        result.hitPoint = from + direction * hit.mFraction;
        result.distance = length * hit.mFraction;

        // Get normal and body info
        JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            JPH::Vec3 hitPos = JPH::Vec3(from.x, from.y, from.z) + JPH::Vec3(direction.x, direction.y, direction.z) * hit.mFraction;
            JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, JPH::RVec3(hitPos));
            result.hitNormal = toGlm(normal);

            // Debug: show what type of body was hit
            static int hitDebugCount = 0;
            if (hitDebugCount++ % 120 == 0) {
                std::cout << "Raycast hit body ID=" << hit.mBodyID.GetIndex()
                          << " motionType=" << (int)body.GetMotionType()
                          << " layer=" << body.GetObjectLayer() << std::endl;
            }
        }
    }

    return result;
}

} // namespace eden
