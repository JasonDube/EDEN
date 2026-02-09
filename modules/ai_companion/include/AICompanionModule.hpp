#pragma once

#include "BeingTypes.hpp"
#include "ConversationManager.hpp"
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <functional>

namespace eden {
namespace ai {

/**
 * Configuration for the AI Companion module.
 */
struct AICompanionConfig {
    std::string backendUrl = "http://localhost:8080";
    float interactionRange = 3.0f;        // Max distance to interact with NPCs
    bool autoGreet = true;                 // NPCs greet player on conversation start
    bool showDialogueBubbles = true;       // Show 3D dialogue bubbles above NPCs
};

/**
 * Interface for objects that can participate in conversations.
 * Implement this in your SceneObject or Entity class.
 */
class IConversable {
public:
    virtual ~IConversable() = default;
    
    virtual std::string getName() const = 0;
    virtual BeingType getBeingType() const = 0;
    virtual glm::vec3 getPosition() const = 0;
    virtual bool isSentient() const { return ai::isSentient(getBeingType()); }
    
    // Optional: custom personality override
    virtual std::string getCustomPersonality() const { return ""; }
};

/**
 * AI Companion Module - Pluggable AI conversation system for EDEN.
 * 
 * Usage:
 *   auto ai = std::make_unique<AICompanionModule>();
 *   ai->initialize(config);
 *   
 *   // In update loop:
 *   ai->update(deltaTime);
 *   
 *   // When player interacts:
 *   if (ai->canInteract(npc, playerPos)) {
 *       ai->startConversation(npc);
 *   }
 *   
 *   // In render:
 *   ai->renderUI();
 */
class AICompanionModule {
public:
    AICompanionModule();
    ~AICompanionModule();
    
    // Disable copy
    AICompanionModule(const AICompanionModule&) = delete;
    AICompanionModule& operator=(const AICompanionModule&) = delete;
    
    /**
     * Initialize the module.
     */
    void initialize(const AICompanionConfig& config = {});
    
    /**
     * Shutdown and cleanup.
     */
    void shutdown();
    
    /**
     * Update - call each frame.
     */
    void update(float deltaTime);
    
    /**
     * Check if backend is connected.
     */
    bool isConnected() const;
    
    /**
     * Check if player can interact with a conversable object.
     */
    bool canInteract(const IConversable* target, const glm::vec3& playerPos) const;
    
    /**
     * Find the nearest conversable object within range.
     * @param conversables List of all conversable objects
     * @param playerPos Player's current position
     * @return Pointer to nearest conversable, or nullptr if none in range
     */
    template<typename Container>
    const IConversable* findNearestConversable(const Container& conversables,
                                                const glm::vec3& playerPos) const {
        const IConversable* nearest = nullptr;
        float nearestDist = m_config.interactionRange;
        
        for (const auto& obj : conversables) {
            const IConversable* conv = getConversable(obj);
            if (!conv || !conv->isSentient()) continue;
            
            float dist = glm::distance(conv->getPosition(), playerPos);
            if (dist < nearestDist) {
                nearestDist = dist;
                nearest = conv;
            }
        }
        return nearest;
    }
    
    /**
     * Start a conversation with a conversable object.
     */
    void startConversation(const IConversable* target);
    
    /**
     * End the current conversation.
     */
    void endConversation();
    
    /**
     * Check if currently in a conversation.
     */
    bool isInConversation() const;
    
    /**
     * Get the current conversation target.
     */
    const IConversable* getCurrentTarget() const;
    
    /**
     * Send a player message (call from UI input handler).
     */
    void sendPlayerMessage(const std::string& message);
    
    /**
     * Check if waiting for AI response.
     */
    bool isWaitingForResponse() const;
    
    /**
     * Get conversation history for current session.
     */
    const std::vector<ChatMessage>& getConversationHistory() const;
    
    /**
     * Render the conversation UI (ImGui-based).
     * Call this in your render/UI pass.
     */
    void renderConversationUI();
    
    /**
     * Get the configuration.
     */
    const AICompanionConfig& getConfig() const { return m_config; }
    
    /**
     * Access the underlying conversation manager.
     */
    ConversationManager& getConversationManager() { return *m_conversationManager; }
    
protected:
    // Override this if your container uses different pointer types
    virtual const IConversable* getConversable(const IConversable* obj) const { return obj; }
    
private:
    AICompanionConfig m_config;
    std::unique_ptr<ConversationManager> m_conversationManager;
    
    const IConversable* m_currentTarget = nullptr;
    std::string m_currentSessionId;
    
    // UI state
    char m_inputBuffer[512] = {0};
    bool m_scrollToBottom = false;
    bool m_focusInput = true;
};

} // namespace ai
} // namespace eden
