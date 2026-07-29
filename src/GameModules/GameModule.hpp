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

    // Where the game starts, if the module has an opinion.
    //
    // Asked once, on entering play mode, AFTER onEnterPlayMode -- so a module
    // that decides where to put things gets to decide before it is asked where
    // the player should stand to see them.
    //
    // It exists because a level can be two and a half miles across and a module
    // can put a ship anywhere in it. Dropping the player wherever the editor
    // camera happened to be and letting them go looking is not a game, it is a
    // search. False means no opinion, and the host does whatever it did before.
    virtual bool playerStart(glm::vec3& outPosition, float& outYawDegrees) const {
        (void)outPosition; (void)outYawDegrees;
        return false;
    }

    // ---- things the module puts underfoot ---------------------------------
    // A module that builds its own geometry -- a landed ship, a bridge, a lift --
    // has floors and walls the host knows nothing about. The host already stands
    // the player on terrain and on placed objects; these let a module join in
    // without becoming one.
    //
    // groundHeight: what is under this point, given feet currently at `fromY`.
    // The `fromY` matters: it is what stops a floor two units up being something
    // you step onto from beside it, and it is the module's own rule to apply.
    // False means "nothing of mine here", not "nothing here".
    virtual bool groundHeight(float x, float z, float fromY, float& outHeight) const {
        (void)x; (void)z; (void)fromY; (void)outHeight;
        return false;
    }

    // carriedPlayer: the module moved the ground the player was standing on.
    //
    // A lift, a moving walkway, a ship in flight. The host owns where the player
    // is and the module owns where its floor went, so the module reports the
    // transform it applied and the host applies the same one to the player. False
    // means nothing moved, or the player was not on it.
    //
    // Reported rather than applied because a module setting the player's position
    // outright would fight everything else that does -- gravity, the character
    // controller, the scripted controller -- whereas a displacement composes with
    // all of them.
    virtual bool carriedPlayer(glm::vec3& outMove, float& outTurnDegrees,
                               glm::vec3& outAbout) const {
        (void)outMove; (void)outTurnDegrees; (void)outAbout;
        return false;
    }

    // resolvePosition: push a body out of anything solid it has ended up inside.
    // Returns true if it moved. Walls, in other words -- without it a player can
    // stand on a module's roof and walk through its walls, which is worse than
    // neither.
    virtual bool resolvePosition(float& x, float& z, float footY, float height,
                                 float radius) const {
        (void)x; (void)z; (void)footY; (void)height; (void)radius;
        return false;
    }

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
