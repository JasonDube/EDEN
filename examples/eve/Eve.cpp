#include "Eve.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>

namespace eve {

Eve::Eve() = default;
Eve::~Eve() {
    if (m_httpClient) {
        m_httpClient->stop();
    }
}

bool Eve::initialize(const std::string& backendUrl) {
    m_httpClient = std::make_unique<eden::AsyncHttpClient>(backendUrl);
    m_httpClient->start();
    
    // Check health
    bool healthy = false;
    m_httpClient->checkHealth([&healthy](const eden::AsyncHttpClient::Response& resp) {
        healthy = resp.success;
    });
    
    // Give it a moment to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    m_httpClient->pollResponses();
    
    m_initialized = true;
    return true; // We'll handle connection issues gracefully
}

bool Eve::loadPersonality(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "Eve: Failed to load personality config: " << configPath << std::endl;
        return false;
    }
    
    try {
        file >> m_personalityConfig;
        
        if (m_personalityConfig.contains("name")) {
            m_name = m_personalityConfig["name"].get<std::string>();
        }
        
        std::cout << "Eve: Personality loaded - " << m_name << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Eve: Error parsing personality config: " << e.what() << std::endl;
        return false;
    }
}

std::string Eve::buildSystemPrompt() const {
    std::stringstream ss;
    
    // Start with base template from config
    if (m_personalityConfig.contains("system_prompt_template")) {
        ss << m_personalityConfig["system_prompt_template"].get<std::string>();
    } else {
        // Fallback prompt
        ss << "You are Eve, an intelligent android companion aboard a trading vessel. ";
        ss << "The person speaking to you is the Captain, your owner. ";
        ss << "Be helpful, intelligent, and genuine.";
    }
    
    ss << "\n\n";
    
    // Add current state context
    ss << "CURRENT STATUS:\n";
    ss << "- Location: " << m_state.current_location << "\n";
    ss << "- Activity: " << m_state.current_activity << "\n";
    
    // Add parameter-driven instructions
    ss << "\nBEHAVIOR PARAMETERS:\n";
    
    if (m_parameters.reasoning_depth > 0.7f) {
        ss << "- Think through complex problems step by step, showing your reasoning.\n";
    }
    
    if (m_parameters.warmth > 0.7f) {
        ss << "- Show genuine warmth and care in your responses.\n";
    } else if (m_parameters.warmth < 0.3f) {
        ss << "- Maintain professional distance; focus on facts and analysis.\n";
    }
    
    if (m_parameters.formality > 0.7f) {
        ss << "- Use formal language and proper terminology.\n";
    } else if (m_parameters.formality < 0.3f) {
        ss << "- Speak casually and conversationally.\n";
    }
    
    if (m_parameters.curiosity > 0.7f) {
        ss << "- Ask follow-up questions when topics interest you.\n";
    }
    
    if (m_parameters.verbosity < 0.3f) {
        ss << "- Keep responses concise and to the point.\n";
    } else if (m_parameters.verbosity > 0.7f) {
        ss << "- Provide thorough, detailed responses.\n";
    }
    
    return ss.str();
}

void Eve::chat(const std::string& message, ResponseCallback callback) {
    if (!m_httpClient || m_thinking) {
        return;
    }
    
    // Record Captain's message
    Message captainMsg;
    captainMsg.role = Message::Role::Captain;
    captainMsg.content = message;
    captainMsg.timestamp = static_cast<float>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count() / 1000.0f
    );
    m_history.push_back(captainMsg);
    
    m_thinking = true;
    m_pendingCallback = callback;
    
    // Build custom personality based on parameters
    std::string personality = buildSystemPrompt();
    
    m_httpClient->sendChatMessage(
        m_sessionId,
        message,
        m_name,
        personality,
        7, // EVE being type
        [this](const eden::AsyncHttpClient::Response& resp) {
            onResponse(resp);
        }
    );
}

void Eve::onResponse(const eden::AsyncHttpClient::Response& response) {
    m_thinking = false;
    
    if (response.success) {
        try {
            auto json = nlohmann::json::parse(response.body);
            
            // Update session ID if provided
            if (json.contains("session_id")) {
                m_sessionId = json["session_id"].get<std::string>();
            }
            
            std::string eveResponse = json.value("response", "...");
            
            // Record Eve's response
            Message eveMsg;
            eveMsg.role = Message::Role::Eve;
            eveMsg.content = eveResponse;
            eveMsg.timestamp = static_cast<float>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count() / 1000.0f
            );
            m_history.push_back(eveMsg);
            
            // Update state
            m_state.conversations_count++;
            m_state.engagement_level = std::min(1.0f, m_state.engagement_level + 0.05f);
            m_state.rapport = std::min(1.0f, m_state.rapport + 0.01f);
            
            if (m_pendingCallback) {
                m_pendingCallback(eveResponse);
            }
        } catch (const std::exception& e) {
            std::cerr << "Eve: Error parsing response: " << e.what() << std::endl;
            if (m_pendingCallback) {
                m_pendingCallback("I... seem to be having difficulty processing. Could you repeat that, Captain?");
            }
        }
    } else {
        std::cerr << "Eve: Communication error: " << response.error << std::endl;
        if (m_pendingCallback) {
            m_pendingCallback("*static* I'm having trouble with my communication systems, Captain.");
        }
    }
    
    m_pendingCallback = nullptr;
}

void Eve::update() {
    if (m_httpClient) {
        m_httpClient->pollResponses();
    }
}

bool Eve::isConnected() const {
    return m_httpClient && m_httpClient->isConnected();
}

void Eve::resetConversation() {
    m_history.clear();
    m_sessionId.clear();
    m_state.engagement_level = 0.5f;
}

void Eve::setLocation(const std::string& location) {
    m_state.current_location = location;
}

} // namespace eve
