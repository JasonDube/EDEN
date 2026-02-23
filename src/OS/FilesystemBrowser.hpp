#pragma once

#include "Renderer/ModelRenderer.hpp"
#include "Editor/SceneObject.hpp"
#include <eden/Terrain.hpp>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>

namespace eden {

class FilesystemBrowser {
public:
    FilesystemBrowser() = default;
    ~FilesystemBrowser();

    void init(ModelRenderer* modelRenderer,
              std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
              Terrain* terrain);

    // Queue a navigation (deferred to next frame for GPU safety)
    void navigate(const std::string& path);

    // Called from update() — performs pending clear + scan + spawn
    void processNavigation();

    // Animate video preview textures — call every frame with deltaTime
    void updateAnimations(float deltaTime);

    // Remove all filesystem objects and free GPU resources
    void clearFilesystemObjects();

    bool isActive() const { return m_active; }
    const std::string& getCurrentPath() const { return m_currentPath; }
    void setSpawnOrigin(const glm::vec3& origin) { m_spawnOrigin = origin; }
    const glm::vec3& getSpawnOrigin() const { return m_spawnOrigin; }

    // Spawn a file object directly onto a wall slot (for paste-in-place)
    // wallPos/wallScale/wallYawDeg come from the selected wall's transform
    void spawnFileAtWall(const std::string& filePath,
                         const glm::vec3& wallPos,
                         const glm::vec3& wallScale,
                         float wallYawDeg);

private:
    enum class FileCategory {
        Folder,
        Image,
        Video,
        Text,
        Executable,
        SourceCode,
        Other
    };

    struct EntryInfo {
        std::string name;
        std::string fullPath;
        FileCategory category;
    };

    FileCategory categorize(const std::filesystem::directory_entry& entry) const;

    void spawnObjects(const std::string& dirPath);

    // Create a single filesystem object (shared by grid and gallery layouts)
    void spawnOneObject(const EntryInfo& entry, size_t index,
                        const glm::vec3& pos, const glm::vec3& scale,
                        float yawDegrees = 0.0f);

    // Gallery room layout — stacks groups of entries on ring levels
    // Each group gets its own level(s), no mixed types per row
    // Returns the next available level index
    int spawnGalleryRing(const std::vector<EntryInfo>& items,
                         const glm::vec3& center, float baseY,
                         int startLevel);

    static glm::vec4 colorForCategory(FileCategory cat);

    // Render a filename label onto a 256x256 RGBA buffer
    void renderLabel(std::vector<unsigned char>& pixels,
                     const std::string& filename,
                     FileCategory category,
                     const glm::vec4& color);

    // Disk cache helpers
    static std::string getCachePath(const std::string& videoPath);
    static bool loadCachedFrames(const std::string& cachePath,
                                 std::vector<std::vector<unsigned char>>& frames,
                                 int frameSize);
    static bool saveCachedFrames(const std::string& cachePath,
                                 const std::vector<std::vector<unsigned char>>& frames,
                                 int frameSize);

    // Background thread: extract frames from video via single ffmpeg pipe
    static void extractionWorker(
        std::string filePath, std::string cachePath,
        int labelSize, int maxFrames,
        std::shared_ptr<std::vector<std::vector<unsigned char>>> outFrames,
        std::shared_ptr<std::atomic<bool>> ready,
        std::shared_ptr<std::atomic<bool>> cancelled);

    ModelRenderer* m_modelRenderer = nullptr;
    std::vector<std::unique_ptr<SceneObject>>* m_sceneObjects = nullptr;
    Terrain* m_terrain = nullptr;

    std::string m_currentPath;
    std::string m_pendingPath;
    bool m_pendingNavigation = false;
    bool m_active = false;
    bool m_spawnFailed = false;

    glm::vec3 m_spawnOrigin{0.0f};

    // Video animation state
    struct VideoAnimation {
        uint32_t bufferHandle;
        std::vector<std::vector<unsigned char>> frames; // RGBA pixel buffers
        int currentFrame = 0;
        float timer = 0.0f;

        // Background extraction state
        std::shared_ptr<std::vector<std::vector<unsigned char>>> pendingFrames;
        std::shared_ptr<std::atomic<bool>> ready;
        bool loaded = false;
    };
    std::vector<VideoAnimation> m_videoAnimations;
    static constexpr float VIDEO_FRAME_INTERVAL = 0.5f; // seconds between frames

    // Track background threads so we can cancel on clear
    std::shared_ptr<std::atomic<bool>> m_cancelExtraction;
    std::vector<std::thread> m_extractionThreads;

    void cancelAllExtractions();

    static constexpr int MAX_ENTRIES = 200;
    static constexpr int MAX_VIDEO_FRAMES = 20;
    static constexpr float GRID_SPACING = 4.0f;
    static constexpr int GRID_COLUMNS = 10;
    static constexpr int LABEL_SIZE = 256;

    // Gallery room
    static constexpr int GALLERY_SIDES = 20;
    static constexpr float GALLERY_RADIUS = 20.0f;
    static constexpr float GALLERY_RING_GAP = 12.0f; // radius decrease per ring
    static constexpr float GALLERY_WALL_HEIGHT = 4.0f;
};

} // namespace eden
