#include "AINodeRenderer.hpp"
#include "VulkanContext.hpp"
#include "PipelineBuilder.hpp"
#include "../Editor/AINode.hpp"
#include "../AI/TraderAI.hpp"
#include <eden/Terrain.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <memory>
#include <iostream>

namespace eden {

struct AINodePushConstants {
    glm::mat4 mvp;
    glm::vec4 color;
};

AINodeRenderer::AINodeRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent)
    : m_context(context)
{
    // Initialize default colors for node types
    m_nodeTypeColors[static_cast<int>(AINodeType::WAYPOINT)]  = {0.2f, 0.6f, 1.0f};  // Blue
    m_nodeTypeColors[static_cast<int>(AINodeType::PATROL)]    = {1.0f, 0.6f, 0.2f};  // Orange
    m_nodeTypeColors[static_cast<int>(AINodeType::SPAWN)]     = {0.2f, 1.0f, 0.2f};  // Green
    m_nodeTypeColors[static_cast<int>(AINodeType::TRIGGER)]   = {1.0f, 0.2f, 0.2f};  // Red
    m_nodeTypeColors[static_cast<int>(AINodeType::OBJECTIVE)] = {1.0f, 1.0f, 0.2f};  // Yellow
    m_nodeTypeColors[static_cast<int>(AINodeType::COVER)]     = {0.6f, 0.4f, 0.2f};  // Brown
    m_nodeTypeColors[static_cast<int>(AINodeType::INTEREST)]  = {0.8f, 0.2f, 1.0f};  // Purple
    m_nodeTypeColors[static_cast<int>(AINodeType::GRAPH)]     = {0.0f, 1.0f, 1.0f};  // Cyan (diamond)
    m_nodeTypeColors[static_cast<int>(AINodeType::CUSTOM)]    = {0.7f, 0.7f, 0.7f};  // Gray

    createBuffers();
    createPipeline(renderPass, extent);
}

AINodeRenderer::~AINodeRenderer() {
    VkDevice device = m_context.getDevice();
    if (device == VK_NULL_HANDLE) return;  // Context already destroyed

    vkDeviceWaitIdle(device);  // Ensure GPU is done before cleanup

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    }

    if (m_nodesMappedMemory) {
        vkUnmapMemory(device, m_nodesMemory);
    }
    if (m_nodesBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_nodesBuffer, nullptr);
    }
    if (m_nodesMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_nodesMemory, nullptr);
    }

    if (m_connectionsMappedMemory) {
        vkUnmapMemory(device, m_connectionsMemory);
    }
    if (m_connectionsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_connectionsBuffer, nullptr);
    }
    if (m_connectionsMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_connectionsMemory, nullptr);
    }

    if (m_arrowsMappedMemory) {
        vkUnmapMemory(device, m_arrowsMemory);
    }
    if (m_arrowsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_arrowsBuffer, nullptr);
    }
    if (m_arrowsMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_arrowsMemory, nullptr);
    }

    if (m_collisionMappedMemory) {
        vkUnmapMemory(device, m_collisionMemory);
    }
    if (m_collisionBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_collisionBuffer, nullptr);
    }
    if (m_collisionMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_collisionMemory, nullptr);
    }
}

void AINodeRenderer::createBuffers() {
    VkDevice device = m_context.getDevice();

    auto createBuffer = [&](VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped, size_t maxVertices) {
        VkDeviceSize bufferSize = sizeof(glm::vec3) * maxVertices;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create AI node buffer");
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, buffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = m_context.findMemoryType(
            memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate AI node buffer memory");
        }

        vkBindBufferMemory(device, buffer, memory, 0);
        vkMapMemory(device, memory, 0, bufferSize, 0, &mapped);
    };

    createBuffer(m_nodesBuffer, m_nodesMemory, m_nodesMappedMemory, MAX_NODE_VERTICES);
    createBuffer(m_connectionsBuffer, m_connectionsMemory, m_connectionsMappedMemory, MAX_CONNECTION_VERTICES);
    createBuffer(m_arrowsBuffer, m_arrowsMemory, m_arrowsMappedMemory, MAX_ARROW_VERTICES);
    createBuffer(m_collisionBuffer, m_collisionMemory, m_collisionMappedMemory, MAX_COLLISION_VERTICES);
}

void AINodeRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    auto result = PipelineBuilder(m_context)
        .setShaders("shaders/brush_ring.vert.spv", "shaders/brush_ring.frag.spv")
        .setVertexBinding(0, sizeof(glm::vec3))
        .addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
        .setPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
        .setCullMode(VK_CULL_MODE_NONE)
        .setDepthBias(-2.0f, -2.0f)
        .setDepthTest(true, false)
        .setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL)
        .setPushConstantSize(sizeof(AINodePushConstants))
        .build(renderPass, extent);

    m_pipeline = result.pipeline;
    m_pipelineLayout = result.layout;
}

void AINodeRenderer::recreatePipeline(VkRenderPass renderPass, VkExtent2D extent) {
    VkDevice device = m_context.getDevice();
    if (device == VK_NULL_HANDLE) return;  // Guard against invalid device

    vkDeviceWaitIdle(device);

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    createPipeline(renderPass, extent);
}

glm::vec3 AINodeRenderer::getColorForType(int type) const {
    auto it = m_nodeTypeColors.find(type);
    if (it != m_nodeTypeColors.end()) {
        return it->second;
    }
    return {0.7f, 0.7f, 0.7f}; // Default gray
}

void AINodeRenderer::update(const std::vector<std::unique_ptr<AINode>>& nodes, const Terrain& terrain) {
    m_nodesVertices.clear();
    m_connectionsVertices.clear();
    m_arrowVertices.clear();
    m_nodeDrawInfos.clear();
    m_connectionDrawInfos.clear();

    // Build a map of node IDs to positions and connections for bidirectional detection
    std::unordered_map<uint32_t, glm::vec3> nodePositions;
    std::unordered_map<uint32_t, std::vector<uint32_t>> nodeConnections;
    for (const auto& node : nodes) {
        if (node && node->isVisible()) {
            nodePositions[node->getId()] = node->getPosition();
            nodeConnections[node->getId()] = node->getConnections();
        }
    }

    // Generate node markers
    for (const auto& node : nodes) {
        if (!node || !node->isVisible()) continue;
        if (m_nodesVertices.size() >= MAX_NODE_VERTICES - NODE_MARKER_SEGMENTS - 1) break;

        glm::vec3 color = node->isSelected() ? m_selectedColor : getColorForType(static_cast<int>(node->getType()));
        float renderRadius = std::max(node->getRadius(), MIN_RENDER_RADIUS);

        // Use diamond shape for GRAPH nodes, circle for others
        if (node->getType() == AINodeType::GRAPH) {
            generateDiamondMarker(node->getPosition(), renderRadius, color, terrain);
        } else {
            generateNodeMarker(node->getPosition(), renderRadius, color, terrain);
        }
    }

    // Generate connections and arrows
    for (const auto& node : nodes) {
        if (!node || !node->isVisible()) continue;

        uint32_t fromId = node->getId();
        for (uint32_t toId : node->getConnections()) {
            auto it = nodePositions.find(toId);
            if (it != nodePositions.end()) {
                if (m_connectionsVertices.size() >= MAX_CONNECTION_VERTICES - 10) break;

                glm::vec3 fromPos = node->getPosition();
                glm::vec3 toPos = it->second;

                // Generate the connection line
                generateConnection(fromPos, toPos, terrain);

                // Check if bidirectional (target also connects back to us)
                bool isBidirectional = false;
                auto targetConns = nodeConnections.find(toId);
                if (targetConns != nodeConnections.end()) {
                    for (uint32_t backConn : targetConns->second) {
                        if (backConn == fromId) {
                            isBidirectional = true;
                            break;
                        }
                    }
                }

                // Generate arrow(s)
                if (m_arrowVertices.size() < MAX_ARROW_VERTICES - 6) {
                    if (isBidirectional) {
                        // Draw arrows on both sides
                        generateArrow(fromPos, toPos, terrain, 0.35f);  // Arrow toward target
                        generateArrow(toPos, fromPos, terrain, 0.35f);  // Arrow back
                    } else {
                        // Single direction arrow
                        generateArrow(fromPos, toPos, terrain, 0.6f);
                    }
                }
            }
        }
    }

    // Generate placement preview if active
    if (m_hasPreview) {
        generateNodeMarker(m_previewPos, MIN_RENDER_RADIUS, m_previewColor, terrain);
    }

    updateBuffers();
}

void AINodeRenderer::setPlacementPreview(const glm::vec3& pos, bool valid) {
    m_previewPos = pos;
    m_hasPreview = valid;
}

void AINodeRenderer::updateTraders(const std::vector<std::unique_ptr<TraderAI>>& traders,
                                    const TraderAI* playerTrader, const Terrain& terrain) {
    // Get color based on trader state
    auto getStateColor = [](TraderState state) -> glm::vec3 {
        switch (state) {
            case TraderState::IDLE:      return {0.3f, 0.3f, 1.0f};  // Blue
            case TraderState::TRAVELING: return {0.2f, 1.0f, 0.2f};  // Green
            case TraderState::BUYING:    return {1.0f, 1.0f, 0.2f};  // Yellow
            case TraderState::SELLING:   return {1.0f, 0.6f, 0.2f};  // Orange
            case TraderState::REFUELING: return {0.2f, 1.0f, 1.0f};  // Cyan
            case TraderState::WAITING:   return {0.6f, 0.6f, 0.6f};  // Gray
            case TraderState::FLEEING:   return {1.0f, 0.2f, 0.2f};  // Red
            default:                     return {1.0f, 1.0f, 1.0f};  // White
        }
    };

    // Render a single trader
    auto renderTrader = [&](const TraderAI* trader, bool isPlayer) {
        if (!trader) return;
        if (m_nodesVertices.size() >= MAX_NODE_VERTICES - NODE_MARKER_SEGMENTS - 10) return;

        glm::vec3 pos = trader->getPosition();
        glm::vec3 color = getStateColor(trader->getState());

        // Player trader is larger and brighter
        float radius = isPlayer ? 4.0f : 2.5f;
        if (isPlayer) {
            color = glm::vec3(1.0f, 0.8f, 0.0f); // Gold for player
        }

        // Generate a filled-looking marker (double circle)
        generateNodeMarker(pos, radius, color, terrain);
        generateNodeMarker(pos, radius * 0.6f, color * 0.7f, terrain);

        // Draw current path if traveling
        if (trader->getState() == TraderState::TRAVELING) {
            const auto& path = trader->getCurrentPath();
            int pathIdx = trader->getCurrentPathIndex();

            if (!path.empty() && pathIdx < static_cast<int>(path.size())) {
                // Draw line from current position to next waypoint
                if (m_connectionsVertices.size() < MAX_CONNECTION_VERTICES - 20) {
                    glm::vec3 pathColor = isPlayer ? glm::vec3(1.0f, 0.9f, 0.3f) : glm::vec3(0.3f, 1.0f, 0.3f);

                    // Line to current waypoint
                    const int samples = 8;
                    glm::vec3 target = path[pathIdx];
                    for (int i = 0; i <= samples; i++) {
                        float t = static_cast<float>(i) / samples;
                        float x = pos.x + (target.x - pos.x) * t;
                        float z = pos.z + (target.z - pos.z) * t;
                        float y = terrain.getHeightAt(x, z) + HEIGHT_OFFSET + 0.2f;
                        m_connectionsVertices.push_back(glm::vec3(x, y, z));
                    }

                    // Store draw info for path color
                    NodeDrawInfo pathInfo;
                    pathInfo.startVertex = static_cast<uint32_t>(m_nodesVertices.size());
                    pathInfo.vertexCount = 0;
                    pathInfo.color = pathColor;
                }
            }
        }
    };

    // Render player trader
    renderTrader(playerTrader, true);

    // Render AI traders
    for (const auto& trader : traders) {
        renderTrader(trader.get(), false);
    }

    // Update buffers again after adding traders
    updateBuffers();
}

void AINodeRenderer::generateNodeMarker(const glm::vec3& center, float radius,
                                         const glm::vec3& color, const Terrain& terrain,
                                         bool /*filled*/) {
    NodeDrawInfo info;
    info.startVertex = static_cast<uint32_t>(m_nodesVertices.size());
    info.color = color;

    // Calculate height offset: use node's Y if above terrain, otherwise use terrain + offset
    float terrainHeightAtCenter = terrain.getHeightAt(center.x, center.z);
    float nodeHeightOffset = center.y - terrainHeightAtCenter;

    // Generate circle vertices
    for (int i = 0; i <= NODE_MARKER_SEGMENTS; i++) {
        float angle = (static_cast<float>(i) / NODE_MARKER_SEGMENTS) * 2.0f * 3.14159265f;
        float x = center.x + radius * std::cos(angle);
        float z = center.z + radius * std::sin(angle);
        float terrainY = terrain.getHeightAt(x, z);
        // Apply node's height offset above terrain, plus the render offset
        float y = terrainY + nodeHeightOffset + HEIGHT_OFFSET;

        m_nodesVertices.push_back(glm::vec3(x, y, z));
    }

    info.vertexCount = static_cast<uint32_t>(m_nodesVertices.size()) - info.startVertex;
    m_nodeDrawInfos.push_back(info);
}

void AINodeRenderer::generateDiamondMarker(const glm::vec3& center, float radius,
                                            const glm::vec3& color, const Terrain& terrain) {
    NodeDrawInfo info;
    info.startVertex = static_cast<uint32_t>(m_nodesVertices.size());
    info.color = color;

    // Calculate height offset: use node's Y if above terrain, otherwise use terrain + offset
    float terrainHeightAtCenter = terrain.getHeightAt(center.x, center.z);
    float nodeHeightOffset = center.y - terrainHeightAtCenter;

    // Diamond shape: 4 points (N, E, S, W) + 1 to close
    // North
    float y = terrain.getHeightAt(center.x, center.z - radius) + nodeHeightOffset + HEIGHT_OFFSET;
    m_nodesVertices.push_back(glm::vec3(center.x, y, center.z - radius));

    // East
    y = terrain.getHeightAt(center.x + radius, center.z) + nodeHeightOffset + HEIGHT_OFFSET;
    m_nodesVertices.push_back(glm::vec3(center.x + radius, y, center.z));

    // South
    y = terrain.getHeightAt(center.x, center.z + radius) + nodeHeightOffset + HEIGHT_OFFSET;
    m_nodesVertices.push_back(glm::vec3(center.x, y, center.z + radius));

    // West
    y = terrain.getHeightAt(center.x - radius, center.z) + nodeHeightOffset + HEIGHT_OFFSET;
    m_nodesVertices.push_back(glm::vec3(center.x - radius, y, center.z));

    // Close back to North
    y = terrain.getHeightAt(center.x, center.z - radius) + nodeHeightOffset + HEIGHT_OFFSET;
    m_nodesVertices.push_back(glm::vec3(center.x, y, center.z - radius));

    info.vertexCount = static_cast<uint32_t>(m_nodesVertices.size()) - info.startVertex;
    m_nodeDrawInfos.push_back(info);
}

void AINodeRenderer::generateConnection(const glm::vec3& from, const glm::vec3& to,
                                         const Terrain& terrain) {
    // Calculate height offsets for both nodes
    float fromTerrainY = terrain.getHeightAt(from.x, from.z);
    float toTerrainY = terrain.getHeightAt(to.x, to.z);
    float fromHeightOffset = from.y - fromTerrainY;
    float toHeightOffset = to.y - toTerrainY;

    // Sample points along the connection, interpolating height offsets
    const int samples = 8;
    for (int i = 0; i <= samples; i++) {
        float t = static_cast<float>(i) / samples;
        float x = from.x + (to.x - from.x) * t;
        float z = from.z + (to.z - from.z) * t;
        float terrainY = terrain.getHeightAt(x, z);
        // Interpolate height offset between the two nodes
        float heightOffset = fromHeightOffset + (toHeightOffset - fromHeightOffset) * t;
        float y = terrainY + heightOffset + HEIGHT_OFFSET;

        m_connectionsVertices.push_back(glm::vec3(x, y, z));
    }
}

void AINodeRenderer::generateArrow(const glm::vec3& from, const glm::vec3& to,
                                    const Terrain& terrain, float position) {
    // Calculate arrow position along the line
    glm::vec3 dir = to - from;
    float length = glm::length(dir);
    if (length < 0.01f) return;

    dir = glm::normalize(dir);

    // Calculate height offsets for interpolation
    float fromTerrainY = terrain.getHeightAt(from.x, from.z);
    float toTerrainY = terrain.getHeightAt(to.x, to.z);
    float fromHeightOffset = from.y - fromTerrainY;
    float toHeightOffset = to.y - toTerrainY;

    // Position along the connection
    glm::vec3 arrowPos = from + dir * (length * position);
    float heightOffset = fromHeightOffset + (toHeightOffset - fromHeightOffset) * position;
    float y = terrain.getHeightAt(arrowPos.x, arrowPos.z) + heightOffset + HEIGHT_OFFSET + 0.1f;
    arrowPos.y = y;

    // Calculate perpendicular direction (in XZ plane)
    glm::vec3 perpDir(-dir.z, 0.0f, dir.x);

    // Arrow triangle vertices (use same height offset for the whole arrow)
    glm::vec3 tip = arrowPos + dir * ARROW_SIZE;
    tip.y = terrain.getHeightAt(tip.x, tip.z) + heightOffset + HEIGHT_OFFSET + 0.1f;

    glm::vec3 left = arrowPos - dir * (ARROW_SIZE * 0.5f) + perpDir * (ARROW_SIZE * 0.5f);
    left.y = terrain.getHeightAt(left.x, left.z) + heightOffset + HEIGHT_OFFSET + 0.1f;

    glm::vec3 right = arrowPos - dir * (ARROW_SIZE * 0.5f) - perpDir * (ARROW_SIZE * 0.5f);
    right.y = terrain.getHeightAt(right.x, right.z) + heightOffset + HEIGHT_OFFSET + 0.1f;

    // Add triangle vertices (3 vertices per arrow, as a line strip triangle)
    m_arrowVertices.push_back(left);
    m_arrowVertices.push_back(tip);
    m_arrowVertices.push_back(right);
    m_arrowVertices.push_back(left);  // Close the triangle
}

void AINodeRenderer::updateBuffers() {
    if (m_nodesMappedMemory && !m_nodesVertices.empty()) {
        memcpy(m_nodesMappedMemory, m_nodesVertices.data(),
               sizeof(glm::vec3) * m_nodesVertices.size());
    }

    if (m_connectionsMappedMemory && !m_connectionsVertices.empty()) {
        memcpy(m_connectionsMappedMemory, m_connectionsVertices.data(),
               sizeof(glm::vec3) * m_connectionsVertices.size());
    }

    if (m_arrowsMappedMemory && !m_arrowVertices.empty()) {
        memcpy(m_arrowsMappedMemory, m_arrowVertices.data(),
               sizeof(glm::vec3) * m_arrowVertices.size());
    }
}

void AINodeRenderer::render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj) {
    if (!m_visible) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    AINodePushConstants pc;
    pc.mvp = viewProj;

    // Render connections first (behind nodes)
    if (!m_connectionsVertices.empty()) {
        pc.color = glm::vec4(m_connectionColor, 1.0f);
        vkCmdPushConstants(commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(AINodePushConstants), &pc);

        VkBuffer vertexBuffers[] = {m_connectionsBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw connections as separate line strips (9 vertices each)
        const int samplesPerConnection = 9; // samples + 1
        uint32_t numConnections = static_cast<uint32_t>(m_connectionsVertices.size()) / samplesPerConnection;
        for (uint32_t i = 0; i < numConnections; i++) {
            vkCmdDraw(commandBuffer, samplesPerConnection, 1, i * samplesPerConnection, 0);
        }
    }

    // Render arrows (direction indicators)
    if (!m_arrowVertices.empty()) {
        pc.color = glm::vec4(m_connectionColor, 1.0f);
        vkCmdPushConstants(commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(AINodePushConstants), &pc);

        VkBuffer vertexBuffers[] = {m_arrowsBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw arrows as line strips (4 vertices each - closed triangle)
        const int verticesPerArrow = 4;
        uint32_t numArrows = static_cast<uint32_t>(m_arrowVertices.size()) / verticesPerArrow;
        for (uint32_t i = 0; i < numArrows; i++) {
            vkCmdDraw(commandBuffer, verticesPerArrow, 1, i * verticesPerArrow, 0);
        }
    }

    // Render node markers
    if (!m_nodesVertices.empty()) {
        VkBuffer vertexBuffers[] = {m_nodesBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        for (const auto& info : m_nodeDrawInfos) {
            pc.color = glm::vec4(info.color, 1.0f);
            vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(AINodePushConstants), &pc);

            vkCmdDraw(commandBuffer, info.vertexCount, 1, info.startVertex, 0);
        }
    }

    // Render collision hulls using dedicated collision buffer
    // Copy all collision data to the buffer first
    if (!m_aabbVertices.empty() || !m_bulletCollisionVertices.empty()) {
        size_t aabbCount = std::min(m_aabbVertices.size(), static_cast<size_t>(MAX_COLLISION_VERTICES));
        size_t bulletCount = std::min(m_bulletCollisionVertices.size(),
                                      static_cast<size_t>(MAX_COLLISION_VERTICES) - aabbCount);

        // Copy AABB vertices first, then Bullet collision vertices
        if (m_collisionMappedMemory) {
            if (!m_aabbVertices.empty()) {
                memcpy(m_collisionMappedMemory, m_aabbVertices.data(),
                       sizeof(glm::vec3) * aabbCount);
            }
            if (!m_bulletCollisionVertices.empty() && bulletCount > 0) {
                char* bulletDest = static_cast<char*>(m_collisionMappedMemory) + sizeof(glm::vec3) * aabbCount;
                memcpy(bulletDest, m_bulletCollisionVertices.data(),
                       sizeof(glm::vec3) * bulletCount);
            }
        }

        VkBuffer vertexBuffers[] = {m_collisionBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw AABB collision (green)
        if (!m_aabbVertices.empty()) {
            pc.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green for AABB
            vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(AINodePushConstants), &pc);
            vkCmdDraw(commandBuffer, static_cast<uint32_t>(aabbCount), 1, 0, 0);
        }

        // Draw Bullet collision (cyan, magenta, or yellow depending on type)
        if (!m_bulletCollisionVertices.empty() && bulletCount > 0) {
            pc.color = glm::vec4(m_bulletCollisionColor, 1.0f);
            vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(AINodePushConstants), &pc);
            vkCmdDraw(commandBuffer, static_cast<uint32_t>(bulletCount), 1, static_cast<uint32_t>(aabbCount), 0);
        }
    }
}

void AINodeRenderer::addCollisionAABB(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color) {
    (void)color;  // Could use per-box colors later

    // 12 edges of the box, each edge is 2 vertices
    // Bottom face
    m_aabbVertices.push_back(glm::vec3(min.x, min.y, min.z));
    m_aabbVertices.push_back(glm::vec3(max.x, min.y, min.z));

    m_aabbVertices.push_back(glm::vec3(max.x, min.y, min.z));
    m_aabbVertices.push_back(glm::vec3(max.x, min.y, max.z));

    m_aabbVertices.push_back(glm::vec3(max.x, min.y, max.z));
    m_aabbVertices.push_back(glm::vec3(min.x, min.y, max.z));

    m_aabbVertices.push_back(glm::vec3(min.x, min.y, max.z));
    m_aabbVertices.push_back(glm::vec3(min.x, min.y, min.z));

    // Top face
    m_aabbVertices.push_back(glm::vec3(min.x, max.y, min.z));
    m_aabbVertices.push_back(glm::vec3(max.x, max.y, min.z));

    m_aabbVertices.push_back(glm::vec3(max.x, max.y, min.z));
    m_aabbVertices.push_back(glm::vec3(max.x, max.y, max.z));

    m_aabbVertices.push_back(glm::vec3(max.x, max.y, max.z));
    m_aabbVertices.push_back(glm::vec3(min.x, max.y, max.z));

    m_aabbVertices.push_back(glm::vec3(min.x, max.y, max.z));
    m_aabbVertices.push_back(glm::vec3(min.x, max.y, min.z));

    // Vertical edges
    m_aabbVertices.push_back(glm::vec3(min.x, min.y, min.z));
    m_aabbVertices.push_back(glm::vec3(min.x, max.y, min.z));

    m_aabbVertices.push_back(glm::vec3(max.x, min.y, min.z));
    m_aabbVertices.push_back(glm::vec3(max.x, max.y, min.z));

    m_aabbVertices.push_back(glm::vec3(max.x, min.y, max.z));
    m_aabbVertices.push_back(glm::vec3(max.x, max.y, max.z));

    m_aabbVertices.push_back(glm::vec3(min.x, min.y, max.z));
    m_aabbVertices.push_back(glm::vec3(min.x, max.y, max.z));
}

void AINodeRenderer::addCollisionLine(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color) {
    m_bulletCollisionColor = color;  // Update color for next render
    m_bulletCollisionVertices.push_back(from);
    m_bulletCollisionVertices.push_back(to);
}

void AINodeRenderer::clearCollisionAABBs() {
    m_aabbVertices.clear();
    m_bulletCollisionVertices.clear();
}

} // namespace eden
