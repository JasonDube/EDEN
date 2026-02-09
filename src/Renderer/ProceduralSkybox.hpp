#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <eden/SkyParameters.hpp>
#include <vector>
#include <string>

namespace eden {

class VulkanContext;

class ProceduralSkybox {
public:
    ProceduralSkybox(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~ProceduralSkybox();

    ProceduralSkybox(const ProceduralSkybox&) = delete;
    ProceduralSkybox& operator=(const ProceduralSkybox&) = delete;

    // Update sky parameters (call when parameters change)
    void updateParameters(const SkyParameters& params);

    // Get current parameters for ImGui editing
    SkyParameters& getParameters() { return m_params; }
    const SkyParameters& getParameters() const { return m_params; }

    // Render the skybox
    void render(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& projection);

private:
    void createCubeGeometry();
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void allocateDescriptorSet();
    void createUniformBuffer();
    void updateUniformBuffer();

    VulkanContext& m_context;

    // Sky parameters
    SkyParameters m_params;

    // Cube geometry
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    uint32_t m_indexCount = 0;

    // Uniform buffer for sky parameters
    VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_uniformMemory = VK_NULL_HANDLE;
    void* m_uniformMapped = nullptr;

    // Descriptors
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace eden
