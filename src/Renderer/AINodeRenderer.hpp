#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <memory>

namespace eden {

class VulkanContext;
class Terrain;
class AINode;
class TraderAI;

/**
 * Renders AI nodes as circular markers on terrain with optional connection lines.
 * Color-codes nodes by type and highlights selected nodes.
 */
class AINodeRenderer {
public:
    AINodeRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~AINodeRenderer();

    AINodeRenderer(const AINodeRenderer&) = delete;
    AINodeRenderer& operator=(const AINodeRenderer&) = delete;

    // Update geometry from node list
    void update(const std::vector<std::unique_ptr<AINode>>& nodes, const Terrain& terrain);

    // Update trader markers and paths (call after update())
    void updateTraders(const std::vector<std::unique_ptr<TraderAI>>& traders,
                       const TraderAI* playerTrader, const Terrain& terrain);

    // Add collision shapes for debug rendering
    void addCollisionAABB(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
    void addCollisionLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color);
    void clearCollisionAABBs();

    // Set Bullet collision color (cyan, magenta, yellow, etc.)
    void setBulletCollisionColor(const glm::vec3& color) { m_bulletCollisionColor = color; }

    // Render placement preview marker
    void setPlacementPreview(const glm::vec3& pos, bool valid);

    // Render all nodes and connections
    void render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj);

    // Visibility
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Colors
    void setConnectionColor(const glm::vec3& color) { m_connectionColor = color; }
    void setSelectedColor(const glm::vec3& color) { m_selectedColor = color; }
    void setPreviewColor(const glm::vec3& color) { m_previewColor = color; }

    // Recreate pipeline on swapchain recreate
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

private:
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createBuffers();
    void generateNodeMarker(const glm::vec3& center, float radius,
                            const glm::vec3& color, const Terrain& terrain,
                            bool filled = false);
    void generateDiamondMarker(const glm::vec3& center, float radius,
                               const glm::vec3& color, const Terrain& terrain);
    void generateConnection(const glm::vec3& from, const glm::vec3& to,
                            const Terrain& terrain);
    void generateArrow(const glm::vec3& from, const glm::vec3& to,
                       const Terrain& terrain, float position = 0.6f);
    void updateBuffers();

    glm::vec3 getColorForType(int type) const;

    VulkanContext& m_context;

    // Pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // Nodes buffer (circles)
    VkBuffer m_nodesBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_nodesMemory = VK_NULL_HANDLE;
    void* m_nodesMappedMemory = nullptr;

    // Connections buffer (lines)
    VkBuffer m_connectionsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_connectionsMemory = VK_NULL_HANDLE;
    void* m_connectionsMappedMemory = nullptr;

    // Arrows buffer (triangles)
    VkBuffer m_arrowsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_arrowsMemory = VK_NULL_HANDLE;
    void* m_arrowsMappedMemory = nullptr;

    // Collision debug buffer (for AABB and Bullet collision visualization)
    VkBuffer m_collisionBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_collisionMemory = VK_NULL_HANDLE;
    void* m_collisionMappedMemory = nullptr;

    // Vertex data
    std::vector<glm::vec3> m_nodesVertices;
    std::vector<glm::vec3> m_connectionsVertices;
    std::vector<glm::vec3> m_arrowVertices;  // Triangle vertices for arrows
    std::vector<glm::vec3> m_aabbVertices;   // AABB collision hull lines
    std::vector<glm::vec3> m_bulletCollisionVertices;  // Bullet physics collision lines
    glm::vec3 m_bulletCollisionColor{0.0f, 1.0f, 1.0f};  // Default cyan

    // Draw info (to track where each node starts in buffer)
    struct NodeDrawInfo {
        uint32_t startVertex;
        uint32_t vertexCount;
        glm::vec3 color;
    };
    std::vector<NodeDrawInfo> m_nodeDrawInfos;

    // Connection info for drawing (tracks direction)
    struct ConnectionDrawInfo {
        uint32_t startVertex;
        uint32_t vertexCount;
        glm::vec3 fromPos;
        glm::vec3 toPos;
    };
    std::vector<ConnectionDrawInfo> m_connectionDrawInfos;

    // Colors for node types
    std::unordered_map<int, glm::vec3> m_nodeTypeColors;
    glm::vec3 m_connectionColor{0.4f, 0.8f, 1.0f};
    glm::vec3 m_selectedColor{1.0f, 1.0f, 0.0f};
    glm::vec3 m_previewColor{0.5f, 1.0f, 0.5f};

    // Placement preview
    glm::vec3 m_previewPos{0};
    bool m_hasPreview = false;

    bool m_visible = true;

    // Configuration
    static constexpr int MAX_NODE_VERTICES = 8192;
    static constexpr int MAX_CONNECTION_VERTICES = 4096;
    static constexpr int MAX_ARROW_VERTICES = 2048;
    static constexpr int MAX_COLLISION_VERTICES = 16384;  // For collision debug visualization
    static constexpr int NODE_MARKER_SEGMENTS = 24;
    static constexpr float HEIGHT_OFFSET = 0.5f;
    static constexpr float MIN_RENDER_RADIUS = 2.0f;
    static constexpr float ARROW_SIZE = 1.5f;
};

} // namespace eden
