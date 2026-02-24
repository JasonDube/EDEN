#include "ForgeRoom.hpp"
#include "../Editor/PrimitiveMeshBuilder.hpp"
#include "../Editor/GLBLoader.hpp"
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>

namespace eden {

// ── Init ───────────────────────────────────────────────────────────────

void ForgeRoom::init(std::vector<std::unique_ptr<SceneObject>>* sceneObjects,
                     ModelRenderer* renderer) {
    m_sceneObjects = sceneObjects;
    m_renderer = renderer;
    loadRegistry();
}

// ── Spawn / Despawn ────────────────────────────────────────────────────

void ForgeRoom::spawn(const glm::vec3& center, float baseY) {
    if (m_spawned || !m_sceneObjects || !m_renderer) return;

    // Create purple disc (teleport pad)
    glm::vec4 padColor{0.5f, 0.1f, 0.9f, 1.0f};
    auto mesh = PrimitiveMeshBuilder::createCylinder(2.5f, 0.15f, 32, padColor);
    uint32_t handle = m_renderer->createModel(
        mesh.vertices, mesh.indices, nullptr, 0, 0);

    auto obj = std::make_unique<SceneObject>("ForgePad");
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setLocalBounds(mesh.bounds);
    obj->setMeshData(mesh.vertices, mesh.indices);
    obj->setPrimitiveType(PrimitiveType::Cylinder);
    obj->setPrimitiveSize(2.5f);
    obj->setPrimitiveColor(padColor);
    obj->setBuildingType("forge_pad");
    obj->setDescription("Robot Forge Pad");

    obj->getTransform().setPosition({center.x, baseY, center.z});
    obj->getTransform().setScale({1.0f, 1.0f, 1.0f});

    m_padObject = obj.get();
    m_sceneObjects->push_back(std::move(obj));
    m_spawned = true;
}

void ForgeRoom::despawn() {
    if (!m_spawned || !m_sceneObjects || !m_renderer) return;

    clearPadModel();

    // Remove pad object
    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (it->get() == m_padObject) {
            uint32_t handle = (*it)->getBufferHandle();
            if (handle != 0) m_renderer->destroyModel(handle);
            it = m_sceneObjects->erase(it);
            break;
        } else {
            ++it;
        }
    }

    m_padObject = nullptr;
    m_spawned = false;
    m_showAssignMenu = false;
}

// ── Model on Pad ───────────────────────────────────────────────────────

void ForgeRoom::placeModelOnPad(const std::string& glbPath) {
    if (!m_sceneObjects || !m_renderer || !m_padObject) return;

    // Clear any existing model on pad
    clearPadModel();

    // Load GLB
    auto result = GLBLoader::load(glbPath);
    if (!result.success || result.meshes.empty()) {
        std::cerr << "[ForgeRoom] Failed to load model: " << glbPath << std::endl;
        return;
    }

    // Merge all meshes
    std::vector<ModelVertex> allVerts;
    std::vector<uint32_t> allIndices;
    bool hasTex = false;
    std::vector<unsigned char> texData;
    int texW = 0, texH = 0;

    for (auto& m : result.meshes) {
        uint32_t baseIdx = static_cast<uint32_t>(allVerts.size());
        allVerts.insert(allVerts.end(), m.vertices.begin(), m.vertices.end());
        for (auto idx : m.indices) allIndices.push_back(baseIdx + idx);
        if (!hasTex && m.hasTexture) {
            texData = m.texture.data;
            texW = m.texture.width;
            texH = m.texture.height;
            hasTex = true;
        }
    }

    if (allVerts.empty()) return;

    // Normalize model: center at origin, scale to fit ~2 units
    glm::vec3 bmin(FLT_MAX), bmax(-FLT_MAX);
    for (auto& v : allVerts) {
        bmin = glm::min(bmin, v.position);
        bmax = glm::max(bmax, v.position);
    }
    glm::vec3 center = (bmin + bmax) * 0.5f;
    for (auto& v : allVerts) v.position -= center;
    bmin -= center; bmax -= center;

    float maxExtent = std::max({bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z});
    if (maxExtent > 0.0f) {
        float scaleFactor = 2.0f / maxExtent;
        for (auto& v : allVerts) v.position *= scaleFactor;
        bmin *= scaleFactor; bmax *= scaleFactor;
    }

    uint32_t handle;
    if (hasTex && !texData.empty()) {
        handle = m_renderer->createModel(allVerts, allIndices, texData.data(), texW, texH);
    } else {
        handle = m_renderer->createModel(allVerts, allIndices, nullptr, 0, 0);
    }

    auto obj = std::make_unique<SceneObject>("ForgeModel");
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(allIndices.size()));
    obj->setVertexCount(static_cast<uint32_t>(allVerts.size()));
    obj->setLocalBounds({bmin, bmax});
    obj->setMeshData(allVerts, allIndices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setBuildingType("forge_model");
    obj->setDescription("Generated Robot");

    // Position on top of pad
    glm::vec3 padPos = m_padObject->getTransform().getPosition();
    obj->getTransform().setPosition({padPos.x, padPos.y + 1.2f, padPos.z});
    obj->getTransform().setScale({1.0f, 1.0f, 1.0f});

    m_padModel = obj.get();
    m_padModelPath = glbPath;
    m_sceneObjects->push_back(std::move(obj));

    std::cout << "[ForgeRoom] Model placed on pad: " << glbPath << std::endl;
}

void ForgeRoom::clearPadModel() {
    if (!m_padModel || !m_sceneObjects || !m_renderer) return;

    auto it = m_sceneObjects->begin();
    while (it != m_sceneObjects->end()) {
        if (it->get() == m_padModel) {
            uint32_t handle = (*it)->getBufferHandle();
            if (handle != 0) m_renderer->destroyModel(handle);
            it = m_sceneObjects->erase(it);
            break;
        } else {
            ++it;
        }
    }

    m_padModel = nullptr;
    m_padModelPath.clear();
}

bool ForgeRoom::isOnPad(SceneObject* obj) const {
    return obj && obj == m_padModel;
}

// ── Assignment UI ──────────────────────────────────────────────────────

bool ForgeRoom::renderAssignmentUI() {
    if (!m_showAssignMenu) return false;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                    ImGui::GetIO().DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(340, 320), ImGuiCond_Appearing);

    bool open = true;
    if (ImGui::Begin("Assign Robot##ForgeAssign", &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {

        // Reject button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Reject & Detonate", ImVec2(-1, 30))) {
            rejectBot();
            m_showAssignMenu = false;
            ImGui::PopStyleColor();
            ImGui::End();
            return false;
        }
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Job selection
        ImGui::Text("Job:");
        const char* jobs[] = {"CleanerBot", "ImageBot"};
        ImGui::Combo("##Job", &m_selectedJob, jobs, IM_ARRAYSIZE(jobs));

        ImGui::Separator();

        // Territory selection
        ImGui::Text("Territory:");
        const char* home = getenv("HOME");
        std::string homeStr = home ? home : "/home";

        bool changed = false;
        changed |= ImGui::RadioButton("~ (Home)", &m_selectedTerritory, 0);
        changed |= ImGui::RadioButton("~/Documents", &m_selectedTerritory, 1);
        changed |= ImGui::RadioButton("~/Downloads", &m_selectedTerritory, 2);
        ImGui::RadioButton("Custom:", &m_selectedTerritory, 3);
        if (m_selectedTerritory == 3) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##CustomTerritory", m_customTerritory, sizeof(m_customTerritory));
        }

        ImGui::Separator();

        // Deploy button
        bool canDeploy = m_padModel != nullptr;
        if (!canDeploy) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
        if (ImGui::Button("Deploy", ImVec2(-1, 35))) {
            deployBot();
            m_showAssignMenu = false;
            ImGui::PopStyleColor();
            if (!canDeploy) ImGui::EndDisabled();
            ImGui::End();
            return false;
        }
        ImGui::PopStyleColor();
        if (!canDeploy) ImGui::EndDisabled();
    }
    ImGui::End();

    if (!open) {
        m_showAssignMenu = false;
        return false;
    }

    return m_showAssignMenu;
}

// ── Deploy / Reject ────────────────────────────────────────────────────

void ForgeRoom::deployBot() {
    if (!m_padModel || m_padModelPath.empty()) return;

    const char* home = getenv("HOME");
    std::string homeStr = home ? home : "/home";

    std::string territory;
    switch (m_selectedTerritory) {
        case 0: territory = homeStr; break;
        case 1: territory = homeStr + "/Documents"; break;
        case 2: territory = homeStr + "/Downloads"; break;
        case 3: {
            territory = std::string(m_customTerritory);
            // Expand ~ to home directory
            if (!territory.empty() && territory[0] == '~') {
                territory = homeStr + territory.substr(1);
            }
            break;
        }
        default: territory = homeStr; break;
    }

    std::string jobName;
    switch (m_selectedJob) {
        case 0: jobName = "CleanerBot"; break;
        case 1: jobName = "ImageBot"; break;
        default: jobName = "CleanerBot"; break;
    }

    DeployedBot bot;
    bot.modelPath = m_padModelPath;
    bot.job = jobName;
    bot.territory = territory;
    m_deployedBots.push_back(bot);
    saveRegistry();

    // Remove model from pad (but don't delete the .glb file)
    clearPadModel();

    std::cout << "[ForgeRoom] Deployed " << jobName << " to " << territory << std::endl;
}

void ForgeRoom::rejectBot() {
    if (!m_padModel) return;

    std::string path = m_padModelPath;

    // Remove from scene
    clearPadModel();

    // Delete the .glb file from disk
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (!ec) {
            std::cout << "[ForgeRoom] Deleted rejected model: " << path << std::endl;
        }
    }
}

// ── Registry ───────────────────────────────────────────────────────────

std::string ForgeRoom::getRegistryPath() {
    const char* home = getenv("HOME");
    std::string configDir = std::string(home ? home : "/tmp") + "/.config/eden";
    std::filesystem::create_directories(configDir);
    return configDir + "/deployed_bots.json";
}

void ForgeRoom::loadRegistry() {
    m_deployedBots.clear();

    std::string path = getRegistryPath();
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_array()) return;

        for (auto& entry : j) {
            DeployedBot bot;
            bot.modelPath = entry.value("model", "");
            bot.job = entry.value("job", "CleanerBot");
            bot.territory = entry.value("territory", "");
            if (!bot.modelPath.empty() && !bot.territory.empty()) {
                m_deployedBots.push_back(std::move(bot));
            }
        }
        std::cout << "[ForgeRoom] Loaded " << m_deployedBots.size() << " deployed bots" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ForgeRoom] Failed to load registry: " << e.what() << std::endl;
    }
}

void ForgeRoom::saveRegistry() {
    nlohmann::json j = nlohmann::json::array();
    for (auto& bot : m_deployedBots) {
        j.push_back({
            {"model", bot.modelPath},
            {"job", bot.job},
            {"territory", bot.territory}
        });
    }

    std::string path = getRegistryPath();
    std::ofstream f(path);
    if (f.is_open()) {
        f << j.dump(2);
        std::cout << "[ForgeRoom] Saved " << m_deployedBots.size() << " deployed bots" << std::endl;
    }
}

std::vector<DeployedBot> ForgeRoom::getDeployedBotsForTerritory(const std::string& dirPath) const {
    std::vector<DeployedBot> result;
    for (auto& bot : m_deployedBots) {
        if (bot.territory == dirPath) {
            // Verify the model file still exists
            if (std::filesystem::exists(bot.modelPath)) {
                result.push_back(bot);
            }
        }
    }
    return result;
}

} // namespace eden
