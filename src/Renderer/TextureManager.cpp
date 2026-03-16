#include "TextureManager.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <array>
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <fstream>

namespace eden {

// Simple DDS header structures
#pragma pack(push, 1)
struct DDSPixelFormat {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DDSHeader {
    uint32_t magic;
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDSPixelFormat pixelFormat;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};
#pragma pack(pop)

TextureManager::TextureManager(VulkanContext& context)
    : m_context(context)
{
    createDescriptorSetLayout();
    createDescriptorPool();
    allocateDescriptorSet();
    createSampler();
    createDefaultTextures();
}

TextureManager::~TextureManager() {
    VkDevice device = m_context.getDevice();

    if (m_normalArrayView) vkDestroyImageView(device, m_normalArrayView, nullptr);
    if (m_normalArray) vkDestroyImage(device, m_normalArray, nullptr);
    if (m_normalMemory) {
        Buffer::trackVramFreeHandle(m_normalMemory);
        vkFreeMemory(device, m_normalMemory, nullptr);
    }

    if (m_textureArrayView) vkDestroyImageView(device, m_textureArrayView, nullptr);
    if (m_textureArray) vkDestroyImage(device, m_textureArray, nullptr);
    if (m_textureMemory) {
        Buffer::trackVramFreeHandle(m_textureMemory);
        vkFreeMemory(device, m_textureMemory, nullptr);
    }

    if (m_sampler) vkDestroySampler(device, m_sampler, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
}

void TextureManager::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    // Binding 0: albedo texture array
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // Binding 1: normal map texture array
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_context.getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void TextureManager::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 2;  // albedo + normal map arrays

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(m_context.getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

void TextureManager::allocateDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(m_context.getDevice(), &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }
}

void TextureManager::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_context.getDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler");
    }
}

void TextureManager::createDefaultTextures() {
    createDefaultTextureArray();
    updateDescriptorSet();
}

void TextureManager::createDefaultTextureArray() {
    // Create 16 slots — first 4 are default colors, rest are empty gray
    const int size = 512;
    const int layerCount = 16;

    std::vector<std::vector<unsigned char>> textures(layerCount);

    struct TexColor { uint8_t r, g, b; };
    TexColor colors[4] = {
        {100, 150, 80},   // Grass green
        {180, 160, 120},  // Sand/dirt
        {120, 110, 100},  // Rock gray
        {240, 240, 250}   // Snow white
    };

    for (int layer = 0; layer < layerCount; layer++) {
        textures[layer].resize(size * size * 4);
        auto& pixels = textures[layer];

        if (layer < 4) {
            auto& col = colors[layer];
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    int i = (y * size + x) * 4;
                    int noise = ((x * 17 + y * 31) % 20) - 10;
                    pixels[i + 0] = static_cast<uint8_t>(std::max(0, std::min(255, col.r + noise)));
                    pixels[i + 1] = static_cast<uint8_t>(std::max(0, std::min(255, col.g + noise)));
                    pixels[i + 2] = static_cast<uint8_t>(std::max(0, std::min(255, col.b + noise)));
                    pixels[i + 3] = 255;
                }
            }
        } else {
            // Empty gray slot
            for (int p = 0; p < size * size; p++) {
                pixels[p * 4 + 0] = 80;
                pixels[p * 4 + 1] = 80;
                pixels[p * 4 + 2] = 80;
                pixels[p * 4 + 3] = 255;
            }
        }
    }

    m_textureNames = {"Slot #1", "Slot #2", "Slot #3", "Slot #4",
                      "Slot #5", "Slot #6", "Slot #7", "Slot #8",
                      "Slot #9", "Slot #10", "Slot #11", "Slot #12",
                      "Slot #13", "Slot #14", "Slot #15", "Slot #16"};
    m_textureColors.clear();
    for (int i = 0; i < 4; i++) m_textureColors.push_back(glm::vec3(colors[i].r/255.0f, colors[i].g/255.0f, colors[i].b/255.0f));
    for (int i = 4; i < 16; i++) m_textureColors.push_back(glm::vec3(80/255.0f));
    m_slotSourcePaths.resize(16);

    createTextureArray(textures, size, size, layerCount);

    // Create default flat normal maps for all 16 slots
    // Flat normal = (128, 128, 255) in tangent space = pointing straight up
    std::vector<std::vector<unsigned char>> normals(layerCount);
    for (int i = 0; i < layerCount; i++) {
        normals[i].resize(size * size * 4);
        for (int p = 0; p < size * size; p++) {
            normals[i][p * 4 + 0] = 128;  // X = 0 (neutral)
            normals[i][p * 4 + 1] = 128;  // Y = 0 (neutral)
            normals[i][p * 4 + 2] = 255;  // Z = 1 (pointing up)
            normals[i][p * 4 + 3] = 255;
        }
    }
    createNormalArray(normals, size, size, layerCount);

    m_textureCount = layerCount;
}

bool TextureManager::loadDDSFile(const std::string& path, std::vector<unsigned char>& outData, int& outWidth, int& outHeight) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);

    if (fileSize < sizeof(DDSHeader)) {
        return false;
    }

    DDSHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    // Check magic number "DDS "
    if (header.magic != 0x20534444) {
        return false;
    }

    outWidth = static_cast<int>(header.width);
    outHeight = static_cast<int>(header.height);

    // Check for uncompressed RGBA format
    bool isUncompressed = (header.pixelFormat.flags & 0x40) != 0;  // DDPF_RGB
    bool hasAlpha = (header.pixelFormat.flags & 0x01) != 0;  // DDPF_ALPHAPIXELS

    if (isUncompressed && header.pixelFormat.rgbBitCount == 32) {
        // Uncompressed 32-bit RGBA
        size_t dataSize = outWidth * outHeight * 4;
        outData.resize(dataSize);
        file.read(reinterpret_cast<char*>(outData.data()), dataSize);
        return true;
    }

    // For compressed formats (BC1/BC3/BC7), fall back to stb_image
    file.close();

    // Try loading with stb_image (works for many DDS variants)
    int channels;
    unsigned char* pixels = stbi_load(path.c_str(), &outWidth, &outHeight, &channels, STBI_rgb_alpha);
    if (pixels) {
        outData.resize(outWidth * outHeight * 4);
        memcpy(outData.data(), pixels, outData.size());
        stbi_image_free(pixels);
        return true;
    }

    return false;
}

void TextureManager::loadTerrainTexturesFromFolder(const std::string& folderPath) {
    VkDevice device = m_context.getDevice();

    // Clean up existing texture array
    if (m_textureArrayView) {
        vkDestroyImageView(device, m_textureArrayView, nullptr);
        m_textureArrayView = VK_NULL_HANDLE;
    }
    if (m_textureArray) {
        vkDestroyImage(device, m_textureArray, nullptr);
        m_textureArray = VK_NULL_HANDLE;
    }
    if (m_textureMemory) {
        Buffer::trackVramFreeHandle(m_textureMemory);
        vkFreeMemory(device, m_textureMemory, nullptr);
        m_textureMemory = VK_NULL_HANDLE;
    }
    if (m_normalArrayView) { vkDestroyImageView(device, m_normalArrayView, nullptr); m_normalArrayView = VK_NULL_HANDLE; }
    if (m_normalArray) { vkDestroyImage(device, m_normalArray, nullptr); m_normalArray = VK_NULL_HANDLE; }
    if (m_normalMemory) { Buffer::trackVramFreeHandle(m_normalMemory); vkFreeMemory(device, m_normalMemory, nullptr); m_normalMemory = VK_NULL_HANDLE; }

    // Find all DDS and PNG files in folder
    std::vector<std::string> texturePaths;

    if (std::filesystem::exists(folderPath) && std::filesystem::is_directory(folderPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".dds" || ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                    texturePaths.push_back(entry.path().string());
                }
            }
        }
    }

    // Sort alphabetically for consistent ordering
    std::sort(texturePaths.begin(), texturePaths.end());

    if (texturePaths.empty()) {
        // No textures found, use defaults
        createDefaultTextureArray();
        updateDescriptorSet();
        return;
    }

    // Limit to max textures
    if (texturePaths.size() > MAX_TERRAIN_TEXTURES) {
        texturePaths.resize(MAX_TERRAIN_TEXTURES);
    }

    // Load all textures
    std::vector<std::vector<unsigned char>> textureData;
    int expectedWidth = 0, expectedHeight = 0;

    for (const auto& path : texturePaths) {
        std::vector<unsigned char> data;
        int width, height;

        bool loaded = false;

        // Try DDS first
        auto ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".dds") {
            loaded = loadDDSFile(path, data, width, height);
        }

        // Fall back to stb_image for PNG/JPG
        if (!loaded) {
            int channels;
            unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (pixels) {
                data.resize(width * height * 4);
                memcpy(data.data(), pixels, data.size());
                stbi_image_free(pixels);
                loaded = true;
            }
        }

        if (!loaded) {
            continue;
        }

        // Standard size for the texture array — 512x512 gives good detail for normal maps
        if (expectedWidth == 0) {
            expectedWidth = 512;
            expectedHeight = 512;
        }

        // Resize to standard size if needed
        if (width != expectedWidth || height != expectedHeight) {
            std::vector<unsigned char> resized(expectedWidth * expectedHeight * 4);
            stbir_resize_uint8_linear(
                data.data(), width, height, width * 4,
                resized.data(), expectedWidth, expectedHeight, expectedWidth * 4,
                STBIR_RGBA);
            data = std::move(resized);
        }

        textureData.push_back(std::move(data));

        // Use slot numbering with source filename hint
        std::string filename = std::filesystem::path(path).stem().string();
        std::string slotName = "Slot #" + std::to_string(textureData.size()) + " (" + filename + ")";
        m_textureNames.push_back(slotName);
        m_slotSourcePaths.push_back(path);
        printf("TextureManager: loaded texture %d: '%s' from '%s' (%dx%d -> %dx%d)\n",
               (int)textureData.size() - 1, slotName.c_str(), path.c_str(), width, height, expectedWidth, expectedHeight);
    }

    if (textureData.empty()) {
        createDefaultTextureArray();
        updateDescriptorSet();
        return;
    }

    // Compute average color for each texture (for UI thumbnails)
    m_textureColors.clear();
    for (const auto& data : textureData) {
        double r = 0, g = 0, b = 0;
        int pixelCount = static_cast<int>(data.size()) / 4;
        // Sample evenly (every 64th pixel for speed)
        int step = std::max(1, pixelCount / 1024);
        int sampled = 0;
        for (int p = 0; p < pixelCount; p += step) {
            r += data[p * 4 + 0];
            g += data[p * 4 + 1];
            b += data[p * 4 + 2];
            sampled++;
        }
        m_textureColors.push_back(glm::vec3(
            static_cast<float>(r / sampled) / 255.0f,
            static_cast<float>(g / sampled) / 255.0f,
            static_cast<float>(b / sampled) / 255.0f));
    }

    // Pad to 16 slots with blank textures
    while (static_cast<int>(textureData.size()) < 16) {
        std::vector<unsigned char> blank(expectedWidth * expectedHeight * 4, 0);
        // Fill with mid-gray so it's visible
        for (int p = 0; p < expectedWidth * expectedHeight; p++) {
            blank[p * 4 + 0] = 80;
            blank[p * 4 + 1] = 80;
            blank[p * 4 + 2] = 80;
            blank[p * 4 + 3] = 255;
        }
        textureData.push_back(std::move(blank));
        m_textureNames.push_back("Slot #" + std::to_string(textureData.size()));
        m_textureColors.push_back(glm::vec3(80/255.0f));
    }

    // Also pad source paths
    m_slotSourcePaths.resize(textureData.size());

    createTextureArray(textureData, expectedWidth, expectedHeight, static_cast<int>(textureData.size()));
    m_textureCount = static_cast<int>(textureData.size());

    // Create flat default normal maps for all slots
    int layerCount = static_cast<int>(textureData.size());
    std::vector<std::vector<unsigned char>> normals(layerCount);
    for (int i = 0; i < layerCount; i++) {
        normals[i].resize(expectedWidth * expectedHeight * 4);
        for (int p = 0; p < expectedWidth * expectedHeight; p++) {
            normals[i][p * 4 + 0] = 128;
            normals[i][p * 4 + 1] = 128;
            normals[i][p * 4 + 2] = 255;
            normals[i][p * 4 + 3] = 255;
        }
    }
    createNormalArray(normals, expectedWidth, expectedHeight, layerCount);

    updateDescriptorSet();
}

void TextureManager::createTextureArray(const std::vector<std::vector<unsigned char>>& textureData, int width, int height, int layerCount) {
    m_arrayWidth = width;
    m_arrayHeight = height;
    VkDevice device = m_context.getDevice();
    VkDeviceSize layerSize = width * height * 4;
    VkDeviceSize totalSize = layerSize * layerCount;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_context.findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
    Buffer::trackVramAllocHandle(stagingMemory, static_cast<int64_t>(memReqs.size));
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    // Copy all texture data to staging buffer
    void* data;
    vkMapMemory(device, stagingMemory, 0, totalSize, 0, &data);
    for (int i = 0; i < layerCount; i++) {
        memcpy(static_cast<char*>(data) + i * layerSize, textureData[i].data(), layerSize);
    }
    vkUnmapMemory(device, stagingMemory);

    // Create 2D array image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layerCount;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    vkCreateImage(device, &imageInfo, nullptr, &m_textureArray);

    vkGetImageMemoryRequirements(device, m_textureArray, &memReqs);

    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_context.findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &m_textureMemory);
    Buffer::trackVramAllocHandle(m_textureMemory, static_cast<int64_t>(memReqs.size));
    vkBindImageMemory(device, m_textureArray, m_textureMemory, 0);

    // Transition and copy
    transitionImageLayout(m_textureArray, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerCount);
    copyBufferToImageArray(stagingBuffer, m_textureArray, width, height, layerCount);
    transitionImageLayout(m_textureArray, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layerCount);

    // Cleanup staging
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    Buffer::trackVramFreeHandle(stagingMemory);
    vkFreeMemory(device, stagingMemory, nullptr);

    // Create image view for 2D array
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_textureArray;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;

    vkCreateImageView(device, &viewInfo, nullptr, &m_textureArrayView);
}

void TextureManager::createNormalArray(const std::vector<std::vector<unsigned char>>& normalData, int width, int height, int layerCount) {
    VkDevice device = m_context.getDevice();
    VkDeviceSize layerSize = width * height * 4;
    VkDeviceSize totalSize = layerSize * layerCount;

    // Clean up existing normal array
    if (m_normalArrayView) { vkDestroyImageView(device, m_normalArrayView, nullptr); m_normalArrayView = VK_NULL_HANDLE; }
    if (m_normalArray) { vkDestroyImage(device, m_normalArray, nullptr); m_normalArray = VK_NULL_HANDLE; }
    if (m_normalMemory) { Buffer::trackVramFreeHandle(m_normalMemory); vkFreeMemory(device, m_normalMemory, nullptr); m_normalMemory = VK_NULL_HANDLE; }

    // Staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_context.findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
    Buffer::trackVramAllocHandle(stagingMemory, static_cast<int64_t>(memReqs.size));
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    void* data;
    vkMapMemory(device, stagingMemory, 0, totalSize, 0, &data);
    for (int i = 0; i < layerCount; i++) {
        memcpy(static_cast<char*>(data) + i * layerSize, normalData[i].data(), layerSize);
    }
    vkUnmapMemory(device, stagingMemory);

    // Create image — use UNORM (not SRGB) for normal maps since they're linear data
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layerCount;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    vkCreateImage(device, &imageInfo, nullptr, &m_normalArray);

    vkGetImageMemoryRequirements(device, m_normalArray, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_context.findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &allocInfo, nullptr, &m_normalMemory);
    Buffer::trackVramAllocHandle(m_normalMemory, static_cast<int64_t>(memReqs.size));
    vkBindImageMemory(device, m_normalArray, m_normalMemory, 0);

    transitionImageLayout(m_normalArray, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layerCount);
    copyBufferToImageArray(stagingBuffer, m_normalArray, width, height, layerCount);
    transitionImageLayout(m_normalArray, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layerCount);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    Buffer::trackVramFreeHandle(stagingMemory);
    vkFreeMemory(device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_normalArray;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;
    vkCreateImageView(device, &viewInfo, nullptr, &m_normalArrayView);
}

void TextureManager::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount) {
    VkCommandBuffer commandBuffer = m_context.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

    VkPipelineStageFlags srcStage, dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::runtime_error("Unsupported layout transition");
    }

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_context.endSingleTimeCommands(commandBuffer);
}

void TextureManager::copyBufferToImageArray(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) {
    VkCommandBuffer commandBuffer = m_context.beginSingleTimeCommands();

    std::vector<VkBufferImageCopy> regions(layerCount);
    VkDeviceSize layerSize = width * height * 4;

    for (uint32_t i = 0; i < layerCount; i++) {
        regions[i].bufferOffset = i * layerSize;
        regions[i].bufferRowLength = 0;
        regions[i].bufferImageHeight = 0;
        regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[i].imageSubresource.mipLevel = 0;
        regions[i].imageSubresource.baseArrayLayer = i;
        regions[i].imageSubresource.layerCount = 1;
        regions[i].imageOffset = {0, 0, 0};
        regions[i].imageExtent = {width, height, 1};
    }

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    m_context.endSingleTimeCommands(commandBuffer);
}

std::string TextureManager::findNormalMapPath(const std::string& albedoPath) {
    namespace fs = std::filesystem;
    fs::path p(albedoPath);
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();
    fs::path dir = p.parent_path();

    // Common normal map naming conventions:
    // texture_n.dds, texture_normal.dds, texture_N.dds, texture_Normal.dds
    // texture_nrm.dds, texture_NRM.dds
    std::vector<std::string> suffixes = {"_n", "_N", "_normal", "_Normal", "_NORMAL", "_nrm", "_NRM"};

    for (const auto& suffix : suffixes) {
        fs::path candidate = dir / (stem + suffix + ext);
        if (fs::exists(candidate)) return candidate.string();
    }

    // Also check if the albedo itself has a suffix we should strip
    // e.g., texture_d.dds (diffuse) -> texture_n.dds
    std::vector<std::string> diffuseSuffixes = {"_d", "_D", "_diffuse", "_Diffuse", "_albedo", "_Albedo", "_color", "_Color"};
    for (const auto& dsuf : diffuseSuffixes) {
        if (stem.size() > dsuf.size() && stem.substr(stem.size() - dsuf.size()) == dsuf) {
            std::string baseStem = stem.substr(0, stem.size() - dsuf.size());
            for (const auto& nsuf : suffixes) {
                fs::path candidate = dir / (baseStem + nsuf + ext);
                if (fs::exists(candidate)) return candidate.string();
            }
        }
    }

    return "";
}

bool TextureManager::replaceNormalLayer(int slot, const unsigned char* rgba, int width, int height) {
    if (slot < 0 || slot >= m_textureCount || !m_normalArray) return false;
    if (!rgba || width <= 0 || height <= 0) return false;

    VkDevice device = m_context.getDevice();

    std::vector<unsigned char> resized(m_arrayWidth * m_arrayHeight * 4);
    if (width == m_arrayWidth && height == m_arrayHeight) {
        memcpy(resized.data(), rgba, resized.size());
    } else {
        stbir_resize_uint8_linear(
            rgba, width, height, width * 4,
            resized.data(), m_arrayWidth, m_arrayHeight, m_arrayWidth * 4,
            STBIR_RGBA);
    }

    VkDeviceSize layerSize = m_arrayWidth * m_arrayHeight * 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    m_context.createBuffer(layerSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(device, stagingMemory, 0, layerSize, 0, &data);
    memcpy(data, resized.data(), layerSize);
    vkUnmapMemory(device, stagingMemory);

    VkCommandBuffer cmd = m_context.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_normalArray;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = static_cast<uint32_t>(slot);
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = static_cast<uint32_t>(slot);
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<uint32_t>(m_arrayWidth), static_cast<uint32_t>(m_arrayHeight), 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_normalArray,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_context.endSingleTimeCommands(cmd);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    printf("TextureManager: replaced normal map slot %d\n", slot);
    return true;
}

bool TextureManager::replaceLayer(int slot, const unsigned char* rgba, int width, int height,
                                   const std::string& name, const std::string& sourcePath) {
    if (slot < 0 || slot >= m_textureCount || !m_textureArray) return false;
    if (!rgba || width <= 0 || height <= 0) return false;

    VkDevice device = m_context.getDevice();

    // Resize input to array dimensions
    std::vector<unsigned char> resized(m_arrayWidth * m_arrayHeight * 4);
    if (width == m_arrayWidth && height == m_arrayHeight) {
        memcpy(resized.data(), rgba, resized.size());
    } else {
        stbir_resize_uint8_linear(
            rgba, width, height, width * 4,
            resized.data(), m_arrayWidth, m_arrayHeight, m_arrayWidth * 4,
            STBIR_RGBA);
    }

    VkDeviceSize layerSize = m_arrayWidth * m_arrayHeight * 4;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    m_context.createBuffer(layerSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(device, stagingMemory, 0, layerSize, 0, &data);
    memcpy(data, resized.data(), layerSize);
    vkUnmapMemory(device, stagingMemory);

    // Transition layer to transfer dst
    VkCommandBuffer cmd = m_context.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_textureArray;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = static_cast<uint32_t>(slot);
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to single layer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = static_cast<uint32_t>(slot);
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<uint32_t>(m_arrayWidth), static_cast<uint32_t>(m_arrayHeight), 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_textureArray,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition back to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    m_context.endSingleTimeCommands(cmd);

    // Cleanup staging
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // Update metadata
    if (!name.empty() && slot < static_cast<int>(m_textureNames.size())) {
        m_textureNames[slot] = name;
    }
    if (slot < static_cast<int>(m_slotSourcePaths.size())) {
        m_slotSourcePaths[slot] = sourcePath;
    }

    // Recompute average color for this slot
    if (slot < static_cast<int>(m_textureColors.size())) {
        double r = 0, g = 0, b = 0;
        int pixelCount = m_arrayWidth * m_arrayHeight;
        int step = std::max(1, pixelCount / 1024);
        int sampled = 0;
        for (int p = 0; p < pixelCount; p += step) {
            r += resized[p * 4 + 0];
            g += resized[p * 4 + 1];
            b += resized[p * 4 + 2];
            sampled++;
        }
        m_textureColors[slot] = glm::vec3(
            static_cast<float>(r / sampled) / 255.0f,
            static_cast<float>(g / sampled) / 255.0f,
            static_cast<float>(b / sampled) / 255.0f);
    }

    printf("TextureManager: replaced slot %d with '%s'\n", slot, name.c_str());
    return true;
}

void TextureManager::updateDescriptorSet() {
    std::array<VkDescriptorImageInfo, 2> imageInfos{};
    // Binding 0: albedo
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = m_textureArrayView;
    imageInfos[0].sampler = m_sampler;
    // Binding 1: normal maps
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = m_normalArrayView ? m_normalArrayView : m_textureArrayView; // fallback
    imageInfos[1].sampler = m_sampler;

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &imageInfos[0];

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &imageInfos[1];

    vkUpdateDescriptorSets(m_context.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

} // namespace eden
