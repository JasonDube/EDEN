#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace eden {

class VulkanContext;

struct WaterVertex {
    glm::vec3 position;
    glm::vec2 uv;

    static VkVertexInputBindingDescription getBindingDescription();
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
};

struct WaterPushConstants {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::vec4 cameraPos;
    float time;
    float waterLevel;
    float waveAmplitude;
    float waveFrequency;
};

class WaterRenderer {
public:
    WaterRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~WaterRenderer();

    WaterRenderer(const WaterRenderer&) = delete;
    WaterRenderer& operator=(const WaterRenderer&) = delete;

    // Update the water plane geometry to cover a specific area
    void updateGeometry(float centerX, float centerZ, float size, int gridResolution = 64);

    // Render the water plane
    void render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj,
                const glm::vec3& cameraPos, float time);

    // Water settings
    void setWaterLevel(float level) { m_waterLevel = level; }
    float getWaterLevel() const { return m_waterLevel; }

    void setWaveAmplitude(float amp) { m_waveAmplitude = amp; }
    float getWaveAmplitude() const { return m_waveAmplitude; }

    void setWaveFrequency(float freq) { m_waveFrequency = freq; }
    float getWaveFrequency() const { return m_waveFrequency; }

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Recreate pipeline for swapchain resize
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createBuffers();
    void destroyBuffers();


    VulkanContext& m_context;

    // Pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // Geometry buffers
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    uint32_t m_indexCount = 0;
    uint32_t m_vertexCount = 0;

    // Water parameters
    float m_waterLevel = 50.0f;
    float m_waveAmplitude = 0.5f;
    float m_waveFrequency = 0.1f;
    bool m_visible = true;

    // Current geometry center for rendering
    float m_centerX = 0.0f;
    float m_centerZ = 0.0f;
    float m_size = 1000.0f;
};

} // namespace eden
