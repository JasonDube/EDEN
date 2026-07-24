#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

namespace eden {

class VulkanContext;

struct ParticleVertex {
    glm::vec3 position;
    glm::vec2 uv;
    float alpha;
    glm::vec3 color;
};

class ParticleRenderer {
public:
    ParticleRenderer(VulkanContext& context, VkRenderPass renderPass, VkExtent2D extent);
    ~ParticleRenderer();

    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    void addEmitter(const glm::vec3& position);
    // A directed spray. color/sizeScale/gravity let a caller make something other
    // than the default gray water — e.g. big blue no-gravity ship-engine exhaust.
    void addDirectedEmitter(const glm::vec3& position, const glm::vec3& direction, float speed = 2.0f,
                            const glm::vec3& color = glm::vec3(0.3f, 0.3f, 0.35f),
                            float sizeScale = 1.0f, bool gravity = true,
                            float emitRadius = 0.03f, float spawnBoost = 1.0f);
    void removeEmitter(const glm::vec3& position, float epsilon = 0.5f);
    void clearEmitters();
    void clearDirectedEmitters();

    void update(float deltaTime, const glm::vec3& cameraPos,
                const glm::vec3& cameraRight, const glm::vec3& cameraUp);

    void render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj);

    void recreatePipeline(VkRenderPass renderPass, VkExtent2D extent);

private:
    struct Particle {
        glm::vec3 position;
        glm::vec3 velocity;
        float lifetime;
        float age;
        float size;
        bool isWater = false;  // true = affected by gravity, false = smoke (floats up)
        glm::vec3 color{0.3f, 0.3f, 0.35f}; // billboard tint (default smoke gray)
        float sizeScale = 1.0f;             // multiplies the final billboard size
    };

    struct Emitter {
        glm::vec3 position;
    };

    struct DirectedEmitter {
        glm::vec3 position;
        glm::vec3 direction;
        float speed;
        glm::vec3 color{0.3f, 0.3f, 0.35f};
        float sizeScale = 1.0f;
        bool gravity = true;
        float emitRadius = 0.03f;  // spawn disc radius (widens the plume)
        float spawnBoost = 1.0f;   // particles per spawn tick (density for big plumes)
    };

    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createBuffers();

    static constexpr uint32_t MAX_PARTICLES = 512;
    static constexpr float SPAWN_RATE = 30.0f; // particles per second per emitter

    VulkanContext& m_context;

    // Pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // Vertex buffer (persistent mapped)
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    void* m_mappedVertexMemory = nullptr;

    // Index buffer (static)
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

    // State
    std::vector<Particle> m_particles;
    std::vector<Emitter> m_emitters;
    std::vector<DirectedEmitter> m_directedEmitters;
    float m_spawnAccumulator = 0.0f;
    uint32_t m_liveQuadCount = 0;
};

} // namespace eden
