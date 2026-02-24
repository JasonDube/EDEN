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
#include "../AI/CleanerBot.hpp"
#include "../AI/ImageBot.hpp"
#include "../Forge/ForgeRoom.hpp"

namespace eden {

class FilesystemBrowser {
public:
    struct SiloConfig {
        glm::vec4 wallColor{0.15f, 0.15f, 0.18f, 1.0f};
        glm::vec4 columnColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

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
    SiloConfig& siloConfig() { return m_siloConfig; }
    CleanerBot* getCleanerBot() { return m_cleanerBot.isSpawned() ? &m_cleanerBot : nullptr; }
    ImageBot* getImageBot() { return m_imageBot.isSpawned() ? &m_imageBot : nullptr; }
    ForgeRoom* getForgeRoom() { return m_forgeRoom.isSpawned() ? &m_forgeRoom : nullptr; }

    // Emanation rendering — call from render loop after scene objects
    // Returns line pairs and color (with alpha) for each batch
    struct EmanationBatch {
        std::vector<glm::vec3> lines;
        glm::vec4 color;  // RGBA with alpha for fade
    };
    std::vector<EmanationBatch> getEmanationRenderData() const;

    // Image focus: reload at full resolution with correct aspect ratio
    void focusImage(SceneObject* panel);
    void unfocusImage();
    bool isImageFocused() const { return m_imageFocus.active; }

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
        Model3D,
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
    SiloConfig m_siloConfig;

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
    size_t m_videoUpdateIndex = 0;  // round-robin index for staggered updates
    static constexpr float VIDEO_FRAME_INTERVAL = 0.5f; // seconds between frames
    static constexpr int MAX_CONCURRENT_EXTRACTIONS = 4;

    // Track background threads so we can cancel on clear
    std::shared_ptr<std::atomic<bool>> m_cancelExtraction;
    std::vector<std::thread> m_extractionThreads;

    // Queued videos waiting for an extraction slot
    struct PendingExtraction {
        std::string filePath;
        std::string cachePath;
        std::shared_ptr<std::vector<std::vector<unsigned char>>> outFrames;
        std::shared_ptr<std::atomic<bool>> ready;
    };
    std::vector<PendingExtraction> m_pendingExtractions;

    void cancelAllExtractions();

    // Model turntable animation state
    struct ModelSpin {
        SceneObject* obj = nullptr;
        float baseYaw = 0.0f;   // original yaw from gallery wall
        float angle = 0.0f;     // current spin angle (degrees)
    };
    std::vector<ModelSpin> m_modelSpins;
    static constexpr float MODEL_SPIN_SPEED = 30.0f; // degrees per second

    // Image focus state
    static constexpr int FOCUS_MAX_SIZE = 2048;

    struct ImageFocusState {
        SceneObject* panel = nullptr;
        uint32_t bufferHandle = 0;
        glm::vec3 originalScale{1.0f};
        bool active = false;
    };
    ImageFocusState m_imageFocus;

    // Cleaner Bot NPC
    CleanerBot m_cleanerBot;

    // Image Bot NPC
    ImageBot m_imageBot;

    // Forge Room (for assets/models/ directory)
    ForgeRoom m_forgeRoom;

    // Folder visit tracking (attention system)
    std::unordered_map<std::string, int> m_folderVisits;
    void loadFolderVisits();
    void saveFolderVisits();
    void recordVisit(const std::string& path);
    float getVisitGlow(const std::string& path) const;  // 0.0 to 1.0

    // Emanation: wireframe squares expanding outward from hot folders
    struct Emanation {
        glm::vec3 center;       // folder object center
        glm::vec3 halfExtent;   // half-size of the folder cube face
        glm::vec3 forward;      // outward direction (away from gallery wall)
        glm::vec3 up;           // up axis of the face
        glm::vec3 right;        // right axis of the face
        float intensity;        // 0..1 based on visit frequency
        float timer = 0.0f;     // time accumulator for spawning rings
    };
    std::vector<Emanation> m_emanations;

    struct EmanationRing {
        size_t emanationIdx;    // which emanation source
        float age = 0.0f;       // seconds since spawn
    };
    std::vector<EmanationRing> m_emanationRings;

    static constexpr float EMANATION_MAX_DIST = 4.0f;   // meters before fade-out
    static constexpr float EMANATION_SPEED = 2.5f;       // meters per second
    static constexpr float EMANATION_SPAWN_INTERVAL = 0.6f; // seconds between rings

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

    // Basement foundation
    static constexpr float BASEMENT_HEIGHT = 16.0f;
    static constexpr float BASEMENT_SIZE = 44.0f;  // side length (wider than silo for a massive foundation)
    void spawnBasement(const glm::vec3& center, float baseY);
};

} // namespace eden
