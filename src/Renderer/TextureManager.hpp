#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace eden {

class VulkanContext;

class TextureManager {
public:
    static constexpr int MAX_TERRAIN_TEXTURES = 32;  // Maximum textures in array

    TextureManager(VulkanContext& context);
    ~TextureManager();

    // Load all DDS textures from a planet folder (e.g., "textures/earth/")
    // All textures must be 1024x1024 DDS files
    void loadTerrainTexturesFromFolder(const std::string& folderPath);

    // Create default textures if none loaded
    void createDefaultTextures();

    // Get number of loaded textures
    int getTextureCount() const { return m_textureCount; }

    // Get texture names (derived from filenames)
    const std::vector<std::string>& getTextureNames() const { return m_textureNames; }

    // Get average color of each texture (for UI thumbnails)
    const std::vector<glm::vec3>& getTextureColors() const { return m_textureColors; }

    // Get the source file path for a slot (empty if procedural/default)
    const std::string& getSlotSourcePath(int slot) const {
        static const std::string empty;
        return (slot >= 0 && slot < static_cast<int>(m_slotSourcePaths.size())) ? m_slotSourcePaths[slot] : empty;
    }

    // Replace a single layer in the texture array with new RGBA pixel data.
    // The data will be resized to match the array dimensions.
    // Returns true on success.
    bool replaceLayer(int slot, const unsigned char* rgba, int width, int height,
                      const std::string& name = "", const std::string& sourcePath = "");

    // Get array dimensions
    int getArrayWidth() const { return m_arrayWidth; }
    int getArrayHeight() const { return m_arrayHeight; }

    // Auto-discover normal map companion file for a given albedo path
    std::string findNormalMapPath(const std::string& albedoPath);
    // Replace a single layer in the normal map array
    bool replaceNormalLayer(int slot, const unsigned char* rgba, int width, int height);

    VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }
    VkDescriptorSet getDescriptorSet() const { return m_descriptorSet; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void allocateDescriptorSet();
    void createSampler();
    void createTextureArray(const std::vector<std::vector<unsigned char>>& textureData, int width, int height, int layerCount);
    void createNormalArray(const std::vector<std::vector<unsigned char>>& normalData, int width, int height, int layerCount);
    void createDefaultTextureArray();
    void updateDescriptorSet();


    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount);
    void copyBufferToImageArray(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);

    // DDS loading helper
    bool loadDDSFile(const std::string& path, std::vector<unsigned char>& outData, int& outWidth, int& outHeight);

    VulkanContext& m_context;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    // Albedo texture array (all terrain textures)
    VkImage m_textureArray = VK_NULL_HANDLE;
    VkDeviceMemory m_textureMemory = VK_NULL_HANDLE;
    VkImageView m_textureArrayView = VK_NULL_HANDLE;

    // Normal map texture array (parallel to albedo — same slot count)
    VkImage m_normalArray = VK_NULL_HANDLE;
    VkDeviceMemory m_normalMemory = VK_NULL_HANDLE;
    VkImageView m_normalArrayView = VK_NULL_HANDLE;

    int m_textureCount = 0;
    int m_arrayWidth = 0;
    int m_arrayHeight = 0;
    std::vector<std::string> m_textureNames;
    std::vector<glm::vec3> m_textureColors;  // Average color per texture (for UI)
    std::vector<std::string> m_slotSourcePaths;  // Source file path per slot
    VkDeviceSize m_textureMemorySize = 0;  // For VRAM tracking
};

} // namespace eden
