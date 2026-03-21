#include "ParticleRenderer.hpp"
#include "VulkanContext.hpp"
#include "Buffer.hpp"
#include "PipelineBuilder.hpp"
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace eden {

struct ParticlePushConstants {
    glm::mat4 viewProj;
};

ParticleRenderer::ParticleRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent)
    : m_context(context)
{
    m_particles.reserve(MAX_PARTICLES);
    createBuffers();
    createPipeline(renderPass, extent);
}

ParticleRenderer::~ParticleRenderer() {
    VkDevice device = m_context.getDevice();

    if (m_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_mappedVertexMemory)
        vkUnmapMemory(device, m_vertexMemory);
    if (m_vertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
    if (m_vertexMemory != VK_NULL_HANDLE) {
        Buffer::trackVramFreeHandle(m_vertexMemory);
        vkFreeMemory(device, m_vertexMemory, nullptr);
    }
    if (m_indexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
    if (m_indexMemory != VK_NULL_HANDLE) {
        Buffer::trackVramFreeHandle(m_indexMemory);
        vkFreeMemory(device, m_indexMemory, nullptr);
    }
}

void ParticleRenderer::createBuffers() {
    VkDevice device = m_context.getDevice();

    // Vertex buffer: 4 vertices per particle, persistent mapped
    {
        VkDeviceSize bufferSize = sizeof(ParticleVertex) * 4 * MAX_PARTICLES;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device, &bufferInfo, nullptr, &m_vertexBuffer);

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, m_vertexBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = m_context.findMemoryType(
            memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        vkAllocateMemory(device, &allocInfo, nullptr, &m_vertexMemory);
        Buffer::trackVramAllocHandle(m_vertexMemory, static_cast<int64_t>(memReqs.size));
        vkBindBufferMemory(device, m_vertexBuffer, m_vertexMemory, 0);
        vkMapMemory(device, m_vertexMemory, 0, bufferSize, 0, &m_mappedVertexMemory);
    }

    // Index buffer: 6 indices per particle (2 triangles), static
    {
        VkDeviceSize bufferSize = sizeof(uint32_t) * 6 * MAX_PARTICLES;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device, &bufferInfo, nullptr, &m_indexBuffer);

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, m_indexBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = m_context.findMemoryType(
            memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        vkAllocateMemory(device, &allocInfo, nullptr, &m_indexMemory);
        Buffer::trackVramAllocHandle(m_indexMemory, static_cast<int64_t>(memReqs.size));
        vkBindBufferMemory(device, m_indexBuffer, m_indexMemory, 0);

        // Fill with quad index pattern: 0,1,2, 0,2,3, 4,5,6, 4,6,7, ...
        void* mapped;
        vkMapMemory(device, m_indexMemory, 0, bufferSize, 0, &mapped);
        auto* indices = static_cast<uint32_t*>(mapped);
        for (uint32_t i = 0; i < MAX_PARTICLES; i++) {
            uint32_t base = i * 4;
            uint32_t idx = i * 6;
            indices[idx + 0] = base + 0;
            indices[idx + 1] = base + 1;
            indices[idx + 2] = base + 2;
            indices[idx + 3] = base + 0;
            indices[idx + 4] = base + 2;
            indices[idx + 5] = base + 3;
        }
        vkUnmapMemory(device, m_indexMemory);
    }
}

void ParticleRenderer::createPipeline(VkRenderPass renderPass, VkExtent2D extent) {
    auto result = PipelineBuilder(m_context)
        .setShaders("shaders/particle.vert.spv", "shaders/particle.frag.spv")
        .setVertexBinding(0, sizeof(ParticleVertex))
        .addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ParticleVertex, position))
        .addVertexAttribute(0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(ParticleVertex, uv))
        .addVertexAttribute(0, 2, VK_FORMAT_R32_SFLOAT, offsetof(ParticleVertex, alpha))
        .setPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setCullMode(VK_CULL_MODE_NONE)
        .enableAlphaBlending()
        .setDepthTest(true, false) // read depth, don't write
        .setPushConstantSize(sizeof(ParticlePushConstants))
        .build(renderPass, extent);

    m_pipeline = result.pipeline;
    m_pipelineLayout = result.layout;
}

void ParticleRenderer::recreatePipeline(VkRenderPass renderPass, VkExtent2D extent) {
    VkDevice device = m_context.getDevice();
    if (m_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    createPipeline(renderPass, extent);
}

void ParticleRenderer::addEmitter(const glm::vec3& position) {
    m_emitters.push_back({position});
}

void ParticleRenderer::removeEmitter(const glm::vec3& position, float epsilon) {
    m_emitters.erase(
        std::remove_if(m_emitters.begin(), m_emitters.end(),
            [&](const Emitter& e) {
                return glm::length(e.position - position) < epsilon;
            }),
        m_emitters.end()
    );
}

void ParticleRenderer::clearEmitters() {
    m_emitters.clear();
}

void ParticleRenderer::addDirectedEmitter(const glm::vec3& position, const glm::vec3& direction, float speed) {
    m_directedEmitters.push_back({position, glm::normalize(direction), speed});
}

void ParticleRenderer::clearDirectedEmitters() {
    m_directedEmitters.clear();
}

static float randFloat(float lo, float hi) {
    return lo + static_cast<float>(rand()) / RAND_MAX * (hi - lo);
}

void ParticleRenderer::update(float deltaTime, const glm::vec3& cameraPos,
                               const glm::vec3& cameraRight, const glm::vec3& cameraUp) {
    // Spawn new particles
    if (!m_emitters.empty()) {
        m_spawnAccumulator += deltaTime * SPAWN_RATE;
        while (m_spawnAccumulator >= 1.0f && m_particles.size() < MAX_PARTICLES) {
            m_spawnAccumulator -= 1.0f;
            // Pick a random emitter
            const auto& emitter = m_emitters[rand() % m_emitters.size()];

            Particle p;
            p.position = emitter.position + glm::vec3(randFloat(-0.15f, 0.15f), 0.0f, randFloat(-0.15f, 0.15f));
            p.velocity = glm::vec3(randFloat(-0.3f, 0.3f), randFloat(1.2f, 1.8f), randFloat(-0.3f, 0.3f));
            p.lifetime = randFloat(2.0f, 3.0f);
            p.age = 0.0f;
            p.size = randFloat(0.2f, 0.35f);
            m_particles.push_back(p);
        }
    }

    // Spawn water particles from directed emitters
    if (!m_directedEmitters.empty()) {
        static float directedAccum = 0.0f;
        directedAccum += deltaTime * SPAWN_RATE * 1.5f;  // Slightly faster spawn for water
        while (directedAccum >= 1.0f && m_particles.size() < MAX_PARTICLES) {
            directedAccum -= 1.0f;
            const auto& emitter = m_directedEmitters[rand() % m_directedEmitters.size()];

            // Build perpendicular vectors for spray spread
            glm::vec3 perp1, perp2;
            glm::vec3 up = (std::abs(emitter.direction.y) < 0.9f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
            perp1 = glm::normalize(glm::cross(emitter.direction, up));
            perp2 = glm::normalize(glm::cross(emitter.direction, perp1));

            Particle p;
            p.position = emitter.position + perp1 * randFloat(-0.03f, 0.03f) + perp2 * randFloat(-0.03f, 0.03f);
            float spread = 0.15f;
            p.velocity = emitter.direction * emitter.speed
                       + perp1 * randFloat(-spread, spread)
                       + perp2 * randFloat(-spread, spread);
            p.lifetime = randFloat(0.8f, 1.5f);
            p.age = 0.0f;
            p.size = randFloat(0.06f, 0.12f);
            p.isWater = true;
            m_particles.push_back(p);
        }
    }

    // Update existing particles
    for (auto& p : m_particles) {
        p.age += deltaTime;
        // Only water particles are affected by gravity — smoke floats up
        if (p.isWater) {
            p.velocity.y -= 3.0f * deltaTime;
        }
        p.position += p.velocity * deltaTime;
        // Slow down horizontal drift over time
        p.velocity.x *= 0.98f;
        p.velocity.z *= 0.98f;
    }

    // Remove dead particles
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
            [](const Particle& p) { return p.age >= p.lifetime; }),
        m_particles.end()
    );

    // Sort back-to-front for correct alpha blending
    std::sort(m_particles.begin(), m_particles.end(),
        [&cameraPos](const Particle& a, const Particle& b) {
            float da = glm::dot(a.position - cameraPos, a.position - cameraPos);
            float db = glm::dot(b.position - cameraPos, b.position - cameraPos);
            return da > db; // far first
        });

    // Build billboard quads
    m_liveQuadCount = static_cast<uint32_t>(m_particles.size());
    if (m_liveQuadCount == 0) return;

    auto* verts = static_cast<ParticleVertex*>(m_mappedVertexMemory);
    for (uint32_t i = 0; i < m_liveQuadCount; i++) {
        const auto& p = m_particles[i];
        float t = p.age / p.lifetime; // 0..1
        float alpha = (1.0f - t) * 0.6f; // fade from 0.6 to 0
        float size = p.size + t * 0.5f; // grow over lifetime

        glm::vec3 right = cameraRight * size;
        glm::vec3 up = cameraUp * size;

        uint32_t base = i * 4;
        // Bottom-left
        verts[base + 0] = {p.position - right - up, {0.0f, 0.0f}, alpha};
        // Bottom-right
        verts[base + 1] = {p.position + right - up, {1.0f, 0.0f}, alpha};
        // Top-right
        verts[base + 2] = {p.position + right + up, {1.0f, 1.0f}, alpha};
        // Top-left
        verts[base + 3] = {p.position - right + up, {0.0f, 1.0f}, alpha};
    }
}

void ParticleRenderer::render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj) {
    if (m_liveQuadCount == 0) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    ParticlePushConstants pc;
    pc.viewProj = viewProj;
    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(ParticlePushConstants), &pc);

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(commandBuffer, m_liveQuadCount * 6, 1, 0, 0, 0);
}

} // namespace eden
