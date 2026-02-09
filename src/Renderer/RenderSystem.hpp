#pragma once

#include "VulkanContext.hpp"
#include "Swapchain.hpp"
#include "Pipeline.hpp"
#include "Buffer.hpp"
#include <eden/Scene.hpp>
#include <memory>

namespace eden {

class Window;

class RenderSystem {
public:
    RenderSystem(Window& window);
    ~RenderSystem();

    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;

    void render(Scene& scene, float deltaTime);
    void uploadMesh(Mesh& mesh);

    VulkanContext& getContext() { return *m_context; }
    BufferManager& getBufferManager() { return *m_bufferManager; }

private:
    void createSyncObjects();
    void createCommandBuffers();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, Scene& scene);

    Window& m_window;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    std::unique_ptr<VulkanContext> m_context;
    std::unique_ptr<Swapchain> m_swapchain;
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<BufferManager> m_bufferManager;

    std::vector<VkCommandBuffer> m_commandBuffers;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;
};

} // namespace eden
