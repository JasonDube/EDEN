#include "GameUI.hpp"
#include <imgui.h>
#include <iostream>

namespace spacegame {

void GameUI::initialize() {
    std::cout << "GameUI initialized" << std::endl;

    // TODO: Initialize your game-specific components here
    // - Connect to AI backend
    // - Load player save data
    // - Initialize ship systems
    // etc.
}

void GameUI::update(float deltaTime) {
    // Reset input capture flags
    m_wantsCaptureKeyboard = false;
    m_wantsCaptureMouse = false;

    // Check if any text input is focused
    if (ImGui::GetIO().WantTextInput) {
        m_wantsCaptureKeyboard = true;
    }

    // TODO: Update game-specific logic
    // - Poll AI backend for responses
    // - Update ship systems
    // - Process quest events
    // etc.
}

void GameUI::render(float screenWidth, float screenHeight) {
    // Render enabled panels
    if (m_showChat) {
        renderChat(screenWidth, screenHeight);
    }

    if (m_showShipStatus) {
        renderShipStatus(screenWidth, screenHeight);
    }

    if (m_showInventory) {
        renderInventory(screenWidth, screenHeight);
    }
}

void GameUI::renderChat(float screenWidth, float screenHeight) {
    // Position chat in bottom-left
    ImGui::SetNextWindowPos(ImVec2(10, screenHeight - 310), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Chat", &m_showChat)) {
        // Chat history area
        ImGui::BeginChild("ChatHistory", ImVec2(0, -30), true);
        for (const auto& msg : m_chatHistory) {
            ImGui::TextWrapped("%s", msg.c_str());
        }
        // Auto-scroll to bottom
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        // Input field
        static char inputBuffer[256] = "";
        ImGui::SetNextItemWidth(-60);
        bool enterPressed = ImGui::InputText("##chatinput", inputBuffer, sizeof(inputBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();
        if (ImGui::Button("Send") || enterPressed) {
            if (strlen(inputBuffer) > 0) {
                m_chatHistory.push_back(std::string("You: ") + inputBuffer);

                // TODO: Send to AI backend and get response
                m_chatHistory.push_back("AI: [Response would appear here]");

                inputBuffer[0] = '\0';
            }
        }

        if (ImGui::IsItemActive()) {
            m_wantsCaptureKeyboard = true;
        }
    }
    ImGui::End();
}

void GameUI::renderShipStatus(float screenWidth, float screenHeight) {
    // Position in top-right
    ImGui::SetNextWindowPos(ImVec2(screenWidth - 260, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(250, 200), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Ship Status", &m_showShipStatus)) {
        // TODO: Replace with actual ship data
        float hull = 0.85f;
        float shields = 0.60f;
        float fuel = 0.45f;
        float power = 0.90f;

        ImGui::Text("Hull Integrity");
        ImGui::ProgressBar(hull, ImVec2(-1, 0), "");

        ImGui::Text("Shields");
        ImGui::ProgressBar(shields, ImVec2(-1, 0), "");

        ImGui::Text("Fuel");
        ImGui::ProgressBar(fuel, ImVec2(-1, 0), "");

        ImGui::Text("Power");
        ImGui::ProgressBar(power, ImVec2(-1, 0), "");

        ImGui::Separator();
        ImGui::Text("Location: Deep Space");
        ImGui::Text("Speed: 0 km/s");
    }
    ImGui::End();
}

void GameUI::renderInventory(float screenWidth, float screenHeight) {
    // Center on screen
    ImGui::SetNextWindowPos(ImVec2(screenWidth / 2 - 200, screenHeight / 2 - 150),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Inventory", &m_showInventory)) {
        ImGui::Text("Cargo Hold");
        ImGui::Separator();

        // TODO: Replace with actual inventory
        ImGui::BulletText("Empty");

        ImGui::Separator();
        ImGui::Text("Credits: 1000");
    }
    ImGui::End();
}

} // namespace spacegame
