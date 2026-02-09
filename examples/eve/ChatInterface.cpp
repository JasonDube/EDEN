#include "ChatInterface.hpp"
#include <imgui.h>
#include <cstring>

namespace eve {

ChatInterface::ChatInterface() = default;

void ChatInterface::render(float width, float height) {
    if (!m_eve) return;
    
    ImGui::SetNextWindowPos(ImVec2(width - 420, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, height - 20), ImGuiCond_FirstUseEver);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    
    if (ImGui::Begin("Communication Terminal", nullptr, flags)) {
        m_hasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        
        float windowHeight = ImGui::GetContentRegionAvail().y;
        float inputHeight = 60.0f;
        float historyHeight = windowHeight - inputHeight - 10.0f;
        
        // Connection status
        if (m_eve->isConnected()) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "● Connected");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "● Disconnected");
        }
        
        ImGui::SameLine();
        if (m_eve->isThinking()) {
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), " [Eve is thinking...]");
        }
        
        ImGui::Separator();
        
        // Message history
        renderMessageHistory(ImGui::GetContentRegionAvail().x, historyHeight);
        
        ImGui::Separator();
        
        // Input area
        renderInputArea(ImGui::GetContentRegionAvail().x);
    }
    ImGui::End();
}

void ChatInterface::renderMessageHistory(float width, float height) {
    ImGui::BeginChild("MessageHistory", ImVec2(width, height), true);
    
    const auto& history = m_eve->getHistory();
    
    for (size_t i = 0; i < history.size(); ++i) {
        const auto& msg = history[i];
        
        ImGui::PushID(static_cast<int>(i));
        
        // Different styling for Captain vs Eve
        if (msg.role == Message::Role::Captain) {
            // Captain's messages - right aligned, blue tint
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
            
            float textWidth = ImGui::CalcTextSize(msg.content.c_str(), nullptr, false, width - 80).x;
            float offset = width - textWidth - 20;
            if (offset > 60) {
                ImGui::SetCursorPosX(offset);
            }
            
            ImGui::TextWrapped("[Captain] %s", msg.content.c_str());
            ImGui::PopStyleColor();
            
        } else if (msg.role == Message::Role::Eve) {
            // Eve's messages - left aligned, warm color
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.8f, 1.0f));
            ImGui::TextWrapped("[%s] %s", m_eve->getName().c_str(), msg.content.c_str());
            ImGui::PopStyleColor();
            
        } else {
            // System messages - gray, centered
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("* %s *", msg.content.c_str());
            ImGui::PopStyleColor();
        }
        
        ImGui::Spacing();
        ImGui::PopID();
    }
    
    // Auto-scroll to bottom
    if (m_scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }
    
    ImGui::EndChild();
}

void ChatInterface::renderInputArea(float width) {
    ImGui::BeginChild("InputArea", ImVec2(width, 0), false);
    
    // Focus input if requested
    if (m_focusInput) {
        ImGui::SetKeyboardFocusHere();
        m_focusInput = false;
    }
    
    bool enterPressed = false;
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    
    ImGui::PushItemWidth(width - 70);
    if (ImGui::InputText("##ChatInput", m_inputBuffer, sizeof(m_inputBuffer), inputFlags)) {
        enterPressed = true;
    }
    ImGui::PopItemWidth();
    
    ImGui::SameLine();
    
    bool canSend = strlen(m_inputBuffer) > 0 && !m_eve->isThinking();
    
    ImGui::BeginDisabled(!canSend);
    if (ImGui::Button("Send", ImVec2(60, 0)) || (enterPressed && canSend)) {
        sendMessage();
    }
    ImGui::EndDisabled();
    
    ImGui::EndChild();
}

void ChatInterface::sendMessage() {
    if (strlen(m_inputBuffer) == 0 || !m_eve || m_eve->isThinking()) {
        return;
    }
    
    std::string message(m_inputBuffer);
    m_inputBuffer[0] = '\0';
    
    m_eve->chat(message, [this](const std::string& response) {
        m_scrollToBottom = true;
        // Response is already stored in Eve's history
    });
    
    m_scrollToBottom = true;
    m_focusInput = true;
}

} // namespace eve
