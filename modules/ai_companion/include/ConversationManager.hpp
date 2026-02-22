#pragma once

#include "BeingTypes.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace eden {
namespace ai {

/**
 * A single message in a conversation.
 */
struct ChatMessage {
    std::string sender;
    std::string text;
    bool isPlayer;
    float timestamp;
};

/**
 * Conversation session state.
 */
struct ConversationSession {
    std::string sessionId;
    std::string npcName;
    BeingType beingType;
    std::vector<ChatMessage> history;
    bool isActive = false;
    bool waitingForResponse = false;
};

/**
 * Callback for when AI response is received.
 */
using ResponseCallback = std::function<void(const std::string& response, bool success)>;

/**
 * Callback for connection status changes.
 */
using ConnectionCallback = std::function<void(bool connected)>;

/**
 * Manages conversation sessions with AI-powered NPCs.
 * Handles communication with the AI backend server.
 */
class ConversationManager {
public:
    ConversationManager();
    ~ConversationManager();
    
    // Disable copy
    ConversationManager(const ConversationManager&) = delete;
    ConversationManager& operator=(const ConversationManager&) = delete;
    
    /**
     * Initialize connection to AI backend.
     * @param backendUrl URL of the AI backend server (e.g., "http://localhost:8080")
     */
    void initialize(const std::string& backendUrl);
    
    /**
     * Shutdown and cleanup.
     */
    void shutdown();
    
    /**
     * Update - call each frame to process async responses.
     */
    void update(float deltaTime);
    
    /**
     * Check if connected to backend.
     */
    bool isConnected() const { return m_connected; }
    
    /**
     * Start a new conversation with an NPC.
     * @param npcName Display name of the NPC
     * @param beingType Type of being (affects personality)
     * @param customPersonality Optional custom personality override
     * @return Session ID for this conversation
     */
    std::string startConversation(const std::string& npcName,
                                   BeingType beingType,
                                   const std::string& customPersonality = "");
    
    /**
     * End an active conversation.
     */
    void endConversation(const std::string& sessionId);
    
    /**
     * Send a player message in a conversation.
     * @param sessionId The conversation session
     * @param message Player's message text
     * @param callback Called when response is received
     */
    void sendMessage(const std::string& sessionId,
                     const std::string& message,
                     ResponseCallback callback = nullptr);
    
    /**
     * Get conversation history.
     */
    const std::vector<ChatMessage>& getHistory(const std::string& sessionId) const;
    
    /**
     * Check if waiting for AI response.
     */
    bool isWaitingForResponse(const std::string& sessionId) const;
    
    /**
     * Get the active session (if any).
     */
    ConversationSession* getActiveSession();
    const ConversationSession* getActiveSession() const;
    
    /**
     * Send a heartbeat (passive perception) request.
     * Called periodically for EDEN companions to detect environmental changes.
     * @param jsonBody The JSON request body as string
     * @param callback Called with response body and success flag
     */
    void postHeartbeat(const std::string& jsonBody,
                       std::function<void(const std::string&, bool)> callback);

    /**
     * Add an NPC message to a session's history (e.g., from heartbeat responses).
     */
    void addNpcMessage(const std::string& sessionId,
                       const std::string& npcName,
                       const std::string& message);

    /**
     * Set callback for connection status changes.
     */
    void setConnectionCallback(ConnectionCallback callback);
    
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_connected = false;
};

} // namespace ai
} // namespace eden
