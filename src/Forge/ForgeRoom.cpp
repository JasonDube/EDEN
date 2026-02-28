#include "ForgeRoom.hpp"
#include "WidgetKit.hpp"
#include "../Editor/PrimitiveMeshBuilder.hpp"
#include "../Editor/GLBLoader.hpp"
#include "../Editor/LimeLoader.hpp"
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
    // Note: loadRegistry() is called explicitly by spawnObjects() after init+spawn
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

    // Spawn the multiview machine next to the pad
    spawnMachine(glm::vec3(center.x + 7.0f, center.y, center.z), baseY,
                 "examples/terrain_editor/assets/models/multiview_machine.lime");

    // Spawn test widget machine on the other side of the pad
    m_widgetKit.spawnFromLime("examples/terrain_editor/assets/models/test_widget_machine.lime",
                              glm::vec3(center.x - 7.0f, center.y, center.z),
                              baseY, m_sceneObjects, m_renderer);
}

void ForgeRoom::despawn() {
    if (!m_spawned || !m_sceneObjects || !m_renderer) return;

    clearPadModel();
    clearMachineSlots();

    // Despawn widget kit objects
    m_widgetKit.despawn(m_sceneObjects, m_renderer);

    // Remove machine objects (slots, lever, platform, visual)
    SceneObject* machineObjs[] = {
        m_machineSlots[0].object, m_machineSlots[1].object,
        m_machineSlots[2].object, m_machineSlots[3].object,
        m_lever, m_platform, m_machineVisual
    };
    for (auto* mobj : machineObjs) {
        if (!mobj) continue;
        auto it = m_sceneObjects->begin();
        while (it != m_sceneObjects->end()) {
            if (it->get() == mobj) {
                uint32_t h = (*it)->getBufferHandle();
                if (h != 0) m_renderer->destroyModel(h);
                it = m_sceneObjects->erase(it);
                break;
            } else {
                ++it;
            }
        }
    }
    for (int i = 0; i < 4; i++) m_machineSlots[i].object = nullptr;
    m_lever = nullptr;
    m_platform = nullptr;
    m_machineVisual = nullptr;
    m_machineSpawned = false;

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
        const char* jobs[] = {"CleanerBot", "ImageBot", "CullRobot"};
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
        case 2: jobName = "CullRobot"; break;
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

// ── Multiview Machine ──────────────────────────────────────────────────

void ForgeRoom::spawnMachine(const glm::vec3& center, float baseY,
                             const std::string& limePath) {
    if (m_machineSpawned || !m_sceneObjects || !m_renderer) return;

    // ── Try loading a .lime model with hitbox control points ────────────
    bool limeLoaded = false;
    if (!limePath.empty() && std::filesystem::exists(limePath)) {
        auto result = LimeLoader::load(limePath);
        if (result.success && !result.mesh.vertices.empty()) {
            // Spawn the visual model
            auto visual = LimeLoader::createSceneObject(result.mesh, *m_renderer);
            if (visual) {
                visual->setBuildingType("machine_visual");
                visual->setDescription("Multiview Generation Machine");
                visual->getTransform().setPosition({center.x, baseY, center.z});
                m_machineVisual = visual.get();
                m_sceneObjects->push_back(std::move(visual));

                // Map control point name suffixes to slot indices
                auto nameToSlotIndex = [](const std::string& suffix) -> int {
                    if (suffix == "slot_front") return 0;
                    if (suffix == "slot_back")  return 1;
                    if (suffix == "slot_left")  return 2;
                    if (suffix == "slot_right") return 3;
                    return -1;
                };

                const char* slotLabels[] = {"FRONT", "BACK", "LEFT", "RIGHT"};

                // Iterate control points and spawn invisible hitbox cubes
                for (auto& cp : result.mesh.controlPoints) {
                    if (cp.name.rfind("hitbox_", 0) != 0) continue;
                    std::string suffix = cp.name.substr(7);  // strip "hitbox_"

                    // Get control point world position
                    glm::vec3 cpPos{0.0f};
                    if (cp.vertexIndex < result.mesh.vertices.size()) {
                        cpPos = result.mesh.vertices[cp.vertexIndex].position;
                    }
                    // Apply model transform offset
                    cpPos += glm::vec3(center.x, baseY, center.z);

                    // Create invisible hitbox cube
                    glm::vec4 hitboxColor{0.0f, 0.0f, 0.0f, 0.0f};
                    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, hitboxColor);
                    uint32_t handle = m_renderer->createModel(
                        mesh.vertices, mesh.indices, nullptr, 0, 0);
                    auto obj = std::make_unique<SceneObject>("Hitbox_" + cp.name);
                    obj->setBufferHandle(handle);
                    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
                    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
                    obj->setLocalBounds(mesh.bounds);
                    obj->setMeshData(mesh.vertices, mesh.indices);
                    obj->setPrimitiveType(PrimitiveType::Cube);
                    obj->setBuildingType("hitbox");
                    obj->setDescription(suffix);
                    obj->setVisible(false);
                    obj->getTransform().setPosition(cpPos);
                    obj->getTransform().setScale({1.5f, 1.5f, 1.5f});

                    int slotIdx = nameToSlotIndex(suffix);
                    if (slotIdx >= 0) {
                        m_machineSlots[slotIdx].object = obj.get();
                        m_machineSlots[slotIdx].label = slotLabels[slotIdx];
                    } else if (suffix == "lever") {
                        m_lever = obj.get();
                    }

                    m_sceneObjects->push_back(std::move(obj));
                }

                limeLoaded = true;
                std::cout << "[ForgeRoom] Multiview machine loaded from " << limePath << std::endl;
            }
        } else {
            std::cerr << "[ForgeRoom] Failed to load .lime: " << result.error << std::endl;
        }
    }

    // ── Fallback: hardcoded primitive cubes ─────────────────────────────
    if (!limeLoaded) {
        // Platform — dark gray flat cube
        {
            glm::vec4 color{0.2f, 0.2f, 0.25f, 1.0f};
            auto mesh = PrimitiveMeshBuilder::createCube(1.0f, color);
            uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
            auto obj = std::make_unique<SceneObject>("MachinePlatform");
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
            obj->setLocalBounds(mesh.bounds);
            obj->setMeshData(mesh.vertices, mesh.indices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setPrimitiveColor(color);
            obj->setBuildingType("machine_platform");
            obj->setDescription("Multiview Generation Machine");
            obj->getTransform().setPosition({center.x, baseY + 0.4f, center.z});
            obj->getTransform().setScale({6.0f, 0.8f, 6.0f});
            m_platform = obj.get();
            m_sceneObjects->push_back(std::move(obj));
        }

        float platformTop = baseY + 0.8f;

        struct SlotDef {
            const char* label;
            glm::vec3 offset;
            glm::vec4 color;
        };
        SlotDef slotDefs[4] = {
            {"FRONT",  {0.0f, 0.0f, -2.0f}, {0.0f, 0.8f, 0.8f, 1.0f}},
            {"BACK",   {0.0f, 0.0f,  2.0f}, {0.9f, 0.5f, 0.1f, 1.0f}},
            {"LEFT",   {-2.0f, 0.0f, 0.0f}, {0.1f, 0.8f, 0.2f, 1.0f}},
            {"RIGHT",  {2.0f, 0.0f, 0.0f},  {0.8f, 0.1f, 0.8f, 1.0f}},
        };

        for (int i = 0; i < 4; i++) {
            auto& sd = slotDefs[i];
            auto mesh = PrimitiveMeshBuilder::createCube(1.0f, sd.color);
            uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
            auto obj = std::make_unique<SceneObject>(std::string("MachineSlot_") + sd.label);
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
            obj->setLocalBounds(mesh.bounds);
            obj->setMeshData(mesh.vertices, mesh.indices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setPrimitiveColor(sd.color);
            obj->setBuildingType("machine_slot");
            obj->setDescription(std::string(sd.label) + " view slot — drop image from hotbar");
            obj->getTransform().setPosition({
                center.x + sd.offset.x,
                platformTop + 0.2f,
                center.z + sd.offset.z
            });
            obj->getTransform().setScale({1.5f, 0.4f, 1.5f});
            m_machineSlots[i].object = obj.get();
            m_machineSlots[i].label = sd.label;
            m_sceneObjects->push_back(std::move(obj));

            glm::vec3 postOffset = sd.offset * 1.6f;
            auto postMesh = PrimitiveMeshBuilder::createCube(1.0f, sd.color);
            uint32_t postHandle = m_renderer->createModel(postMesh.vertices, postMesh.indices, nullptr, 0, 0);
            auto post = std::make_unique<SceneObject>(std::string("SlotLabel_") + sd.label);
            post->setBufferHandle(postHandle);
            post->setIndexCount(static_cast<uint32_t>(postMesh.indices.size()));
            post->setVertexCount(static_cast<uint32_t>(postMesh.vertices.size()));
            post->setLocalBounds(postMesh.bounds);
            post->setMeshData(postMesh.vertices, postMesh.indices);
            post->setPrimitiveType(PrimitiveType::Cube);
            post->setPrimitiveColor(sd.color);
            post->setBuildingType("machine_label");
            post->setDescription(std::string(sd.label));
            post->getTransform().setPosition({
                center.x + postOffset.x,
                platformTop + 1.0f,
                center.z + postOffset.z
            });
            post->getTransform().setScale({0.15f, 1.6f, 0.15f});
            m_sceneObjects->push_back(std::move(post));
        }

        // Lever — red tall thin cube at platform center
        {
            glm::vec4 color{0.9f, 0.1f, 0.1f, 1.0f};
            auto mesh = PrimitiveMeshBuilder::createCube(1.0f, color);
            uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
            auto obj = std::make_unique<SceneObject>("MachineLever");
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
            obj->setLocalBounds(mesh.bounds);
            obj->setMeshData(mesh.vertices, mesh.indices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setPrimitiveColor(color);
            obj->setBuildingType("machine_lever");
            obj->setDescription("Pull lever to generate 3D model from views");
            obj->getTransform().setPosition({center.x, platformTop + 1.0f, center.z});
            obj->getTransform().setScale({0.3f, 1.8f, 0.3f});
            m_lever = obj.get();
            m_sceneObjects->push_back(std::move(obj));
        }
    }

    m_machineSpawned = true;
    std::cout << "[ForgeRoom] Multiview machine spawned at (" << center.x << ", " << baseY << ", " << center.z << ")" << std::endl;
}

int ForgeRoom::getMachineSlotIndex(SceneObject* obj) const {
    if (!obj) return -1;
    for (int i = 0; i < 4; i++) {
        if (m_machineSlots[i].object == obj) return i;
    }
    // Also match hitbox objects by description
    if (obj->getBuildingType() == "hitbox") {
        const auto& desc = obj->getDescription();
        if (desc == "slot_front") return 0;
        if (desc == "slot_back")  return 1;
        if (desc == "slot_left")  return 2;
        if (desc == "slot_right") return 3;
    }
    // Check widget kit slots
    if (obj->getBuildingType() == "widget") {
        return m_widgetKit.getSlotIndex(obj);
    }
    return -1;
}

bool ForgeRoom::isLever(SceneObject* obj) const {
    if (!obj) return false;
    if (obj == m_lever) return true;
    return obj->getBuildingType() == "hitbox" && obj->getDescription() == "lever";
}

void ForgeRoom::setSlotImage(int slot, const std::string& path) {
    if (slot < 0 || slot > 3) return;
    m_machineSlots[slot].imagePath = path;

    // Spawn a small colored preview indicator on top of the slot
    if (m_machineSlots[slot].preview) {
        clearSlotImage(slot);  // remove old preview first
        m_machineSlots[slot].imagePath = path;  // restore after clear
    }

    if (!m_sceneObjects || !m_renderer) return;

    // Create a bright white flat cube as "image placed" indicator
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, color);
    uint32_t handle = m_renderer->createModel(mesh.vertices, mesh.indices, nullptr, 0, 0);
    auto obj = std::make_unique<SceneObject>("SlotPreview_" + m_machineSlots[slot].label);
    obj->setBufferHandle(handle);
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setLocalBounds(mesh.bounds);
    obj->setMeshData(mesh.vertices, mesh.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveColor(color);
    obj->setBuildingType("machine_preview");

    // Extract filename for description
    std::string name = path;
    auto slashPos = name.rfind('/');
    if (slashPos != std::string::npos) name = name.substr(slashPos + 1);
    obj->setDescription(m_machineSlots[slot].label + ": " + name);

    // Position on top of the slot
    glm::vec3 slotPos = m_machineSlots[slot].object->getTransform().getPosition();
    obj->getTransform().setPosition({slotPos.x, slotPos.y + 0.35f, slotPos.z});
    obj->getTransform().setScale({1.2f, 0.15f, 1.2f});

    m_machineSlots[slot].preview = obj.get();
    m_machineSlots[slot].previewHandle = handle;
    m_sceneObjects->push_back(std::move(obj));

    std::cout << "[ForgeRoom] Image placed on " << m_machineSlots[slot].label << " slot: " << path << std::endl;
}

void ForgeRoom::clearSlotImage(int slot) {
    if (slot < 0 || slot > 3) return;

    if (m_machineSlots[slot].preview && m_sceneObjects && m_renderer) {
        auto it = m_sceneObjects->begin();
        while (it != m_sceneObjects->end()) {
            if (it->get() == m_machineSlots[slot].preview) {
                uint32_t h = (*it)->getBufferHandle();
                if (h != 0) m_renderer->destroyModel(h);
                it = m_sceneObjects->erase(it);
                break;
            } else {
                ++it;
            }
        }
    }

    m_machineSlots[slot].preview = nullptr;
    m_machineSlots[slot].previewHandle = 0;
    m_machineSlots[slot].imagePath.clear();

    // Clear the slot object's targetLevel
    if (m_machineSlots[slot].object) {
        m_machineSlots[slot].object->setTargetLevel("");
    }
}

std::string ForgeRoom::getSlotImagePath(int slot) const {
    if (slot < 0 || slot > 3) return "";
    return m_machineSlots[slot].imagePath;
}

bool ForgeRoom::isFrontFilled() const {
    return !m_machineSlots[0].imagePath.empty();
}

void ForgeRoom::clearMachineSlots() {
    for (int i = 0; i < 4; i++) {
        clearSlotImage(i);
    }
}

} // namespace eden
