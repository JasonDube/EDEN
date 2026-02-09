#pragma once

#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace eden {

/**
 * GameModule - Base class for loadable game modules
 *
 * A game module provides game-specific UI and logic that activates
 * during play mode. Modules can be loaded/unloaded like models.
 *
 * Each module contains:
 * - Interface: UI panels rendered during play mode
 * - Backend: AI connections, game logic, etc.
 */
class GameModule {
public:
    virtual ~GameModule() = default;

    // Module identity
    virtual const char* getName() const = 0;
    virtual const char* getDescription() const = 0;

    // Lifecycle
    virtual bool initialize() = 0;      // Called when module is loaded
    virtual void shutdown() = 0;        // Called when module is unloaded

    // Called when entering/exiting play mode
    virtual void onEnterPlayMode() {}
    virtual void onExitPlayMode() {}

    // Per-frame update (only called during play mode)
    virtual void update(float deltaTime) = 0;

    // Render UI (only called during play mode)
    // screenWidth/screenHeight are the viewport dimensions
    virtual void renderUI(float screenWidth, float screenHeight) = 0;

    // Input handling - return true if module consumed the input
    virtual bool wantsCaptureKeyboard() const { return false; }
    virtual bool wantsCaptureMouse() const { return false; }

    // Optional: Module can receive player position for proximity-based features
    virtual void setPlayerPosition(const glm::vec3& pos) { m_playerPosition = pos; }

    // Check if module is ready/connected
    virtual bool isReady() const { return true; }
    virtual std::string getStatusMessage() const { return "Ready"; }

protected:
    glm::vec3 m_playerPosition{0.0f};
};

/**
 * GameModuleFactory - Creates game module instances
 *
 * Register module types here. In the future this could load from
 * shared libraries or scripts.
 */
class GameModuleFactory {
public:
    // Get available module types
    static std::vector<std::string> getAvailableModules();

    // Create a module by name
    static std::unique_ptr<GameModule> create(const std::string& moduleName);
};

} // namespace eden
