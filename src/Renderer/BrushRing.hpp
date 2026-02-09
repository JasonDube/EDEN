#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <eden/Terrain.hpp>
#include <vector>
#include <string>

namespace eden {

class VulkanContext;

class BrushRing {
public:
    BrushRing(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~BrushRing();

    BrushRing(const BrushRing&) = delete;
    BrushRing& operator=(const BrushRing&) = delete;

    // Update ring geometry based on brush position, radius and shape
    void update(const glm::vec3& brushPosition, float radius, const Terrain& terrain,
                const BrushShapeParams& shapeParams = BrushShapeParams{});

    // Render the ring
    void render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj);

    // Set ring color
    void setColor(const glm::vec3& color) { m_color = color; }

    // Enable/disable ring visibility
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createVertexBuffer();
    void updateVertexBuffer();

    VulkanContext& m_context;

    // Pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // Vertex buffer (dynamic, updated each frame)
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    void* m_mappedMemory = nullptr;

    // Ring properties
    glm::vec3 m_color{1.0f, 1.0f, 0.0f};  // Yellow by default
    bool m_visible = true;

    // Ring geometry
    std::vector<glm::vec3> m_vertices;
    static constexpr int RING_SEGMENTS = 64;
    static constexpr float HEIGHT_OFFSET = 0.3f;  // Slight offset above terrain
};

} // namespace eden
