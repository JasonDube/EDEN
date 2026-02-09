#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace eden {

class VulkanContext;

class Skybox {
public:
    Skybox(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~Skybox();

    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    // Load cubemap from horizontal cross format image
    bool loadFromHorizontalCross(const std::string& path);

    // Render the skybox
    void render(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& projection);

private:
    void createCubeGeometry();
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void allocateDescriptorSet();
    void createSampler();
    void updateDescriptorSet();

    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);

    VulkanContext& m_context;

    // Cube geometry
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    uint32_t m_indexCount = 0;

    // Cubemap texture
    VkImage m_cubemapImage = VK_NULL_HANDLE;
    VkDeviceMemory m_cubemapMemory = VK_NULL_HANDLE;
    VkImageView m_cubemapView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    // Descriptors
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace eden
