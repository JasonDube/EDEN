#include "ProceduralSkybox.hpp"
#include "VulkanContext.hpp"
#include <stdexcept>
#include <array>
#include <cstring>

namespace eden {

struct SkyboxPushConstants {
    glm::mat4 viewProj;
};

ProceduralSkybox::ProceduralSkybox(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent)
    : m_context(context)
{
    createCubeGeometry();
    createDescriptorSetLayout();
    createDescriptorPool();
    createUniformBuffer();
    allocateDescriptorSet();
    createPipeline(renderPass, extent);

    // Initialize with default parameters
    updateUniformBuffer();
}

ProceduralSkybox::~ProceduralSkybox() {
    VkDevice device = m_context.getDevice();

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    }
    if (m_uniformMapped) {
        vkUnmapMemory(device, m_uniformMemory);
    }
    if (m_uniformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_uniformBuffer, nullptr);
    }
    if (m_uniformMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_uniformMemory, nullptr);
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    }
    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
    }
    if (m_indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_indexMemory, nullptr);
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    }
    if (m_vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_vertexMemory, nullptr);
    }
}

void ProceduralSkybox::updateParameters(const SkyParameters& params) {
    m_params = params;
    updateUniformBuffer();
}

void ProceduralSkybox::createCubeGeometry() {
    // Unit cube vertices (positions only)
    std::vector<glm::vec3> vertices = {
        // Front face
        {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f},
        // Back face
        { 1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f},
        // Top face
        {-1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
        // Bottom face
        {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f},
        // Right face
        { 1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f,  1.0f},
        // Left face
        {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f, -1.0f},
    };

    std::vector<uint16_t> indices;
    for (int i = 0; i < 6; i++) {
        uint16_t base = i * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 0);
    }
    m_indexCount = static_cast<uint32_t>(indices.size());

    VkDevice device = m_context.getDevice();

    // Create vertex buffer
    VkDeviceSize vertexSize = sizeof(glm::vec3) * vertices.size();
    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = vertexSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &vertexBufferInfo, nullptr, &m_vertexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skybox vertex buffer");
    }

    VkMemoryRequirements vertexMemReqs;
    vkGetBufferMemoryRequirements(device, m_vertexBuffer, &vertexMemReqs);

    VkMemoryAllocateInfo vertexAllocInfo{};
    vertexAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vertexAllocInfo.allocationSize = vertexMemReqs.size;
    vertexAllocInfo.memoryTypeIndex = m_context.findMemoryType(
        vertexMemReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(device, &vertexAllocInfo, nullptr, &m_vertexMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate skybox vertex buffer memory");
    }

    vkBindBufferMemory(device, m_vertexBuffer, m_vertexMemory, 0);

    void* vertexData;
    vkMapMemory(device, m_vertexMemory, 0, vertexSize, 0, &vertexData);
    memcpy(vertexData, vertices.data(), vertexSize);
    vkUnmapMemory(device, m_vertexMemory);

    // Create index buffer
    VkDeviceSize indexSize = sizeof(uint16_t) * indices.size();
    VkBufferCreateInfo indexBufferInfo{};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = indexSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &indexBufferInfo, nullptr, &m_indexBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skybox index buffer");
    }

    VkMemoryRequirements indexMemReqs;
    vkGetBufferMemoryRequirements(device, m_indexBuffer, &indexMemReqs);

    VkMemoryAllocateInfo indexAllocInfo{};
    indexAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    indexAllocInfo.allocationSize = indexMemReqs.size;
    indexAllocInfo.memoryTypeIndex = m_context.findMemoryType(
        indexMemReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(device, &indexAllocInfo, nullptr, &m_indexMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate skybox index buffer memory");
    }

    vkBindBufferMemory(device, m_indexBuffer, m_indexMemory, 0);

    void* indexData;
    vkMapMemory(device, m_indexMemory, 0, indexSize, 0, &indexData);
    memcpy(indexData, indices.data(), indexSize);
    vkUnmapMemory(device, m_indexMemory);
}

void ProceduralSkybox::createUniformBuffer() {
    VkDevice device = m_context.getDevice();
    VkDeviceSize bufferSize = sizeof(SkyParametersGPU);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_uniformBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skybox uniform buffer");
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, m_uniformBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_context.findMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_uniformMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate skybox uniform buffer memory");
    }

    vkBindBufferMemory(device, m_uniformBuffer, m_uniformMemory, 0);

    // Keep mapped for easy updates
    vkMapMemory(device, m_uniformMemory, 0, bufferSize, 0, &m_uniformMapped);
}

void ProceduralSkybox::updateUniformBuffer() {
    if (m_uniformMapped) {
        SkyParametersGPU gpuParams = SkyParametersGPU::fromCPU(m_params);
        memcpy(m_uniformMapped, &gpuParams, sizeof(SkyParametersGPU));
    }
}

void ProceduralSkybox::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;

    if (vkCreateDescriptorSetLayout(m_context.getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skybox descriptor set layout");
    }
}

void ProceduralSkybox::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(m_context.getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skybox descriptor pool");
    }
}

void ProceduralSkybox::allocateDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(m_context.getDevice(), &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate skybox descriptor set");
    }

    // Update descriptor with uniform buffer
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(SkyParametersGPU);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_context.getDevice(), 1, &descriptorWrite, 0, nullptr);
}

void ProceduralSkybox::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    auto vertShaderCode = m_context.readFile("shaders/skybox_procedural.vert.spv");
    auto fragShaderCode = m_context.readFile("shaders/skybox_procedural.frag.spv");

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

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    rasterizer.depthBiasEnable = VK_FALSE;

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

    // Push constants for view-projection matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SkyboxPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_context.getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skybox pipeline layout");
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

    if (vkCreateGraphicsPipelines(m_context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create skybox graphics pipeline");
    }

    vkDestroyShaderModule(m_context.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_context.getDevice(), vertShaderModule, nullptr);
}

void ProceduralSkybox::render(VkCommandBuffer commandBuffer, const glm::mat4& view, const glm::mat4& projection) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    // Remove translation from view matrix
    glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
    SkyboxPushConstants pc;
    pc.viewProj = projection * viewNoTranslation;

    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
        0, sizeof(SkyboxPushConstants), &pc);

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);
}

} // namespace eden
