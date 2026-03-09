#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace eden {

class VulkanContext;

class TerrainPipeline {
public:
    TerrainPipeline(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent,
                    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE);
    ~TerrainPipeline();

    TerrainPipeline(const TerrainPipeline&) = delete;
    TerrainPipeline& operator=(const TerrainPipeline&) = delete;

    VkPipeline getHandle() const {
        return (m_wireframe && m_wireframePipeline != VK_NULL_HANDLE)
            ? m_wireframePipeline : m_pipeline;
    }
    VkPipelineLayout getLayout() const { return m_pipelineLayout; }

    void setWireframe(bool on) { m_wireframe = on; }
    bool isWireframe() const { return m_wireframe; }

private:
    void createPipelineLayout(VkDescriptorSetLayout textureSetLayout);
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);

    VulkanContext& m_context;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipeline m_wireframePipeline = VK_NULL_HANDLE;
    bool m_wireframe = false;
};

} // namespace eden
