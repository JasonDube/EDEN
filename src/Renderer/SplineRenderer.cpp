#include "SplineRenderer.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"
#include <eden/Terrain.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>

namespace eden {

struct SplinePushConstants {
    glm::mat4 mvp;
    glm::vec4 color;
};

SplineRenderer::SplineRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent)
    : m_context(context)
{
    m_curveVertices.reserve(MAX_CURVE_VERTICES);
    m_pointsVertices.reserve(MAX_POINT_VERTICES);
    createBuffers();
    createPipeline(renderPass, extent);
}

SplineRenderer::~SplineRenderer() {
    VkDevice device = m_context.getDevice();

    if (m_curvePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_curvePipeline, nullptr);
    }
    if (m_pointsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pointsPipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    }
    if (m_curveMappedMemory) {
        vkUnmapMemory(device, m_curveMemory);
    }
    if (m_curveBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_curveBuffer, nullptr);
    }
    if (m_curveMemory != VK_NULL_HANDLE) {
        Buffer::trackVramFreeHandle(m_curveMemory);
        vkFreeMemory(device, m_curveMemory, nullptr);
    }
    if (m_pointsMappedMemory) {
        vkUnmapMemory(device, m_pointsMemory);
    }
    if (m_pointsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_pointsBuffer, nullptr);
    }
    if (m_pointsMemory != VK_NULL_HANDLE) {
        Buffer::trackVramFreeHandle(m_pointsMemory);
        vkFreeMemory(device, m_pointsMemory, nullptr);
    }
}

void SplineRenderer::createBuffers() {
    VkDevice device = m_context.getDevice();

    // Create curve vertex buffer
    {
        VkDeviceSize bufferSize = sizeof(glm::vec3) * MAX_CURVE_VERTICES;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_curveBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create spline curve vertex buffer");
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, m_curveBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = m_context.findMemoryType(
            memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_curveMemory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate spline curve vertex buffer memory");
        }
        Buffer::trackVramAllocHandle(m_curveMemory, static_cast<int64_t>(memReqs.size));

        vkBindBufferMemory(device, m_curveBuffer, m_curveMemory, 0);
        vkMapMemory(device, m_curveMemory, 0, bufferSize, 0, &m_curveMappedMemory);
    }

    // Create control points vertex buffer
    {
        VkDeviceSize bufferSize = sizeof(glm::vec3) * MAX_POINT_VERTICES;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_pointsBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create spline points vertex buffer");
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, m_pointsBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = m_context.findMemoryType(
            memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_pointsMemory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate spline points vertex buffer memory");
        }
        Buffer::trackVramAllocHandle(m_pointsMemory, static_cast<int64_t>(memReqs.size));

        vkBindBufferMemory(device, m_pointsBuffer, m_pointsMemory, 0);
        vkMapMemory(device, m_pointsMemory, 0, bufferSize, 0, &m_pointsMappedMemory);
    }
}

void SplineRenderer::generatePointMarker(const glm::vec3& center, float radius, const Terrain& terrain) {
    // Generate a circle of vertices around the control point
    for (int i = 0; i <= POINT_MARKER_SEGMENTS; i++) {
        float angle = (static_cast<float>(i) / POINT_MARKER_SEGMENTS) * 2.0f * 3.14159265f;
        float x = center.x + radius * std::cos(angle);
        float z = center.z + radius * std::sin(angle);
        float y = terrain.getHeightAt(x, z) + HEIGHT_OFFSET;

        m_pointsVertices.push_back(glm::vec3(x, y, z));
    }
}

void SplineRenderer::update(const std::vector<glm::vec3>& controlPoints,
                            const std::vector<glm::vec3>& splineSamples,
                            const Terrain& terrain) {
    // Update curve vertices from spline samples
    m_curveVertices.clear();
    for (const auto& sample : splineSamples) {
        if (m_curveVertices.size() >= MAX_CURVE_VERTICES) break;

        // Sample terrain height and add offset
        float y = terrain.getHeightAt(sample.x, sample.z) + HEIGHT_OFFSET;
        m_curveVertices.push_back(glm::vec3(sample.x, y, sample.z));
    }
    updateCurveBuffer();

    // Update control point markers
    m_pointsVertices.clear();
    for (const auto& point : controlPoints) {
        if (m_pointsVertices.size() + POINT_MARKER_SEGMENTS + 1 > MAX_POINT_VERTICES) break;
        generatePointMarker(point, POINT_MARKER_RADIUS, terrain);
    }
    updatePointsBuffer();
}

void SplineRenderer::updateCurveBuffer() {
    if (m_curveMappedMemory && !m_curveVertices.empty()) {
        size_t copySize = sizeof(glm::vec3) * std::min(m_curveVertices.size(), static_cast<size_t>(MAX_CURVE_VERTICES));
        memcpy(m_curveMappedMemory, m_curveVertices.data(), copySize);
    }
}

void SplineRenderer::updatePointsBuffer() {
    if (m_pointsMappedMemory && !m_pointsVertices.empty()) {
        size_t copySize = sizeof(glm::vec3) * std::min(m_pointsVertices.size(), static_cast<size_t>(MAX_POINT_VERTICES));
        memcpy(m_pointsMappedMemory, m_pointsVertices.data(), copySize);
    }
}

void SplineRenderer::render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj) {
    if (!m_visible) {
        return;
    }

    SplinePushConstants pc;
    pc.mvp = viewProj;

    VkDeviceSize offsets[] = {0};

    // Render spline curve
    if (!m_curveVertices.empty()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_curvePipeline);

        pc.color = glm::vec4(m_curveColor, 1.0f);
        vkCmdPushConstants(commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SplinePushConstants), &pc);

        VkBuffer vertexBuffers[] = {m_curveBuffer};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(m_curveVertices.size()), 1, 0, 0);
    }

    // Render control point markers
    if (!m_pointsVertices.empty()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pointsPipeline);

        pc.color = glm::vec4(m_pointColor, 1.0f);
        vkCmdPushConstants(commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SplinePushConstants), &pc);

        VkBuffer vertexBuffers[] = {m_pointsBuffer};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw each control point marker as a separate line strip
        uint32_t verticesPerMarker = POINT_MARKER_SEGMENTS + 1;
        uint32_t numMarkers = static_cast<uint32_t>(m_pointsVertices.size()) / verticesPerMarker;
        for (uint32_t i = 0; i < numMarkers; i++) {
            vkCmdDraw(commandBuffer, verticesPerMarker, 1, i * verticesPerMarker, 0);
        }
    }
}

void SplineRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    // Reuse brush_ring shaders - they work perfectly for this use case
    auto vertShaderCode = m_context.readFile("shaders/brush_ring.vert.spv");
    auto fragShaderCode = m_context.readFile("shaders/brush_ring.frag.spv");

    VkShaderModule vertShaderModule = m_context.createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = m_context.createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input - just vec3 positions
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(glm::vec3);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    // Input assembly - LINE_STRIP for both curve and point markers
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = -2.0f;  // Stronger bias to render on top
    rasterizer.depthBiasSlopeFactor = -2.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push constants for MVP + color
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SplinePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_context.getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create spline pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    // Create curve pipeline
    if (vkCreateGraphicsPipelines(m_context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_curvePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create spline curve graphics pipeline");
    }

    // Create points pipeline (same configuration, we just use it separately)
    if (vkCreateGraphicsPipelines(m_context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pointsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create spline points graphics pipeline");
    }

    vkDestroyShaderModule(m_context.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_context.getDevice(), vertShaderModule, nullptr);
}

} // namespace eden
