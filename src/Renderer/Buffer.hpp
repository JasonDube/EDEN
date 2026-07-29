#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <atomic>
#include <unordered_map>
#include <mutex>

namespace eden {

class VulkanContext;

class Buffer {
public:
    Buffer(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags properties);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    VkBuffer getHandle() const { return m_buffer; }
    VkDeviceSize getSize() const { return m_size; }

    void* map();
    void unmap();
    void upload(const void* data, VkDeviceSize size);

    static void copy(VulkanContext& context, Buffer& src, Buffer& dst, VkDeviceSize size);
    // Record a copy into an existing command buffer (no submit) — for batching
    // many copies into one submit. Caller must keep `src` alive until the submit
    // completes and submit/wait the command buffer itself.
    static void recordCopy(VkCommandBuffer cmd, Buffer& src, Buffer& dst, VkDeviceSize size);

    // VRAM usage tracking (across all Vulkan allocations)
    static std::atomic<int64_t> s_vramUsedBytes;
    static std::unordered_map<VkDeviceMemory, int64_t> s_vramAllocSizes;
    static std::mutex s_vramMutex;

    static void trackVramAlloc(int64_t bytes) { s_vramUsedBytes += bytes; }
    static void trackVramFree(int64_t bytes)  { s_vramUsedBytes -= bytes; }
    static int64_t getVramUsedBytes() { return s_vramUsedBytes.load(); }

    // Track by handle — records size on alloc, looks it up on free
    static void trackVramAllocHandle(VkDeviceMemory mem, int64_t bytes);
    static void trackVramFreeHandle(VkDeviceMemory mem);

private:
    VulkanContext& m_context;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkDeviceSize m_size;
    void* m_mapped = nullptr;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

// Manages multiple mesh buffers
class BufferManager {
public:
    BufferManager(VulkanContext& context);
    ~BufferManager();

    struct MeshBuffers {
        std::unique_ptr<Buffer> vertexBuffer;
        std::unique_ptr<Buffer> indexBuffer;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        // Kept for meshes that are refilled rather than rebuilt. A staging buffer
        // allocated fresh every frame is the same driver allocation the device
        // buffers were costing, moved. Only ever populated by updateMeshBuffers;
        // meshes created once and left alone never allocate these.
        std::unique_ptr<Buffer> vertexStaging;
        std::unique_ptr<Buffer> indexStaging;
    };

    uint32_t createMeshBuffers(const void* vertices, uint32_t vertexCount, size_t vertexSize,
                               const uint32_t* indices = nullptr, uint32_t indexCount = 0);

    MeshBuffers* getMeshBuffers(uint32_t handle);
    void destroyMeshBuffers(uint32_t handle);

    // Refill an existing mesh IN PLACE, keeping its device allocations whenever
    // they are still big enough.
    //
    // This is what makes geometry that changes every frame affordable. Destroying
    // and recreating a mesh per frame -- which is the only thing createMeshBuffers
    // offers -- means two device allocations and two frees every frame per mesh,
    // and on the non-batched path two vkQueueWaitIdle stalls with them. A creature
    // whose legs are a different shape every frame does not need new buffers; it
    // needs the same buffers with new numbers in them.
    //
    // Grows with headroom rather than to fit, so a mesh that wobbles a few
    // vertices either side of its size stops reallocating after the first frame.
    // False means the handle is not one of ours and the caller should create.
    bool updateMeshBuffers(uint32_t handle, const void* vertices, uint32_t vertexCount,
                           size_t vertexSize, const uint32_t* indices = nullptr,
                           uint32_t indexCount = 0);

    // Batched upload: between begin/endBatch, createMeshBuffers records its copies
    // into ONE command buffer instead of submitting + full-GPU-waiting per buffer.
    // Cuts thousands of vkQueueWaitIdle stalls (e.g. 2048 → a few) when loading a
    // 1024-chunk level. Staging buffers are held alive until endBatch submits once.
    void beginBatch();
    void endBatch();

    // Whether a batch is already open. Callers that want to batch their own work
    // have to ask, because beginBatch on an open batch is a no-op and the matching
    // endBatch would then submit somebody else's batch early -- a level load's,
    // mid-load.
    bool batching() const { return m_batching; }

private:
    VulkanContext& m_context;
    std::vector<std::unique_ptr<MeshBuffers>> m_meshBuffers;
    std::vector<uint32_t> m_freeHandles;

    bool m_batching = false;
    VkCommandBuffer m_batchCmd = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Buffer>> m_batchStaging;   // kept alive until endBatch
};

} // namespace eden
