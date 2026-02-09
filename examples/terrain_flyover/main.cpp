#include <eden/Window.hpp>
#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Terrain.hpp>

#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/TerrainPipeline.hpp"
#include "Renderer/Buffer.hpp"

#include <iostream>
#include <chrono>
#include <array>

using namespace eden;

class TerrainFlyover {
public:
    void run() {
        init();
        mainLoop();
        cleanup();
    }

private:
    void init() {
        m_window = std::make_unique<Window>(1280, 720, "EDEN - Terrain Flyover");

        m_context = std::make_unique<VulkanContext>();
        m_surface = m_window->createSurface(m_context->getInstance());
        m_context->initialize(m_surface);

        m_swapchain = std::make_unique<Swapchain>(
            *m_context, m_surface, m_window->getWidth(), m_window->getHeight());

        m_pipeline = std::make_unique<TerrainPipeline>(
            *m_context, m_swapchain->getRenderPass(), m_swapchain->getExtent());

        m_bufferManager = std::make_unique<BufferManager>(*m_context);

        createCommandBuffers();
        createSyncObjects();

        // Initialize input
        Input::init(m_window->getHandle());
        Input::setMouseCaptured(true);

        // Initialize camera - start above terrain
        float startHeight = m_terrain.getHeightAt(0, 0) + 50.0f;
        m_camera.setPosition({0, startHeight, 0});
        m_camera.setSpeed(80.0f);

        m_window->setResizeCallback([this](int, int) {
            m_framebufferResized = true;
        });

        std::cout << "Controls:\n";
        std::cout << "  WASD - Move\n";
        std::cout << "  Space/Ctrl - Up/Down\n";
        std::cout << "  Mouse - Look around\n";
        std::cout << "  Shift - Move faster\n";
        std::cout << "  Escape - Release mouse\n";
    }

    void createCommandBuffers() {
        m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_context->getCommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

        if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffers");
        }
    }

    void createSyncObjects() {
        m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create synchronization objects");
            }
        }
    }

    void mainLoop() {
        auto lastTime = std::chrono::high_resolution_clock::now();

        while (!m_window->shouldClose()) {
            m_window->pollEvents();

            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            update(deltaTime);
            render();

            Input::update();
        }

        m_context->waitIdle();
    }

    void update(float deltaTime) {
        // Handle escape to release mouse
        if (Input::isKeyPressed(Input::KEY_ESCAPE)) {
            Input::setMouseCaptured(!Input::isMouseCaptured());
        }

        // Camera movement
        float speedMult = Input::isKeyDown(Input::KEY_LEFT_SHIFT) ? 3.0f : 1.0f;
        float effectiveDt = deltaTime * speedMult;

        m_camera.processKeyboard(
            effectiveDt,
            Input::isKeyDown(Input::KEY_W),
            Input::isKeyDown(Input::KEY_S),
            Input::isKeyDown(Input::KEY_A),
            Input::isKeyDown(Input::KEY_D),
            Input::isKeyDown(Input::KEY_SPACE),
            Input::isKeyDown(Input::KEY_LEFT_CONTROL)
        );

        if (Input::isMouseCaptured()) {
            glm::vec2 mouseDelta = Input::getMouseDelta();
            m_camera.processMouse(mouseDelta.x, -mouseDelta.y);
        }

        // Update terrain chunks around camera
        m_terrain.update(m_camera.getPosition());

        // Upload new chunks
        for (auto& vc : m_terrain.getVisibleChunks()) {
            if (vc.chunk->needsUpload()) {
                uploadChunk(*vc.chunk);
            }
        }
    }

    void uploadChunk(TerrainChunk& chunk) {
        const auto& vertices = chunk.getVertices();
        const auto& indices = chunk.getIndices();

        uint32_t handle = m_bufferManager->createMeshBuffers(
            vertices.data(),
            static_cast<uint32_t>(vertices.size()),
            sizeof(Vertex3D),
            indices.data(),
            static_cast<uint32_t>(indices.size())
        );

        chunk.setBufferHandle(handle);
        chunk.markUploaded();
    }

    void render() {
        VkDevice device = m_context->getDevice();

        vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, m_swapchain->getHandle(), UINT64_MAX,
                                                m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }

        vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);

        vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
        recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

        VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit draw command buffer");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {m_swapchain->getHandle()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
            m_framebufferResized = false;
            recreateSwapchain();
        }

        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_swapchain->getRenderPass();
        renderPassInfo.framebuffer = m_swapchain->getFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_swapchain->getExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.5f, 0.7f, 1.0f, 1.0f}};  // Sky blue
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getHandle());

        // Calculate view-projection matrix
        float aspect = static_cast<float>(m_swapchain->getExtent().width) /
                       static_cast<float>(m_swapchain->getExtent().height);
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 2000.0f);
        proj[1][1] *= -1;  // Flip Y for Vulkan
        glm::mat4 vp = proj * view;

        // Render all visible terrain chunks
        for (const auto& vc : m_terrain.getVisibleChunks()) {
            auto* buffers = m_bufferManager->getMeshBuffers(vc.chunk->getBufferHandle());
            if (!buffers || !buffers->vertexBuffer) continue;

            VkBuffer vertexBuffers[] = {buffers->vertexBuffer->getHandle()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

            if (buffers->indexBuffer) {
                vkCmdBindIndexBuffer(commandBuffer, buffers->indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT32);
            }

            // Push MVP matrix (model is identity for terrain)
            glm::mat4 mvp = vp;
            vkCmdPushConstants(commandBuffer, m_pipeline->getLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

            if (buffers->indexBuffer) {
                vkCmdDrawIndexed(commandBuffer, buffers->indexCount, 1, 0, 0, 0);
            } else {
                vkCmdDraw(commandBuffer, buffers->vertexCount, 1, 0, 0);
            }
        }

        vkCmdEndRenderPass(commandBuffer);
        vkEndCommandBuffer(commandBuffer);
    }

    void recreateSwapchain() {
        int width = 0, height = 0;
        while (width == 0 || height == 0) {
            width = m_window->getWidth();
            height = m_window->getHeight();
            m_window->pollEvents();
        }

        m_context->waitIdle();
        m_swapchain->recreate(width, height);
        m_pipeline = std::make_unique<TerrainPipeline>(
            *m_context, m_swapchain->getRenderPass(), m_swapchain->getExtent());
    }

    void cleanup() {
        VkDevice device = m_context->getDevice();

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(device, m_imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, m_inFlightFences[i], nullptr);
        }

        m_bufferManager.reset();
        m_pipeline.reset();
        m_swapchain.reset();

        vkDestroySurfaceKHR(m_context->getInstance(), m_surface, nullptr);
    }

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanContext> m_context;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    std::unique_ptr<Swapchain> m_swapchain;
    std::unique_ptr<TerrainPipeline> m_pipeline;
    std::unique_ptr<BufferManager> m_bufferManager;

    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;

    Camera m_camera{{0, 100, 0}};
    Terrain m_terrain{{
        .chunkResolution = 128,
        .tileSize = 2.0f,
        .viewDistance = 3,
        .heightScale = 120.0f,     // Tall but smooth hills
        .noiseScale = 0.003f,      // Very broad, gentle features
        .noiseOctaves = 3,         // Few octaves = no fine detail
        .noisePersistence = 0.3f   // Low = smooth rolling hills
    }};
};

int main() {
    try {
        TerrainFlyover app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
