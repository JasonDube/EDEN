#pragma once

#include "../Editor/SceneObject.hpp"
#include "../Renderer/ModelRenderer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace eden {

enum class CleanerBotState {
    IDLE,
    SCANNING,
    WALKING_TO_FILE,
    PICKING_UP,
    WALKING_HOME,
    DONE
};

struct CleanerTarget {
    std::string sourcePath;   // full path on disk
    std::string destDir;      // ~/Pictures or ~/Videos
    glm::vec3 position;       // position in room (value copy)
    std::string objName;      // SceneObject name for lookup at removal time
};

// A single work session log entry
struct CleanerLogEntry {
    std::string timestamp;              // "YYYY-MM-DD HH:MM:SS"
    std::vector<std::string> filesMoved; // filenames moved
    std::string destination;             // e.g. "~/Pictures"
};

class CleanerBot {
public:
    CleanerBot() = default;

    void init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
              ModelRenderer* renderer);

    void spawn(const glm::vec3& homePos, ModelRenderer* renderer,
               const std::string& modelPath = "");
    void despawn();

    void activate();              // start cleaning
    void update(float deltaTime); // drives state machine

    bool isActive() const;
    bool isSpawned() const { return m_spawned; }
    SceneObject* getSceneObject() const { return m_sceneObject; }

    const char* getStateName() const;
    int getFilesRemaining() const;
    int getTotalFiles() const;

    // Interaction menu (E press shows menu instead of auto-activating)
    void showMenu() { m_showMenu = true; }
    bool isMenuOpen() const { return m_showMenu; }
    bool renderMenuUI();  // returns true while open

    // Work log
    const std::vector<CleanerLogEntry>& getLog() const { return m_workLog; }

private:
    void scanForFiles();
    void performFileMove(const CleanerTarget& target);
    void removeSceneObject(const std::string& objName);
    std::string resolveDestPath(const std::string& filename,
                                const std::string& destDir);

    static bool isImageExtension(const std::string& ext);
    static bool isVideoExtension(const std::string& ext);

    // Work log persistence
    void loadWorkLog();
    void saveWorkLog();
    static std::string getLogPath();
    static std::string currentTimestamp();

    // Current session tracking
    std::vector<std::string> m_sessionFilesMoved;
    std::string m_sessionDestination;

    std::vector<std::unique_ptr<SceneObject>>* m_sceneObjects = nullptr;
    ModelRenderer* m_renderer = nullptr;

    SceneObject* m_sceneObject = nullptr; // raw pointer into m_sceneObjects
    bool m_spawned = false;

    CleanerBotState m_state = CleanerBotState::IDLE;
    float m_stateTimer = 0.0f;

    glm::vec3 m_homePos{0.0f};
    std::vector<CleanerTarget> m_targets;
    int m_targetIndex = 0;

    // Interaction menu
    bool m_showMenu = false;
    bool m_showReport = false;

    // Persistent work log
    std::vector<CleanerLogEntry> m_workLog;

    static constexpr float SCAN_DURATION = 1.5f;
    static constexpr float PICKUP_DURATION = 0.5f;
    static constexpr float DONE_DURATION = 2.0f;
    static constexpr float MOVE_SPEED = 4.0f; // units per second
};

} // namespace eden
