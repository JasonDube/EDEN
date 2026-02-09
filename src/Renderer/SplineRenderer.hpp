#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace eden {

class VulkanContext;
class Terrain;

class SplineRenderer {
public:
    SplineRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~SplineRenderer();

    SplineRenderer(const SplineRenderer&) = delete;
    SplineRenderer& operator=(const SplineRenderer&) = delete;

    // Update geometry from spline data
    // controlPoints: the user-placed control points
    // splineSamples: dense samples along the spline curve
    void update(const std::vector<glm::vec3>& controlPoints,
                const std::vector<glm::vec3>& splineSamples,
                const Terrain& terrain);

    // Render the spline and control points
    void render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj);

    // Enable/disable visibility
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Set colors
    void setCurveColor(const glm::vec3& color) { m_curveColor = color; }
    void setPointColor(const glm::vec3& color) { m_pointColor = color; }

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createBuffers();
    void updateCurveBuffer();
    void updatePointsBuffer();

    // Generate circle vertices for a control point marker
    void generatePointMarker(const glm::vec3& center, float radius, const Terrain& terrain);

    VulkanContext& m_context;

    // Pipeline (shared for curve and points)
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_curvePipeline = VK_NULL_HANDLE;   // LINE_STRIP for curve
    VkPipeline m_pointsPipeline = VK_NULL_HANDLE;  // LINE_STRIP for point markers

    // Curve buffer (spline samples)
    VkBuffer m_curveBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_curveMemory = VK_NULL_HANDLE;
    void* m_curveMappedMemory = nullptr;
    std::vector<glm::vec3> m_curveVertices;

    // Control points buffer (small circle markers)
    VkBuffer m_pointsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_pointsMemory = VK_NULL_HANDLE;
    void* m_pointsMappedMemory = nullptr;
    std::vector<glm::vec3> m_pointsVertices;

    // Colors
    glm::vec3 m_curveColor{1.0f, 0.8f, 0.2f};   // Orange/yellow for curve
    glm::vec3 m_pointColor{1.0f, 0.2f, 0.2f};   // Red for control points

    bool m_visible = false;

    // Configuration
    static constexpr int MAX_CURVE_VERTICES = 2048;    // Max spline samples
    static constexpr int MAX_POINT_VERTICES = 512;     // Max control point marker vertices
    static constexpr int POINT_MARKER_SEGMENTS = 12;   // Segments per control point circle
    static constexpr float POINT_MARKER_RADIUS = 1.5f; // Size of control point markers
    static constexpr float HEIGHT_OFFSET = 0.5f;       // Offset above terrain
};

} // namespace eden
