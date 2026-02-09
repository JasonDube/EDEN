#include <eden/Core.hpp>
#include <eden/Window.hpp>
#include "Renderer/RenderSystem.hpp"
#include <chrono>

namespace eden {

Core::Core() = default;

Core::~Core() {
    shutdown();
}

void Core::init(const EngineConfig& config) {
    if (m_initialized) return;

    m_window = std::make_unique<Window>(config.width, config.height, config.title);
    m_renderSystem = std::make_unique<RenderSystem>(*m_window);
    m_initialized = true;
}

void Core::shutdown() {
    if (!m_initialized) return;

    m_scene.clear();
    m_renderSystem.reset();
    m_window.reset();
    m_initialized = false;
}

MeshPtr Core::createMesh(const MeshDescriptor& desc) {
    auto mesh = std::make_shared<Mesh>(desc);
    m_renderSystem->uploadMesh(*mesh);
    m_scene.add(mesh);
    return mesh;
}

void Core::destroyMesh(MeshPtr mesh) {
    if (!mesh) return;

    m_scene.remove(mesh);
    // Buffer cleanup happens when mesh is destroyed
}

void Core::run(UpdateCallback update) {
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!m_window->shouldClose()) {
        m_window->pollEvents();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        if (update) {
            update(deltaTime);
        }

        m_renderSystem->render(m_scene, deltaTime);
    }

    m_renderSystem->getContext().waitIdle();
}

bool Core::update(float deltaTime) {
    if (!m_initialized || m_window->shouldClose()) {
        return false;
    }

    m_window->pollEvents();
    return true;
}

void Core::render() {
    if (m_initialized) {
        m_renderSystem->render(m_scene, 0.0f);
    }
}

bool Core::isRunning() const {
    return m_initialized && !m_window->shouldClose();
}

} // namespace eden
