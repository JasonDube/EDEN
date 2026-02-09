#include "WaterRenderer.hpp"
#include "VulkanContext.hpp"
#include <stdexcept>
#include <cstring>

namespace eden {

VkVertexInputBindingDescription WaterVertex::getBindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(WaterVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::vector<VkVertexInputAttributeDescription> WaterVertex::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attrs(2);

    // Position
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(WaterVertex, position);

    // UV
    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = offsetof(WaterVertex, uv);

    return attrs;
}

WaterRenderer::WaterRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent)
    : m_context(context)
{
    createPipeline(renderPass, extent);
    // Create initial geometry covering a large area
    updateGeometry(0.0f, 0.0f, 2000.0f, 64);
}

WaterRenderer::~WaterRenderer() {
    VkDevice device = m_context.getDevice();

    destroyBuffers();

    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
}

void WaterRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    auto vertCode = m_context.readFile("shaders/water.vert.spv");
    auto fragCode = m_context.readFile("shaders/water.frag.spv");

    VkShaderModule vertModule = m_context.createShaderModule(vertCode);
    VkShaderModule fragModule = m_context.createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    auto bindingDesc = WaterVertex::getBindingDescription();
    auto attrDescs = WaterVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Render both sides of water
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth buffer (transparent)
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // Enable alpha blending
    VkPipelineColorBlendAttachmentState colorBlend{};
    colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlend.blendEnable = VK_TRUE;
    colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlend;

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(WaterPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    if (vkCreatePipelineLayout(m_context.getDevice(), &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;

    if (vkCreateGraphicsPipelines(m_context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water pipeline");
    }

    vkDestroyShaderModule(m_context.getDevice(), vertModule, nullptr);
    vkDestroyShaderModule(m_context.getDevice(), fragModule, nullptr);
}

void WaterRenderer::updateGeometry(float centerX, float centerZ, float size, int gridResolution) {
    m_centerX = centerX;
    m_centerZ = centerZ;
    m_size = size;

    // Destroy old buffers if they exist
    destroyBuffers();

    // Generate grid mesh
    std::vector<WaterVertex> vertices;
    std::vector<uint32_t> indices;

    float halfSize = size * 0.5f;
    float step = size / static_cast<float>(gridResolution);

    // Generate vertices
    for (int z = 0; z <= gridResolution; z++) {
        for (int x = 0; x <= gridResolution; x++) {
            WaterVertex v;
            v.position.x = centerX - halfSize + x * step;
            v.position.y = 0.0f;  // Will be set to water level in shader
            v.position.z = centerZ - halfSize + z * step;
            v.uv.x = static_cast<float>(x) / gridResolution;
            v.uv.y = static_cast<float>(z) / gridResolution;
            vertices.push_back(v);
        }
    }

    // Generate indices
    for (int z = 0; z < gridResolution; z++) {
        for (int x = 0; x < gridResolution; x++) {
            uint32_t topLeft = z * (gridResolution + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (gridResolution + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;

            // Two triangles per quad
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    m_vertexCount = static_cast<uint32_t>(vertices.size());
    m_indexCount = static_cast<uint32_t>(indices.size());

    // Create vertex buffer
    VkDeviceSize vertexSize = sizeof(WaterVertex) * vertices.size();
    m_context.createBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           m_vertexBuffer, m_vertexMemory);

    void* data;
    vkMapMemory(m_context.getDevice(), m_vertexMemory, 0, vertexSize, 0, &data);
    memcpy(data, vertices.data(), vertexSize);
    vkUnmapMemory(m_context.getDevice(), m_vertexMemory);

    // Create index buffer
    VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();
    m_context.createBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           m_indexBuffer, m_indexMemory);

    vkMapMemory(m_context.getDevice(), m_indexMemory, 0, indexSize, 0, &data);
    memcpy(data, indices.data(), indexSize);
    vkUnmapMemory(m_context.getDevice(), m_indexMemory);
}

void WaterRenderer::destroyBuffers() {
    VkDevice device = m_context.getDevice();

    if (m_vertexBuffer) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexMemory) {
        vkFreeMemory(device, m_vertexMemory, nullptr);
        m_vertexMemory = VK_NULL_HANDLE;
    }
    if (m_indexBuffer) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
    }
    if (m_indexMemory) {
        vkFreeMemory(device, m_indexMemory, nullptr);
        m_indexMemory = VK_NULL_HANDLE;
    }
}

void WaterRenderer::render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj,
                           const glm::vec3& cameraPos, float time) {
    if (!m_visible || !m_vertexBuffer || !m_indexBuffer) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Set up push constants
    WaterPushConstants pc{};
    glm::mat4 model = glm::mat4(1.0f);
    model[3][1] = m_waterLevel;  // Translate to water level
    pc.mvp = viewProj * model;
    pc.model = model;
    pc.cameraPos = glm::vec4(cameraPos, 1.0f);
    pc.time = time;
    pc.waterLevel = m_waterLevel;
    pc.waveAmplitude = m_waveAmplitude;
    pc.waveFrequency = m_waveFrequency;

    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(WaterPushConstants), &pc);

    // Bind and draw
    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);
}

void WaterRenderer::recreatePipeline(VkRenderPass renderPass, VkExtent2D extent) {
    VkDevice device = m_context.getDevice();

    m_context.waitIdle();

    if (m_pipeline) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    createPipeline(renderPass, extent);
}

} // namespace eden
