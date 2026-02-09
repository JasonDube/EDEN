#pragma once

#include "Network/AsyncHttpClient.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace eve {

/**
 * @brief Represents a single message in the conversation
 */
struct Message {
    enum class Role { Captain, Eve, System };
    Role role;
    std::string content;
    float timestamp;
};

/**
 * @brief Eve's tunable personality parameters
 */
struct PersonalityParameters {
    // Cognitive parameters
    float temperature = 0.7f;           // Response creativity (0.0 - 1.0)
    float reasoning_depth = 0.8f;       // How much she thinks out loud (0.0 - 1.0)
    float verbosity = 0.5f;             // Response length tendency (0.0 - 1.0)
    
    // Personality parameters  
    float warmth = 0.6f;                // Emotional warmth in responses (0.0 - 1.0)
    float formality = 0.4f;             // Formal vs casual speech (0.0 - 1.0)
    float assertiveness = 0.7f;         // How strongly she states opinions (0.0 - 1.0)
    float curiosity = 0.8f;             // How often she asks questions (0.0 - 1.0)
    
    // World awareness
    bool acknowledge_android_nature = true;
    bool maintain_ship_awareness = true;
    bool remember_previous_context = true;
};

/**
 * @brief Eve's current emotional/cognitive state
 */
struct EveState {
    std::string current_location = "Bridge";
    std::string current_activity = "Monitoring ship systems";
    float engagement_level = 0.5f;      // How engaged she is in conversation
    float concern_level = 0.0f;         // If she's worried about something
    
    // Relationship metrics (evolve over time)
    float trust_level = 0.3f;           // How much she trusts the Captain
    float rapport = 0.2f;               // Quality of relationship
    int conversations_count = 0;
};

/**
 * @brief Main Eve AI interface
 */
class Eve {
public:
    using ResponseCallback = std::function<void(const std::string& response)>;
    
    Eve();
    ~Eve();
    
    /**
     * @brief Initialize Eve with the AI backend
     * @param backendUrl URL of the EDEN AI backend (e.g., "http://localhost:8080")
     * @return true if connection successful
     */
    bool initialize(const std::string& backendUrl);
    
    /**
     * @brief Load personality configuration from JSON file
     */
    bool loadPersonality(const std::string& configPath);
    
    /**
     * @brief Send a message to Eve and receive her response
     * @param message The Captain's message
     * @param callback Called when Eve responds
     */
    void chat(const std::string& message, ResponseCallback callback);
    
    /**
     * @brief Poll for completed responses (call each frame)
     */
    void update();
    
    /**
     * @brief Get conversation history
     */
    const std::vector<Message>& getHistory() const { return m_history; }
    
    /**
     * @brief Get/Set personality parameters
     */
    PersonalityParameters& getParameters() { return m_parameters; }
    const PersonalityParameters& getParameters() const { return m_parameters; }
    
    /**
     * @brief Get/Set Eve's current state
     */
    EveState& getState() { return m_state; }
    const EveState& getState() const { return m_state; }
    
    /**
     * @brief Get Eve's name
     */
    const std::string& getName() const { return m_name; }
    
    /**
     * @brief Check if connected to backend
     */
    bool isConnected() const;
    
    /**
     * @brief Check if Eve is currently "thinking" (waiting for response)
     */
    bool isThinking() const { return m_thinking; }
    
    /**
     * @brief Clear conversation history and start fresh
     */
    void resetConversation();
    
    /**
     * @brief Set Eve's current location on the ship
     */
    void setLocation(const std::string& location);
    
private:
    std::string buildSystemPrompt() const;
    void onResponse(const eden::AsyncHttpClient::Response& response);
    
    std::unique_ptr<eden::AsyncHttpClient> m_httpClient;
    std::string m_sessionId;
    std::string m_name = "Eve";
    
    nlohmann::json m_personalityConfig;
    PersonalityParameters m_parameters;
    EveState m_state;
    
    std::vector<Message> m_history;
    ResponseCallback m_pendingCallback;
    bool m_thinking = false;
    bool m_initialized = false;
};

} // namespace eve
