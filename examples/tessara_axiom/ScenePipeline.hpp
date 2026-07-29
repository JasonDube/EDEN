#pragma once

#include <vulkan/vulkan.h>

namespace eden { class VulkanContext; }

namespace tessara {

// One pipeline for the whole scene: SceneVertex in, tinted three-point shading
// out. Unculled and alpha-blended, so the same pipeline draws the terrain, the
// creature's solid body, and its translucent panels without variants.
class ScenePipeline {
public:
    ScenePipeline(eden::VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~ScenePipeline();

    ScenePipeline(const ScenePipeline&) = delete;
    ScenePipeline& operator=(const ScenePipeline&) = delete;

    VkPipeline       getHandle() const { return m_pipeline; }

    // Identical, except that it does not write depth. A translucent object that
    // writes depth hides the parts of itself that are behind the parts in front,
    // which is the one thing a see-through creature must not do. Depth TESTING
    // stays on, so the terrain still occludes him properly.
    VkPipeline       getGhostHandle() const { return m_ghostPipeline; }

    VkPipelineLayout getLayout() const { return m_layout; }

private:
    VkPipeline createVariant(VkRenderPass renderPass, VkExtent2D extent,
                             VkShaderModule vert, VkShaderModule frag,
                             bool depthWrite);

    eden::VulkanContext& m_context;
    VkPipelineLayout m_layout        = VK_NULL_HANDLE;
    VkPipeline       m_pipeline      = VK_NULL_HANDLE;
    VkPipeline       m_ghostPipeline = VK_NULL_HANDLE;
};

} // namespace tessara
