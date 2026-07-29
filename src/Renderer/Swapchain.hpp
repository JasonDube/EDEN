#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace eden {

class VulkanContext;

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class Swapchain {
public:
    Swapchain(VulkanContext& context, VkSurfaceKHR surface, int width, int height);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void recreate(int width, int height);

    VkSwapchainKHR getHandle() const { return m_swapchain; }
    VkFormat getImageFormat() const { return m_imageFormat; }
    VkExtent2D getExtent() const { return m_extent; }
    const std::vector<VkImageView>& getImageViews() const { return m_imageViews; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return m_framebuffers; }
    uint32_t getImageCount() const { return static_cast<uint32_t>(m_images.size()); }

    // Which present mode was actually chosen, in words. Worth showing: a frame
    // time pinned to the refresh rate and a frame time that is genuinely too slow
    // look identical, and only one of them is worth optimising.
    VkPresentModeKHR getPresentMode() const { return m_presentMode; }
    const char* getPresentModeName() const;

    // Whether to wait for the display. Static because a swapchain is destroyed and
    // rebuilt to change it, so the preference has to outlive the object -- and
    // because there is only ever one of these.
    //
    // Read from EDEN_VSYNC the first time it is asked, so the environment still
    // sets the initial state and the UI can override it afterwards.
    static void preferVsync(bool on);
    static bool vsyncPreferred();
    VkFormat getDepthFormat() const { return m_depthFormat; }
    const std::vector<VkImage>& getImages() const { return m_images; }

private:
    void create(int width, int height);
    void cleanup();
    void createImageViews();
    void createDepthResources();
    void createRenderPass();
    void createFramebuffers();
    VkFormat findDepthFormat();

    SwapchainSupportDetails querySwapchainSupport();
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, int width, int height);

    VulkanContext& m_context;
    VkSurfaceKHR m_surface;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_imageFormat;
    VkExtent2D m_extent;

    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkFormat m_depthFormat;
};

} // namespace eden
