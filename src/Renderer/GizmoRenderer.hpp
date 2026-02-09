#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "../Editor/Gizmo.hpp"

namespace eden {

class VulkanContext;

class GizmoRenderer {
public:
    GizmoRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~GizmoRenderer();

    GizmoRenderer(const GizmoRenderer&) = delete;
    GizmoRenderer& operator=(const GizmoRenderer&) = delete;

    // Update gizmo mesh if needed
    void update(Gizmo& gizmo);

    // Render the gizmo
    void render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj, const Gizmo& gizmo);

    // Recreate pipeline for new extent (after swapchain recreation)
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createBuffers();
    void updateBuffers(const Gizmo& gizmo);

    VulkanContext& m_context;

    // Pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // Vertex and index buffers
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    void* m_mappedVertexMemory = nullptr;
    void* m_mappedIndexMemory = nullptr;

    uint32_t m_indexCount = 0;
    bool m_buffersCreated = false;
};

} // namespace eden
