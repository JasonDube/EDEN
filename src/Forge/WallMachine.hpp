#pragma once

#include "OS/PlatformGrid.hpp"
#include "../Network/Hunyuan3DClient.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

namespace eden {

// Machine execution state
enum class MachineState {
    Idle,
    Running,
    Complete,
    Error
};

// Schema definition for a machine type (loaded from JSON)
struct MachineSchema {
    std::string name;           // e.g. "hunyuan3d"
    std::string displayName;    // e.g. "Hunyuan3D Model Generator"

    struct ParamDef {
        std::string name;       // e.g. "image"
        std::string type;       // "image", "text", "bool", "float", "int"
        bool required = false;
        std::string defaultValue;
    };
    std::vector<ParamDef> inputs;
    std::vector<ParamDef> params;   // checkboxes, sliders, etc.
    std::vector<std::string> outputs; // e.g. ["model"]
};

// A live machine instance on the wall
struct WallMachineInstance {
    int machineFrameIndex = -1;   // index into PlatformGrid::frames
    MachineState state = MachineState::Idle;
    std::string jobUID;           // Hunyuan job UID
    std::string outputGLBPath;    // path to completed GLB
    std::vector<std::string> logLines;

    // Collected params from wired widgets at execution time
    struct CollectedParam {
        std::string name;
        std::string value;       // string representation
    };
    std::vector<CollectedParam> collectedParams;
};

class WallMachine {
public:
    WallMachine() = default;
    ~WallMachine();

    // Load machine schema from a JSON file
    bool loadSchema(const std::string& jsonPath);
    const MachineSchema& getSchema() const { return m_schema; }

    // Create a machine instance for a given frame
    WallMachineInstance& getOrCreateInstance(int machineFrameIndex);
    WallMachineInstance* findInstance(int machineFrameIndex);

    // Execute a machine: collect wired params, start Hunyuan generation
    // imagePath: path to the input image (from wired inp_ frame)
    // outputDir: directory to save the GLB output
    bool execute(int machineFrameIndex, const std::string& imagePath,
                 const std::string& outputDir,
                 const std::vector<WallMachineInstance::CollectedParam>& params);

    // Poll running jobs (call every frame or on timer)
    // Returns list of machine frame indices that completed this poll
    std::vector<int> poll();

    // Get machine state for rendering
    MachineState getState(int machineFrameIndex) const;
    const std::vector<std::string>& getLogLines(int machineFrameIndex) const;
    std::string getOutputPath(int machineFrameIndex) const;

    // Cancel all running jobs
    void cancelAll();

private:
    MachineSchema m_schema;
    std::vector<WallMachineInstance> m_instances;

    // Background polling
    Hunyuan3DClient m_client{"localhost", 8081};
    std::mutex m_mutex;

    static const std::vector<std::string> s_emptyLog;
};

} // namespace eden
