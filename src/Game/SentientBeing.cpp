#include "SentientBeing.hpp"
#include "../Renderer/ModelRenderer.hpp"  // For ModelVertex (needed by SceneObject)
#include <glm/glm.hpp>

namespace eden {

SentientBeing::SentientBeing() : SceneObject() {
    setName("Sentient Being");
}

SentientBeing::SentientBeing(const std::string& name) : SceneObject(name) {
}

bool SentientBeing::isInInteractionRange(const glm::vec3& position) const {
    glm::vec3 myPos = getTransform().getPosition();
    float distSq = glm::dot(position - myPos, position - myPos);
    return distSq <= (m_interactionRadius * m_interactionRadius);
}

void SentientBeing::onInteract() {
    // Default behavior: show dialogue if we have one
    if (!m_currentDialogue.empty() && m_canInteract) {
        m_dialogueVisible = true;
        m_dialogueDuration = m_dialogueDisplayTime;
    }
}

void SentientBeing::update(float deltaTime) {
    // Update dialogue timer
    if (m_dialogueVisible && m_dialogueDuration > 0.0f) {
        m_dialogueDuration -= deltaTime;
        if (m_dialogueDuration <= 0.0f) {
            m_dialogueVisible = false;
            m_dialogueDuration = 0.0f;
        }
    }

    // Update behaviors from parent class
    updateBehaviors(deltaTime);
}

} // namespace eden
