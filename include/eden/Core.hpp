#pragma once

#include "Mesh.hpp"
#include "Scene.hpp"
#include <memory>
#include <functional>
#include <string>

namespace eden {

class Window;
class RenderSystem;

struct EngineConfig {
    std::string title = "EDEN Application";
    int width = 800;
    int height = 600;
};

class Core {
public:
    Core();
    ~Core();

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;

    void init(const EngineConfig& config = {});
    void shutdown();

    // Create a mesh and automatically add it to the scene
    MeshPtr createMesh(const MeshDescriptor& desc);

    // Remove a mesh from the scene
    void destroyMesh(MeshPtr mesh);

    // Access the scene for manual management
    Scene& getScene() { return m_scene; }

    // Run the main loop with an update callback
    // Callback receives delta time in seconds
    using UpdateCallback = std::function<void(float)>;
    void run(UpdateCallback update = nullptr);

    // Single frame update (for custom loops)
    bool update(float deltaTime);
    void render();

    // Check if engine is running
    bool isRunning() const;

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<RenderSystem> m_renderSystem;
    Scene m_scene;
    bool m_initialized = false;
};

} // namespace eden
