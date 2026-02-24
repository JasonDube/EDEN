#pragma once

#include "../Editor/SceneObject.hpp"
#include "../Renderer/ModelRenderer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

namespace eden {

enum class ImageBotState {
    IDLE,
    SCANNING,
    WALKING_TO_IMAGE,
    DESCRIBING,      // waiting for SmolVLM HTTP response (async)
    WALKING_HOME,
    DONE
};

struct ImageTarget {
    std::string imagePath;    // full disk path (from targetLevel "fs://...")
    glm::vec3 position;      // position in room
    std::string objName;      // SceneObject name for lookup
};

struct ImageBotLogEntry {
    std::string timestamp;
    std::vector<std::string> filesDescribed;
    int totalDescribed;
};

class ImageBot {
public:
    ImageBot() = default;

    void init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
              ModelRenderer* renderer);

    void spawn(const glm::vec3& homePos, ModelRenderer* renderer,
               const std::string& modelPath = "");
    void despawn();

    void activate();
    void update(float deltaTime);

    bool isActive() const;
    bool isSpawned() const { return m_spawned; }
    SceneObject* getSceneObject() const { return m_sceneObject; }

    const char* getStateName() const;
    int getFilesRemaining() const;
    int getTotalFiles() const;

    // Menu (E press)
    void showMenu() { m_showMenu = true; }
    bool isMenuOpen() const { return m_showMenu; }
    bool renderMenuUI();

    // Work log
    const std::vector<ImageBotLogEntry>& getLog() const { return m_workLog; }

    // SmolVLM server control (called from main.cpp)
    void setSmolVLMReady(bool ready) { m_smolvlmReady = ready; }
    bool isSmolVLMReady() const { return m_smolvlmReady; }

private:
    void scanForImages();
    void startDescribeAsync(const ImageTarget& target);
    void applyDescription(const ImageTarget& target, const std::string& desc);
    void writeSidecarFile(const std::string& imagePath, const std::string& desc);
    void spawnDescCube(const glm::vec3& imagePos, const std::string& desc,
                       const std::string& imageName);

    // HTTP call to SmolVLM (runs in background thread)
    std::string callSmolVLM(const std::string& imagePath);

    static bool isImageExtension(const std::string& ext);

    // Log persistence
    void loadWorkLog();
    void saveWorkLog();
    static std::string getLogPath();
    static std::string currentTimestamp();

    // Scene state
    std::vector<std::unique_ptr<SceneObject>>* m_sceneObjects = nullptr;
    ModelRenderer* m_renderer = nullptr;

    SceneObject* m_sceneObject = nullptr;
    bool m_spawned = false;

    // State machine
    ImageBotState m_state = ImageBotState::IDLE;
    float m_stateTimer = 0.0f;

    glm::vec3 m_homePos{0.0f};
    std::vector<ImageTarget> m_targets;
    int m_targetIndex = 0;

    // Async SmolVLM call
    std::thread m_describeThread;
    std::atomic<bool> m_describeComplete{false};
    std::string m_describeResult;
    bool m_smolvlmReady = false;

    // Menu
    bool m_showMenu = false;
    bool m_showReport = false;

    // Persistent work log
    std::vector<ImageBotLogEntry> m_workLog;
    std::vector<std::string> m_sessionFilesDescribed;

    static constexpr float SCAN_DURATION = 1.5f;
    static constexpr float DONE_DURATION = 2.0f;
    static constexpr float MOVE_SPEED = 3.0f;
};

} // namespace eden
