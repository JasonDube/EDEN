#include "BrushRing.hpp"
#include "VulkanContext.hpp"
#include "PipelineBuilder.hpp"
#include <eden/Terrain.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>

namespace eden {

struct BrushRingPushConstants {
    glm::mat4 mvp;
    glm::vec4 color;
};

BrushRing::BrushRing(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent)
    : m_context(context)
{
    m_vertices.resize(RING_SEGMENTS + 1);  // +1 to close the loop
    createVertexBuffer();
    createPipeline(renderPass, extent);
}

BrushRing::~BrushRing() {
    VkDevice device = m_context.getDevice();

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    }
    if (m_mappedMemory) {
        vkUnmapMemory(device, m_vertexMemory);
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    }
    if (m_vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_vertexMemory, nullptr);
    }
}

void BrushRing::createVertexBuffer() {
    VkDevice device = m_context.getDevice();
    VkDeviceSize bufferSize = sizeof(glm::vec3) * m_vertices.size();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_vertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create brush ring vertex buffer");
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, m_vertexBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_context.findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_vertexMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate brush ring vertex buffer memory");
    }

    vkBindBufferMemory(device, m_vertexBuffer, m_vertexMemory, 0);

    // Keep buffer persistently mapped for efficient updates
    vkMapMemory(device, m_vertexMemory, 0, bufferSize, 0, &m_mappedMemory);
}

void BrushRing::update(const glm::vec3& brushPosition, float radius, const Terrain& terrain,
                       const BrushShapeParams& shapeParams) {
    const float PI = 3.14159265f;
    float cosRot = std::cos(shapeParams.rotation);
    float sinRot = std::sin(shapeParams.rotation);

    for (int i = 0; i <= RING_SEGMENTS; i++) {
        float t = static_cast<float>(i) / RING_SEGMENTS;
        float localX = 0.0f, localZ = 0.0f;

        switch (shapeParams.shape) {
            case BrushShape::Circle: {
                float angle = t * 2.0f * PI;
                localX = radius * std::cos(angle);
                localZ = radius * std::sin(angle);
                break;
            }
            case BrushShape::Ellipse: {
                float angle = t * 2.0f * PI;
                localX = radius * std::cos(angle);
                localZ = radius * shapeParams.aspectRatio * std::sin(angle);
                break;
            }
            case BrushShape::Square: {
                // Walk around the square perimeter
                float perimeter = t * 4.0f;  // 0-4 for 4 sides
                if (perimeter < 1.0f) {
                    // Bottom edge: (-1,-1) to (1,-1)
                    localX = radius * (-1.0f + 2.0f * perimeter);
                    localZ = -radius;
                } else if (perimeter < 2.0f) {
                    // Right edge: (1,-1) to (1,1)
                    localX = radius;
                    localZ = radius * (-1.0f + 2.0f * (perimeter - 1.0f));
                } else if (perimeter < 3.0f) {
                    // Top edge: (1,1) to (-1,1)
                    localX = radius * (1.0f - 2.0f * (perimeter - 2.0f));
                    localZ = radius;
                } else {
                    // Left edge: (-1,1) to (-1,-1)
                    localX = -radius;
                    localZ = radius * (1.0f - 2.0f * (perimeter - 3.0f));
                }
                break;
            }
        }

        // Apply rotation
        float rotatedX = localX * cosRot - localZ * sinRot;
        float rotatedZ = localX * sinRot + localZ * cosRot;

        float x = brushPosition.x + rotatedX;
        float z = brushPosition.z + rotatedZ;

        // Sample terrain height at this position
        float y = terrain.getHeightAt(x, z) + HEIGHT_OFFSET;

        m_vertices[i] = glm::vec3(x, y, z);
    }

    updateVertexBuffer();
}

void BrushRing::updateVertexBuffer() {
    if (m_mappedMemory) {
        memcpy(m_mappedMemory, m_vertices.data(), sizeof(glm::vec3) * m_vertices.size());
    }
}

void BrushRing::render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj) {
    if (!m_visible || m_vertices.empty()) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    BrushRingPushConstants pc;
    pc.mvp = viewProj;  // No model transform needed, vertices are in world space
    pc.color = glm::vec4(m_color, 1.0f);

    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(BrushRingPushConstants), &pc);

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    vkCmdDraw(commandBuffer, static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);
}

void BrushRing::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    auto result = PipelineBuilder(m_context)
        .setShaders("shaders/brush_ring.vert.spv", "shaders/brush_ring.frag.spv")
        .setVertexBinding(0, sizeof(glm::vec3))
        .addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
        .setPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
        .setCullMode(VK_CULL_MODE_NONE)
        .setDepthBias(-1.0f, -1.0f)
        .setDepthTest(true, false)
        .setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL)
        .setPushConstantSize(sizeof(BrushRingPushConstants))
        .build(renderPass, extent);

    m_pipeline = result.pipeline;
    m_pipelineLayout = result.layout;
}

} // namespace eden
