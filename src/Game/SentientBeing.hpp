#pragma once

#include "../Editor/SceneObject.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace eden {

class AINode;

/**
 * SentientBeing is a SceneObject that can be interacted with,
 * have dialogue, and follow AI waypoints.
 *
 * Base class for NPCs, creatures, and any entity with "presence".
 */
class SentientBeing : public SceneObject {
public:
    SentientBeing();
    SentientBeing(const std::string& name);
    virtual ~SentientBeing() = default;

    // Interaction
    void setInteractionRadius(float radius) { m_interactionRadius = radius; }
    float getInteractionRadius() const { return m_interactionRadius; }

    void setCanInteract(bool canInteract) { m_canInteract = canInteract; }
    bool canInteract() const { return m_canInteract; }

    // Check if a position is within interaction range
    bool isInInteractionRange(const glm::vec3& position) const;

    // Called when player presses interact key (E) while in range
    virtual void onInteract();

    // Per-frame update for AI logic, dialogue, etc.
    virtual void update(float deltaTime);

    // Dialogue
    void setCurrentDialogue(const std::string& dialogue) { m_currentDialogue = dialogue; }
    const std::string& getCurrentDialogue() const { return m_currentDialogue; }

    void setDialogueVisible(bool visible) { m_dialogueVisible = visible; }
    bool isDialogueVisible() const { return m_dialogueVisible; }

    void setDialogueDuration(float duration) { m_dialogueDuration = duration; }
    float getDialogueDuration() const { return m_dialogueDuration; }

    // AI Waypoint following (for patrol, schedules, etc.)
    void setCurrentWaypointId(uint32_t id) { m_currentWaypointId = id; }
    uint32_t getCurrentWaypointId() const { return m_currentWaypointId; }

    void setTargetWaypointId(uint32_t id) { m_targetWaypointId = id; }
    uint32_t getTargetWaypointId() const { return m_targetWaypointId; }

    void setMovementSpeed(float speed) { m_movementSpeed = speed; }
    float getMovementSpeed() const { return m_movementSpeed; }

    void setIsMoving(bool moving) { m_isMoving = moving; }
    bool isMoving() const { return m_isMoving; }

    // Type identification
    virtual const char* getTypeName() const { return "SentientBeing"; }
    bool isSentient() const { return true; }

protected:
    // Interaction
    float m_interactionRadius = 5.0f;
    bool m_canInteract = true;

    // Dialogue state
    std::string m_currentDialogue;
    bool m_dialogueVisible = false;
    float m_dialogueDuration = 0.0f;      // Time remaining to show dialogue
    float m_dialogueDisplayTime = 3.0f;   // How long dialogue stays visible

    // AI waypoint state
    uint32_t m_currentWaypointId = 0;     // Where we are (or came from)
    uint32_t m_targetWaypointId = 0;      // Where we're going
    float m_movementSpeed = 5.0f;         // Units per second
    bool m_isMoving = false;
};

} // namespace eden
