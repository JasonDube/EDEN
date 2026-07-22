#pragma once

#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
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

    // Eyedropper: when the user picks a color off a reference image (in eyedropper
    // mode), this is invoked with the sampled RGB in 0..1. Wire it to the terrain
    // paint color. No callback set = the eyedropper still previews but does nothing
    // on click.
    void setColorPickCallback(std::function<void(const glm::vec3&)> cb) { m_onPick = std::move(cb); }

private:
    struct RefImage {
        std::string name;
        std::string filepath;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;   // RGBA CPU copy, for eyedropper sampling

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
    bool m_eyedropper = false;                       // sampling mode toggle
    std::function<void(const glm::vec3&)> m_onPick;  // -> set terrain paint color
};

} // namespace eden
