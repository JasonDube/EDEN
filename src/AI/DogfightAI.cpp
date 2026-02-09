#include "DogfightAI.hpp"
#include <glm/gtx/norm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <algorithm>
#include <cstdio>

namespace eden {

DogfightAI::DogfightAI(uint32_t id, const std::string& name)
    : m_id(id)
    , m_name(name.empty() ? "Fighter_" + std::to_string(id) : name)
{
}

const char* DogfightAI::getStateName() const {
    switch (m_state) {
        case DogfightState::IDLE:     return "Idle";
        case DogfightState::PATROL:   return "Patrol";
        case DogfightState::PURSUE:   return "Pursue";
        case DogfightState::ENGAGE:   return "Engage";
        case DogfightState::EVADE:    return "Evade";
        case DogfightState::FLEEING:  return "Fleeing";
        case DogfightState::EJECTING: return "Ejecting";
        case DogfightState::DEAD:     return "Dead";
        default: return "Unknown";
    }
}

glm::vec3 DogfightAI::getPosition() const {
    if (m_sceneObject) {
        return m_sceneObject->getTransform().getPosition();
    }
    return m_position;
}

void DogfightAI::setPosition(const glm::vec3& pos) {
    if (m_sceneObject) {
        m_sceneObject->getTransform().setPosition(pos);
    }
    m_position = pos;
}

glm::vec3 DogfightAI::getForward() const {
    glm::vec3 rot = getRotation();
    // Convert euler to forward vector (assuming Y-up, Z-forward initially)
    float yaw = glm::radians(rot.y);
    float pitch = glm::radians(rot.x);

    glm::vec3 forward;
    forward.x = sin(yaw) * cos(pitch);
    forward.y = -sin(pitch);
    forward.z = cos(yaw) * cos(pitch);

    return glm::normalize(forward);
}

void DogfightAI::setRotation(const glm::vec3& eulerDegrees) {
    if (m_sceneObject) {
        m_sceneObject->setEulerRotation(eulerDegrees);
    }
    m_rotation = eulerDegrees;
}

glm::vec3 DogfightAI::getRotation() const {
    if (m_sceneObject) {
        return m_sceneObject->getEulerRotation();
    }
    return m_rotation;
}

void DogfightAI::update(float deltaTime) {
    if (m_state == DogfightState::DEAD) {
        return;
    }

    m_stateTimer += deltaTime;
    m_isFiring = false;

    // Check health status and attack state from scene object
    if (m_sceneObject) {
        float healthPercent = m_sceneObject->getHealthPercent();

        // Check for death
        if (m_sceneObject->isDead() && m_state != DogfightState::EJECTING) {
            transitionTo(DogfightState::EJECTING);
        }
        // Check for jettison threshold (30%)
        else if (healthPercent <= m_jettisonHealthPercent &&
                 !m_sceneObject->hasJettisonedCargo() &&
                 hasCargo()) {
            jettisonCargo();
        }
        // Check for flee threshold (20%) - only flee if there's an active threat
        else if (healthPercent <= m_fleeHealthPercent &&
                 m_state != DogfightState::FLEEING &&
                 m_state != DogfightState::EJECTING &&
                 (hasTarget() || m_hasAttacker)) {
            transitionTo(DogfightState::FLEEING);
        }

        // Check if we're being attacked (defensive behavior)
        if (m_sceneObject->isUnderAttack() && m_state != DogfightState::EJECTING && m_state != DogfightState::DEAD) {
            m_hasAttacker = true;
            // Always update attacker position to track moving targets
            m_attackerPosition = m_sceneObject->getAttackerPosition();

            // Only react if we're not already in combat mode
            if (m_state == DogfightState::IDLE || m_state == DogfightState::PATROL) {
                emitEvent("Under attack! Engaging hostile!");
                transitionTo(DogfightState::PURSUE);
            }
        }
        // Keep updating attacker position even if already in combat
        else if (m_hasAttacker && m_state != DogfightState::IDLE && m_state != DogfightState::PATROL) {
            m_attackerPosition = m_sceneObject->getAttackerPosition();
        }
    }

    // Evaluate tactical situation periodically
    evaluateSituation();

    // Update based on current state
    switch (m_state) {
        case DogfightState::IDLE:     updateIdle(deltaTime); break;
        case DogfightState::PATROL:   updatePatrol(deltaTime); break;
        case DogfightState::PURSUE:   updatePursue(deltaTime); break;
        case DogfightState::ENGAGE:   updateEngage(deltaTime); break;
        case DogfightState::EVADE:    updateEvade(deltaTime); break;
        case DogfightState::FLEEING:  updateFleeing(deltaTime); break;
        case DogfightState::EJECTING: updateEjecting(deltaTime); break;
        case DogfightState::DEAD:     break;
    }

    // Update fire cooldown
    if (m_fireCooldown > 0) {
        m_fireCooldown -= deltaTime;
    }
}

void DogfightAI::evaluateSituation() {
    // Skip evaluation during certain states
    if (m_state == DogfightState::EJECTING ||
        m_state == DogfightState::DEAD ||
        m_state == DogfightState::FLEEING) {
        return;
    }

    // Check if we have any valid target (DogfightAI or attacker position)
    bool hasValidTarget = hasTarget() || m_hasAttacker;

    // If we have a target
    if (hasValidTarget) {
        // Check if DogfightAI target is still alive
        if (hasTarget() && m_target->getSceneObject() && m_target->getSceneObject()->isDead()) {
            clearTarget();
            // Fall through to check m_hasAttacker
            hasValidTarget = m_hasAttacker;
        }

        if (hasValidTarget) {
            // Get target info (works for both DogfightAI targets and attacker positions)
            glm::vec3 targetPos = hasTarget() ? m_target->getPosition() : m_attackerPosition;
            glm::vec3 pos = getPosition();
            float dist = glm::distance(pos, targetPos);
            glm::vec3 toTarget = glm::normalize(targetPos - pos);
            float angle = glm::degrees(acos(std::clamp(glm::dot(getForward(), toTarget), -1.0f, 1.0f)));

            // Only check "target behind" for DogfightAI targets
            bool targetBehind = hasTarget() && isTargetBehind();

            // Target is behind us - EVADE!
            if (targetBehind && m_state != DogfightState::EVADE) {
                transitionTo(DogfightState::EVADE);
            }
            // In firing position - ENGAGE
            else if (!targetBehind && dist < m_weaponRange && angle < m_weaponConeAngle * 2.0f) {
                if (m_state != DogfightState::ENGAGE) {
                    transitionTo(DogfightState::ENGAGE);
                }
            }
            // Need to close distance or get better angle - PURSUE
            else if (!targetBehind && m_state != DogfightState::PURSUE) {
                transitionTo(DogfightState::PURSUE);
            }
            return;
        }
    }

    // No target - patrol or idle
    if (m_state == DogfightState::PURSUE ||
        m_state == DogfightState::ENGAGE ||
        m_state == DogfightState::EVADE) {
        if (!m_patrolPoints.empty()) {
            transitionTo(DogfightState::PATROL);
        } else {
            transitionTo(DogfightState::IDLE);
        }
    }
}

void DogfightAI::transitionTo(DogfightState newState) {
    if (m_state == newState) return;

    DogfightState oldState = m_state;
    m_state = newState;
    m_stateTimer = 0.0f;

    // State entry actions
    switch (newState) {
        case DogfightState::EVADE:
            m_evasionTimer = 0.0f;
            m_evasionDirection = (rand() % 2) * 2 - 1;  // -1 or 1
            emitEvent("Evading!");
            break;
        case DogfightState::ENGAGE:
            emitEvent("Engaging target!");
            break;
        case DogfightState::PURSUE:
            emitEvent("Pursuing target");
            break;
        case DogfightState::FLEEING:
            emitEvent("Taking heavy damage, fleeing!");
            break;
        case DogfightState::EJECTING:
            ejectPilot();
            break;
        default:
            break;
    }
}

void DogfightAI::updateIdle(float deltaTime) {
    // Just hover/maintain position
    // Could add slight random movement or scanning behavior
    if (!m_patrolPoints.empty()) {
        transitionTo(DogfightState::PATROL);
    }
}

void DogfightAI::updatePatrol(float deltaTime) {
    if (m_patrolPoints.empty()) {
        transitionTo(DogfightState::IDLE);
        return;
    }

    glm::vec3 targetPoint = m_patrolPoints[m_patrolIndex];
    glm::vec3 pos = getPosition();
    float dist = glm::distance(pos, targetPoint);

    // Arrived at waypoint
    if (dist < 10.0f) {
        m_patrolIndex = (m_patrolIndex + 1) % m_patrolPoints.size();
        return;
    }

    // Turn toward waypoint and move
    turnToward(targetPoint, deltaTime);
    moveForward(deltaTime);
}

void DogfightAI::updatePursue(float deltaTime) {
    // Get target position (either from DogfightAI target or attacker position)
    glm::vec3 targetPos;
    bool hasValidTarget = false;

    if (hasTarget()) {
        targetPos = m_target->getPosition();
        hasValidTarget = true;
    } else if (m_hasAttacker) {
        targetPos = m_attackerPosition;
        hasValidTarget = true;
    }

    if (!hasValidTarget) {
        transitionTo(DogfightState::IDLE);
        return;
    }

    glm::vec3 pos = getPosition();
    float dist = glm::distance(pos, targetPos);

    // If we have a DogfightAI target, try to get behind them
    glm::vec3 pursuitPoint = targetPos;
    if (hasTarget()) {
        glm::vec3 targetForward = m_target->getForward();
        glm::vec3 idealPosition = targetPos - targetForward * 100.0f;  // Behind target
        float behindWeight = std::clamp(1.0f - dist / m_weaponRange, 0.0f, 0.7f);
        pursuitPoint = glm::mix(targetPos, idealPosition, behindWeight);
    }

    m_throttle = 1.0f;  // Full speed pursuit!
    turnToward(pursuitPoint, deltaTime);
    moveForward(deltaTime);

    // Transition to engage when close enough and facing target
    glm::vec3 toTarget = glm::normalize(targetPos - pos);
    float angle = glm::degrees(acos(std::clamp(glm::dot(getForward(), toTarget), -1.0f, 1.0f)));
    if (dist < m_weaponRange && angle < m_weaponConeAngle * 3.0f) {
        transitionTo(DogfightState::ENGAGE);
    }
}

void DogfightAI::updateEngage(float deltaTime) {
    // Get target position
    glm::vec3 targetPos;
    bool hasValidTarget = false;

    if (hasTarget()) {
        targetPos = m_target->getPosition();
        hasValidTarget = true;
    } else if (m_hasAttacker) {
        targetPos = m_attackerPosition;
        hasValidTarget = true;
    }

    if (!hasValidTarget) {
        transitionTo(DogfightState::IDLE);
        return;
    }

    glm::vec3 pos = getPosition();
    float dist = glm::distance(pos, targetPos);
    glm::vec3 toTarget = glm::normalize(targetPos - pos);
    glm::vec3 forward = getForward();
    float dotToTarget = glm::dot(forward, toTarget);

    // Only break off if we REALLY overshot (target far behind us)
    if (dotToTarget < -0.7f && dist < 30.0f) {
        // We badly overshot - break off and circle back
        m_evasionDirection = (rand() % 2 == 0) ? 1 : -1;
        m_evasionTimer = 0.0f;
        transitionTo(DogfightState::EVADE);
        emitEvent("Breaking off!");
        return;
    }

    // Calculate engagement point - aim directly at target with slight lead
    glm::vec3 engagePoint = targetPos;

    // If target is another AI, try to get behind them
    if (hasTarget()) {
        glm::vec3 targetForward = m_target->getForward();
        // Lead the target slightly based on their movement
        engagePoint += targetForward * (dist * 0.1f);

        // Try to position behind them when close
        if (dist < 150.0f) {
            glm::vec3 behindTarget = targetPos - targetForward * 50.0f;
            float behindWeight = std::clamp(1.0f - dist / 150.0f, 0.0f, 0.4f);
            engagePoint = glm::mix(engagePoint, behindTarget, behindWeight);
        }
    }

    // Vertical maneuvering - try to get above
    float heightDiff = pos.y - targetPos.y;
    if (heightDiff < -15.0f) {
        engagePoint.y += 30.0f;  // Climb to get above
    }

    // Speed control - stay aggressive
    if (dist < 50.0f) {
        m_throttle = 0.5f;  // Slow a bit when very close
    } else {
        m_throttle = 1.0f;  // Full speed otherwise
    }

    turnToward(engagePoint, deltaTime);
    moveForward(deltaTime);

    // Try to fire at target - this actually deals damage!
    tryFire(deltaTime);
}

void DogfightAI::updateEvade(float deltaTime) {
    m_evasionTimer += deltaTime;

    // Evasive maneuver: break turn away from enemy
    glm::vec3 rot = getRotation();

    // Roll and pull (corkscrew maneuver)
    rot.y += m_evasionDirection * m_turnRate * 1.5f * deltaTime;  // Faster turn
    rot.x += sin(m_evasionTimer * 3.0f) * 30.0f * deltaTime;      // Pitch variation

    setRotation(rot);
    m_throttle = 1.0f;  // Full speed
    moveForward(deltaTime);

    // After evading for a bit, re-evaluate
    if (m_evasionTimer > 2.0f) {
        // Check if we've shaken them
        if (!isTargetBehind()) {
            transitionTo(DogfightState::PURSUE);
        } else {
            // Keep evading but change direction
            m_evasionTimer = 0.0f;
            m_evasionDirection *= -1;
        }
    }
}

void DogfightAI::updateFleeing(float deltaTime) {
    // Get threat position
    glm::vec3 threatPos;
    bool hasThreat = false;

    if (hasTarget()) {
        threatPos = m_target->getPosition();
        hasThreat = true;
    } else if (m_hasAttacker) {
        threatPos = m_attackerPosition;
        hasThreat = true;
    }

    if (!hasThreat) {
        // No one to flee from, go idle
        transitionTo(DogfightState::IDLE);
        return;
    }

    // Run away from threat
    glm::vec3 pos = getPosition();
    glm::vec3 awayDir = glm::normalize(pos - threatPos);
    glm::vec3 fleePoint = pos + awayDir * 500.0f;

    turnToward(fleePoint, deltaTime);
    m_throttle = 1.0f;  // Full speed
    moveForward(deltaTime);

    // Check if we've escaped
    float dist = glm::distance(pos, threatPos);
    if (dist > m_detectionRange * 1.5f) {
        clearTarget();
        m_hasAttacker = false;
        // Clear the scene object's attack state so it doesn't keep triggering
        if (m_sceneObject) {
            m_sceneObject->clearAttackState();
        }
        emitEvent("Escaped!");
        if (!m_patrolPoints.empty()) {
            transitionTo(DogfightState::PATROL);
        } else {
            transitionTo(DogfightState::IDLE);
        }
    }
}

void DogfightAI::updateEjecting(float deltaTime) {
    // Ejection is handled, just wait to transition to dead
    if (m_stateTimer > 1.0f) {
        m_state = DogfightState::DEAD;
        emitEvent("Pilot ejected");
    }
}

void DogfightAI::turnToward(const glm::vec3& targetPos, float deltaTime) {
    glm::vec3 pos = getPosition();
    glm::vec3 toTarget = targetPos - pos;

    if (glm::length2(toTarget) < 0.001f) return;

    toTarget = glm::normalize(toTarget);

    // Calculate desired yaw and pitch
    float desiredYaw = glm::degrees(atan2(toTarget.x, toTarget.z));
    float desiredPitch = glm::degrees(-asin(toTarget.y));

    glm::vec3 rot = getRotation();

    // Smoothly rotate toward target
    float yawDiff = desiredYaw - rot.y;
    // Normalize angle difference to -180 to 180
    while (yawDiff > 180.0f) yawDiff -= 360.0f;
    while (yawDiff < -180.0f) yawDiff += 360.0f;

    float pitchDiff = desiredPitch - rot.x;

    float maxTurn = m_turnRate * deltaTime;

    rot.y += std::clamp(yawDiff, -maxTurn, maxTurn);
    rot.x += std::clamp(pitchDiff, -maxTurn, maxTurn);

    // Clamp pitch
    rot.x = std::clamp(rot.x, -89.0f, 89.0f);

    setRotation(rot);
}

void DogfightAI::moveForward(float deltaTime) {
    glm::vec3 forward = getForward();
    glm::vec3 pos = getPosition();

    pos += forward * m_speed * m_throttle * deltaTime;
    setPosition(pos);
}

bool DogfightAI::canFireAtTarget() const {
    if (!hasTarget()) return false;

    float dist = getDistanceToTarget();
    float angle = getAngleToTarget();

    return dist <= m_weaponRange && angle <= m_weaponConeAngle;
}

float DogfightAI::getAngleToTarget() const {
    if (!hasTarget()) return 180.0f;

    glm::vec3 pos = getPosition();
    glm::vec3 targetPos = m_target->getPosition();
    glm::vec3 toTarget = glm::normalize(targetPos - pos);
    glm::vec3 forward = getForward();

    float dot = glm::dot(forward, toTarget);
    return glm::degrees(acos(std::clamp(dot, -1.0f, 1.0f)));
}

float DogfightAI::getDistanceToTarget() const {
    if (!hasTarget()) return 99999.0f;
    return glm::distance(getPosition(), m_target->getPosition());
}

bool DogfightAI::isTargetBehind() const {
    if (!hasTarget()) return false;

    // Check if target is behind us AND facing toward us (threatening position)
    glm::vec3 pos = getPosition();
    glm::vec3 targetPos = m_target->getPosition();
    glm::vec3 toTarget = glm::normalize(targetPos - pos);
    glm::vec3 forward = getForward();

    // Target is behind if dot product is negative
    float dot = glm::dot(forward, toTarget);
    if (dot > -0.3f) return false;  // Target not really behind us

    // Check if target is facing toward us
    glm::vec3 targetForward = m_target->getForward();
    glm::vec3 toUs = -toTarget;
    float targetDot = glm::dot(targetForward, toUs);

    return targetDot > 0.5f;  // Target is facing us from behind
}

void DogfightAI::tryFire(float deltaTime) {
    if (m_fireCooldown > 0) return;
    if (!canFireAtTarget()) return;

    // Fire!
    m_isFiring = true;
    m_fireCooldown = 1.0f / m_fireRate;
    m_lastShotDirection = getForward();

    // Deal damage to target
    if (m_target && m_target->getSceneObject()) {
        SceneObject* targetObj = m_target->getSceneObject();
        float oldHP = targetObj->getHealth();
        targetObj->takeDamage(m_damagePerShot);
        float newHP = targetObj->getHealth();
        targetObj->setUnderAttack(true, getPosition());

        printf("[DAMAGE] %s hit %s for %.0f dmg (%.0f -> %.0f HP)\n",
               m_name.c_str(), targetObj->getName().c_str(),
               m_damagePerShot, oldHP, newHP);
    }

    emitEvent("Firing!");
}

void DogfightAI::fireWeapon() {
    // Manual fire (for player control)
    if (m_fireCooldown > 0) return;

    m_isFiring = true;
    m_fireCooldown = 1.0f / m_fireRate;
    m_lastShotDirection = getForward();
}

void DogfightAI::jettisonCargo() {
    if (!hasCargo()) return;
    if (m_sceneObject && m_sceneObject->hasJettisonedCargo()) return;

    glm::vec3 pos = getPosition();
    float value = m_cargoValue;

    emitEvent("Jettisoning cargo!");

    if (m_onCargoJettison) {
        m_onCargoJettison(pos, value);
    }

    m_cargoValue = 0.0f;

    if (m_sceneObject) {
        m_sceneObject->setJettisonedCargo(true);
    }
}

void DogfightAI::ejectPilot() {
    glm::vec3 pos = getPosition();
    glm::vec3 velocity = getForward() * m_speed * 0.5f;  // Some forward momentum
    velocity.y += 20.0f;  // Upward ejection boost

    emitEvent("EJECT! EJECT! EJECT!");

    if (m_onEjection) {
        m_onEjection(pos, velocity);
    }

    if (m_sceneObject) {
        m_sceneObject->setEjected(true);
    }
}

void DogfightAI::emitEvent(const std::string& event) {
    if (m_onEvent) {
        m_onEvent(m_name + ": " + event);
    }
}

} // namespace eden
