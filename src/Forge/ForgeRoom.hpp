#pragma once

#include "../Editor/SceneObject.hpp"
#include "../Renderer/ModelRenderer.hpp"
#include <string>
#include <vector>
#include <memory>

namespace eden {

struct DeployedBot {
    std::string modelPath;
    std::string job;        // "CleanerBot"
    std::string territory;  // e.g. "/home/user"
};

class ForgeRoom {
public:
    ForgeRoom() = default;

    void init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
              ModelRenderer* renderer);

    void spawn(const glm::vec3& center, float baseY);
    void despawn();

    bool isSpawned() const { return m_spawned; }

    // Place a generated .glb model on the pad
    void placeModelOnPad(const std::string& glbPath);

    // Remove model from pad (used internally and by reject)
    void clearPadModel();

    // Check if a scene object is the model sitting on the pad
    bool isOnPad(SceneObject* obj) const;

    // Assignment menu
    void showAssignmentMenu() { m_showAssignMenu = true; }
    bool isAssignmentMenuOpen() const { return m_showAssignMenu; }

    // Render the assignment ImGui popup — call from renderPlayModeUI()
    // Returns true if popup is still open
    bool renderAssignmentUI();

    // Deploy bot: save to registry, remove from pad
    void deployBot();

    // Reject bot: destroy model, delete .glb from disk
    void rejectBot();

    // Registry
    void loadRegistry();
    void saveRegistry();
    std::vector<DeployedBot> getDeployedBotsForTerritory(const std::string& dirPath) const;

    SceneObject* getPadObject() const { return m_padObject; }

private:
    std::vector<std::unique_ptr<SceneObject>>* m_sceneObjects = nullptr;
    ModelRenderer* m_renderer = nullptr;

    SceneObject* m_padObject = nullptr;     // the purple disc
    SceneObject* m_padModel = nullptr;      // generated model sitting on pad
    std::string m_padModelPath;             // .glb path on disk
    bool m_spawned = false;

    // Assignment UI state
    bool m_showAssignMenu = false;
    int m_selectedJob = 0;                  // 0=CleanerBot
    int m_selectedTerritory = 0;            // 0=Home, 1=Documents, 2=Downloads, 3=Custom
    char m_customTerritory[256] = "";

    // Registry of deployed bots
    std::vector<DeployedBot> m_deployedBots;

    static std::string getRegistryPath();
};

} // namespace eden
