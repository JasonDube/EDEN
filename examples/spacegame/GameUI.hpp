#pragma once

#include "GameConfig.hpp"
#include <memory>
#include <string>
#include <vector>

namespace spacegame {

/**
 * GameUI - Game-specific user interface
 *
 * This is where you add your game's custom UI components:
 * - Chat interface for AI companions
 * - Ship/vehicle status displays
 * - Inventory systems
 * - Quest logs
 * - etc.
 *
 * The UI is separate from the rendering and can be customized
 * without modifying the core game loop.
 */
class GameUI {
public:
    GameUI() = default;
    ~GameUI() = default;

    // Initialize UI components
    void initialize();

    // Update UI state (called each frame)
    void update(float deltaTime);

    // Render all UI components
    void render(float screenWidth, float screenHeight);

    // Returns true if UI wants keyboard input (e.g., typing in chat)
    bool wantsCaptureKeyboard() const { return m_wantsCaptureKeyboard; }

    // Returns true if UI wants mouse input
    bool wantsCaptureMouse() const { return m_wantsCaptureMouse; }

    // Toggle specific UI panels
    void toggleChat() { m_showChat = !m_showChat; }
    void toggleShipStatus() { m_showShipStatus = !m_showShipStatus; }
    void toggleInventory() { m_showInventory = !m_showInventory; }

private:
    void renderChat(float screenWidth, float screenHeight);
    void renderShipStatus(float screenWidth, float screenHeight);
    void renderInventory(float screenWidth, float screenHeight);

    // UI state
    bool m_wantsCaptureKeyboard = false;
    bool m_wantsCaptureMouse = false;

    // Panel visibility
    bool m_showChat = false;
    bool m_showShipStatus = false;
    bool m_showInventory = false;

    // Chat state
    std::string m_chatInput;
    std::vector<std::string> m_chatHistory;

    // TODO: Add your game-specific state here
    // - AI companion connection
    // - Ship systems
    // - Player inventory
    // - Quest state
    // etc.
};

} // namespace spacegame
