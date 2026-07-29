#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace eden {

class VulkanContext;
class BufferManager;

// What a module needs from the host to put geometry on the screen, and what it
// gets handed each frame to draw with.
//
// Kept as two small structs rather than a widening argument list, because the
// last thing a seam like this wants is for every module to break the day the
// renderer grows a parameter.
struct ModuleRenderSetup {
    VulkanContext& context;
    BufferManager& buffers;
    VkRenderPass   renderPass;
    VkExtent2D     extent;
};

struct ModuleRenderFrame {
    VkCommandBuffer cmd;
    glm::mat4       viewProj;
    glm::vec3       eye;
};

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

    // ---- world content ----------------------------------------------------
    // A module with things IN the world -- creatures, vehicles, a landed ship --
    // draws them here, in the host's pass with the host's camera. Optional,
    // because plenty of modules are only a UI and a backend.
    //
    // attachRenderer comes once, before any frame, and is where a module builds
    // whatever pipelines and buffers it needs. It is separate from initialize()
    // because a module can be loaded before there is a swapchain to build
    // against, and because the render pass changes when the window resizes.
    virtual void attachRenderer(const ModuleRenderSetup&) {}
    virtual void detachRenderer() {}
    virtual void renderWorld(const ModuleRenderFrame&) {}

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
    using Maker = std::function<std::unique_ptr<GameModule>()>;

    // Modules REGISTER themselves rather than being listed here.
    //
    // The alternative -- a hardcoded switch in this file -- would have the engine
    // naming its games, and then depending on them to build. That is backwards:
    // a game knows about the engine, never the other way round. A host links
    // whichever modules it ships and registers them at startup, so the same
    // engine binary serves a level that wants one and a level that wants none.
    static void registerModule(const std::string& name, Maker maker);

    // Get available module types
    static std::vector<std::string> getAvailableModules();

    // Create a module by name
    static std::unique_ptr<GameModule> create(const std::string& moduleName);
};

} // namespace eden
