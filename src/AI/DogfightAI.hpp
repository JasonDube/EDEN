#pragma once

#include "../Editor/SceneObject.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <string>
#include <vector>
#include <functional>
#include <cmath>

namespace eden {

// Dogfight behavior states
enum class DogfightState {
    IDLE,           // No target, waiting
    PATROL,         // Following patrol route
    PURSUE,         // Chasing target, trying to get behind
    ENGAGE,         // In firing position, attacking
    EVADE,          // Target is behind us, evasive maneuvers
    FLEEING,        // Low health, running away
    EJECTING,       // Ship destroyed, pilot ejecting
    DEAD            // Destroyed
};

// Jettisoned cargo item (floating in space/falling)
struct JettisonedCargo {
    glm::vec3 position;
    glm::vec3 velocity;
    float value;            // Credits worth
    float lifetime = 60.0f; // Seconds before despawn
    int sceneObjectIndex = -1;  // Visual representation
};

// Callback types
using DogfightEventCallback = std::function<void(const std::string& event)>;
using CargoJettisonCallback = std::function<void(const glm::vec3& position, float value)>;
using EjectionCallback = std::function<void(const glm::vec3& position, const glm::vec3& velocity)>;

/**
 * DogfightAI - Combat AI for aerial/space dogfighting
 *
 * Simple state machine:
 * - PATROL: Follow waypoints when no enemy
 * - PURSUE: Close distance to target, try to get behind
 * - ENGAGE: In firing cone, shoot at target
 * - EVADE: Enemy behind us, break and maneuver
 * - FLEEING: Low health, disengage
 * - EJECTING: Ship destroyed, pilot ejects
 */
class DogfightAI {
public:
    DogfightAI(uint32_t id, const std::string& name = "Fighter");

    // Update (call each frame)
    void update(float deltaTime);

    // Identity
    uint32_t getId() const { return m_id; }
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    // Link to scene object (for position, health, etc.)
    void setSceneObject(SceneObject* obj) { m_sceneObject = obj; }
    SceneObject* getSceneObject() const { return m_sceneObject; }

    // Position and movement (reads from scene object if linked)
    glm::vec3 getPosition() const;
    void setPosition(const glm::vec3& pos);
    glm::vec3 getForward() const;
    void setRotation(const glm::vec3& eulerDegrees);
    glm::vec3 getRotation() const;

    // Speed settings
    void setSpeed(float speed) { m_speed = speed; }
    float getSpeed() const { return m_speed; }
    void setTurnRate(float degreesPerSecond) { m_turnRate = degreesPerSecond; }
    float getTurnRate() const { return m_turnRate; }

    // Current state
    DogfightState getState() const { return m_state; }
    const char* getStateName() const;

    // Target management
    void setTarget(DogfightAI* target) { m_target = target; }
    DogfightAI* getTarget() const { return m_target; }
    void clearTarget() { m_target = nullptr; }
    bool hasTarget() const { return m_target != nullptr; }

    // Combat parameters
    void setWeaponRange(float range) { m_weaponRange = range; }
    float getWeaponRange() const { return m_weaponRange; }
    void setWeaponConeAngle(float degrees) { m_weaponConeAngle = degrees; }
    float getWeaponConeAngle() const { return m_weaponConeAngle; }
    void setDamagePerShot(float damage) { m_damagePerShot = damage; }
    float getDamagePerShot() const { return m_damagePerShot; }
    void setFireRate(float shotsPerSecond) { m_fireRate = shotsPerSecond; }
    float getFireRate() const { return m_fireRate; }

    // Health thresholds
    void setFleeHealthPercent(float percent) { m_fleeHealthPercent = percent; }
    float getFleeHealthPercent() const { return m_fleeHealthPercent; }
    void setJettisonHealthPercent(float percent) { m_jettisonHealthPercent = percent; }
    float getJettisonHealthPercent() const { return m_jettisonHealthPercent; }

    // Cargo (for traders that can fight)
    void setCargoValue(float value) { m_cargoValue = value; }
    float getCargoValue() const { return m_cargoValue; }
    bool hasCargo() const { return m_cargoValue > 0.0f; }

    // Check if we can fire at target
    bool canFireAtTarget() const;
    float getAngleToTarget() const;
    float getDistanceToTarget() const;

    // Check if target is behind us (danger!)
    bool isTargetBehind() const;

    // Manual controls (for player or scripted behavior)
    void fireWeapon();
    void setThrottle(float throttle) { m_throttle = std::clamp(throttle, 0.0f, 1.0f); }
    float getThrottle() const { return m_throttle; }

    // Patrol waypoints
    void setPatrolPoints(const std::vector<glm::vec3>& points) { m_patrolPoints = points; m_patrolIndex = 0; }
    const std::vector<glm::vec3>& getPatrolPoints() const { return m_patrolPoints; }
    void clearPatrolPoints() { m_patrolPoints.clear(); m_patrolIndex = 0; }

    // Detection range (how far can we see enemies)
    void setDetectionRange(float range) { m_detectionRange = range; }
    float getDetectionRange() const { return m_detectionRange; }

    // Faction/team (for friend/foe identification)
    void setFaction(int faction) { m_faction = faction; }
    int getFaction() const { return m_faction; }
    bool isEnemy(const DogfightAI* other) const { return other && other->m_faction != m_faction; }
    bool isFriendly(const DogfightAI* other) const { return other && other->m_faction == m_faction; }

    // Callbacks
    void setOnEvent(DogfightEventCallback callback) { m_onEvent = callback; }
    void setOnCargoJettison(CargoJettisonCallback callback) { m_onCargoJettison = callback; }
    void setOnEjection(EjectionCallback callback) { m_onEjection = callback; }

    // Is this AI currently shooting?
    bool isFiring() const { return m_isFiring; }
    glm::vec3 getLastShotDirection() const { return m_lastShotDirection; }

private:
    // State updates
    void updateIdle(float deltaTime);
    void updatePatrol(float deltaTime);
    void updatePursue(float deltaTime);
    void updateEngage(float deltaTime);
    void updateEvade(float deltaTime);
    void updateFleeing(float deltaTime);
    void updateEjecting(float deltaTime);

    // AI decision making
    void evaluateSituation();
    void transitionTo(DogfightState newState);

    // Movement helpers
    void turnToward(const glm::vec3& targetPos, float deltaTime);
    void moveForward(float deltaTime);

    // Combat helpers
    void tryFire(float deltaTime);
    void jettisonCargo();
    void ejectPilot();

    // Event emission
    void emitEvent(const std::string& event);

    // Identity
    uint32_t m_id;
    std::string m_name;

    // Linked scene object
    SceneObject* m_sceneObject = nullptr;

    // Fallback position/rotation if no scene object
    glm::vec3 m_position{0.0f};
    glm::vec3 m_rotation{0.0f};  // Euler degrees

    // Movement
    float m_speed = 100.0f;         // Units per second at full throttle
    float m_turnRate = 45.0f;       // Degrees per second (lower = wider circles, ~128m radius)
    float m_throttle = 1.0f;        // 0-1

    // State
    DogfightState m_state = DogfightState::IDLE;
    float m_stateTimer = 0.0f;

    // Target (another DogfightAI)
    DogfightAI* m_target = nullptr;

    // Attacker tracking (for player or non-AI attackers)
    bool m_hasAttacker = false;
    glm::vec3 m_attackerPosition{0.0f};

    // Combat parameters
    float m_weaponRange = 300.0f;       // Max effective range
    float m_weaponConeAngle = 10.0f;    // Degrees from forward to hit
    float m_damagePerShot = 10.0f;      // Damage per hit
    float m_fireRate = 5.0f;            // Shots per second
    float m_fireCooldown = 0.0f;        // Time until next shot
    bool m_isFiring = false;
    glm::vec3 m_lastShotDirection{0, 0, 1};

    // Health thresholds
    float m_fleeHealthPercent = 0.2f;       // 20% - start fleeing
    float m_jettisonHealthPercent = 0.3f;   // 30% - jettison cargo

    // Cargo
    float m_cargoValue = 0.0f;

    // Detection
    float m_detectionRange = 500.0f;

    // Faction
    int m_faction = 0;  // 0 = neutral, 1+ = teams

    // Patrol
    std::vector<glm::vec3> m_patrolPoints;
    size_t m_patrolIndex = 0;

    // Evasion
    float m_evasionTimer = 0.0f;
    int m_evasionDirection = 1;  // 1 or -1 for random breaks

    // Callbacks
    DogfightEventCallback m_onEvent;
    CargoJettisonCallback m_onCargoJettison;
    EjectionCallback m_onEjection;
};

} // namespace eden
