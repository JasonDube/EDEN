#include "VulkanApplicationBase.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <cstdlib>

namespace eden {

VulkanApplicationBase::VulkanApplicationBase(int width, int height, const char* title)
    : m_initialWidth(width)
    , m_initialHeight(height)
    , m_title(title)
{
}

VulkanApplicationBase::~VulkanApplicationBase() = default;

void VulkanApplicationBase::run() {
    init();
    try {
        mainLoop();
    } catch (...) {
        // Ensure cleanup always runs even if mainLoop throws (e.g. Vulkan/OOM errors)
        cleanup();
        throw;
    }
    cleanup();
}

void VulkanApplicationBase::init() {
    const bool kms = std::getenv("EDEN_KMS") != nullptr;

    if (kms) {
        // ── DRM/KMS backend: EDEN owns the display, no GLFW/X11/Wayland ──────
        m_kms = std::make_unique<KmsPlatform>();
        if (!m_kms->openDrm())
            throw std::runtime_error("KMS: failed to open the DRM display");

        // Instance must carry the display extensions (set before the context).
        VulkanContext::useKmsInstanceExtensions(true);
        m_context = std::make_unique<VulkanContext>();

        // Acquire the display -> presentable surface, then finish the device.
        m_surface = m_kms->createSurface(m_context->getInstance());
        if (m_surface == VK_NULL_HANDLE)
            throw std::runtime_error("KMS: failed to create the display surface");
        m_context->initialize(m_surface);

        m_swapchain = std::make_unique<Swapchain>(
            *m_context, m_surface, m_kms->width(), m_kms->height());

        m_bufferManager = std::make_unique<BufferManager>(*m_context);
        createCommandBuffers();
        createSyncObjects();

        if (!m_kms->initInput())
            std::cerr << "[KMS] input init failed — running without keyboard/mouse\n";

        onInit();
        return;
    }

    // ── GLFW backend (default; every existing example) ──────────────────────
    m_window = std::make_unique<Window>(m_initialWidth, m_initialHeight, m_title);

    m_context = std::make_unique<VulkanContext>();
    m_surface = m_window->createSurface(m_context->getInstance());
    m_context->initialize(m_surface);

    m_swapchain = std::make_unique<Swapchain>(
        *m_context, m_surface, m_window->getWidth(), m_window->getHeight());

    m_bufferManager = std::make_unique<BufferManager>(*m_context);
    createCommandBuffers();
    createSyncObjects();

    Input::init(m_window->getHandle());

    m_window->setResizeCallback([this](int, int) {
        m_framebufferResized = true;
    });

    onInit();
}

void VulkanApplicationBase::mainLoop() {
    // Allow derived classes to do pre-loop work (e.g., terrain preloading)
    onBeforeMainLoop();

    auto lastTime = std::chrono::high_resolution_clock::now();

    auto shouldClose = [this] { return m_kms ? m_kms->shouldClose() : m_window->shouldClose(); };
    auto pollEvents  = [this] { if (m_kms) m_kms->pollEvents(); else m_window->pollEvents(); };

    // Smoothed rather than instantaneous: a number that changes sixty times a
    // second is a number nobody can read off the screen.
    auto mark = [](std::chrono::high_resolution_clock::time_point& since, float& into) {
        const auto now = std::chrono::high_resolution_clock::now();
        const float took = std::chrono::duration<float, std::milli>(now - since).count();
        into += (took - into) * 0.1f;
        since = now;
    };

    while (!shouldClose()) {
        auto phase = std::chrono::high_resolution_clock::now();
        const auto frameBegan = phase;

        pollEvents();
        mark(phase, m_frameCost.poll);

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        update(deltaTime);
        mark(phase, m_frameCost.update);

        uint32_t imageIndex;
        const bool got = beginFrame(imageIndex);
        mark(phase, m_frameCost.acquire);

        if (got) {
            recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);
            mark(phase, m_frameCost.record);

            endFrame(imageIndex);
            mark(phase, m_frameCost.present);
        }

        if (!m_kms) Input::update();  // Input is the GLFW path; KMS apps use callbacks

        const float whole = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - frameBegan).count();
        m_frameCost.total += (whole - m_frameCost.total) * 0.1f;
    }

    m_context->waitIdle();
}

void VulkanApplicationBase::cleanup() {
    if (m_cleanedUp) return;
    m_cleanedUp = true;

    m_context->waitIdle();

    // Call derived class cleanup first
    onCleanup();

    // Destroy sync objects
    destroySyncObjects();

    // Destroy Vulkan resources
    m_bufferManager.reset();
    m_swapchain.reset();

    vkDestroySurfaceKHR(m_context->getInstance(), m_surface, nullptr);
    m_context.reset();
    m_window.reset();
    m_kms.reset();   // drops DRM master + closes libinput (no-op in GLFW mode)
}

void VulkanApplicationBase::createCommandBuffers() {
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

void VulkanApplicationBase::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects");
        }
    }
}

void VulkanApplicationBase::destroySyncObjects() {
    VkDevice device = m_context->getDevice();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, m_inFlightFences[i], nullptr);
    }
}

bool VulkanApplicationBase::beginFrame(uint32_t& imageIndex) {
    VkDevice device = m_context->getDevice();

    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        device, m_swapchain->getHandle(), UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return false;
    }

    vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    return true;
}

void VulkanApplicationBase::endFrame(uint32_t imageIndex) {
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

    VkResult submitResult = vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitResult != VK_SUCCESS) {
        std::cerr << "[VULKAN] vkQueueSubmit failed with error code: " << submitResult << std::endl;
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {m_swapchain->getHandle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapchain();
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanApplicationBase::recreateSwapchain() {
    int width = 0, height = 0;
    if (m_kms) {
        // The display mode is fixed; nothing to wait on.
        width = m_kms->width(); height = m_kms->height();
    } else {
        glfwGetFramebufferSize(m_window->getHandle(), &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(m_window->getHandle(), &width, &height);
            glfwWaitEvents();
        }
    }

    m_context->waitIdle();
    m_swapchain->recreate(width, height);

    // Notify derived class
    onSwapchainRecreated();
}

} // namespace eden
