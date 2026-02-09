#include "RenderSystem.hpp"
#include <eden/Window.hpp>
#include <glm/glm.hpp>
#include <stdexcept>
#include <array>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace eden {

RenderSystem::RenderSystem(Window& window) : m_window(window) {
    m_context = std::make_unique<VulkanContext>();
    m_surface = window.createSurface(m_context->getInstance());
    m_context->initialize(m_surface);

    m_swapchain = std::make_unique<Swapchain>(
        *m_context, m_surface, window.getWidth(), window.getHeight());

    m_pipeline = std::make_unique<Pipeline>(
        *m_context, m_swapchain->getRenderPass(), m_swapchain->getExtent());

    m_bufferManager = std::make_unique<BufferManager>(*m_context);

    createCommandBuffers();
    createSyncObjects();

    window.setResizeCallback([this](int, int) {
        m_framebufferResized = true;
    });
}

RenderSystem::~RenderSystem() {
    m_context->waitIdle();

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

void RenderSystem::createCommandBuffers() {
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

void RenderSystem::createSyncObjects() {
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

void RenderSystem::uploadMesh(Mesh& mesh) {
    if (!mesh.needsUpload()) return;

    const auto& vertices = mesh.getVertices();
    const auto& indices = mesh.getIndices();

    uint32_t handle = m_bufferManager->createMeshBuffers(
        vertices.data(), mesh.getVertexCount(), sizeof(Vertex),
        indices.empty() ? nullptr : indices.data(), mesh.getIndexCount());

    mesh.setBufferHandle(handle);
    mesh.markUploaded();
}

void RenderSystem::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, Scene& scene) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_swapchain->getRenderPass();
    renderPassInfo.framebuffer = m_swapchain->getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getHandle());

    // Render all meshes in the scene
    for (const auto& mesh : scene.getMeshes()) {
        if (!mesh) continue;

        auto* buffers = m_bufferManager->getMeshBuffers(mesh->getBufferHandle());
        if (!buffers || !buffers->vertexBuffer) continue;

        VkBuffer vertexBuffers[] = {buffers->vertexBuffer->getHandle()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Push model matrix
        glm::mat4 transform = mesh->getModelMatrix();
        vkCmdPushConstants(commandBuffer, m_pipeline->getLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &transform);

        if (buffers->indexBuffer) {
            vkCmdBindIndexBuffer(commandBuffer, buffers->indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, buffers->indexCount, 1, 0, 0, 0);
        } else {
            vkCmdDraw(commandBuffer, buffers->vertexCount, 1, 0, 0);
        }
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer");
    }
}

void RenderSystem::render(Scene& scene, float deltaTime) {
    VkDevice device = m_context->getDevice();

    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, m_swapchain->getHandle(), UINT64_MAX,
                                            m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_swapchain->recreate(m_window.getWidth(), m_window.getHeight());
        m_pipeline = std::make_unique<Pipeline>(*m_context, m_swapchain->getRenderPass(), m_swapchain->getExtent());
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image");
    }

    vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);

    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, scene);

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
        m_swapchain->recreate(m_window.getWidth(), m_window.getHeight());
        m_pipeline = std::make_unique<Pipeline>(*m_context, m_swapchain->getRenderPass(), m_swapchain->getExtent());
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace eden
