#pragma once

#include "Renderer/ModelRenderer.hpp"
#include "Editor/SceneObject.hpp"
#include <string>
#include <vector>
#include <memory>

namespace eden {

// Frame type determined by filename prefix convention
enum class FrameType {
    None,       // plain frame (no prefix)
    Machine,    // mach_
    Input,      // inp_
    Output,     // out_
    Button,     // btn_
    Checkbox,   // ckbx_
    Slider,     // sldr_
    Log,        // log_
    Relay       // "relay" anywhere in filename
};

struct WallFrame {
    float worldX = 0, worldY = 0, worldZ = 0; // frame center position
    int size = 4;                               // 1..16 meters (square)
    float normalX = 0, normalZ = 1;            // outward face normal (which side of wall)
    float yawDeg = 0;                          // rotation to face outward
    std::string filePath;                       // "" = empty, path = occupied
    std::string texturePath;                    // custom door image path (optional)
    FrameType frameType = FrameType::None;
    std::string machineName;                    // e.g. "hunyuan3d" for mach_ frames
    std::string paramName;                      // e.g. "image" for inp_image.png
    bool spinPaused = false;                    // true = model rotation stopped by user
    float spinAngle = 0.0f;                     // persisted rotation angle (degrees)
};

struct WireConnection {
    int fromFrame = -1;   // index into PlatformGrid::frames
    int toFrame = -1;
    std::string fromCP;   // control point name on source widget (e.g. "wire_out_0")
    std::string toCP;     // control point name on dest widget (e.g. "wire_in_0")
};

struct FileSlotAssignment {
    int frameIndex = -1;        // which frame the widget is on
    std::string cpName;         // e.g. "file_slot_0A" (the A corner identifies the slot)
    std::string filePath;       // assigned file path
};

struct BrushWall {
    float posX = 0, posY = 0, posZ = 0;
    float scaleX = 1, scaleY = 8, scaleZ = 1;
    float yawDeg = 0; // rotation around Y axis (degrees)
};

struct PlatformGrid {
    int width = 90;           // grid cells X (90 * 2m = 180m, overhangs silo)
    int depth = 90;           // grid cells Z (90 * 2m = 180m)
    float cellSize = 2.0f;    // meters per cell
    float savedPlatformY = 0; // Y when frames were last saved (for relocation)
    std::vector<bool> walls;  // width*depth, true = wall cell
    std::vector<WallFrame> frames; // user-placed frames on wall surfaces
    std::vector<WireConnection> wires; // connections between frames
    std::vector<FileSlotAssignment> fileSlots; // file_slot CP assignments
    std::vector<BrushWall> brushWalls; // user-drawn walls (wall brush mode)

    PlatformGrid() : walls(width * depth, false) {}

    bool getCell(int x, int z) const {
        if (x < 0 || x >= width || z < 0 || z >= depth) return false;
        return walls[z * width + x];
    }
    void setCell(int x, int z, bool val) {
        if (x < 0 || x >= width || z < 0 || z >= depth) return;
        walls[z * width + x] = val;
    }
    void toggleCell(int x, int z) {
        if (x < 0 || x >= width || z < 0 || z >= depth) return;
        walls[z * width + x] = !walls[z * width + x];
    }
    void resize(int w, int d) {
        width = w; depth = d;
        walls.assign(w * d, false);
    }
};

class PlatformGridBuilder {
public:
    PlatformGridBuilder() = default;

    void init(ModelRenderer* renderer,
              std::vector<std::unique_ptr<SceneObject>>* sceneObjects);

    // Spawn the flat platform slab with center hole
    void spawnPlatform(const glm::vec3& center, float baseY,
                       float holeHalfSize, float wallHeight);

    // Build 3D walls from grid data (greedy row-merge for efficiency)
    void buildWallsFromGrid(const glm::vec3& center, float baseY,
                            float wallHeight, const glm::vec4& wallColor);

    // Remove all platform_wall objects (but keep the platform slab)
    void clearWalls();

    // Remove everything (platform + walls + frames)
    void clearAll();

    // Spawn brush walls from saved data
    void spawnBrushWalls(const glm::vec4& wallColor);

    // Spawn/clear wall frame objects
    void spawnFrameObjects(const glm::vec3& center, float baseY);
    void spawnSingleFrame(int frameIndex);  // incremental — add one frame without rebuilding all
    void clearFrames();

    // Save/load grid config for a folder path
    bool saveConfig(const std::string& folderPath);
    bool loadConfig(const std::string& folderPath);

    // Load config and rebuild walls. defaultSize is used when no saved config exists.
    void loadAndBuild(const std::string& folderPath,
                      const glm::vec3& center, float baseY,
                      float wallHeight, const glm::vec4& wallColor,
                      int defaultSize = 90);

    PlatformGrid& grid() { return m_grid; }
    const PlatformGrid& grid() const { return m_grid; }

    bool hasConfig(const std::string& folderPath) const;

private:
    static std::string configPath(const std::string& folderPath);

    PlatformGrid m_grid;
    ModelRenderer* m_renderer = nullptr;
    std::vector<std::unique_ptr<SceneObject>>* m_sceneObjects = nullptr;

    // Track spawned objects for cleanup
    std::vector<SceneObject*> m_spawnedWalls;
    std::vector<SceneObject*> m_spawnedFrames;
    SceneObject* m_platformSlab = nullptr;
};

} // namespace eden
