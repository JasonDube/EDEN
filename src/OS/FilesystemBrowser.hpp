#pragma once

#include "Renderer/ModelRenderer.hpp"
#include "Editor/SceneObject.hpp"
#include <eden/Terrain.hpp>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <set>
#include <thread>
#include <mutex>
#include <atomic>
#include "../AI/CleanerBot.hpp"
#include "../AI/ImageBot.hpp"
#include "../AI/CullSession.hpp"
#include "../Forge/ForgeRoom.hpp"
#include "PlatformGrid.hpp"

namespace eden {

class FilesystemBrowser {
public:
    enum class SiloSize { Small = 0, Medium = 1, Large = 2 };

    struct SiloConfig {
        glm::vec4 wallColor{0.15f, 0.15f, 0.18f, 1.0f};
        glm::vec4 columnColor{1.0f, 1.0f, 1.0f, 1.0f};
        SiloSize size = SiloSize::Large;
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
    void setPlayerPosition(const glm::vec3& pos) { m_playerPos = pos; }

    // Remove all filesystem objects and free GPU resources
    void clearFilesystemObjects();

    bool isActive() const { return m_active; }
    bool hasPendingNavigation() const { return m_pendingNavigation; }
    const std::string& getCurrentPath() const { return m_currentPath; }
    void setSpawnOrigin(const glm::vec3& origin) { m_spawnOrigin = origin; m_basementBaseYValid = false; }
    const glm::vec3& getSpawnOrigin() const { return m_spawnOrigin; }
    SiloConfig& siloConfig() { return m_siloConfig; }
    CleanerBot* getCleanerBot() { return m_cleanerBot.isSpawned() ? &m_cleanerBot : nullptr; }
    ImageBot* getImageBot() { return m_imageBot.isSpawned() ? &m_imageBot : nullptr; }
    ForgeRoom* getForgeRoom() { return m_forgeRoom.isSpawned() ? &m_forgeRoom : nullptr; }
    CullSession* getCullSession() { return &m_cullSession; }
    PlatformGridBuilder& getPlatformGrid() { return m_platformGrid; }
    float getPlatformY() const { return m_platformY; }
    float getRingBaseY() const { return m_ringBaseY; }
    float getBasementHeight() const { return basementHeight(); }
    float getBasementFloorY() const { return m_basementBaseY - basementHeight(); }
    float getCeilingTopY() const { return m_basementBaseY - 0.5f; } // top surface of basement ceiling slab
    bool hasVoid() const { return m_hasVoid; }
    const glm::vec3& getVoidCenter() const { return m_voidCenter; }

    // Emanation rendering — call from render loop after scene objects
    // Returns line pairs and color (with alpha) for each batch
    struct EmanationBatch {
        std::vector<glm::vec3> lines;
        glm::vec4 color;  // RGBA with alpha for fade
    };
    std::vector<EmanationBatch> getEmanationRenderData() const;

    // Void wireframe — light wireframe around every cube in the structure
    std::vector<glm::vec3> getVoidWireframeLines() const;

    // Image focus: reload at full resolution with correct aspect ratio
    void focusImage(SceneObject* panel);
    void unfocusImage();
    bool isImageFocused() const { return m_imageFocus.active; }

    // SAM2 segmentation: isolate subject in focused image
    void segmentImage(int imgX, int imgY);
    void segmentImageBox(int x1, int y1, int x2, int y2);
    void pollSegmentation();
    void undoSegmentation();
    void cancelSegmentation();
    bool isSegmenting() const { return m_segmentation.processing; }
    bool hasSegmentation() const { return m_segmentation.active; }
    void eraseSegmentAt(int imgX, int imgY, int radius);
    void restoreSegmentAt(int imgX, int imgY, int radius);
    // Save segmented image as PNG to the forge (assets/models) directory
    // Returns the saved file path, or empty string on failure
    std::string saveSegmentationToForge();
    std::string getSegmentationStatus() const {
        if (!m_segmentation.statusMutex || !m_segmentation.statusText) return "";
        std::lock_guard<std::mutex> lk(*m_segmentation.statusMutex);
        return *m_segmentation.statusText;
    }
    std::pair<int,int> getFocusedImageDimensions() const { return {m_imageFocus.focusedWidth, m_imageFocus.focusedHeight}; }
    std::string getFocusedImagePath() const {
        if (!m_imageFocus.active || !m_imageFocus.panel) return "";
        std::string t = m_imageFocus.panel->getTargetLevel();
        return (t.size() > 5 && t.substr(0, 5) == "fs://") ? t.substr(5) : "";
    }

    // Parse filename prefix to determine frame type and set WallFrame fields
    static void parseFrameType(const std::string& filename, WallFrame& frame);

    // Spawn a file object directly onto a wall slot (for paste-in-place)
    // wallPos/wallScale/wallYawDeg come from the selected wall's transform
    void spawnFileAtWall(const std::string& filePath,
                         const glm::vec3& wallPos,
                         const glm::vec3& wallScale,
                         float wallYawDeg);

    // Spawn a file object directly at a file_slot CP position (no gallery offsets)
    void spawnFileAtSlot(const std::string& filePath,
                         const glm::vec3& pos,
                         const glm::vec3& scale,
                         float yawDeg);

    // Spawn a file object onto a wall frame (uses frame's exact position/size/yaw)
    void spawnFileAtFrame(const std::string& filePath, SceneObject* frame,
                          const std::string& texturePath = "");

    // Re-spawn files for all occupied wall frames (call after spawnFrameObjects)
    void respawnFrameFiles();

    // Spawn a file as a functional freestanding object at a world position (Ctrl+Number)
    void spawnFileAtPosition(const std::string& filePath, const glm::vec3& position);

    void spawnBasement(const glm::vec3& center, float baseY);

    // App launcher ring (level -1 below home silo)
    void spawnAppRing(const glm::vec3& center, float baseY);
    void spawnAppSpace(const std::string& appPath, const glm::vec3& center, float baseY);

    // Paths currently held in inventory — excluded from silo spawning
    void setExcludedPaths(const std::set<std::string>& paths) { m_excludedPaths = paths; }

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

    // Menger sponge void — fractal overflow space below the basement
    void spawnVoid(const glm::vec3& center, float topY,
                   const std::vector<EntryInfo>& overflowItems);

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
    float m_basementBaseY = 0.0f;   // cached baseY so silo stays aligned with persistent basement
    bool m_basementBaseYValid = false;
    SiloConfig m_siloConfig;
    std::set<std::string> m_excludedPaths; // inventory items to skip during spawn

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
        float sideAngle = 0.0f; // arrow key rotation on secondary axis (degrees)
        bool paused = false;
    };
    std::vector<ModelSpin> m_modelSpins;
    static constexpr float MODEL_SPIN_SPEED = 30.0f; // degrees per second
public:
    // Toggle spin on/off for a given scene object; returns true if it was found
    bool toggleSpin(SceneObject* obj);
    // Rotate a paused spin by the given degrees (e.g. 45 or -45); returns true if handled
    bool rotateSpinIncrement(SceneObject* obj, float degrees);
    // Check if a scene object has a spin entry (for suppressing movement keys)
    bool hasSpinEntry(SceneObject* obj) const;
    // Remove a scene object from the spin list (call before erasing the object)
    void removeModelSpin(SceneObject* obj);
    // Remove a scene object from the void file tracking (call before erasing the object)
    void removeVoidFileObj(SceneObject* obj);
private:

    // Image focus state
    static constexpr int FOCUS_MAX_SIZE = 2048;

    struct ImageFocusState {
        SceneObject* panel = nullptr;
        uint32_t bufferHandle = 0;
        glm::vec3 originalScale{1.0f};
        bool active = false;
        int focusedWidth = 0;
        int focusedHeight = 0;
    };
    ImageFocusState m_imageFocus;

    // SAM2 segmentation state
    struct SegmentationState {
        bool active = false;        // segmented texture currently displayed
        bool processing = false;    // background thread running
        std::string imagePath;      // source image path
        std::shared_ptr<std::atomic<bool>> done;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::string outputPath;     // temp file for result PNG
        std::thread workerThread;
        std::shared_ptr<std::mutex> statusMutex;
        std::shared_ptr<std::string> statusText;
        std::vector<unsigned char> rgbaData;     // raw RGBA pixels (editable)
        std::vector<unsigned char> originalData; // original full image RGBA (for restore brush)
        int rgbaW = 0, rgbaH = 0;               // dimensions of rgbaData/originalData
    };
    SegmentationState m_segmentation;

    // Cleaner Bot NPC
    CleanerBot m_cleanerBot;

    // Image Bot NPC
    ImageBot m_imageBot;

    // Forge Room (for assets/models/ directory)
    ForgeRoom m_forgeRoom;

    // Cull Session (binary object culling workflow)
    CullSession m_cullSession;

    // Platform grid (user-customizable walls on top of silo)
    PlatformGridBuilder m_platformGrid;
    float m_platformY = 100.0f;  // Y of the platform (top of silo)
    float m_ringBaseY = 100.0f;  // Y of the bottom of gallery rings (silo floor)

    // Void (Menger sponge) — spawned at a distant location, accessed via portal door
    bool m_hasVoid = false;
    glm::vec3 m_voidCenter{0.0f};  // center of the void structure (for teleportation)
    glm::vec3 m_voidRotSpeed{0.0f}; // random rotation speed (degrees/sec) per axis
    float m_voidRotAngleX = 0.0f;
    float m_voidRotAngleY = 0.0f;
    float m_voidRotAngleZ = 0.0f;
    struct VoidFileObj {
        SceneObject* obj;
        glm::vec3 localOffset; // position relative to voidCenter
        glm::vec3 baseScale{1.0f}; // original scale for pulse animation
    };
    std::vector<VoidFileObj> m_voidFileObjs;
    struct VoidCubeInfo {
        glm::vec3 localCenter; // relative to voidCenter
        float halfSize;
    };
    std::vector<VoidCubeInfo> m_voidCubeInfos; // all cube positions for wireframe
    glm::vec3 m_playerPos{0.0f};
    float m_voidPulseTimer = 0.0f; // timer for selection pulse animation

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

    static constexpr int MAX_ENTRIES = 1000;
    static constexpr int MAX_VIDEO_FRAMES = 20;
    static constexpr float GRID_SPACING = 16.0f;
    static constexpr int GRID_COLUMNS = 10;
    static constexpr int LABEL_SIZE = 256;

    // Gallery room — dimensions vary by silo size
    static constexpr float GALLERY_WALL_HEIGHT = 16.0f;

    int gallerySides() const {
        switch (m_siloConfig.size) {
            case SiloSize::Small:  return 5;
            case SiloSize::Medium: return 10;
            default:               return 20;
        }
    }
    float galleryRadius() const {
        switch (m_siloConfig.size) {
            case SiloSize::Small:  return 20.0f;
            case SiloSize::Medium: return 40.0f;
            default:               return 80.0f;
        }
    }
    float basementSize() const {
        switch (m_siloConfig.size) {
            case SiloSize::Small:  return 44.0f;
            case SiloSize::Medium: return 88.0f;
            default:               return 176.0f;
        }
    }
    float basementHeight() const {
        switch (m_siloConfig.size) {
            case SiloSize::Small:  return 16.0f;
            case SiloSize::Medium: return 32.0f;
            default:               return 64.0f;
        }
    }
};

} // namespace eden
