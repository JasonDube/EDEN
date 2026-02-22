#include "AICompanionModule.hpp"
#include <imgui.h>
#include <iostream>
#include <nlohmann/json.hpp>

namespace eden {
namespace ai {

AICompanionModule::AICompanionModule()
    : m_conversationManager(std::make_unique<ConversationManager>()) {
}

AICompanionModule::~AICompanionModule() {
    shutdown();
}

void AICompanionModule::initialize(const AICompanionConfig& config) {
    m_config = config;
    m_conversationManager->initialize(config.backendUrl);
    std::cout << "[AICompanionModule] Initialized" << std::endl;
}

void AICompanionModule::shutdown() {
    if (m_conversationManager) {
        endConversation();
        m_conversationManager->shutdown();
    }
}

void AICompanionModule::update(float deltaTime) {
    if (m_conversationManager) {
        m_conversationManager->update(deltaTime);
    }

    // Passive perception heartbeat for EDEN companions
    if (m_config.enableHeartbeat &&
        m_currentTarget &&
        m_currentTarget->getBeingType() == BeingType::EDEN_COMPANION &&
        m_perceptionProvider) {

        m_heartbeatTimer += deltaTime;
        if (m_heartbeatTimer >= m_config.heartbeatInterval) {
            m_heartbeatTimer = 0.0f;
            sendHeartbeat();
        }
    }
}

bool AICompanionModule::isConnected() const {
    return m_conversationManager && m_conversationManager->isConnected();
}

bool AICompanionModule::canInteract(const IConversable* target, const glm::vec3& playerPos) const {
    if (!target || !target->isSentient()) return false;
    float dist = glm::distance(target->getPosition(), playerPos);
    return dist <= m_config.interactionRange;
}

void AICompanionModule::startConversation(const IConversable* target) {
    if (!target || !target->isSentient()) return;
    if (isInConversation()) {
        endConversation();
    }
    
    m_currentTarget = target;
    m_currentSessionId = m_conversationManager->startConversation(
        target->getName(),
        target->getBeingType(),
        target->getCustomPersonality()
    );
    
    m_focusInput = true;
    m_inputBuffer[0] = '\0';
    
    // Auto-greet if enabled
    if (m_config.autoGreet && isConnected()) {
        m_conversationManager->sendMessage(
            m_currentSessionId,
            "The player approaches you. Greet them briefly in character.",
            [](const std::string& /*response*/, bool /*success*/) {
                // Greeting handled in conversation history
            }
        );
    }
}

void AICompanionModule::endConversation() {
    if (!m_currentSessionId.empty()) {
        m_conversationManager->endConversation(m_currentSessionId);
    }
    m_currentTarget = nullptr;
    m_currentSessionId.clear();
}

bool AICompanionModule::isInConversation() const {
    return m_currentTarget != nullptr && !m_currentSessionId.empty();
}

const IConversable* AICompanionModule::getCurrentTarget() const {
    return m_currentTarget;
}

void AICompanionModule::sendPlayerMessage(const std::string& message) {
    if (!isInConversation() || message.empty()) return;
    
    m_conversationManager->sendMessage(m_currentSessionId, message,
        [this](const std::string& /*response*/, bool /*success*/) {
            m_scrollToBottom = true;
        });
    m_scrollToBottom = true;
}

bool AICompanionModule::isWaitingForResponse() const {
    return isInConversation() && m_conversationManager->isWaitingForResponse(m_currentSessionId);
}

const std::vector<ChatMessage>& AICompanionModule::getConversationHistory() const {
    static std::vector<ChatMessage> empty;
    if (!isInConversation()) return empty;
    return m_conversationManager->getHistory(m_currentSessionId);
}

void AICompanionModule::sendHeartbeat() {
    if (!isConnected() || !m_currentTarget || !m_perceptionProvider) return;

    // Gather current perception via the provider callback (returns JSON)
    nlohmann::json perceptionJson = m_perceptionProvider(m_currentTarget);

    // Build request JSON
    nlohmann::json request;
    request["session_id"] = m_currentSessionId;
    request["npc_name"] = m_currentTarget->getName();
    request["being_type"] = static_cast<int>(m_currentTarget->getBeingType());
    request["perception"] = perceptionJson;

    // Post heartbeat asynchronously
    m_conversationManager->postHeartbeat(request.dump(),
        [this](const std::string& body, bool success) {
            if (!success) return;

            try {
                auto resp = nlohmann::json::parse(body);

                std::string response;
                if (resp.contains("response") && !resp["response"].is_null()) {
                    response = resp["response"].get<std::string>();
                }

                nlohmann::json action = nullptr;
                if (resp.contains("action") && !resp["action"].is_null()) {
                    action = resp["action"];
                }

                bool changesDetected = resp.value("changes_detected", false);

                onHeartbeatResponse(response, action, changesDetected);

            } catch (const std::exception& e) {
                std::cerr << "[AICompanion] Heartbeat parse error: " << e.what() << std::endl;
            }
        });
}

void AICompanionModule::onHeartbeatResponse(const std::string& response,
                                             const nlohmann::json& /*action*/,
                                             bool /*changesDetected*/) {
    if (response.empty()) return;

    // Add unprompted dialogue to conversation history
    if (!m_currentSessionId.empty()) {
        m_conversationManager->addNpcMessage(m_currentSessionId,
                                              m_currentTarget->getName(),
                                              response);
        m_scrollToBottom = true;
    }
}

void AICompanionModule::renderConversationUI() {
    if (!isInConversation()) return;
    
    ImGuiIO& io = ImGui::GetIO();
    float windowWidth = io.DisplaySize.x;
    float windowHeight = io.DisplaySize.y;
    float chatWidth = 500.0f;
    float chatHeight = 400.0f;
    float padding = 20.0f;
    
    // Position on right side
    ImGui::SetNextWindowPos(
        ImVec2(windowWidth - chatWidth - padding, (windowHeight - chatHeight) * 0.5f),
        ImGuiCond_Once
    );
    ImGui::SetNextWindowSize(ImVec2(chatWidth, chatHeight));
    
    std::string windowTitle = "Conversation - " + m_currentTarget->getName();
    
    if (ImGui::Begin(windowTitle.c_str(), nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        
        // Chat history area
        float inputAreaHeight = 60.0f;
        float historyHeight = ImGui::GetContentRegionAvail().y - inputAreaHeight;
        
        ImGui::BeginChild("ChatHistory", ImVec2(0, historyHeight), true);
        
        const auto& history = getConversationHistory();
        for (const auto& msg : history) {
            if (msg.isPlayer) {
                // Player messages - green
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                ImGui::TextWrapped("[You]: %s", msg.text.c_str());
                ImGui::PopStyleColor();
            } else {
                // NPC messages - cyan
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                ImGui::TextWrapped("[%s]: %s", msg.sender.c_str(), msg.text.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
        }
        
        // Thinking indicator
        if (isWaitingForResponse()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::TextWrapped("...");
            ImGui::PopStyleColor();
        }
        
        // Auto-scroll
        if (m_scrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            m_scrollToBottom = false;
        }
        
        ImGui::EndChild();
        
        ImGui::Separator();
        
        // Input area
        bool waiting = isWaitingForResponse();
        
        if (waiting) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1.0f), "Waiting for response...");
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Your message:");
        }
        
        // Auto-focus input
        if (m_focusInput && !waiting) {
            ImGui::SetKeyboardFocusHere();
            m_focusInput = false;
        }
        
        if (waiting) ImGui::BeginDisabled();
        
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
        bool enterPressed = ImGui::InputText("##chatinput", m_inputBuffer, sizeof(m_inputBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue);
        
        ImGui::SameLine();
        bool sendClicked = ImGui::Button("Send", ImVec2(60, 0));
        
        if (waiting) ImGui::EndDisabled();
        
        if ((enterPressed || sendClicked) && m_inputBuffer[0] != '\0' && !waiting) {
            sendPlayerMessage(m_inputBuffer);
            m_inputBuffer[0] = '\0';
            m_focusInput = true;
        }
        
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Press Escape to end conversation");
    }
    ImGui::End();
}

} // namespace ai
} // namespace eden
