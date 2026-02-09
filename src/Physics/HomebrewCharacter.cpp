#include <eden/HomebrewCharacter.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace eden {

HomebrewCharacter::HomebrewCharacter() {
}

HomebrewCharacter::~HomebrewCharacter() {
    shutdown();
}

bool HomebrewCharacter::initialize() {
    if (m_initialized) return true;

    m_initialized = true;
    std::cout << "Homebrew Physics initialized successfully" << std::endl;
    return true;
}

void HomebrewCharacter::shutdown() {
    if (!m_initialized) return;

    clearBodies();
    m_initialized = false;
}

void HomebrewCharacter::clearBodies() {
    m_staticBoxes.clear();
    m_kinematicBoxes.clear();
    m_triangles.clear();
    m_heightfields.clear();
    m_nextPlatformId = 1;
}

void HomebrewCharacter::addStaticMesh(const std::vector<glm::vec3>& vertices,
                                       const std::vector<uint32_t>& indices,
                                       const glm::mat4& transform) {
    if (!m_initialized || vertices.empty() || indices.empty()) return;

    // Convert mesh to triangles
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Triangle tri;
        tri.v0 = glm::vec3(transform * glm::vec4(vertices[indices[i]], 1.0f));
        tri.v1 = glm::vec3(transform * glm::vec4(vertices[indices[i + 1]], 1.0f));
        tri.v2 = glm::vec3(transform * glm::vec4(vertices[indices[i + 2]], 1.0f));

        // Calculate normal
        glm::vec3 edge1 = tri.v1 - tri.v0;
        glm::vec3 edge2 = tri.v2 - tri.v0;
        tri.normal = glm::normalize(glm::cross(edge1, edge2));

        m_triangles.push_back(tri);
    }
}

void HomebrewCharacter::addStaticBox(const glm::vec3& halfExtents,
                                      const glm::vec3& position,
                                      const glm::quat& rotation) {
    if (!m_initialized) return;

    // For now, ignore rotation and create axis-aligned box
    // TODO: Support oriented boxes
    AABB box;
    box.min = position - halfExtents;
    box.max = position + halfExtents;
    box.id = 0;  // Static boxes don't need IDs
    m_staticBoxes.push_back(box);
}

void HomebrewCharacter::addConvexHull(const std::vector<glm::vec3>& points,
                                       const glm::vec3& position,
                                       const glm::quat& rotation) {
    if (!m_initialized || points.empty()) return;

    // Approximate convex hull as AABB
    glm::vec3 minP = points[0];
    glm::vec3 maxP = points[0];
    for (const auto& p : points) {
        minP = glm::min(minP, p);
        maxP = glm::max(maxP, p);
    }

    AABB box;
    box.min = minP + position;
    box.max = maxP + position;
    box.id = 0;
    m_staticBoxes.push_back(box);
}

void HomebrewCharacter::addTerrainHeightfield(const std::vector<float>& heightData,
                                               int sampleCount,
                                               const glm::vec3& offset,
                                               const glm::vec3& scale) {
    if (!m_initialized || heightData.empty()) return;

    Heightfield hf;
    hf.data = heightData;
    hf.sampleCount = sampleCount;
    hf.offset = offset;
    hf.scale = scale;
    m_heightfields.push_back(hf);
}

uint32_t HomebrewCharacter::addKinematicPlatform(const glm::vec3& halfExtents,
                                                  const glm::vec3& position,
                                                  const glm::quat& rotation) {
    if (!m_initialized) return UINT32_MAX;

    AABB box;
    box.min = position - halfExtents;
    box.max = position + halfExtents;
    box.id = m_nextPlatformId++;
    box.velocity = glm::vec3(0.0f);
    m_kinematicBoxes.push_back(box);

    std::cout << "Homebrew: Added kinematic platform ID=" << box.id
              << " at (" << position.x << "," << position.y << "," << position.z << ")" << std::endl;

    return box.id;
}

void HomebrewCharacter::updatePlatformTransform(uint32_t platformId,
                                                 const glm::vec3& position,
                                                 const glm::quat& rotation,
                                                 const glm::vec3& velocity,
                                                 float deltaTime) {
    if (!m_initialized || platformId == UINT32_MAX) return;

    for (auto& box : m_kinematicBoxes) {
        if (box.id == platformId) {
            glm::vec3 halfExtents = (box.max - box.min) * 0.5f;

            // Use the provided velocity directly from the behavior system
            // This is calculated analytically from the easing function for smooth movement
            box.velocity = velocity;

            // Update position
            box.min = position - halfExtents;
            box.max = position + halfExtents;
            return;
        }
    }
}

void HomebrewCharacter::createCharacter(const glm::vec3& position, float height, float radius) {
    if (!m_initialized) return;

    m_position = position;
    m_characterHeight = height;
    m_characterRadius = radius;
    m_velocity = glm::vec3(0.0f);
    m_onGround = false;

    std::cout << "Homebrew: Created character at (" << position.x << "," << position.y << "," << position.z << ")" << std::endl;
}

glm::vec3 HomebrewCharacter::update(float deltaTime, const glm::vec3& desiredVelocity, bool jump, float jumpVelocity) {
    // Delegate to extendedUpdate with no stair stepping
    return extendedUpdate(deltaTime, desiredVelocity, jump, jumpVelocity, 0.0f);
}

glm::vec3 HomebrewCharacter::extendedUpdate(float deltaTime,
                                             const glm::vec3& desiredVelocity,
                                             bool jump,
                                             float jumpVelocity,
                                             float maxStairHeight) {
    if (!m_initialized) return m_position;

    // Debug output (every ~2 seconds at 60fps)
    static int debugCounter = 0;
    bool showDebug = (debugCounter++ % 120 == 0);

    // Check ground state before movement
    checkGroundState();

    // Detect if we're on a kinematic platform (even if ground detection flickers)
    // If ground velocity is non-zero, we're definitely on a moving platform
    bool onKinematicPlatform = glm::length(m_groundVelocity) > 0.001f;

    if (showDebug) {
        std::cout << "Homebrew: pos=(" << m_position.x << "," << m_position.y << "," << m_position.z << ")"
                  << " onGround=" << m_onGround
                  << " onPlatform=" << onKinematicPlatform
                  << " groundVel=(" << m_groundVelocity.x << "," << m_groundVelocity.y << "," << m_groundVelocity.z << ")"
                  << " kinematicBodies=" << m_kinematicBoxes.size()
                  << std::endl;
    }

    // Apply gravity ONLY if not on ground AND not on a kinematic platform
    // This prevents jitter on descending platforms
    if (!m_onGround && !onKinematicPlatform) {
        m_velocity.y -= m_gravity * deltaTime;
    } else {
        // On ground or platform - inherit ground velocity exactly
        m_velocity = m_groundVelocity;

        // Handle jump
        if (jump) {
            m_velocity.y = jumpVelocity;
            m_onGround = false;
        }
    }

    // Apply desired horizontal velocity (add to platform velocity)
    m_velocity.x = desiredVelocity.x + m_groundVelocity.x;
    m_velocity.z = desiredVelocity.z + m_groundVelocity.z;

    // Calculate new position
    glm::vec3 newPos = m_position + m_velocity * deltaTime;

    // Collision response against boxes
    float capsuleHalfHeight = (m_characterHeight - 2.0f * m_characterRadius) / 2.0f;
    if (capsuleHalfHeight < 0.01f) capsuleHalfHeight = 0.01f;

    // Collide with static boxes
    for (const auto& box : m_staticBoxes) {
        glm::vec3 pushOut;
        if (capsuleAABBCollision(newPos, m_characterRadius, capsuleHalfHeight, box, pushOut)) {
            newPos += pushOut;

            // If pushed up, we might be on ground
            if (pushOut.y > 0.001f) {
                m_velocity.y = 0;
            }
            // If pushed horizontally, stop horizontal velocity in that direction
            if (std::abs(pushOut.x) > 0.001f) {
                m_velocity.x = 0;
            }
            if (std::abs(pushOut.z) > 0.001f) {
                m_velocity.z = 0;
            }
        }
    }

    // Collide with kinematic boxes (platforms)
    for (const auto& box : m_kinematicBoxes) {
        glm::vec3 pushOut;
        if (capsuleAABBCollision(newPos, m_characterRadius, capsuleHalfHeight, box, pushOut)) {
            newPos += pushOut;

            if (pushOut.y > 0.001f) {
                m_velocity.y = 0;
                // Standing on platform - inherit its velocity
                m_groundVelocity = box.velocity;
            }
        }
    }

    // Heightfield collision
    for (const auto& hf : m_heightfields) {
        float terrainY = getHeightfieldHeight(newPos.x, newPos.z);
        float feetY = newPos.y - m_characterHeight * 0.5f;

        if (feetY < terrainY) {
            newPos.y = terrainY + m_characterHeight * 0.5f;
            if (m_velocity.y < 0) {
                m_velocity.y = 0;
            }
        }
    }

    // Use external height query if available and no heightfields
    if (m_heightfields.empty() && m_heightQuery) {
        float terrainY = m_heightQuery(newPos.x, newPos.z);
        float feetY = newPos.y - m_characterHeight * 0.5f;

        if (feetY < terrainY) {
            newPos.y = terrainY + m_characterHeight * 0.5f;
            if (m_velocity.y < 0) {
                m_velocity.y = 0;
            }
        }
    }

    // Stair stepping
    if (maxStairHeight > 0.0f && m_onGround) {
        // Try stepping up
        glm::vec3 stepPos = newPos + glm::vec3(0, maxStairHeight, 0);
        // Then move forward and down to find step
        // Simplified: just check if we can move up
    }

    m_position = newPos;

    // Update ground state after movement
    checkGroundState();

    return m_position;
}

void HomebrewCharacter::checkGroundState() {
    // FIRST: Check if we're on a kinematic platform
    // Use tight vertical tolerance so we don't override ground detection when stepping off
    m_groundVelocity = glm::vec3(0.0f);
    float feetY = m_position.y - m_characterHeight * 0.5f;
    for (const auto& box : m_kinematicBoxes) {
        // Check if feet are close to platform top (within 0.15m)
        if (m_position.x >= box.min.x - 0.1f && m_position.x <= box.max.x + 0.1f &&
            m_position.z >= box.min.z - 0.1f && m_position.z <= box.max.z + 0.1f &&
            feetY >= box.max.y - 0.15f && feetY <= box.max.y + 0.15f) {
            m_groundVelocity = box.velocity;
            // If we found a platform, we're effectively on ground
            m_onGround = true;
            m_groundNormal = glm::vec3(0, 1, 0);
            m_onSteepGround = false;
            return;  // Platform takes priority
        }
    }

    // Cast ray downward to check for static ground
    glm::vec3 rayStart = m_position;
    glm::vec3 rayEnd = m_position - glm::vec3(0, m_characterHeight * 0.5f + 0.1f, 0);

    auto result = raycast(rayStart, rayEnd);

    m_onGround = result.hit && (result.distance < m_characterHeight * 0.5f + 0.05f);
    m_groundNormal = result.hit ? result.hitNormal : glm::vec3(0, 1, 0);

    if (m_onGround) {
        float slopeAngle = glm::degrees(std::acos(glm::dot(m_groundNormal, glm::vec3(0, 1, 0))));
        m_onSteepGround = slopeAngle > m_maxSlopeAngle;
    } else {
        m_onSteepGround = false;
    }
}

glm::vec3 HomebrewCharacter::getPosition() const {
    return m_position;
}

glm::vec3 HomebrewCharacter::getLinearVelocity() const {
    return m_velocity;
}

bool HomebrewCharacter::isOnGround() const {
    return m_onGround;
}

bool HomebrewCharacter::isOnSteepGround() const {
    return m_onSteepGround;
}

glm::vec3 HomebrewCharacter::getGroundNormal() const {
    return m_groundNormal;
}

glm::vec3 HomebrewCharacter::getGroundVelocity() const {
    return m_groundVelocity;
}

void HomebrewCharacter::setPosition(const glm::vec3& position) {
    m_position = position;
}

void HomebrewCharacter::setLinearVelocity(const glm::vec3& velocity) {
    m_velocity = velocity;
}

HomebrewCharacter::RaycastResult HomebrewCharacter::raycast(const glm::vec3& from, const glm::vec3& to) const {
    RaycastResult result;
    if (!m_initialized) return result;

    glm::vec3 dir = to - from;
    float maxDist = glm::length(dir);
    if (maxDist < 0.0001f) return result;
    dir /= maxDist;

    float closestDist = maxDist;
    glm::vec3 closestNormal(0, 1, 0);

    // Check static boxes
    for (const auto& box : m_staticBoxes) {
        float dist;
        glm::vec3 normal;
        if (raycastAABB(from, dir, closestDist, box, dist, normal)) {
            if (dist < closestDist) {
                closestDist = dist;
                closestNormal = normal;
                result.hit = true;
            }
        }
    }

    // Check kinematic boxes
    for (const auto& box : m_kinematicBoxes) {
        float dist;
        glm::vec3 normal;
        if (raycastAABB(from, dir, closestDist, box, dist, normal)) {
            if (dist < closestDist) {
                closestDist = dist;
                closestNormal = normal;
                result.hit = true;
            }
        }
    }

    // Check heightfields
    for (const auto& hf : m_heightfields) {
        float dist;
        glm::vec3 normal;
        if (raycastHeightfield(from, dir, closestDist, dist, normal)) {
            if (dist < closestDist) {
                closestDist = dist;
                closestNormal = normal;
                result.hit = true;
            }
        }
    }

    // Check triangles (mesh collision)
    for (const auto& tri : m_triangles) {
        float dist;
        if (raycastTriangle(from, dir, tri, dist)) {
            if (dist > 0 && dist < closestDist) {
                closestDist = dist;
                closestNormal = tri.normal;
                result.hit = true;
            }
        }
    }

    if (result.hit) {
        result.hitPoint = from + dir * closestDist;
        result.hitNormal = closestNormal;
        result.distance = closestDist;
    }

    return result;
}

bool HomebrewCharacter::raycastAABB(const glm::vec3& from, const glm::vec3& dir, float maxDist,
                                     const AABB& box, float& outDist, glm::vec3& outNormal) const {
    // Slab method for ray-AABB intersection
    float tmin = 0.0f;
    float tmax = maxDist;
    glm::vec3 normal(0.0f);

    for (int i = 0; i < 3; i++) {
        if (std::abs(dir[i]) < 0.0001f) {
            // Ray parallel to slab
            if (from[i] < box.min[i] || from[i] > box.max[i]) {
                return false;
            }
        } else {
            float invD = 1.0f / dir[i];
            float t1 = (box.min[i] - from[i]) * invD;
            float t2 = (box.max[i] - from[i]) * invD;

            glm::vec3 n(0.0f);
            n[i] = -glm::sign(dir[i]);

            if (t1 > t2) {
                std::swap(t1, t2);
                n[i] = -n[i];
            }

            if (t1 > tmin) {
                tmin = t1;
                normal = n;
            }
            tmax = std::min(tmax, t2);

            if (tmin > tmax) {
                return false;
            }
        }
    }

    if (tmin >= 0 && tmin <= maxDist) {
        outDist = tmin;
        outNormal = normal;
        return true;
    }

    return false;
}

bool HomebrewCharacter::raycastTriangle(const glm::vec3& from, const glm::vec3& dir,
                                         const Triangle& tri, float& outDist) const {
    // Möller–Trumbore intersection algorithm
    const float EPSILON = 0.0000001f;

    glm::vec3 edge1 = tri.v1 - tri.v0;
    glm::vec3 edge2 = tri.v2 - tri.v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);

    if (a > -EPSILON && a < EPSILON) {
        return false;  // Ray parallel to triangle
    }

    float f = 1.0f / a;
    glm::vec3 s = from - tri.v0;
    float u = f * glm::dot(s, h);

    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);

    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    float t = f * glm::dot(edge2, q);

    if (t > EPSILON) {
        outDist = t;
        return true;
    }

    return false;
}

bool HomebrewCharacter::raycastHeightfield(const glm::vec3& from, const glm::vec3& dir, float maxDist,
                                            float& outDist, glm::vec3& outNormal) const {
    if (m_heightfields.empty() && !m_heightQuery) return false;

    // Step along ray and check height - use fine step size for accurate ground detection
    const float stepSize = 0.05f;
    int numSteps = static_cast<int>(maxDist / stepSize) + 1;

    for (int i = 0; i < numSteps; i++) {
        float t = i * stepSize;
        if (t > maxDist) break;

        glm::vec3 pos = from + dir * t;
        float terrainY = m_heightfields.empty() ?
            (m_heightQuery ? m_heightQuery(pos.x, pos.z) : 0.0f) :
            getHeightfieldHeight(pos.x, pos.z);

        if (pos.y <= terrainY) {
            // Hit terrain - calculate exact intersection distance
            // Binary search to refine the hit point
            float tLow = (i > 0) ? (i - 1) * stepSize : 0.0f;
            float tHigh = t;
            for (int j = 0; j < 5; j++) {
                float tMid = (tLow + tHigh) * 0.5f;
                glm::vec3 midPos = from + dir * tMid;
                float midTerrainY = m_heightfields.empty() ?
                    (m_heightQuery ? m_heightQuery(midPos.x, midPos.z) : 0.0f) :
                    getHeightfieldHeight(midPos.x, midPos.z);
                if (midPos.y <= midTerrainY) {
                    tHigh = tMid;
                } else {
                    tLow = tMid;
                }
            }
            outDist = tHigh;
            glm::vec3 hitPos = from + dir * outDist;
            outNormal = m_heightfields.empty() ?
                glm::vec3(0, 1, 0) :
                getHeightfieldNormal(hitPos.x, hitPos.z);
            return true;
        }
    }

    return false;
}

float HomebrewCharacter::getHeightfieldHeight(float x, float z) const {
    if (m_heightfields.empty()) {
        return m_heightQuery ? m_heightQuery(x, z) : 0.0f;
    }

    // Use first heightfield (typically terrain)
    const auto& hf = m_heightfields[0];

    // Convert world coords to heightfield coords
    float localX = (x - hf.offset.x) / hf.scale.x;
    float localZ = (z - hf.offset.z) / hf.scale.z;

    // Clamp to heightfield bounds
    localX = glm::clamp(localX, 0.0f, static_cast<float>(hf.sampleCount - 1));
    localZ = glm::clamp(localZ, 0.0f, static_cast<float>(hf.sampleCount - 1));

    // Bilinear interpolation
    int x0 = static_cast<int>(localX);
    int z0 = static_cast<int>(localZ);
    int x1 = std::min(x0 + 1, hf.sampleCount - 1);
    int z1 = std::min(z0 + 1, hf.sampleCount - 1);

    float fx = localX - x0;
    float fz = localZ - z0;

    float h00 = hf.data[z0 * hf.sampleCount + x0];
    float h10 = hf.data[z0 * hf.sampleCount + x1];
    float h01 = hf.data[z1 * hf.sampleCount + x0];
    float h11 = hf.data[z1 * hf.sampleCount + x1];

    float h0 = h00 * (1 - fx) + h10 * fx;
    float h1 = h01 * (1 - fx) + h11 * fx;
    float height = h0 * (1 - fz) + h1 * fz;

    return hf.offset.y + height * hf.scale.y;
}

glm::vec3 HomebrewCharacter::getHeightfieldNormal(float x, float z) const {
    // Approximate normal using finite differences
    const float delta = 0.1f;
    float hL = getHeightfieldHeight(x - delta, z);
    float hR = getHeightfieldHeight(x + delta, z);
    float hD = getHeightfieldHeight(x, z - delta);
    float hU = getHeightfieldHeight(x, z + delta);

    glm::vec3 normal(hL - hR, 2.0f * delta, hD - hU);
    return glm::normalize(normal);
}

bool HomebrewCharacter::capsuleAABBCollision(const glm::vec3& capsulePos, float radius, float halfHeight,
                                              const AABB& box, glm::vec3& pushOut) const {
    // Simplified capsule-AABB: treat as sphere at capsule center for now
    // TODO: Proper capsule collision

    // Find closest point on AABB to sphere center
    glm::vec3 closest;
    closest.x = glm::clamp(capsulePos.x, box.min.x, box.max.x);
    closest.y = glm::clamp(capsulePos.y, box.min.y, box.max.y);
    closest.z = glm::clamp(capsulePos.z, box.min.z, box.max.z);

    // Check if sphere intersects
    glm::vec3 diff = capsulePos - closest;
    float distSq = glm::dot(diff, diff);
    float combinedRadius = radius + halfHeight;  // Approximate capsule as larger sphere

    if (distSq < combinedRadius * combinedRadius && distSq > 0.0001f) {
        float dist = std::sqrt(distSq);
        float penetration = combinedRadius - dist;
        pushOut = (diff / dist) * penetration;
        return true;
    }

    // Check if capsule is inside AABB
    if (capsulePos.x >= box.min.x && capsulePos.x <= box.max.x &&
        capsulePos.y >= box.min.y && capsulePos.y <= box.max.y &&
        capsulePos.z >= box.min.z && capsulePos.z <= box.max.z) {
        // Find shortest escape direction
        float dx1 = capsulePos.x - box.min.x;
        float dx2 = box.max.x - capsulePos.x;
        float dy1 = capsulePos.y - box.min.y;
        float dy2 = box.max.y - capsulePos.y;
        float dz1 = capsulePos.z - box.min.z;
        float dz2 = box.max.z - capsulePos.z;

        float minDist = dx1;
        pushOut = glm::vec3(-dx1 - radius, 0, 0);

        if (dx2 < minDist) { minDist = dx2; pushOut = glm::vec3(dx2 + radius, 0, 0); }
        if (dy1 < minDist) { minDist = dy1; pushOut = glm::vec3(0, -dy1 - radius, 0); }
        if (dy2 < minDist) { minDist = dy2; pushOut = glm::vec3(0, dy2 + radius, 0); }
        if (dz1 < minDist) { minDist = dz1; pushOut = glm::vec3(0, 0, -dz1 - radius); }
        if (dz2 < minDist) { minDist = dz2; pushOut = glm::vec3(0, 0, dz2 + radius); }

        return true;
    }

    return false;
}

} // namespace eden
