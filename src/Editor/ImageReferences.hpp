#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace eden {

class VulkanContext;

class ImageReferences {
public:
    ImageReferences() = default;
    ~ImageReferences();

    void init(VulkanContext& context);
    void cleanup();

    // Render the ImGui window. Pass pointer to bool for close button.
    void render(bool* open);

    // Load an image from file (called from file dialog or drag-drop)
    bool loadImage(const std::string& path);

private:
    struct RefImage {
        std::string name;
        std::string filepath;
        int width = 0;
        int height = 0;

        // Per-image pan/zoom
        float zoom = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;
        bool panning = false;

        // Vulkan resources
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };

    RefImage uploadImage(const unsigned char* rgba, int width, int height, const std::string& name, const std::string& path);
    void destroyImage(RefImage& img);

    VulkanContext* m_context = nullptr;
    std::vector<RefImage> m_images;
    int m_selectedTab = 0;
};

} // namespace eden
