#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/ImGuiManager.hpp"
#include "Renderer/TerrainPipeline.hpp"
#include "Renderer/TextureManager.hpp"
#include "Renderer/Skybox.hpp"
#include "Renderer/ProceduralSkybox.hpp"
#include "Renderer/BrushRing.hpp"
#include "Renderer/GizmoRenderer.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/SkinnedModelRenderer.hpp"
#include "Renderer/SplineRenderer.hpp"
#include "Renderer/WaterRenderer.hpp"

#include "Editor/EditorUI.hpp"
#include "Editor/Gizmo.hpp"
#include "Editor/TerrainBrushTool.hpp"
#include "Editor/ChunkManager.hpp"
#include "Editor/SceneObject.hpp"
#include "Editor/GLBLoader.hpp"
#include "Editor/LimeLoader.hpp"
#include "Editor/SkinnedGLBLoader.hpp"
#include "Editor/PathTool.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Editor/AINode.hpp"
#include "Editor/AIPath.hpp"
#include "Editor/BinaryLevelWriter.hpp"
#include "Editor/BinaryLevelReader.hpp"
#include "Renderer/AINodeRenderer.hpp"
#include "Renderer/DialogueBubbleRenderer.hpp"
#include "Network/AsyncHttpClient.hpp"

// Economy and Trading Systems
#include "Economy/EconomySystem.hpp"
#include "City/CityGovernor.hpp"
#include "AI/AStarPathfinder.hpp"
#include "AI/TraderAI.hpp"
#include "AI/DogfightAI.hpp"

// Zone System
#include "Zone/ZoneSystem.hpp"

// Game Modules
#include "GameModules/GameModule.hpp"

// OS / Filesystem
#include "OS/FilesystemBrowser.hpp"

#include <eden/Window.hpp>
#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Terrain.hpp>
#include <eden/ActionSystem.hpp>
#include <eden/LevelSerializer.hpp>
#include <eden/Audio.hpp>
#include <eden/PhysicsWorld.hpp>
#include <eden/ICharacterController.hpp>
#include <eden/JoltCharacter.hpp>
#include <eden/HomebrewCharacter.hpp>

#include <nfd.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <stb_image.h>
#include <grove.h>

#include "grove_host.hpp"
#include "MCPServer.hpp"
#include "Terminal/EdenTerminal.hpp"
#include <httplib.h>

#include <dirent.h>
#include <iostream>
#include <sstream>
#include <csignal>
#include <execinfo.h>
#include <chrono>
#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <map>
#include <glm/gtc/matrix_transform.hpp>

using namespace eden;

static std::string shellEscapeFS(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    result += "'";
    return result;
}

struct TerrainPushConstants {
    glm::mat4 mvp;
    glm::vec4 fogColor;
    float fogStart;
    float fogEnd;
};

enum class TransformMode { Select, Move, Rotate, Scale };

class TerrainEditor : public VulkanApplicationBase {
public:
    TerrainEditor() : VulkanApplicationBase(1280, 720, "EDEN - Terrain Editor") {}

    void setSessionMode(bool enabled) { m_sessionMode = enabled; }

protected:
    void onInit() override {
        NFD_Init();
        Audio::getInstance().init();

        m_textureManager = std::make_unique<TextureManager>(getContext());
        m_textureManager->loadTerrainTexturesFromFolder("textures/");

        m_pipeline = std::make_unique<TerrainPipeline>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent(),
            m_textureManager->getDescriptorSetLayout());

        m_chunkManager = std::make_unique<ChunkManager>(getBufferManager());
        m_brushTool = std::make_unique<TerrainBrushTool>(m_terrain, m_camera);
        m_pathTool = std::make_unique<PathTool>(m_terrain, m_camera);

        setupUICallbacks();

        m_skybox = std::make_unique<ProceduralSkybox>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_editorUI.setSkyParameters(&m_skybox->getParameters());
        m_editorUI.setSkyChangedCallback([this](const SkyParameters& params) {
            m_skybox->updateParameters(params);
        });

        m_brushRing = std::make_unique<BrushRing>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_gizmoRenderer = std::make_unique<GizmoRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_splineRenderer = std::make_unique<SplineRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_aiNodeRenderer = std::make_unique<AINodeRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_filesystemBrowser.init(m_modelRenderer.get(), &m_sceneObjects, &m_terrain);

        m_skinnedModelRenderer = std::make_unique<SkinnedModelRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_waterRenderer = std::make_unique<WaterRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());
        m_waterRenderer->setWaterLevel(-5.0f);
        m_waterRenderer->setVisible(false);

        m_editorUI.setWaterChangedCallback([this](float level, float amplitude, float frequency, bool visible) {
            m_waterRenderer->setWaterLevel(level);
            m_waterRenderer->setWaveAmplitude(amplitude);
            m_waterRenderer->setWaveFrequency(frequency);
            m_waterRenderer->setVisible(visible);
        });

        TerrainInfo terrainInfo;
        terrainInfo.chunkCountX = 32;
        terrainInfo.chunkCountZ = 32;
        terrainInfo.chunkResolution = 64;
        terrainInfo.tileSize = 2.0f;
        terrainInfo.heightScale = 200.0f;
        m_editorUI.setTerrainInfo(terrainInfo);

        initImGui();

        // Load monospace font for terminal
        {
            ImGuiIO& io = ImGui::GetIO();
            // Try common monospace font paths
            const char* fontPaths[] = {
                "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
                "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
                "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                nullptr
            };
            for (const char** p = fontPaths; *p; ++p) {
                if (std::filesystem::exists(*p)) {
                    m_monoFont = io.Fonts->AddFontFromFileTTF(*p, 16.0f);
                    if (m_monoFont) {
                        std::cout << "[EdenTerminal] Loaded mono font: " << *p << std::endl;
                        break;
                    }
                }
            }
            if (!m_monoFont) {
                std::cout << "[EdenTerminal] No mono font found, using default" << std::endl;
            }
        }

        loadSplashTexture();
        loadGroveLogoTexture();
        loadBuildingTextures();

        // Create scripts directory
        {
            const char* home = getenv("HOME");
            m_groveScriptsDir = home ? std::string(home) + "/eden/scripts" : "scripts";
            std::filesystem::create_directories(m_groveScriptsDir);
            std::cout << "Grove scripts directory: " << m_groveScriptsDir << std::endl;
        }

        // Initialize Economy and Trading Systems
        initializeEconomySystems();

        // Initialize Zone System (BEFORE initGroveVM so context pointer is valid)
        m_zoneSystem = std::make_unique<ZoneSystem>(-2016.0f, -2016.0f, 2016.0f, 2016.0f, 32.0f);
        m_zoneSystem->generateDefaultLayout();
        m_editorUI.setZoneSystem(m_zoneSystem.get());

        initGroveVM();
        loadEditorConfig();

        // Initialize AI backend client
        m_httpClient = std::make_unique<AsyncHttpClient>("http://localhost:8080");
        m_httpClient->start();
        m_httpClient->checkHealth([](const AsyncHttpClient::Response& resp) {
            if (resp.success) {
                std::cout << "AI Backend connected successfully\n";
            } else {
                std::cout << "AI Backend not available (start backend/server.py)\n";
            }
        });

        // Initialize MCP Server
        initMCPServer();

        // Initialize Bullet physics collision world
        m_physicsWorld = std::make_unique<PhysicsWorld>();

        float startHeight = 20.0f;
        m_camera.setPosition({0, startHeight, 0});
        m_camera.setSpeed(15.0f);

        m_camera.setEyeHeight(1.7f);
        m_camera.setGravity(40.0f);
        m_camera.setJumpVelocity(15.0f);
        m_camera.setMaxSlopeAngle(80.0f);
        m_camera.setCollisionRadius(1.5f);
        m_camera.setNoClip(true);  // Editor mode starts with noclip enabled

        std::cout << "Terrain Editor Controls:\n";
        std::cout << "  Right-click + drag - Look around\n";
        std::cout << "  WASD - Move camera\n";
        std::cout << "  Space - Jump (walk mode) / Up (fly mode)\n";
        std::cout << "  Shift - Down (fly mode)\n";
        std::cout << "  Double-tap Space - Toggle fly mode\n";
        std::cout << "  Ctrl - Speed boost\n";
        std::cout << "  Left-click - Paint with brush\n";

        // Session mode: auto-open terminal with claude
        if (m_sessionMode) {
            m_editorUI.showTerminal() = true;
            m_terminal.init(120, 40);
            m_terminalInitialized = true;
            // Give shell a moment to start, then launch claude
            // (sendCommand will queue it — shell processes it when ready)
            m_terminal.sendCommand("claude");
            std::cout << "[EDEN OS] Session mode: terminal + claude auto-launched" << std::endl;
        }
    }

    void initializeEconomySystems() {
        // Create economy system
        m_economySystem = std::make_unique<EconomySystem>();

        // Create city governor
        m_cityGovernor = std::make_unique<CityGovernor>();
        m_cityGovernor->setEconomySystem(m_economySystem.get());
        m_cityGovernor->setAutoBuild(true);

        // Create pathfinder
        m_pathfinder = std::make_unique<AStarPathfinder>();
        m_pathfinder->setNodes(&m_aiNodes);

        // Note: Traders are now created via the "trader" script on models
        // No pre-created AI traders or player trader

        std::cout << "Economy systems initialized\n";
    }

    void initMCPServer() {
        m_mcpServer = std::make_unique<MCPServer>(9998);

        // ── ping ──
        m_mcpServer->registerTool("ping", "Connectivity test — returns pong",
            [](const MCPParams&) -> MCPResult {
                return {{"message", MCPValue("pong")}};
            });

        // ── get_camera_position ──
        m_mcpServer->registerTool("get_camera_position", "Get current camera world position",
            [this](const MCPParams&) -> MCPResult {
                auto pos = m_camera.getPosition();
                return {
                    {"x", MCPValue(pos.x)},
                    {"y", MCPValue(pos.y)},
                    {"z", MCPValue(pos.z)}
                };
            });

        // ── set_camera_position ──
        m_mcpServer->registerTool("set_camera_position", "Move camera to world position (x, y, z)",
            [this](const MCPParams& p) -> MCPResult {
                auto xi = p.find("x"), yi = p.find("y"), zi = p.find("z");
                if (xi == p.end() || zi == p.end())
                    return {{"error", MCPValue("Missing x or z parameter")}};
                float x = xi->second.getFloat();
                float z = zi->second.getFloat();
                float y = (yi != p.end()) ? yi->second.getFloat() : m_terrain.getHeightAt(x, z) + 10.0f;
                m_camera.setPosition({x, y, z});
                return {
                    {"x", MCPValue(x)},
                    {"y", MCPValue(y)},
                    {"z", MCPValue(z)}
                };
            });

        // ── get_terrain_height ──
        m_mcpServer->registerTool("get_terrain_height", "Get terrain height at world (x, z)",
            [this](const MCPParams& p) -> MCPResult {
                auto xi = p.find("x"), zi = p.find("z");
                if (xi == p.end() || zi == p.end())
                    return {{"error", MCPValue("Missing x or z parameter")}};
                float x = xi->second.getFloat();
                float z = zi->second.getFloat();
                float h = m_terrain.getHeightAt(x, z);
                return {{"x", MCPValue(x)}, {"z", MCPValue(z)}, {"height", MCPValue(h)}};
            });

        // ── list_scene_objects ──
        m_mcpServer->registerTool("list_scene_objects", "List all scene objects with positions and types",
            [this](const MCPParams&) -> MCPResult {
                std::ostringstream ss;
                ss << "[";
                bool first = true;
                for (const auto& obj : m_sceneObjects) {
                    if (!obj) continue;
                    if (!first) ss << ",";
                    first = false;
                    auto pos = obj->getTransform().getPosition();
                    ss << "{\"name\":\"" << obj->getName()
                       << "\",\"x\":" << pos.x
                       << ",\"y\":" << pos.y
                       << ",\"z\":" << pos.z;
                    if (!obj->getBuildingType().empty())
                        ss << ",\"buildingType\":\"" << obj->getBuildingType() << "\"";
                    if (!obj->getModelPath().empty())
                        ss << ",\"modelPath\":\"" << obj->getModelPath() << "\"";
                    ss << "}";
                }
                ss << "]";
                return {
                    {"count", MCPValue(static_cast<int>(m_sceneObjects.size()))},
                    {"objects", MCPValue(ss.str())}
                };
            });

        // ── query_zone ──
        m_mcpServer->registerTool("query_zone", "Get zone type, resource, owner, price at world (x, z)",
            [this](const MCPParams& p) -> MCPResult {
                auto xi = p.find("x"), zi = p.find("z");
                if (xi == p.end() || zi == p.end())
                    return {{"error", MCPValue("Missing x or z parameter")}};
                float x = xi->second.getFloat();
                float z = zi->second.getFloat();
                if (!m_zoneSystem)
                    return {{"error", MCPValue("Zone system not initialized")}};

                auto zt = m_zoneSystem->getZoneType(x, z);
                auto rt = m_zoneSystem->getResource(x, z);
                uint32_t owner = m_zoneSystem->getOwner(x, z);
                auto grid = m_zoneSystem->worldToGrid(x, z);
                float price = m_zoneSystem->getPlotPrice(grid.x, grid.y);

                return {
                    {"zone_type", MCPValue(std::string(ZoneSystem::zoneTypeName(zt)))},
                    {"resource", MCPValue(std::string(ZoneSystem::resourceTypeName(rt)))},
                    {"owner_id", MCPValue(static_cast<int>(owner))},
                    {"price", MCPValue(price)},
                    {"grid_x", MCPValue(grid.x)},
                    {"grid_z", MCPValue(grid.y)}
                };
            });

        // ── get_zone_summary ──
        m_mcpServer->registerTool("get_zone_summary", "Overview of all zone types and resource counts",
            [this](const MCPParams&) -> MCPResult {
                if (!m_zoneSystem)
                    return {{"error", MCPValue("Zone system not initialized")}};

                int wilderness = 0, battlefield = 0, spawn = 0;
                int residential = 0, commercial = 0, industrial = 0, resource = 0;
                std::map<std::string, int> resourceCounts;

                int w = m_zoneSystem->getGridWidth();
                int h = m_zoneSystem->getGridHeight();
                for (int gz = 0; gz < h; gz++) {
                    for (int gx = 0; gx < w; gx++) {
                        auto worldPos = m_zoneSystem->gridToWorld(gx, gz);
                        const ZoneCell* cell = m_zoneSystem->getCell(worldPos.x, worldPos.y);
                        if (!cell) continue;
                        switch (cell->type) {
                            case ZoneType::Wilderness:  wilderness++; break;
                            case ZoneType::Battlefield: battlefield++; break;
                            case ZoneType::SpawnSafe:   spawn++; break;
                            case ZoneType::Residential: residential++; break;
                            case ZoneType::Commercial:  commercial++; break;
                            case ZoneType::Industrial:  industrial++; break;
                            case ZoneType::Resource:    resource++; break;
                        }
                        if (!cell->resourceName.empty())
                            resourceCounts[cell->resourceName]++;
                    }
                }

                // Build resource counts JSON string
                std::ostringstream resSS;
                resSS << "{";
                bool first = true;
                for (auto& [name, cnt] : resourceCounts) {
                    if (!first) resSS << ",";
                    first = false;
                    resSS << "\"" << name << "\":" << cnt;
                }
                resSS << "}";

                return {
                    {"total_cells", MCPValue(w * h)},
                    {"wilderness", MCPValue(wilderness)},
                    {"battlefield", MCPValue(battlefield)},
                    {"spawn_safe", MCPValue(spawn)},
                    {"residential", MCPValue(residential)},
                    {"commercial", MCPValue(commercial)},
                    {"industrial", MCPValue(industrial)},
                    {"resource_cells", MCPValue(resource)},
                    {"resource_counts", MCPValue(resSS.str())}
                };
            });

        // ── get_building_catalog ──
        m_mcpServer->registerTool("get_building_catalog", "List all building types with properties",
            [](const MCPParams&) -> MCPResult {
                const auto& catalog = ::getCityBuildingCatalog();
                std::ostringstream ss;
                ss << "[";
                bool first = true;
                for (const auto& def : catalog) {
                    if (!first) ss << ",";
                    first = false;
                    ss << "{\"type\":\"" << def.type
                       << "\",\"name\":\"" << def.name
                       << "\",\"category\":\"" << def.category
                       << "\",\"zoneReq\":\"" << def.zoneReq
                       << "\",\"cost\":" << def.cost
                       << ",\"maxWorkers\":" << def.maxWorkers
                       << ",\"footprint\":" << def.footprint
                       << ",\"produces\":\"" << def.produces
                       << "\",\"requires\":\"" << def.requires
                       << "\"}";
                }
                ss << "]";
                return {
                    {"count", MCPValue(static_cast<int>(catalog.size()))},
                    {"buildings", MCPValue(ss.str())}
                };
            });

        // ── list_buildings ──
        m_mcpServer->registerTool("list_buildings", "List all placed buildings with positions and types",
            [this](const MCPParams&) -> MCPResult {
                std::ostringstream ss;
                ss << "[";
                bool first = true;
                int count = 0;
                for (const auto& obj : m_sceneObjects) {
                    if (!obj || obj->getBuildingType().empty()) continue;
                    if (!first) ss << ",";
                    first = false;
                    auto pos = obj->getTransform().getPosition();
                    const ::CityBuildingDef* def = ::findCityBuildingDef(obj->getBuildingType());
                    ss << "{\"name\":\"" << obj->getName()
                       << "\",\"type\":\"" << obj->getBuildingType()
                       << "\",\"category\":\"" << (def ? def->category : "")
                       << "\",\"x\":" << pos.x
                       << ",\"y\":" << pos.y
                       << ",\"z\":" << pos.z
                       << "}";
                    count++;
                }
                ss << "]";
                return {
                    {"count", MCPValue(count)},
                    {"buildings", MCPValue(ss.str())}
                };
            });

        // ── place_building ──
        m_mcpServer->registerTool("place_building", "Place a building at position (type, x, z). Validates zone and deducts cost from city treasury.",
            [this](const MCPParams& p) -> MCPResult {
                auto ti = p.find("type"), xi = p.find("x"), zi = p.find("z");
                if (ti == p.end() || xi == p.end() || zi == p.end())
                    return {{"error", MCPValue("Missing type, x, or z parameter")}};

                std::string type = ti->second.getString();
                float posX = xi->second.getFloat();
                float posZ = zi->second.getFloat();

                const ::CityBuildingDef* def = ::findCityBuildingDef(type);
                if (!def)
                    return {{"error", MCPValue("Unknown building type: " + type)}};

                // Check zone compatibility
                if (m_zoneSystem) {
                    ZoneType zt = m_zoneSystem->getZoneType(posX, posZ);
                    bool matches = def->zoneReq.empty();
                    if (!matches) {
                        if (def->zoneReq == "residential" && zt == ZoneType::Residential) matches = true;
                        if (def->zoneReq == "commercial"  && zt == ZoneType::Commercial)  matches = true;
                        if (def->zoneReq == "industrial"  && zt == ZoneType::Industrial)  matches = true;
                        if (def->zoneReq == "resource"    && zt == ZoneType::Resource)    matches = true;
                    }
                    if (!matches)
                        return {{"error", MCPValue("Zone mismatch — " + type + " requires " + def->zoneReq + " zone")}};
                }

                // Check cost
                if (m_cityCredits < def->cost)
                    return {{"error", MCPValue("Insufficient city funds (need " + std::to_string(static_cast<int>(def->cost))
                                               + ", have " + std::to_string(static_cast<int>(m_cityCredits)) + ")")}};
                m_cityCredits -= def->cost;

                // Generate unique name
                int count = 0;
                for (auto& obj : m_sceneObjects) {
                    if (obj && obj->getBuildingType() == type) count++;
                }
                std::string objName = def->name + "_" + std::to_string(count + 1);

                float terrainY = m_terrain.getHeightAt(posX, posZ);

                // Spawn placeholder cube colored by category
                float size = def->footprint * 0.6f;
                glm::vec4 color(0.7f, 0.7f, 0.7f, 1.0f);
                if (def->category == "housing")    color = glm::vec4(0.9f, 0.8f, 0.2f, 1.0f);
                else if (def->category == "food")       color = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f);
                else if (def->category == "resource")   color = glm::vec4(0.6f, 0.4f, 0.2f, 1.0f);
                else if (def->category == "industry")   color = glm::vec4(0.5f, 0.5f, 0.6f, 1.0f);
                else if (def->category == "commercial") color = glm::vec4(0.2f, 0.5f, 0.9f, 1.0f);

                auto meshData = PrimitiveMeshBuilder::createCube(size, color);
                auto obj = std::make_unique<SceneObject>(objName);
                uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
                obj->setBufferHandle(handle);
                obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
                obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
                obj->setLocalBounds(meshData.bounds);
                obj->setModelPath("");
                obj->setMeshData(meshData.vertices, meshData.indices);
                obj->setPrimitiveType(PrimitiveType::Cube);
                obj->setPrimitiveSize(size);
                obj->setPrimitiveColor(color);
                obj->getTransform().setPosition(glm::vec3(posX, terrainY, posZ));
                obj->setName(objName);
                obj->setBuildingType(type);
                obj->setDescription(def->name);

                m_sceneObjects.push_back(std::move(obj));

                return {
                    {"placed", MCPValue(true)},
                    {"name", MCPValue(objName)},
                    {"type", MCPValue(type)},
                    {"x", MCPValue(posX)},
                    {"y", MCPValue(terrainY)},
                    {"z", MCPValue(posZ)},
                    {"cost", MCPValue(def->cost)},
                    {"city_credits_remaining", MCPValue(m_cityCredits)}
                };
            });

        // ── find_empty_plot ──
        m_mcpServer->registerTool("find_empty_plot", "Find a suitable empty location for a building type (type, optional near_x/near_z)",
            [this](const MCPParams& p) -> MCPResult {
                auto ti = p.find("type");
                if (ti == p.end())
                    return {{"error", MCPValue("Missing type parameter")}};

                std::string type = ti->second.getString();
                const ::CityBuildingDef* def = ::findCityBuildingDef(type);
                if (!def)
                    return {{"error", MCPValue("Unknown building type: " + type)}};
                if (!m_zoneSystem)
                    return {{"error", MCPValue("Zone system not initialized")}};

                // Collect existing building positions and footprints
                std::vector<std::pair<glm::vec2, float>> existingBuildings;
                for (auto& obj : m_sceneObjects) {
                    if (!obj || obj->getBuildingType().empty()) continue;
                    auto pos = obj->getTransform().getPosition();
                    const ::CityBuildingDef* bd = ::findCityBuildingDef(obj->getBuildingType());
                    float fp = bd ? bd->footprint : 10.0f;
                    existingBuildings.push_back({{pos.x, pos.z}, fp});
                }

                // Search center
                float centerX = 0.0f, centerZ = 0.0f;
                auto nxi = p.find("near_x"), nzi = p.find("near_z");
                if (nxi != p.end()) centerX = nxi->second.getFloat();
                if (nzi != p.end()) centerZ = nzi->second.getFloat();

                float cellSize = m_zoneSystem->getCellSize();
                float bestDist = 1e9f;
                glm::vec2 bestPos(0.0f);
                bool found = false;

                // Spiral search from center
                int maxRadius = 50;
                for (int r = 0; r <= maxRadius && !found; r++) {
                    for (int dz = -r; dz <= r; dz++) {
                        for (int dx = -r; dx <= r; dx++) {
                            if (std::abs(dx) != r && std::abs(dz) != r) continue; // only shell
                            float wx = centerX + dx * cellSize;
                            float wz = centerZ + dz * cellSize;

                            // Check zone match
                            ZoneType zt = m_zoneSystem->getZoneType(wx, wz);
                            bool matches = def->zoneReq.empty();
                            if (!matches) {
                                if (def->zoneReq == "residential" && zt == ZoneType::Residential) matches = true;
                                if (def->zoneReq == "commercial"  && zt == ZoneType::Commercial)  matches = true;
                                if (def->zoneReq == "industrial"  && zt == ZoneType::Industrial)  matches = true;
                                if (def->zoneReq == "resource"    && zt == ZoneType::Resource)    matches = true;
                            }
                            if (!matches) continue;

                            // Check no existing building too close
                            bool tooClose = false;
                            for (auto& [bp, bfp] : existingBuildings) {
                                float minDist = (bfp + def->footprint) * 0.5f;
                                if (glm::length(glm::vec2(wx, wz) - bp) < minDist) {
                                    tooClose = true;
                                    break;
                                }
                            }
                            if (tooClose) continue;

                            float dist = glm::length(glm::vec2(wx - centerX, wz - centerZ));
                            if (dist < bestDist) {
                                bestDist = dist;
                                bestPos = {wx, wz};
                                found = true;
                            }
                        }
                    }
                }

                if (!found)
                    return {{"error", MCPValue("No suitable plot found for " + type)}};

                float h = m_terrain.getHeightAt(bestPos.x, bestPos.y);
                return {
                    {"x", MCPValue(bestPos.x)},
                    {"z", MCPValue(bestPos.y)},
                    {"terrain_height", MCPValue(h)},
                    {"zone_type", MCPValue(std::string(ZoneSystem::zoneTypeName(m_zoneSystem->getZoneType(bestPos.x, bestPos.y))))},
                    {"distance_from_center", MCPValue(bestDist)}
                };
            });

        // ── get_city_stats ──
        m_mcpServer->registerTool("get_city_stats", "Get city statistics: population, housing, workers, production, treasury",
            [this](const MCPParams&) -> MCPResult {
                const auto& catalog = ::getCityBuildingCatalog();
                int totalBuildings = 0;
                int totalWorkerSlots = 0;
                int housingCount = 0;
                std::unordered_map<std::string, int> buildingCounts;
                std::unordered_map<std::string, int> production;

                for (const auto& obj : m_sceneObjects) {
                    if (!obj || obj->getBuildingType().empty()) continue;
                    totalBuildings++;
                    buildingCounts[obj->getBuildingType()]++;
                    const ::CityBuildingDef* def = ::findCityBuildingDef(obj->getBuildingType());
                    if (def) {
                        totalWorkerSlots += def->maxWorkers;
                        if (def->category == "housing") housingCount++;
                        if (!def->produces.empty()) production[def->produces]++;
                    }
                }

                // Build summary strings
                std::ostringstream countsSS;
                countsSS << "{";
                bool first = true;
                for (const auto& def : catalog) {
                    if (!first) countsSS << ",";
                    first = false;
                    int c = 0;
                    auto it = buildingCounts.find(def.type);
                    if (it != buildingCounts.end()) c = it->second;
                    countsSS << "\"" << def.type << "\":" << c;
                }
                countsSS << "}";

                std::ostringstream prodSS;
                prodSS << "{";
                first = true;
                for (auto& [res, cnt] : production) {
                    if (!first) prodSS << ",";
                    first = false;
                    prodSS << "\"" << res << "\":" << cnt;
                }
                prodSS << "}";

                return {
                    {"total_buildings", MCPValue(totalBuildings)},
                    {"housing_count", MCPValue(housingCount)},
                    {"estimated_population", MCPValue(housingCount * 4)},
                    {"total_worker_slots", MCPValue(totalWorkerSlots)},
                    {"city_credits", MCPValue(m_cityCredits)},
                    {"building_counts", MCPValue(countsSS.str())},
                    {"production", MCPValue(prodSS.str())}
                };
            });

        // ── Planet/Species tools (query backend on localhost:8080) ──

        // ── generate_planet ──
        m_mcpServer->registerTool("generate_planet", "Generate a random planet. Optional params: seed(int), biome(string), government(string), tech_level(int)",
            [](const MCPParams& p) -> MCPResult {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);

                // Build JSON body from params
                std::ostringstream body;
                body << "{";
                bool first = true;
                auto si = p.find("seed");
                if (si != p.end()) { body << "\"seed\":" << si->second.getInt(); first = false; }
                auto bi = p.find("biome");
                if (bi != p.end()) { if (!first) body << ","; body << "\"biome\":\"" << bi->second.getString() << "\""; first = false; }
                auto gi = p.find("government");
                if (gi != p.end()) { if (!first) body << ","; body << "\"government\":\"" << gi->second.getString() << "\""; first = false; }
                auto ti = p.find("tech_level");
                if (ti != p.end()) { if (!first) body << ","; body << "\"tech_level\":" << ti->second.getInt(); first = false; }
                body << "}";

                auto res = cli.Post("/planet/generate", body.str(), "application/json");
                if (res && res->status == 200) {
                    return {{"planet_json", MCPValue(res->body)}};
                }
                return {{"error", MCPValue("Backend not available — start backend/server.py")}};
            });

        // ── get_planet_info ──
        m_mcpServer->registerTool("get_planet_info", "Get the current planet profile (biome, species, tech level, resources)",
            [](const MCPParams&) -> MCPResult {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                auto res = cli.Get("/planet/current");
                if (res && res->status == 200) {
                    return {{"planet_json", MCPValue(res->body)}};
                }
                return {{"error", MCPValue("No planet generated or backend not available")}};
            });

        // ── get_species_info ──
        m_mcpServer->registerTool("get_species_info", "Get species data by civilization ID (e.g. 'democracy_7'). Param: civ_id",
            [](const MCPParams& p) -> MCPResult {
                auto ci = p.find("civ_id");
                if (ci == p.end())
                    return {{"error", MCPValue("Missing civ_id parameter")}};
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                auto res = cli.Get(("/species/" + ci->second.getString()).c_str());
                if (res && res->status == 200) {
                    return {{"species_json", MCPValue(res->body)}};
                }
                return {{"error", MCPValue("Species not found or backend not available")}};
            });

        // ── get_tech_capabilities ──
        m_mcpServer->registerTool("get_tech_capabilities", "Get all tech levels with capabilities and available buildings",
            [](const MCPParams&) -> MCPResult {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                auto res = cli.Get("/tech_levels");
                if (res && res->status == 200) {
                    return {{"tech_levels_json", MCPValue(res->body)}};
                }
                return {{"error", MCPValue("Backend not available")}};
            });

        // ── list_biomes ──
        m_mcpServer->registerTool("list_biomes", "List all available planet biome types",
            [](const MCPParams&) -> MCPResult {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                auto res = cli.Get("/planet/biomes");
                if (res && res->status == 200) {
                    return {{"biomes_json", MCPValue(res->body)}};
                }
                return {{"error", MCPValue("Backend not available")}};
            });

        // ── list_governments ──
        m_mcpServer->registerTool("list_governments", "List all government types with tendencies and descriptions",
            [](const MCPParams&) -> MCPResult {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                auto res = cli.Get("/governments");
                if (res && res->status == 200) {
                    return {{"governments_json", MCPValue(res->body)}};
                }
                return {{"error", MCPValue("Backend not available")}};
            });

        // ── get_diplomacy ──
        m_mcpServer->registerTool("get_diplomacy", "Get relationship between two civilizations. Params: civ_a, civ_b (e.g. 'democracy_7', 'empire_6')",
            [](const MCPParams& p) -> MCPResult {
                auto ai = p.find("civ_a"), bi = p.find("civ_b");
                if (ai == p.end() || bi == p.end())
                    return {{"error", MCPValue("Missing civ_a or civ_b parameter")}};
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                std::string path = "/diplomacy/" + ai->second.getString() + "/" + bi->second.getString();
                auto res = cli.Get(path.c_str());
                if (res && res->status == 200) {
                    return {{"diplomacy_json", MCPValue(res->body)}};
                }
                return {{"error", MCPValue("Backend not available")}};
            });

        // ── generate_world (procedural settlement) ──
        m_mcpServer->registerTool("generate_world", "Generate a random planet and build a settlement on the current terrain. Optional: seed(int), biome(string)",
            [this](const MCPParams& p) -> MCPResult {
                // Optionally pass seed/biome to backend
                std::ostringstream body;
                body << "{";
                bool first = true;
                auto si = p.find("seed");
                if (si != p.end()) { body << "\"seed\":" << si->second.getInt(); first = false; }
                auto bi = p.find("biome");
                if (bi != p.end()) { if (!first) body << ","; body << "\"biome\":\"" << bi->second.getString() << "\""; first = false; }
                body << "}";

                // Fetch planet from backend
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(5);
                auto res = cli.Post("/planet/generate", body.str(), "application/json");
                if (!res || res->status != 200)
                    return {{"error", MCPValue("Backend not available — start backend/server.py")}};

                try {
                    m_planetData = nlohmann::json::parse(res->body);
                } catch (...) {
                    return {{"error", MCPValue("Failed to parse planet JSON")}};
                }

                // Generate zone layout
                if (m_zoneSystem) {
                    m_zoneSystem->generatePlanetLayout(m_planetData);
                }

                // Build settlement
                int placed = buildSettlement(m_planetData);
                m_worldGenerated = true;

                std::string planetName = m_planetData.value("name", "Unknown");
                std::string biome = m_planetData.value("biome_name", "unknown");
                int pop = m_planetData.value("population", 0);
                int tech = m_planetData.value("tech_level", 0);

                return {
                    {"success", MCPValue(true)},
                    {"planet_name", MCPValue(planetName)},
                    {"biome", MCPValue(biome)},
                    {"population", MCPValue(pop)},
                    {"tech_level", MCPValue(tech)},
                    {"buildings_placed", MCPValue(placed)},
                    {"city_credits", MCPValue(m_cityCredits)}
                };
            });

        m_mcpServer->start();
    }

    void onBeforeMainLoop() override {
        if (m_terrain.getConfig().useFixedBounds) {
            int totalChunks = m_terrain.getTotalChunkCount();
            std::cout << "Pre-loading " << totalChunks << " terrain chunks...\n";

            m_chunkManager->preloadAllChunks(m_terrain, [this](int loaded, int total) {
                m_chunksLoaded = loaded;
                m_totalChunks = total;
                if (loaded % 32 == 0 || loaded == total) {
                    getWindow().pollEvents();
                    renderLoadingScreen();
                }
            });

            m_terrain.update(m_camera.getPosition());
            std::cout << "Terrain loaded! Total chunks: " << totalChunks << "\n";
        }

        // Auto-load xenk.eden from Desktop if it exists
        {
            const char* home = getenv("HOME");
            if (home) {
                std::string defaultLevel = std::string(home) + "/Desktop/xenk.eden";
                if (std::filesystem::exists(defaultLevel)) {
                    std::cout << "Auto-loading " << defaultLevel << std::endl;
                    loadLevel(defaultLevel);
                }
            }
        }
    }

    void onCleanup() override {
        // Shutdown terminal before Vulkan cleanup
        m_terminalScreenObject = nullptr;
        m_terminalPixelsDirty = false;
        m_terminalScreenBound = false;
        m_terminal.shutdown();

        getContext().waitIdle();

        saveEditorConfig();
        Audio::getInstance().shutdown();
        NFD_Quit();
        cleanupSplashTexture();
        cleanupGroveLogoTexture();
        cleanupBuildingTextures();
        if (m_groveVm) { grove_destroy(m_groveVm); m_groveVm = nullptr; }

        if (m_mcpServer) {
            m_mcpServer->stop();
            m_mcpServer.reset();
        }

        m_imguiManager.cleanup();

        m_waterRenderer.reset();
        m_skinnedModelRenderer.reset();
        m_modelRenderer.reset();
        m_skybox.reset();
        m_pipeline.reset();
    }

    void update(float deltaTime) override {
        // Lazy-bind terminal to "terminal_screen" scene object
        if (!m_terminalScreenBound) {
            for (auto& obj : m_sceneObjects) {
                if (obj->getName().find("terminal_screen") == 0) {
                    m_terminalScreenObject = obj.get();
                    std::cout << "[EdenTerminal] Bound to: " << obj->getName() << std::endl;
                    break;
                }
            }
            if (m_terminalScreenObject) {
                if (!m_terminalInitialized) {
                    m_terminal.init(82, 41);  // Full-face UV at 3x scale with small padding
                    m_terminalInitialized = true;
                }
                m_terminal.setLockSize(true);  // Don't let ImGui window resize the terminal
                m_terminalScreenBound = true;
            }
        }

        // Update terminal emulator (data only — texture upload happens in render)
        if (m_terminal.isAlive()) {
            m_terminal.update();

            // Render terminal to pixel buffer (CPU side, variant 2 = vertical flip)
            if (m_terminalScreenObject) {
                if (m_terminal.renderToPixels(m_terminalPixelBuffer, 2048, 2048, 2)) {
                    m_terminalPixelsDirty = true;
                }
            }
            m_terminal.clearDirty();
        }

        // Process pending filesystem navigation
        m_filesystemBrowser.processNavigation();
        m_filesystemBrowser.updateAnimations(deltaTime);

        // Update level transition fade (runs even during transitions)
        updateFade(deltaTime);

        handleCameraInput(deltaTime);
        handleKeyboardShortcuts(deltaTime);

        m_totalTime += deltaTime;
        m_actionSystem.update(deltaTime, m_camera.getPosition());
        m_dialogueRenderer.update(deltaTime);
        updateChatLog(deltaTime);

        // Update skinned model animations
        for (const auto& objPtr : m_sceneObjects) {
            if (objPtr && objPtr->isSkinned()) {
                m_skinnedModelRenderer->updateAnimation(objPtr->getSkinnedModelHandle(), deltaTime);
            }
        }

        // Poll for AI backend responses
        if (m_httpClient) {
            m_httpClient->pollResponses();
        }

        // Update TTS cooldown timer
        if (m_ttsCooldown > 0.0f) m_ttsCooldown -= deltaTime;

        // Heartbeat: passive perception for all EDEN companions in scene (play mode only)
        // Skip heartbeat while chat TTS is playing or pending to prevent audio overlap
        if (m_heartbeatEnabled && m_isPlayMode && m_httpClient && m_httpClient->isConnected()
            && !m_heartbeatInFlight && !m_ttsInFlight && m_ttsCooldown <= 0.0f) {
            m_heartbeatTimer += deltaTime;
            if (m_heartbeatTimer >= m_heartbeatInterval) {
                m_heartbeatTimer = 0.0f;

                // Find first EDEN companion in the scene
                SceneObject* companion = nullptr;
                for (const auto& obj : m_sceneObjects) {
                    if (obj && obj->getBeingType() == BeingType::EDEN_COMPANION) {
                        companion = obj.get();
                        break;
                    }
                }

                if (companion) {
                    m_heartbeatInFlight = true;
                    PerceptionData perception = performScanCone(companion, 360.0f, 50.0f);
                    std::string npcName = companion->getName();
                    int beingType = static_cast<int>(companion->getBeingType());

                    // Use quick chat session for this NPC (persists across heartbeats)
                    std::string sessionId = m_quickChatSessionIds.count(npcName) ?
                                            m_quickChatSessionIds[npcName] : "";

                    m_httpClient->sendHeartbeat(sessionId, npcName, beingType, perception,
                        [this, npcName, companion](const AsyncHttpClient::Response& resp) {
                            m_heartbeatInFlight = false;
                            if (!resp.success) return;

                            try {
                                auto json = nlohmann::json::parse(resp.body);

                                // Track session ID
                                if (json.contains("session_id") && !json["session_id"].is_null()) {
                                    m_quickChatSessionIds[npcName] = json["session_id"].get<std::string>();
                                }

                                // Extract response text
                                std::string responseText;
                                if (json.contains("response") && !json["response"].is_null()) {
                                    responseText = json["response"].get<std::string>();
                                }

                                // If NPC has something to say, show in chat log and speak
                                if (!responseText.empty()) {
                                    addChatMessage(npcName, responseText);

                                    // Also add to conversation history if we're talking to this NPC
                                    if (m_inConversation && m_currentInteractObject == companion) {
                                        m_conversationHistory.push_back({npcName, responseText, false});
                                        m_scrollToBottom = true;
                                    }

                                    // speakTTS internally blocks if another TTS is playing
                                    speakTTS(responseText, npcName);

                                    // Cycle expression on each response
                                    cycleExpression(companion);
                                }

                                // Handle action if present (needs m_currentInteractObject)
                                if (json.contains("action") && !json["action"].is_null()) {
                                    SceneObject* prevTarget = m_currentInteractObject;
                                    m_currentInteractObject = companion;
                                    executeAIAction(json["action"]);
                                    if (!m_inConversation) {
                                        m_currentInteractObject = prevTarget;
                                    }
                                }

                                // Parse spatial analysis for mind map
                                if (json.contains("spatial_analysis") && !json["spatial_analysis"].is_null()) {
                                    m_editorUI.updateSpatialGrid(json["spatial_analysis"]);
                                }

                            } catch (const std::exception& e) {
                                std::cerr << "[Heartbeat] Parse error: " << e.what() << std::endl;
                            }
                        });
                }
            }
        }

        // Process MCP server commands
        if (m_mcpServer) {
            m_mcpServer->processCommands();
        }

        trackFPS(deltaTime);

        m_terrain.update(m_camera.getPosition());
        m_chunkManager->uploadPendingChunks(m_terrain);

        if (m_isPlayMode) {
            updatePlayMode(deltaTime);
            return;
        }

        updateEditorMode(deltaTime);
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        m_chunkManager->processPendingDeletes();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Create dockspace over the entire viewport for side docking
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGuiWindowFlags dockspaceFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpaceWindow", nullptr, dockspaceFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();

        if (m_isPlayMode) {
            renderPlayModeUI();
        } else {
            m_editorUI.render();
            renderModulePanel();
            renderZoneOverlay();

            // Terminal emulator window (lazy-init on first show)
            if (m_editorUI.showTerminal()) {
                if (!m_terminalInitialized) {
                    m_terminal.init(120, 40);
                    m_terminalInitialized = true;
                }
                if (m_terminal.isAlive()) {
                    m_terminal.renderImGui(&m_editorUI.showTerminal(), m_monoFont);
                }
            }

            // Wall draw / foundation preview (green wireframe box)
            if (m_wallDrawing) {
                float wallH = (m_editorUI.getBrushMode() == BrushMode::Foundation)
                    ? m_editorUI.getFoundationHeight() : m_editorUI.getWallHeight();
                float x1 = std::min(m_wallCorner1.x, m_wallCorner2.x);
                float x2 = std::max(m_wallCorner1.x, m_wallCorner2.x);
                float z1 = std::min(m_wallCorner1.z, m_wallCorner2.z);
                float z2 = std::max(m_wallCorner1.z, m_wallCorner2.z);
                float yBot = std::min(m_wallCorner1.y, m_wallCorner2.y);
                float yTop = yBot + wallH;

                glm::vec3 corners[8] = {
                    {x1, yBot, z1}, {x2, yBot, z1}, {x2, yBot, z2}, {x1, yBot, z2},
                    {x1, yTop, z1}, {x2, yTop, z1}, {x2, yTop, z2}, {x1, yTop, z2}
                };

                float wAspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();
                glm::mat4 wVP = m_camera.getProjectionMatrix(wAspect, 0.1f, 5000.0f) * m_camera.getViewMatrix();
                float sw = static_cast<float>(getWindow().getWidth());
                float sh = static_cast<float>(getWindow().getHeight());

                auto projectW = [&](const glm::vec3& world) -> ImVec2 {
                    glm::vec4 clip = wVP * glm::vec4(world, 1.0f);
                    if (clip.w <= 0.001f) return ImVec2(-1, -1);
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    return ImVec2((ndc.x * 0.5f + 0.5f) * sw, (ndc.y * -0.5f + 0.5f) * sh);
                };

                ImVec2 sp[8];
                for (int i = 0; i < 8; i++) sp[i] = projectW(corners[i]);

                auto* drawList = ImGui::GetForegroundDrawList();
                ImU32 green = IM_COL32(0, 255, 0, 200);

                // Bottom edges
                drawList->AddLine(sp[0], sp[1], green, 2.0f);
                drawList->AddLine(sp[1], sp[2], green, 2.0f);
                drawList->AddLine(sp[2], sp[3], green, 2.0f);
                drawList->AddLine(sp[3], sp[0], green, 2.0f);
                // Top edges
                drawList->AddLine(sp[4], sp[5], green, 2.0f);
                drawList->AddLine(sp[5], sp[6], green, 2.0f);
                drawList->AddLine(sp[6], sp[7], green, 2.0f);
                drawList->AddLine(sp[7], sp[4], green, 2.0f);
                // Vertical pillars
                drawList->AddLine(sp[0], sp[4], green, 2.0f);
                drawList->AddLine(sp[1], sp[5], green, 2.0f);
                drawList->AddLine(sp[2], sp[6], green, 2.0f);
                drawList->AddLine(sp[3], sp[7], green, 2.0f);
            }
        }

        // Draw collision hull when in third-person with checkbox on (works in both modes)
        if (m_editorUI.getCameraMode() == EditorUI::CameraMode::ThirdPerson &&
            m_editorUI.getShowCollisionHull()) {
            ImDrawList* hullDrawList = ImGui::GetForegroundDrawList();
            VkExtent2D hullExtent = getSwapchain().getExtent();
            float hullAspect = static_cast<float>(hullExtent.width) / static_cast<float>(hullExtent.height);
            glm::mat4 hullVP = m_camera.getProjectionMatrix(hullAspect, 0.1f, 5000.0f) * m_camera.getViewMatrix();
            float sw = static_cast<float>(hullExtent.width);
            float sh = static_cast<float>(hullExtent.height);

            auto proj3D = [&](const glm::vec3& wp, ImVec2& sp) -> bool {
                glm::vec4 clip = hullVP * glm::vec4(wp, 1.0f);
                if (clip.w <= 0.001f) return false;
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                sp.x = (ndc.x * 0.5f + 0.5f) * sw;
                sp.y = (ndc.y * -0.5f + 0.5f) * sh;
                return ndc.z >= 0.0f && ndc.z <= 1.0f;
            };

            glm::vec3 pp = m_thirdPersonPlayerPos;
            float eyeH = m_collisionHullHeight;
            float collR = m_collisionHullRadius;
            float feetY = pp.y - eyeH;
            ImU32 hullCol = IM_COL32(0, 255, 255, 200);

            const int segs = 16;
            for (int ring = 0; ring <= 2; ring++) {
                float ringY = feetY + ring * (eyeH * 0.5f);
                ImVec2 prev;
                bool prevOk = false;
                for (int i = 0; i <= segs; i++) {
                    float angle = (float)i / segs * 6.28318f;
                    glm::vec3 wp(pp.x + std::cos(angle) * collR, ringY, pp.z + std::sin(angle) * collR);
                    ImVec2 sp;
                    bool ok = proj3D(wp, sp);
                    if (ok && prevOk) {
                        hullDrawList->AddLine(prev, sp, hullCol, 1.5f);
                    }
                    prev = sp;
                    prevOk = ok;
                }
            }

            for (int i = 0; i < 4; i++) {
                float angle = (float)i / 4 * 6.28318f;
                float dx = std::cos(angle) * collR;
                float dz = std::sin(angle) * collR;
                ImVec2 bot, top;
                bool botOk = proj3D(glm::vec3(pp.x + dx, feetY, pp.z + dz), bot);
                bool topOk = proj3D(glm::vec3(pp.x + dx, feetY + eyeH, pp.z + dz), top);
                if (botOk && topOk) {
                    hullDrawList->AddLine(bot, top, hullCol, 1.5f);
                }
            }
        }

        // Zone map (M key) — works in both editor and play mode
        if (m_showZoneMap) {
            renderZoneMap();
        }

        // Render conversation UI when in conversation mode
        if (m_inConversation) {
            renderConversationUI();
        }

        // Render quick chat bar when in quick chat mode
        if (m_quickChatMode) {
            renderQuickChatUI();
        }

        // Render Minecraft-style chat log (always visible when there are messages)
        renderChatLog();

        // Render persistent world chat history window (Tab to toggle)
        renderWorldChatHistory();

        // Render dialogue bubbles (uses ImGui foreground draw list)
        {
            VkExtent2D extent = getSwapchain().getExtent();
            float aspect = (float)extent.width / (float)extent.height;
            glm::mat4 view = m_camera.getViewMatrix();
            glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 5000.0f);
            proj[1][1] *= -1.0f; // Flip Y for Vulkan
            glm::mat4 viewProj = proj * view;
            m_dialogueRenderer.render(viewProj, (float)extent.width, (float)extent.height);

            // Debug: render facing direction arrow for AI NPCs (Xenk + Eve)
            // Use unflipped projection for glm::project (it expects OpenGL convention)
            glm::mat4 projGL = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 5000.0f);
            for (const auto& obj : m_sceneObjects) {
                if (!obj || (obj->getBeingType() != BeingType::AI_ARCHITECT && obj->getBeingType() != BeingType::EVE && obj->getBeingType() != BeingType::ROBOT)) continue;

                glm::vec3 npcPos = obj->getTransform().getPosition();
                glm::vec3 euler = obj->getEulerRotation();
                float yawRad = glm::radians(euler.y);
                glm::vec3 facing(sin(yawRad), 0.0f, cos(yawRad));

                float arrowLen = 3.0f;
                float headSize = 0.6f;
                float liftY = 0.5f;

                glm::vec3 base = npcPos + glm::vec3(0, liftY, 0);
                glm::vec3 tip = base + facing * arrowLen;

                glm::vec3 right(facing.z, 0.0f, -facing.x);
                glm::vec3 wingL = tip - facing * headSize + right * headSize * 0.5f;
                glm::vec3 wingR = tip - facing * headSize - right * headSize * 0.5f;

                glm::vec4 viewport(0, 0, (float)extent.width, (float)extent.height);
                float h = (float)extent.height;

                auto toScreen = [&](const glm::vec3& worldPt, ImVec2& out) -> bool {
                    glm::vec3 s = glm::project(worldPt, view, projGL, viewport);
                    if (s.z <= 0 || s.z >= 1) return false;
                    out = ImVec2(s.x, h - s.y);
                    return true;
                };

                ImVec2 sBase, sTip, sWingL, sWingR;
                if (toScreen(base, sBase) && toScreen(tip, sTip) &&
                    toScreen(wingL, sWingL) && toScreen(wingR, sWingR)) {
                    ImDrawList* dl = ImGui::GetForegroundDrawList();
                    dl->AddLine(sBase, sTip, IM_COL32(0, 255, 100, 200), 2.0f);
                    dl->AddTriangleFilled(sTip, sWingL, sWingR, IM_COL32(0, 255, 100, 200));
                }
            }
        }

        // Render level transition fade overlay (on top of everything)
        renderFadeOverlay();

        ImGui::Render();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = getSwapchain().getRenderPass();
        renderPassInfo.framebuffer = getSwapchain().getFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = getSwapchain().getExtent();

        std::array<VkClearValue, 2> clearValues{};
        // Use neutral gray for test levels, black for space, sky blue for normal levels
        if (m_isSpaceLevel) {
            clearValues[0].color = {{0.0f, 0.0f, 0.02f, 1.0f}};  // Near black for space
        } else if (m_isTestLevel) {
            clearValues[0].color = {{0.2f, 0.2f, 0.2f, 1.0f}};
        } else {
            clearValues[0].color = {{0.5f, 0.7f, 1.0f, 1.0f}};
        }
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        float aspect = static_cast<float>(getSwapchain().getExtent().width) /
                       static_cast<float>(getSwapchain().getExtent().height);
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 5000.0f);
        proj[1][1] *= -1;
        glm::mat4 vp = proj * view;

        // Skip skybox in test level mode (but show it in space level for stars)
        if (m_skybox && !m_isTestLevel) {
            m_skybox->render(cmd, view, proj);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getHandle());

        VkDescriptorSet texDescSet = m_textureManager->getDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipeline->getLayout(), 0, 1, &texDescSet, 0, nullptr);

        TerrainPushConstants pushConstants{};
        pushConstants.fogColor = glm::vec4(m_editorUI.getFogColor(), 1.0f);
        pushConstants.fogStart = m_editorUI.getFogStart();
        pushConstants.fogEnd = m_editorUI.getFogEnd();

        // Skip terrain rendering in test/space level mode
        if (!m_isTestLevel && !m_isSpaceLevel) {
            for (const auto& vc : m_terrain.getVisibleChunks()) {
                auto* buffers = getBufferManager().getMeshBuffers(vc.chunk->getBufferHandle());
                if (!buffers || !buffers->vertexBuffer) continue;

                VkBuffer vertexBuffers[] = {buffers->vertexBuffer->getHandle()};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

                if (buffers->indexBuffer) {
                    vkCmdBindIndexBuffer(cmd, buffers->indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT32);
                }

                glm::mat4 model = glm::translate(glm::mat4(1.0f), vc.renderOffset);
                pushConstants.mvp = vp * model;
                vkCmdPushConstants(cmd, m_pipeline->getLayout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(TerrainPushConstants), &pushConstants);

                if (buffers->indexBuffer) {
                    vkCmdDrawIndexed(cmd, buffers->indexCount, 1, 0, 0, 0);
                } else {
                    vkCmdDraw(cmd, buffers->vertexCount, 1, 0, 0);
                }
            }
        }

        // Upload terminal texture to GPU before rendering objects
        if (m_terminalScreenObject && m_terminalPixelsDirty && !m_terminalPixelBuffer.empty()) {
            m_terminalPixelsDirty = false;
            m_modelRenderer->updateTexture(
                m_terminalScreenObject->getBufferHandle(),
                m_terminalPixelBuffer.data(), 2048, 2048);
        }

        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            const auto& objPtr = m_sceneObjects[i];
            if (!objPtr || !objPtr->isVisible()) continue;

            // Hide door trigger zones in play mode (they're invisible interaction areas)
            // But keep filesystem doors visible — they represent folder entries
            if (m_isPlayMode && objPtr->isDoor() && objPtr->getBuildingType() != "filesystem") continue;

            glm::mat4 modelMatrix = const_cast<SceneObject*>(objPtr.get())->getTransform().getMatrix();

            float hue = objPtr->getHueShift();
            float sat = objPtr->getSaturation();
            float bright = objPtr->getBrightness();

            bool isSelected = (static_cast<int>(i) == m_selectedObjectIndex);
            if (isSelected && !m_isPlayMode) {
                hue += 15.0f;
                bright *= 1.3f;
            }

            // Hit flash effect - turn red when damaged
            if (objPtr->isHitFlashing()) {
                hue = 0.0f;      // Red hue
                sat = 3.0f;      // High saturation
                bright = 2.0f;   // Bright
            }

            // Use appropriate renderer based on model type
            if (objPtr->isSkinned()) {
                m_skinnedModelRenderer->render(cmd, vp, objPtr->getSkinnedModelHandle(), modelMatrix,
                                               hue, sat, bright);
            } else {
                m_modelRenderer->render(cmd, vp, objPtr->getBufferHandle(), modelMatrix,
                                        hue, sat, bright);
            }
        }

        // Draw wireframe outlines for selected/drag-hovered filesystem objects
        for (auto& obj : m_sceneObjects) {
            if (!obj || !obj->isSelected()) continue;
            const auto& bt = obj->getBuildingType();
            if (bt != "filesystem" && bt != "filesystem_wall") continue;
            // Green for drag-hover target, yellow for normal selection
            bool isDragHover = (m_fsDragActive && obj.get() == m_fsDragHoverWall);
            glm::vec3 wireColor = isDragHover ? glm::vec3(0.0f, 1.0f, 0.3f) : glm::vec3(1.0f, 0.7f, 0.0f);
            const AABB& lb = obj->getLocalBounds();
            if (lb.getSize().x > 0.001f || lb.getSize().y > 0.001f || lb.getSize().z > 0.001f) {
                glm::mat4 m = obj->getTransform().getMatrix();
                glm::vec3 c[8];
                c[0] = glm::vec3(m * glm::vec4(lb.min.x, lb.min.y, lb.min.z, 1.0f));
                c[1] = glm::vec3(m * glm::vec4(lb.max.x, lb.min.y, lb.min.z, 1.0f));
                c[2] = glm::vec3(m * glm::vec4(lb.max.x, lb.min.y, lb.max.z, 1.0f));
                c[3] = glm::vec3(m * glm::vec4(lb.min.x, lb.min.y, lb.max.z, 1.0f));
                c[4] = glm::vec3(m * glm::vec4(lb.min.x, lb.max.y, lb.min.z, 1.0f));
                c[5] = glm::vec3(m * glm::vec4(lb.max.x, lb.max.y, lb.min.z, 1.0f));
                c[6] = glm::vec3(m * glm::vec4(lb.max.x, lb.max.y, lb.max.z, 1.0f));
                c[7] = glm::vec3(m * glm::vec4(lb.min.x, lb.max.y, lb.max.z, 1.0f));
                std::vector<glm::vec3> boxLines = {
                    c[0],c[1], c[1],c[2], c[2],c[3], c[3],c[0],
                    c[4],c[5], c[5],c[6], c[6],c[7], c[7],c[4],
                    c[0],c[4], c[1],c[5], c[2],c[6], c[3],c[7]
                };
                m_modelRenderer->renderLines(cmd, vp, boxLines, wireColor);
            }
        }

        if (m_waterRenderer && m_waterRenderer->isVisible()) {
            m_waterRenderer->render(cmd, vp, m_camera.getPosition(), m_totalTime);
        }

        if (m_brushRing) {
            m_brushRing->render(cmd, vp);
        }

        // Draw line-based gizmo (matching model editor style)
        if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size()) && !m_isPlayMode) {
            SceneObject* selObj = m_sceneObjects[m_selectedObjectIndex].get();

            // OBB selection box wireframe (rotates with object)
            const AABB& lb = selObj->getLocalBounds();
            if (lb.getSize().x > 0.001f || lb.getSize().y > 0.001f || lb.getSize().z > 0.001f) {
                glm::mat4 m = selObj->getTransform().getMatrix();
                glm::vec3 c[8];
                c[0] = glm::vec3(m * glm::vec4(lb.min.x, lb.min.y, lb.min.z, 1.0f));
                c[1] = glm::vec3(m * glm::vec4(lb.max.x, lb.min.y, lb.min.z, 1.0f));
                c[2] = glm::vec3(m * glm::vec4(lb.max.x, lb.min.y, lb.max.z, 1.0f));
                c[3] = glm::vec3(m * glm::vec4(lb.min.x, lb.min.y, lb.max.z, 1.0f));
                c[4] = glm::vec3(m * glm::vec4(lb.min.x, lb.max.y, lb.min.z, 1.0f));
                c[5] = glm::vec3(m * glm::vec4(lb.max.x, lb.max.y, lb.min.z, 1.0f));
                c[6] = glm::vec3(m * glm::vec4(lb.max.x, lb.max.y, lb.max.z, 1.0f));
                c[7] = glm::vec3(m * glm::vec4(lb.min.x, lb.max.y, lb.max.z, 1.0f));
                std::vector<glm::vec3> boxLines = {
                    c[0],c[1], c[1],c[2], c[2],c[3], c[3],c[0],
                    c[4],c[5], c[5],c[6], c[6],c[7], c[7],c[4],
                    c[0],c[4], c[1],c[5], c[2],c[6], c[3],c[7]
                };
                m_modelRenderer->renderLines(cmd, vp, boxLines, glm::vec3(1.0f, 0.7f, 0.0f));
            }

            // Transform gizmo (Move/Rotate/Scale modes only)
            if (m_transformMode != TransformMode::Select && m_editorUI.getBrushMode() == BrushMode::MoveObject) {
                AABB wb = selObj->getWorldBounds();
                glm::vec3 gizmoPos((wb.min.x + wb.max.x) * 0.5f, wb.max.y, (wb.min.z + wb.max.z) * 0.5f);
                float dist = glm::length(m_camera.getPosition() - gizmoPos);
                float size = dist * 0.08f;  // Scale with camera distance

                GizmoAxis hovered = m_gizmoDragging ? m_gizmoActiveAxis : m_gizmoHoveredAxis;
                glm::vec3 xColor = (hovered == GizmoAxis::X) ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.2f, 0.2f);
                glm::vec3 yColor = (hovered == GizmoAxis::Y) ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(0.2f, 1.0f, 0.2f);
                glm::vec3 zColor = (hovered == GizmoAxis::Z) ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(0.2f, 0.2f, 1.0f);
                glm::vec3 xColorDim = (hovered == GizmoAxis::X) ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(0.6f, 0.12f, 0.12f);
                glm::vec3 yColorDim = (hovered == GizmoAxis::Y) ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(0.12f, 0.6f, 0.12f);
                glm::vec3 zColorDim = (hovered == GizmoAxis::Z) ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(0.12f, 0.12f, 0.6f);

                glm::vec3 xAxis(1, 0, 0), yAxis(0, 1, 0), zAxis(0, 0, 1);

                if (m_transformMode == TransformMode::Rotate) {
                    // Circles around each axis
                    auto makeCircle = [](glm::vec3 center, float radius, glm::vec3 axis, int segments = 32) {
                        std::vector<glm::vec3> lines;
                        glm::vec3 perp1, perp2;
                        if (std::abs(axis.x) < 0.9f)
                            perp1 = glm::normalize(glm::cross(axis, glm::vec3(1, 0, 0)));
                        else
                            perp1 = glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0)));
                        perp2 = glm::normalize(glm::cross(axis, perp1));
                        for (int i = 0; i < segments; ++i) {
                            float a1 = (float)i / segments * 2.0f * 3.14159265f;
                            float a2 = (float)(i + 1) / segments * 2.0f * 3.14159265f;
                            lines.push_back(center + (perp1 * std::cos(a1) + perp2 * std::sin(a1)) * radius);
                            lines.push_back(center + (perp1 * std::cos(a2) + perp2 * std::sin(a2)) * radius);
                        }
                        return lines;
                    };
                    m_modelRenderer->renderLines(cmd, vp, makeCircle(gizmoPos, size * 0.9f, xAxis), xColor);
                    m_modelRenderer->renderLines(cmd, vp, makeCircle(gizmoPos, size * 0.9f, yAxis), yColor);
                    m_modelRenderer->renderLines(cmd, vp, makeCircle(gizmoPos, size * 0.9f, zAxis), zColor);
                } else {
                    // Move or Scale: axis lines with arrowheads or cube handles
                    auto getArrowPerps = [](const glm::vec3& ax) -> std::pair<glm::vec3, glm::vec3> {
                        glm::vec3 up = (std::abs(ax.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                        glm::vec3 p1 = glm::normalize(glm::cross(ax, up));
                        glm::vec3 p2 = glm::normalize(glm::cross(ax, p1));
                        return {p1, p2};
                    };
                    auto makeCubeLines = [](glm::vec3 center, float halfSize) {
                        float s = halfSize;
                        std::vector<glm::vec3> lines;
                        lines.push_back(center + glm::vec3(-s,-s,-s)); lines.push_back(center + glm::vec3( s,-s,-s));
                        lines.push_back(center + glm::vec3( s,-s,-s)); lines.push_back(center + glm::vec3( s,-s, s));
                        lines.push_back(center + glm::vec3( s,-s, s)); lines.push_back(center + glm::vec3(-s,-s, s));
                        lines.push_back(center + glm::vec3(-s,-s, s)); lines.push_back(center + glm::vec3(-s,-s,-s));
                        lines.push_back(center + glm::vec3(-s, s,-s)); lines.push_back(center + glm::vec3( s, s,-s));
                        lines.push_back(center + glm::vec3( s, s,-s)); lines.push_back(center + glm::vec3( s, s, s));
                        lines.push_back(center + glm::vec3( s, s, s)); lines.push_back(center + glm::vec3(-s, s, s));
                        lines.push_back(center + glm::vec3(-s, s, s)); lines.push_back(center + glm::vec3(-s, s,-s));
                        lines.push_back(center + glm::vec3(-s,-s,-s)); lines.push_back(center + glm::vec3(-s, s,-s));
                        lines.push_back(center + glm::vec3( s,-s,-s)); lines.push_back(center + glm::vec3( s, s,-s));
                        lines.push_back(center + glm::vec3( s,-s, s)); lines.push_back(center + glm::vec3( s, s, s));
                        lines.push_back(center + glm::vec3(-s,-s, s)); lines.push_back(center + glm::vec3(-s, s, s));
                        return lines;
                    };
                    bool isScale = (m_transformMode == TransformMode::Scale);
                    float cubeSize = size * 0.12f;

                    // X axis
                    glm::vec3 xEnd = gizmoPos + xAxis * size;
                    std::vector<glm::vec3> xLines = { gizmoPos, xEnd };
                    if (isScale) {
                        auto cl = makeCubeLines(xEnd, cubeSize);
                        xLines.insert(xLines.end(), cl.begin(), cl.end());
                    } else {
                        auto [p1, p2] = getArrowPerps(xAxis);
                        glm::vec3 ab = gizmoPos + xAxis * (size * 0.85f);
                        xLines.push_back(xEnd); xLines.push_back(ab + p1 * (size * 0.1f));
                        xLines.push_back(xEnd); xLines.push_back(ab - p1 * (size * 0.1f));
                        xLines.push_back(xEnd); xLines.push_back(ab + p2 * (size * 0.1f));
                        xLines.push_back(xEnd); xLines.push_back(ab - p2 * (size * 0.1f));
                    }
                    m_modelRenderer->renderLines(cmd, vp, xLines, xColor);

                    // Y axis
                    glm::vec3 yEnd = gizmoPos + yAxis * size;
                    std::vector<glm::vec3> yLines = { gizmoPos, yEnd };
                    if (isScale) {
                        auto cl = makeCubeLines(yEnd, cubeSize);
                        yLines.insert(yLines.end(), cl.begin(), cl.end());
                    } else {
                        auto [p1, p2] = getArrowPerps(yAxis);
                        glm::vec3 ab = gizmoPos + yAxis * (size * 0.85f);
                        yLines.push_back(yEnd); yLines.push_back(ab + p1 * (size * 0.1f));
                        yLines.push_back(yEnd); yLines.push_back(ab - p1 * (size * 0.1f));
                        yLines.push_back(yEnd); yLines.push_back(ab + p2 * (size * 0.1f));
                        yLines.push_back(yEnd); yLines.push_back(ab - p2 * (size * 0.1f));
                    }
                    m_modelRenderer->renderLines(cmd, vp, yLines, yColor);

                    // Z axis
                    glm::vec3 zEnd = gizmoPos + zAxis * size;
                    std::vector<glm::vec3> zLines = { gizmoPos, zEnd };
                    if (isScale) {
                        auto cl = makeCubeLines(zEnd, cubeSize);
                        zLines.insert(zLines.end(), cl.begin(), cl.end());
                    } else {
                        auto [p1, p2] = getArrowPerps(zAxis);
                        glm::vec3 ab = gizmoPos + zAxis * (size * 0.85f);
                        zLines.push_back(zEnd); zLines.push_back(ab + p1 * (size * 0.1f));
                        zLines.push_back(zEnd); zLines.push_back(ab - p1 * (size * 0.1f));
                        zLines.push_back(zEnd); zLines.push_back(ab + p2 * (size * 0.1f));
                        zLines.push_back(zEnd); zLines.push_back(ab - p2 * (size * 0.1f));
                    }
                    m_modelRenderer->renderLines(cmd, vp, zLines, zColor);

                    // Negative axis arms (dimmer colors)
                    // -X axis
                    glm::vec3 nxEnd = gizmoPos - xAxis * size;
                    std::vector<glm::vec3> nxLines = { gizmoPos, nxEnd };
                    if (isScale) {
                        auto cl = makeCubeLines(nxEnd, cubeSize);
                        nxLines.insert(nxLines.end(), cl.begin(), cl.end());
                    } else {
                        auto [p1, p2] = getArrowPerps(xAxis);
                        glm::vec3 ab = gizmoPos - xAxis * (size * 0.85f);
                        nxLines.push_back(nxEnd); nxLines.push_back(ab + p1 * (size * 0.1f));
                        nxLines.push_back(nxEnd); nxLines.push_back(ab - p1 * (size * 0.1f));
                        nxLines.push_back(nxEnd); nxLines.push_back(ab + p2 * (size * 0.1f));
                        nxLines.push_back(nxEnd); nxLines.push_back(ab - p2 * (size * 0.1f));
                    }
                    m_modelRenderer->renderLines(cmd, vp, nxLines, xColorDim);

                    // -Y axis
                    glm::vec3 nyEnd = gizmoPos - yAxis * size;
                    std::vector<glm::vec3> nyLines = { gizmoPos, nyEnd };
                    if (isScale) {
                        auto cl = makeCubeLines(nyEnd, cubeSize);
                        nyLines.insert(nyLines.end(), cl.begin(), cl.end());
                    } else {
                        auto [p1, p2] = getArrowPerps(yAxis);
                        glm::vec3 ab = gizmoPos - yAxis * (size * 0.85f);
                        nyLines.push_back(nyEnd); nyLines.push_back(ab + p1 * (size * 0.1f));
                        nyLines.push_back(nyEnd); nyLines.push_back(ab - p1 * (size * 0.1f));
                        nyLines.push_back(nyEnd); nyLines.push_back(ab + p2 * (size * 0.1f));
                        nyLines.push_back(nyEnd); nyLines.push_back(ab - p2 * (size * 0.1f));
                    }
                    m_modelRenderer->renderLines(cmd, vp, nyLines, yColorDim);

                    // -Z axis
                    glm::vec3 nzEnd = gizmoPos - zAxis * size;
                    std::vector<glm::vec3> nzLines = { gizmoPos, nzEnd };
                    if (isScale) {
                        auto cl = makeCubeLines(nzEnd, cubeSize);
                        nzLines.insert(nzLines.end(), cl.begin(), cl.end());
                    } else {
                        auto [p1, p2] = getArrowPerps(zAxis);
                        glm::vec3 ab = gizmoPos - zAxis * (size * 0.85f);
                        nzLines.push_back(nzEnd); nzLines.push_back(ab + p1 * (size * 0.1f));
                        nzLines.push_back(nzEnd); nzLines.push_back(ab - p1 * (size * 0.1f));
                        nzLines.push_back(nzEnd); nzLines.push_back(ab + p2 * (size * 0.1f));
                        nzLines.push_back(nzEnd); nzLines.push_back(ab - p2 * (size * 0.1f));
                    }
                    m_modelRenderer->renderLines(cmd, vp, nzLines, zColorDim);
                }
            }
        }

        // Draw yellow outlines for Alt+click face selection
        if (!m_selectedFaces.empty() && !m_isPlayMode) {
            std::vector<glm::vec3> faceLines;
            for (const auto& sf : m_selectedFaces) {
                if (sf.objectIndex < 0 || sf.objectIndex >= static_cast<int>(m_sceneObjects.size())) continue;
                auto& obj = m_sceneObjects[sf.objectIndex];
                if (!obj) continue;
                glm::vec3 P = obj->getTransform().getPosition();
                glm::vec3 q0, q1, q2, q3;
                if (sf.normal.x == 1) {       // +X face
                    float x = P.x + 0.5f;
                    q0 = {x, P.y,     P.z - 0.5f};
                    q1 = {x, P.y,     P.z + 0.5f};
                    q2 = {x, P.y + 1, P.z + 0.5f};
                    q3 = {x, P.y + 1, P.z - 0.5f};
                } else if (sf.normal.x == -1) { // -X face
                    float x = P.x - 0.5f;
                    q0 = {x, P.y,     P.z - 0.5f};
                    q1 = {x, P.y,     P.z + 0.5f};
                    q2 = {x, P.y + 1, P.z + 0.5f};
                    q3 = {x, P.y + 1, P.z - 0.5f};
                } else if (sf.normal.y == 1) {  // +Y face (top)
                    float y = P.y + 1.0f;
                    q0 = {P.x - 0.5f, y, P.z - 0.5f};
                    q1 = {P.x + 0.5f, y, P.z - 0.5f};
                    q2 = {P.x + 0.5f, y, P.z + 0.5f};
                    q3 = {P.x - 0.5f, y, P.z + 0.5f};
                } else if (sf.normal.y == -1) { // -Y face (bottom)
                    float y = P.y;
                    q0 = {P.x - 0.5f, y, P.z - 0.5f};
                    q1 = {P.x + 0.5f, y, P.z - 0.5f};
                    q2 = {P.x + 0.5f, y, P.z + 0.5f};
                    q3 = {P.x - 0.5f, y, P.z + 0.5f};
                } else if (sf.normal.z == 1) {  // +Z face
                    float z = P.z + 0.5f;
                    q0 = {P.x - 0.5f, P.y,     z};
                    q1 = {P.x + 0.5f, P.y,     z};
                    q2 = {P.x + 0.5f, P.y + 1, z};
                    q3 = {P.x - 0.5f, P.y + 1, z};
                } else if (sf.normal.z == -1) { // -Z face
                    float z = P.z - 0.5f;
                    q0 = {P.x - 0.5f, P.y,     z};
                    q1 = {P.x + 0.5f, P.y,     z};
                    q2 = {P.x + 0.5f, P.y + 1, z};
                    q3 = {P.x - 0.5f, P.y + 1, z};
                } else {
                    continue;
                }
                faceLines.push_back(q0); faceLines.push_back(q1);
                faceLines.push_back(q1); faceLines.push_back(q2);
                faceLines.push_back(q2); faceLines.push_back(q3);
                faceLines.push_back(q3); faceLines.push_back(q0);
            }
            if (!faceLines.empty()) {
                m_modelRenderer->renderLines(cmd, vp, faceLines, glm::vec3(1.0f, 0.7f, 0.0f));
            }
        }

        if (m_splineRenderer && m_splineRenderer->isVisible()) {
            m_splineRenderer->render(cmd, vp);
        }

        // Render AI nodes
        if (m_aiNodeRenderer && m_aiNodeRenderer->isVisible()) {
            m_aiNodeRenderer->render(cmd, vp);
        }

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

    void onSwapchainRecreated() override {
        m_pipeline = std::make_unique<TerrainPipeline>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent(),
            m_textureManager->getDescriptorSetLayout());

        SkyParameters savedParams = m_skybox->getParameters();
        m_skybox = std::make_unique<ProceduralSkybox>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());
        m_skybox->updateParameters(savedParams);
        m_editorUI.setSkyParameters(&m_skybox->getParameters());

        m_brushRing = std::make_unique<BrushRing>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_gizmoRenderer = std::make_unique<GizmoRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_splineRenderer = std::make_unique<SplineRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        m_aiNodeRenderer->recreatePipeline(getSwapchain().getRenderPass(), getSwapchain().getExtent());
        m_modelRenderer->recreatePipeline(getSwapchain().getRenderPass(), getSwapchain().getExtent());
        m_skinnedModelRenderer->recreatePipeline(getSwapchain().getRenderPass(), getSwapchain().getExtent());
        m_waterRenderer->recreatePipeline(getSwapchain().getRenderPass(), getSwapchain().getExtent());
    }

private:
    void setupUICallbacks() {
        m_editorUI.setSpeedChangedCallback([this](float speed) {
            m_camera.setSpeed(speed);
            m_cameraSpeed = speed;  // Keep in sync so slider doesn't reset
        });

        m_editorUI.setClearSelectionCallback([this]() {
            m_terrain.clearAllSelection();
            m_chunkManager->updateModifiedChunks(m_terrain);
            m_gizmo.setVisible(false);
        });

        m_editorUI.setMoveSelectionCallback([this](const glm::vec3& delta) {
            m_terrain.moveSelection(delta);
            m_chunkManager->updateModifiedChunks(m_terrain);
        });

        m_editorUI.setTiltSelectionCallback([this](float tiltX, float tiltZ) {
            m_terrain.tiltSelection(tiltX, tiltZ);
            m_chunkManager->updateModifiedChunks(m_terrain);
        });

        m_editorUI.setImportModelCallback([this](const std::string& path) {
            importModel(path);
        });

        m_editorUI.setApplyBuildingTextureCallback([this](SceneObject* target, int textureIndex, float uScale, float vScale) {
            if (!target || textureIndex < 0 || textureIndex >= static_cast<int>(m_buildingTextures.size())) return;
            auto& tex = m_buildingTextures[textureIndex];
            target->setTextureData(tex.pixels, tex.width, tex.height);
            m_modelRenderer->updateTexture(target->getBufferHandle(), tex.pixels.data(), tex.width, tex.height);

            // Rescale UVs on the mesh vertices
            if (target->hasMeshData() && (std::abs(uScale - 1.0f) > 0.001f || std::abs(vScale - 1.0f) > 0.001f)) {
                auto vertices = target->getVertices();  // copy
                for (auto& v : vertices) {
                    v.texCoord.x *= uScale;
                    v.texCoord.y *= vScale;
                }
                target->setMeshData(vertices, target->getIndices());
                m_modelRenderer->updateVertices(target->getBufferHandle(), vertices);
            }
        });

        // Face-aware texture application (Alt+click face selection → apply to all selected blocks)
        m_editorUI.setApplyFaceTextureCallback([this](int textureIndex, float uScale, float vScale) {
            if (textureIndex < 0 || textureIndex >= static_cast<int>(m_buildingTextures.size())) return;
            if (m_selectedFaces.empty()) return;

            auto& tex = m_buildingTextures[textureIndex];

            // Collect unique object indices
            std::set<int> uniqueIndices;
            for (const auto& sf : m_selectedFaces) {
                uniqueIndices.insert(sf.objectIndex);
            }

            for (int idx : uniqueIndices) {
                if (idx < 0 || idx >= static_cast<int>(m_sceneObjects.size())) continue;
                auto* obj = m_sceneObjects[idx].get();
                if (!obj) continue;

                // Apply texture to the whole block
                obj->setTextureData(tex.pixels, tex.width, tex.height);
                m_modelRenderer->updateTexture(obj->getBufferHandle(), tex.pixels.data(), tex.width, tex.height);

                // Set vertex colors to white so texture shows through
                if (obj->hasMeshData()) {
                    auto vertices = obj->getVertices();
                    for (auto& v : vertices) {
                        v.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                        v.texCoord.x *= uScale;
                        v.texCoord.y *= vScale;
                    }
                    obj->setMeshData(vertices, obj->getIndices());
                    m_modelRenderer->updateVertices(obj->getBufferHandle(), vertices);
                }
            }
        });

        m_editorUI.setBrowseModelCallback([this]() {
            showModelImportDialog();
        });

        m_editorUI.setSelectObjectCallback([this](int index) {
            selectObject(index);
        });

        m_editorUI.setDeleteObjectCallback([this](int index) {
            deleteObject(index);
        });

        m_editorUI.setMultiSelectObjectCallback([this](const std::set<int>& indices) {
            m_selectedObjectIndices = indices;
        });

        m_editorUI.setGroupObjectsCallback([this](const std::set<int>& indices, const std::string& name) {
            if (indices.size() < 2) return;
            EditorUI::ObjectGroup group;
            group.name = name;
            group.objectIndices = indices;
            group.expanded = true;
            m_objectGroups.push_back(group);
            m_editorUI.setObjectGroups(m_objectGroups);
            std::cout << "Created group '" << name << "' with " << indices.size() << " objects" << std::endl;
        });

        m_editorUI.setUngroupObjectsCallback([this](int groupIndex) {
            if (groupIndex >= 0 && groupIndex < static_cast<int>(m_objectGroups.size())) {
                std::cout << "Ungrouped '" << m_objectGroups[groupIndex].name << "'" << std::endl;
                m_objectGroups.erase(m_objectGroups.begin() + groupIndex);
                m_editorUI.setObjectGroups(m_objectGroups);
            }
        });

        m_editorUI.setBulletCollisionChangedCallback([this](SceneObject* obj) {
            if (!obj || !m_physicsWorld) return;
            // Update the physics world when bullet collision type changes
            BulletCollisionType type = obj->getBulletCollisionType();
            std::cout << "[Physics] Bullet collision changed for " << obj->getName()
                      << " to type " << static_cast<int>(type) << std::endl;
            if (type == BulletCollisionType::NONE) {
                m_physicsWorld->removeObject(obj);
            } else {
                m_physicsWorld->addObject(obj, type);
            }
        });

        m_editorUI.setObjectTransformChangedCallback([this](SceneObject* obj) {
            if (!obj || !m_physicsWorld) return;
            // Update physics when object transform changes (especially scale)
            m_physicsWorld->updateObjectTransform(obj);
        });

        // Freeze Transform: bake rotation/scale into mesh vertices
        m_editorUI.setFreezeTransformCallback([this](SceneObject* obj) {
            if (!obj || !obj->hasMeshData()) {
                std::cout << "Cannot freeze transform: no mesh data" << std::endl;
                return;
            }

            // Get current transform
            glm::quat rotation = obj->getTransform().getRotation();
            glm::vec3 scale = obj->getTransform().getScale();

            // Skip if already identity
            if (rotation == glm::quat(1, 0, 0, 0) && scale == glm::vec3(1.0f)) {
                std::cout << "Transform already frozen (identity rotation and scale)" << std::endl;
                return;
            }

            // Get mesh data
            std::vector<ModelVertex> vertices = obj->getVertices();
            const std::vector<uint32_t>& indices = obj->getIndices();

            // Create transformation matrices
            glm::mat4 rotMat = glm::mat4_cast(rotation);
            glm::mat3 normalMat = glm::mat3(rotMat);  // For transforming normals (rotation only)

            // Transform each vertex
            glm::vec3 minBounds(FLT_MAX);
            glm::vec3 maxBounds(-FLT_MAX);

            for (auto& v : vertices) {
                // Apply scale then rotation to position
                glm::vec3 scaledPos = v.position * scale;
                v.position = glm::vec3(rotMat * glm::vec4(scaledPos, 1.0f));

                // Rotate normals (no scale)
                v.normal = glm::normalize(normalMat * v.normal);

                // Track bounds
                minBounds = glm::min(minBounds, v.position);
                maxBounds = glm::max(maxBounds, v.position);
            }

            // Update mesh data in SceneObject
            obj->setMeshData(vertices, indices);

            // Update local bounds
            AABB newBounds;
            newBounds.min = minBounds;
            newBounds.max = maxBounds;
            obj->setLocalBounds(newBounds);

            // Store the frozen transform for serialization (accumulate if already frozen)
            glm::vec3 eulerRotation = obj->getEulerRotation();
            if (obj->hasFrozenTransform()) {
                // Accumulate with previous frozen transform
                glm::vec3 prevRot = obj->getFrozenRotation();
                glm::vec3 prevScale = obj->getFrozenScale();
                obj->setFrozenTransform(prevRot + eulerRotation, prevScale * scale);
            } else {
                obj->setFrozenTransform(eulerRotation, scale);
            }

            // Reset rotation and scale to identity (keep position)
            obj->setEulerRotation(glm::vec3(0.0f));
            obj->getTransform().setScale(glm::vec3(1.0f));

            // Update GPU buffer
            if (obj->getBufferHandle() != UINT32_MAX) {
                m_modelRenderer->updateModelBuffer(obj->getBufferHandle(), vertices);
            }

            // Update physics if needed
            if (m_physicsWorld) {
                m_physicsWorld->updateObjectTransform(obj);
            }

            std::cout << "Frozen transform for " << obj->getName()
                      << " - new bounds: (" << minBounds.x << "," << minBounds.y << "," << minBounds.z
                      << ") to (" << maxBounds.x << "," << maxBounds.y << "," << maxBounds.z << ")" << std::endl;
        });

        m_editorUI.setApplyPathCallback([this]() {
            m_pathTool->applyToPath(
                m_editorUI.getPathBrushMode(),
                m_editorUI.getBrushRadius(),
                m_editorUI.getBrushStrength(),
                m_editorUI.getBrushFalloff(),
                m_editorUI.getPaintColor(),
                m_editorUI.getSelectedTexture(),
                m_editorUI.getSelectedTexHue(),
                m_editorUI.getSelectedTexSaturation(),
                m_editorUI.getSelectedTexBrightness(),
                m_editorUI.getPathElevation()
            );
            m_chunkManager->updateModifiedChunks(m_terrain);
        });

        m_editorUI.setClearPathCallback([this]() {
            m_pathTool->clearPoints();
        });

        m_editorUI.setUndoPathPointCallback([this]() {
            m_pathTool->removeLastPoint();
        });

        m_editorUI.setCreateTubeCallback([this](float radius, int segments, const glm::vec3& color) {
            if (m_pathTool->getPointCount() < 2) return;

            // Generate tube mesh from path
            auto tubeMesh = m_pathTool->generateTubeMesh(radius, segments, color);
            if (tubeMesh.vertices.empty()) return;

            // Create SceneObject from mesh
            auto obj = GLBLoader::createSceneObject(tubeMesh, *m_modelRenderer);
            if (obj) {
                obj->setName("Wire_" + std::to_string(m_sceneObjects.size()));
                // Don't set model path - this is procedural geometry
                m_sceneObjects.push_back(std::move(obj));
                std::cout << "Created tube mesh with " << tubeMesh.vertices.size()
                          << " vertices, " << tubeMesh.indices.size() / 3 << " triangles" << std::endl;
            }
        });

        m_editorUI.setCreateRoadCallback([this](float width, const glm::vec3& color, bool useFixedY, float fixedY) {
            if (m_pathTool->getPointCount() < 2) return;

            auto roadMesh = m_pathTool->generateRoadMesh(width, color, useFixedY, fixedY);
            if (roadMesh.vertices.empty()) return;

            auto obj = GLBLoader::createSceneObject(roadMesh, *m_modelRenderer);
            if (obj) {
                obj->setName("Road_" + std::to_string(m_sceneObjects.size()));
                m_sceneObjects.push_back(std::move(obj));
                std::cout << "Created road mesh with " << roadMesh.vertices.size()
                          << " vertices, " << roadMesh.indices.size() / 3 << " triangles" << std::endl;
            }
        });

        m_editorUI.setFileNewCallback([this]() {
            newLevel();
        });
        m_editorUI.setNewTestLevelCallback([this]() {
            newTestLevel();
        });
        m_editorUI.setNewSpaceLevelCallback([this]() {
            newSpaceLevel();
        });
        m_editorUI.setFileOpenCallback([this]() {
            showLoadDialog();
        });
        m_editorUI.setFileSaveCallback([this]() {
            showSaveDialog();
        });
        m_editorUI.setFileExitCallback([this]() {
            getWindow().close();
        });
        m_editorUI.setExportTerrainCallback([this]() {
            exportTerrainOBJ();
        });

        m_editorUI.setAddSpawnCallback([this]() {
            addSpawnPoint();
        });
        m_editorUI.setAddCylinderCallback([this]() {
            addCylinder();
        });
        m_editorUI.setAddCubeCallback([this](float size) {
            addCube(size);
        });
        m_editorUI.setAddDoorCallback([this]() {
            addDoor();
        });
        m_editorUI.setRunGameCallback([this]() {
            runGame();
        });

        // AI Node callbacks
        m_editorUI.setToggleAIPlacementCallback([this](bool /*enabled*/, int nodeType) {
            // Drop a node directly below the camera
            glm::vec3 camPos = m_camera.getPosition();
            float terrainHeight = m_terrain.getHeightAt(camPos.x, camPos.z);
            glm::vec3 nodePos(camPos.x, terrainHeight, camPos.z);

            m_aiPlacementType = nodeType;
            addAINode(nodePos, static_cast<AINodeType>(nodeType));
        });

        m_editorUI.setSelectAINodeCallback([this](int index) {
            selectAINode(index);
        });

        m_editorUI.setDeleteAINodeCallback([this](int index) {
            deleteAINode(index);
        });

        m_editorUI.setAINodePropertyChangedCallback([this]() {
            updateAINodeRenderer();
        });

        m_editorUI.setGenerateAINodesCallback([this](int pattern, int count, float radius) {
            generateAINodes(pattern, count, radius);
        });

        m_editorUI.setConnectAINodesCallback([this](int fromIndex, int toIndex) {
            if (fromIndex >= 0 && fromIndex < static_cast<int>(m_aiNodes.size()) &&
                toIndex >= 0 && toIndex < static_cast<int>(m_aiNodes.size()) &&
                fromIndex != toIndex) {
                uint32_t targetId = m_aiNodes[toIndex]->getId();
                m_aiNodes[fromIndex]->addConnection(targetId);
                updateAINodeRenderer();
            }
        });

        m_editorUI.setDisconnectAINodesCallback([this](int fromIndex, int toIndex) {
            if (fromIndex >= 0 && fromIndex < static_cast<int>(m_aiNodes.size()) &&
                toIndex >= 0 && toIndex < static_cast<int>(m_aiNodes.size())) {
                uint32_t targetId = m_aiNodes[toIndex]->getId();
                m_aiNodes[fromIndex]->removeConnection(targetId);
                updateAINodeRenderer();
            }
        });

        m_editorUI.setConnectAllGraphNodesCallback([this]() {
            connectAllGraphNodes();
        });

        m_editorUI.setCreateTestEconomyCallback([this]() {
            createTestEconomy();
        });

        // Path callbacks
        m_editorUI.setCreatePathFromNodesCallback([this](const std::string& name, const std::vector<int>& nodeIndices) {
            createPathFromNodes(name, nodeIndices);
        });

        m_editorUI.setDeletePathCallback([this](int index) {
            deletePath(index);
        });

        m_editorUI.setSelectPathCallback([this](int index) {
            selectPath(index);
        });

        m_editorUI.setPathPropertyChangedCallback([this]() {
            // Could update path renderer here if needed
        });

        // Script callbacks - handle adding/removing scripts from models
        m_editorUI.setScriptAddedCallback([this](int objectIndex, const std::string& scriptName) {
            if (objectIndex < 0 || objectIndex >= static_cast<int>(m_sceneObjects.size())) return;
            auto& obj = m_sceneObjects[objectIndex];

            if (scriptName == "trader") {
                // Create a TraderAI for this model
                auto trader = std::make_unique<TraderAI>(m_nextTraderId, obj->getName() + "_Trader");
                trader->setEconomySystem(m_economySystem.get());
                trader->setPathfinder(m_pathfinder.get());
                trader->setNodes(&m_aiNodes);
                trader->setAIEnabled(true);
                trader->setCredits(5000.0f);  // Starting capital
                trader->setCargoCapacity(150.0f);
                trader->setMovementLayer(GraphLayer::FLYING);
                trader->setMinProfitMargin(0.1f);

                // Set up health for the trader
                obj->setMaxHealth(100.0f);
                obj->setHealth(100.0f);

                // Place at a random graph node
                placeTraderAtRandomNode(trader.get());

                // Link the model to the trader
                obj->setTraderId(m_nextTraderId);
                m_nextTraderId++;

                m_modelTraders.push_back(std::move(trader));

                // Traders fight back when attacked - create DogfightAI for combat
                auto fighter = std::make_unique<DogfightAI>(m_nextDogfighterId, obj->getName() + "_Combat");
                fighter->setSceneObject(obj.get());
                fighter->setSpeed(50.0f);        // ~112 mph
                fighter->setTurnRate(60.0f);     // Tighter turns at lower speed
                fighter->setWeaponRange(300.0f);
                fighter->setWeaponConeAngle(15.0f);  // Wider cone = easier to hit
                fighter->setDamagePerShot(10.0f);
                fighter->setFireRate(5.0f);
                fighter->setDetectionRange(500.0f);
                fighter->setFaction(1);  // Trader faction

                // Cargo jettison callback (at 30% health)
                fighter->setOnCargoJettison([this](const glm::vec3& pos, float value) {
                    spawnJettisonedCargo(pos, value);
                });

                // Ejection callback (at 0% health)
                fighter->setOnEjection([this](const glm::vec3& pos, const glm::vec3& vel) {
                    spawnEjectedPilot(pos, vel);
                });

                fighter->setOnEvent([](const std::string& event) {
                    std::cout << "[TRADER COMBAT] " << event << std::endl;
                });

                fighter->setCargoValue(500.0f);  // Will jettison at 30% health
                m_nextDogfighterId++;
                m_dogfighters.push_back(std::move(fighter));

                std::cout << "Created trader for model: " << obj->getName() << " (will fight back if attacked)" << std::endl;
            }
            else if (scriptName == "fighter") {
                // Create a DogfightAI for combat (defensive - only fights when attacked)
                auto fighter = std::make_unique<DogfightAI>(m_nextDogfighterId, obj->getName() + "_Fighter");
                fighter->setSceneObject(obj.get());
                fighter->setSpeed(50.0f);        // ~112 mph
                fighter->setTurnRate(60.0f);     // Tighter turns at lower speed
                fighter->setWeaponRange(300.0f);
                fighter->setWeaponConeAngle(15.0f);  // Wider cone = easier to hit
                fighter->setDamagePerShot(10.0f);
                fighter->setFireRate(5.0f);
                fighter->setDetectionRange(500.0f);
                fighter->setFaction(1);  // AI faction (0 = player)

                // Set up health if not already set
                if (obj->getMaxHealth() <= 0) {
                    obj->setMaxHealth(100.0f);
                    obj->setHealth(100.0f);
                }

                // Cargo value for jettison (traders will have actual cargo value)
                fighter->setCargoValue(500.0f);

                // Cargo jettison callback (at 30% health)
                fighter->setOnCargoJettison([this](const glm::vec3& pos, float value) {
                    spawnJettisonedCargo(pos, value);
                });

                // Ejection callback (at 0% health)
                fighter->setOnEjection([this](const glm::vec3& pos, const glm::vec3& vel) {
                    spawnEjectedPilot(pos, vel);
                });

                // Event callback for debug
                fighter->setOnEvent([](const std::string& event) {
                    std::cout << "[FIGHTER] " << event << std::endl;
                });

                m_nextDogfighterId++;
                m_dogfighters.push_back(std::move(fighter));
                std::cout << "Created fighter AI for model: " << obj->getName() << std::endl;
            }
            else if (scriptName == "pirate") {
                // Create a DogfightAI for the pirate (aggressive - actively seeks targets)
                auto fighter = std::make_unique<DogfightAI>(m_nextDogfighterId, obj->getName() + "_Pirate");
                fighter->setSceneObject(obj.get());
                fighter->setSpeed(80.0f);        // ~179 mph, significantly faster than traders
                fighter->setTurnRate(80.0f);     // More agile than traders
                fighter->setWeaponRange(300.0f);
                fighter->setWeaponConeAngle(15.0f);  // Wider cone = easier to hit
                fighter->setDamagePerShot(12.0f);  // Hits harder
                fighter->setFireRate(6.0f);
                fighter->setDetectionRange(800.0f);  // Can see far
                fighter->setFaction(2);  // Pirate faction (hostile to all)

                // Set up health
                if (obj->getMaxHealth() <= 0) {
                    obj->setMaxHealth(80.0f);   // Pirates are less armored
                    obj->setHealth(80.0f);
                }

                // No cargo to jettison (pirates take, not give)
                fighter->setCargoValue(0.0f);

                // Ejection callback (pirates eject too)
                fighter->setOnEjection([this](const glm::vec3& pos, const glm::vec3& vel) {
                    spawnEjectedPilot(pos, vel);
                });

                // Event callback for debug
                fighter->setOnEvent([](const std::string& event) {
                    std::cout << "[PIRATE] " << event << std::endl;
                });

                // Create pirate tracker
                Pirate pirate;
                pirate.dogfighterId = m_nextDogfighterId;
                pirate.sceneObject = obj.get();
                pirate.scanTimer = static_cast<float>(rand() % 1000) / 1000.0f;  // Stagger scans
                m_pirates.push_back(pirate);

                m_nextDogfighterId++;
                m_dogfighters.push_back(std::move(fighter));
                std::cout << "Created pirate AI for model: " << obj->getName() << " (will hunt traders)" << std::endl;
            }
        });

        m_editorUI.setScriptRemovedCallback([this](int objectIndex, const std::string& scriptName) {
            if (objectIndex < 0 || objectIndex >= static_cast<int>(m_sceneObjects.size())) return;
            auto& obj = m_sceneObjects[objectIndex];

            if (scriptName == "trader") {
                uint32_t traderId = obj->getTraderId();

                // Remove the trader
                m_modelTraders.erase(
                    std::remove_if(m_modelTraders.begin(), m_modelTraders.end(),
                        [traderId](const std::unique_ptr<TraderAI>& t) {
                            return t->getId() == traderId;
                        }),
                    m_modelTraders.end()
                );

                // Also remove the associated DogfightAI
                SceneObject* rawPtr = obj.get();
                m_dogfighters.erase(
                    std::remove_if(m_dogfighters.begin(), m_dogfighters.end(),
                        [rawPtr](const std::unique_ptr<DogfightAI>& f) {
                            return f->getSceneObject() == rawPtr;
                        }),
                    m_dogfighters.end()
                );

                obj->setTraderId(0);
                std::cout << "Removed trader for model: " << obj->getName() << std::endl;
            }
            else if (scriptName == "fighter") {
                // Remove the fighter by finding one linked to this scene object
                SceneObject* rawPtr = obj.get();
                m_dogfighters.erase(
                    std::remove_if(m_dogfighters.begin(), m_dogfighters.end(),
                        [rawPtr](const std::unique_ptr<DogfightAI>& f) {
                            return f->getSceneObject() == rawPtr;
                        }),
                    m_dogfighters.end()
                );
                std::cout << "Removed fighter AI for model: " << obj->getName() << std::endl;
            }
            else if (scriptName == "pirate") {
                // Remove pirate tracker and DogfightAI
                SceneObject* rawPtr = obj.get();

                // Remove from pirate tracker
                m_pirates.erase(
                    std::remove_if(m_pirates.begin(), m_pirates.end(),
                        [rawPtr](const Pirate& p) {
                            return p.sceneObject == rawPtr;
                        }),
                    m_pirates.end()
                );

                // Remove the DogfightAI
                m_dogfighters.erase(
                    std::remove_if(m_dogfighters.begin(), m_dogfighters.end(),
                        [rawPtr](const std::unique_ptr<DogfightAI>& f) {
                            return f->getSceneObject() == rawPtr;
                        }),
                    m_dogfighters.end()
                );
                std::cout << "Removed pirate AI for model: " << obj->getName() << std::endl;
            }
        });
    }

    void initImGui() {
        m_imguiManager.init(getContext(), getSwapchain(), getWindow().getHandle(),
                           "imgui_terrain_editor.ini");
    }

    void loadSplashTexture() {
        const char* splashPath = "eden_splash.jpg";
        int channels;
        unsigned char* pixels = stbi_load(splashPath, &m_splashWidth, &m_splashHeight, &channels, STBI_rgb_alpha);
        if (!pixels) {
            std::cout << "Splash image not found at: " << splashPath << std::endl;
            return;
        }

        VkDevice device = getContext().getDevice();
        VkDeviceSize imageSize = m_splashWidth * m_splashHeight * 4;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = getContext().findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
        vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

        void* data;
        vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, imageSize);
        vkUnmapMemory(device, stagingMemory);

        stbi_image_free(pixels);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_splashWidth;
        imageInfo.extent.height = m_splashHeight;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        vkCreateImage(device, &imageInfo, nullptr, &m_splashImage);

        vkGetImageMemoryRequirements(device, m_splashImage, &memReq);
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = getContext().findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(device, &allocInfo, nullptr, &m_splashMemory);
        vkBindImageMemory(device, m_splashImage, m_splashMemory, 0);

        VkCommandBuffer cmd = getContext().beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_splashImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {static_cast<uint32_t>(m_splashWidth), static_cast<uint32_t>(m_splashHeight), 1};

        vkCmdCopyBufferToImage(cmd, stagingBuffer, m_splashImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        getContext().endSingleTimeCommands(cmd);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_splashImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(device, &viewInfo, nullptr, &m_splashView);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        vkCreateSampler(device, &samplerInfo, nullptr, &m_splashSampler);

        m_splashDescriptor = ImGui_ImplVulkan_AddTexture(m_splashSampler, m_splashView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_splashLoaded = true;
    }

    void cleanupSplashTexture() {
        if (!m_splashLoaded) return;

        VkDevice device = getContext().getDevice();

        if (m_splashDescriptor) {
            ImGui_ImplVulkan_RemoveTexture(m_splashDescriptor);
        }
        if (m_splashSampler) vkDestroySampler(device, m_splashSampler, nullptr);
        if (m_splashView) vkDestroyImageView(device, m_splashView, nullptr);
        if (m_splashImage) vkDestroyImage(device, m_splashImage, nullptr);
        if (m_splashMemory) vkFreeMemory(device, m_splashMemory, nullptr);

        m_splashLoaded = false;
    }

    void initGroveVM() {
        m_groveVm = grove_new();
        if (!m_groveVm) {
            std::cout << "Failed to create Grove VM\n";
            return;
        }
        grove_set_instruction_limit(m_groveVm, 1000000);

        // Populate context for grove host functions
        m_groveContext.sceneObjects = &m_sceneObjects;
        m_groveContext.terrain = &m_terrain;
        m_groveContext.camera = &m_camera;
        m_groveContext.modelRenderer = m_modelRenderer.get();
        m_groveContext.zoneSystem = m_zoneSystem.get();
        m_groveContext.groveVm = m_groveVm;
        m_groveContext.groveOutputAccum = &m_groveOutputAccum;
        m_groveContext.groveBotTarget = &m_groveBotTarget;
        m_groveContext.groveCurrentScriptName = &m_groveCurrentScriptName;
        m_groveContext.playerCredits = &m_playerCredits;
        m_groveContext.cityCredits = &m_cityCredits;
        m_groveContext.isPlayMode = &m_isPlayMode;
        m_groveContext.currentLevelPath = &m_currentLevelPath;
        m_groveContext.spawnPlotPosts = [this](int gx, int gz) { spawnPlotPosts(gx, gz); };
        m_groveContext.removePlotPosts = [this](int gx, int gz) { removePlotPosts(gx, gz); };
        m_groveContext.loadPathForAction = [this](SceneObject* o, const Action& a) { loadPathForAction(o, a); };

        registerGroveHostFunctions(m_groveVm, &m_groveContext);

        // Pass logo descriptor to EditorUI
        if (m_groveLogoLoaded) {
            m_editorUI.setGroveLogoDescriptor(m_groveLogoDescriptor);
        }

        // Wire up the run callback
        m_editorUI.setGroveRunCallback([this](const std::string& source) {
            m_groveOutputAccum.clear();
            int32_t ret = grove_eval(m_groveVm, source.c_str());
            if (ret == 0) {
                m_editorUI.setGroveOutput(m_groveOutputAccum);
            } else {
                const char* err = grove_last_error(m_groveVm);
                int line = static_cast<int>(grove_last_error_line(m_groveVm));
                m_editorUI.setGroveError(err ? err : "unknown error", line);
            }
        });

        // Wire up file callbacks
        m_editorUI.setGroveOpenCallback([this]() {
            nfdchar_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = {{"Grove Script", "grove"}};
            nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, m_groveScriptsDir.c_str());
            if (result == NFD_OKAY) {
                std::ifstream file(outPath);
                if (file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
                    m_editorUI.setGroveSource(content);
                    m_editorUI.setGroveCurrentFile(outPath);
                    m_editorUI.setGroveOutput("");
                    std::cout << "Opened grove script: " << outPath << std::endl;
                }
                NFD_FreePath(outPath);
            }
        });

        m_editorUI.setGroveSaveCallback([this](const std::string& source, const std::string& path) {
            std::ofstream file(path);
            if (file.is_open()) {
                file << source;
                file.close();
                m_editorUI.setGroveSource(source); // Clears modified flag
                std::cout << "Saved grove script: " << path << std::endl;
            } else {
                std::cerr << "Failed to save grove script: " << path << std::endl;
            }
        });

        m_editorUI.setGroveSaveAsCallback([this](const std::string& source) {
            nfdchar_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = {{"Grove Script", "grove"}};
            nfdresult_t result = NFD_SaveDialog(&outPath, filters, 1, m_groveScriptsDir.c_str(), "script.grove");
            if (result == NFD_OKAY) {
                std::string path = outPath;
                if (path.find(".grove") == std::string::npos) path += ".grove";
                std::ofstream file(path);
                if (file.is_open()) {
                    file << source;
                    file.close();
                    m_editorUI.setGroveSource(source);
                    m_editorUI.setGroveCurrentFile(path);
                    std::cout << "Saved grove script: " << path << std::endl;
                }
                NFD_FreePath(outPath);
            }
        });

        m_editorUI.setGroveFileListCallback([this]() -> std::vector<std::string> {
            std::vector<std::string> files;
            if (std::filesystem::exists(m_groveScriptsDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(m_groveScriptsDir)) {
                    if (entry.path().extension() == ".grove") {
                        files.push_back(entry.path().string());
                    }
                }
                std::sort(files.begin(), files.end());
            }
            return files;
        });

        // Wire up behavior script loading (Load Grove Script button in behavior editor)
        m_editorUI.setLoadBehaviorScriptCallback([this](SceneObject* target) {
            nfdchar_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = {{"Grove Script", "grove"}};
            nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, "scripts");
            if (result == NFD_OKAY) {
                std::ifstream file(outPath);
                if (file.is_open()) {
                    std::string source((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
                    file.close();

                    // Pre-set bot target to the selected object so queued
                    // functions land on it (script can still override with bot_target)
                    SceneObject* prevTarget = m_groveBotTarget;
                    m_groveBotTarget = target;

                    // Set behavior name to script filename (without .grove extension)
                    std::string prevScriptName = m_groveCurrentScriptName;
                    std::filesystem::path fp(outPath);
                    std::string baseName = fp.stem().string();
                    if (!baseName.empty()) {
                        m_groveCurrentScriptName = baseName;
                    }

                    std::cout << "[Grove] Loading script onto '" << target->getName()
                              << "': " << outPath << " (" << source.size() << " bytes)" << std::endl;

                    m_groveOutputAccum.clear();
                    int32_t ret = grove_eval(m_groveVm, source.c_str());
                    if (ret != 0) {
                        const char* err = grove_last_error(m_groveVm);
                        std::cerr << "[Grove] Script error: " << (err ? err : "unknown") << std::endl;
                    }

                    m_groveCurrentScriptName = prevScriptName;
                    // Restore previous bot target if script didn't change it
                    if (m_groveBotTarget == target) {
                        m_groveBotTarget = prevTarget;
                    }
                }
                NFD_FreePath(outPath);
            }
        });

        // List scripts for a bot by name (looks in scripts/<name>/)
        m_editorUI.setListBotScriptsCallback([](const std::string& botName) -> std::vector<std::string> {
            std::vector<std::string> scripts;
            std::string dir = "scripts/" + botName;
            if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (entry.path().extension() == ".grove") {
                        scripts.push_back(entry.path().filename().string());
                    }
                }
                std::sort(scripts.begin(), scripts.end());
            }
            return scripts;
        });

        // Load a specific bot script by name
        m_editorUI.setLoadBotScriptCallback([this](SceneObject* target, const std::string& scriptName) {
            std::string path = "scripts/" + target->getName() + "/" + scriptName;
            std::ifstream file(path);
            if (!file.is_open()) {
                std::cerr << "[Grove] Could not open: " << path << std::endl;
                return;
            }
            std::string source((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            file.close();

            SceneObject* prevTarget = m_groveBotTarget;
            m_groveBotTarget = target;

            // Set behavior name to script filename (without .grove extension)
            std::string prevScriptName = m_groveCurrentScriptName;
            std::string baseName = scriptName;
            if (baseName.size() > 6 && baseName.substr(baseName.size() - 6) == ".grove") {
                baseName = baseName.substr(0, baseName.size() - 6);
            }
            m_groveCurrentScriptName = baseName;

            std::cout << "[Grove] Loading '" << scriptName << "' for " << target->getName()
                      << " (" << source.size() << " bytes)" << std::endl;

            m_groveOutputAccum.clear();
            int32_t ret = grove_eval(m_groveVm, source.c_str());
            if (ret != 0) {
                const char* err = grove_last_error(m_groveVm);
                std::cerr << "[Grove] Script error: " << (err ? err : "unknown") << std::endl;
            }

            m_groveCurrentScriptName = prevScriptName;
            if (m_groveBotTarget == target) {
                m_groveBotTarget = prevTarget;
            }
        });

        // Save a behavior back to a .grove script file
        m_editorUI.setSaveBotScriptCallback([this](SceneObject* target, const std::string& behaviorName) {
            if (!target) return;

            // Find the behavior by name
            Behavior* beh = nullptr;
            for (auto& b : target->getBehaviors()) {
                if (b.name == behaviorName) { beh = &b; break; }
            }
            if (!beh || beh->actions.empty()) {
                std::cerr << "[Grove] No actions to save in behavior '" << behaviorName << "'" << std::endl;
                return;
            }

            // Generate Grove script from behavior actions
            std::ostringstream ss;
            ss << "-- " << behaviorName << ".grove\n";
            ss << "-- Auto-saved from behavior editor\n\n";
            ss << "bot_target(\"" << target->getName() << "\")\n";
            ss << "bot_clear()\n\n";

            for (const auto& act : beh->actions) {
                switch (act.type) {
                    case ActionType::PICKUP:
                        ss << "pickup(\"" << act.stringParam << "\"";
                        if (act.boolParam || act.floatParam != 2.0f) {
                            ss << ", " << (act.boolParam ? "true" : "false");
                            if (act.floatParam != 2.0f) {
                                ss << ", " << act.floatParam;
                            }
                        }
                        ss << ")\n";
                        break;
                    case ActionType::PLACE_VERTICAL:
                        ss << "place_vertical(\"" << act.stringParam << "\"";
                        if (act.boolParam || act.floatParam != 2.0f) {
                            ss << ", " << (act.boolParam ? "true" : "false");
                            if (act.floatParam != 2.0f) {
                                ss << ", " << act.floatParam;
                            }
                        }
                        ss << ")\n";
                        break;
                    case ActionType::PLACE_AT:
                        ss << "place_at(vec3(" << act.vec3Param.x << ", "
                           << act.vec3Param.y << ", " << act.vec3Param.z << ")";
                        if (act.boolParam || act.floatParam != 2.0f) {
                            ss << ", " << (act.boolParam ? "true" : "false");
                            if (act.floatParam != 2.0f) {
                                ss << ", " << act.floatParam;
                            }
                        }
                        ss << ")\n";
                        break;
                    case ActionType::PLACE_HORIZONTAL: {
                        // stringParam = "nameA|nameB"
                        size_t pipePos = act.stringParam.find('|');
                        std::string nA = (pipePos != std::string::npos) ? act.stringParam.substr(0, pipePos) : act.stringParam;
                        std::string nB = (pipePos != std::string::npos) ? act.stringParam.substr(pipePos + 1) : "";
                        ss << "place_horizontal(\"" << nA << "\", \"" << nB << "\"";
                        if (act.boolParam || act.floatParam != 2.0f) {
                            ss << ", " << (act.boolParam ? "true" : "false");
                            if (act.floatParam != 2.0f) {
                                ss << ", " << act.floatParam;
                            }
                        }
                        ss << ")\n";
                        break;
                    }
                    case ActionType::PLACE_ROOF: {
                        // stringParam = "c1|c2|c3|c4"
                        std::string roofNames[4];
                        size_t rStart = 0;
                        for (int ri = 0; ri < 4; ri++) {
                            size_t rPipe = act.stringParam.find('|', rStart);
                            if (rPipe != std::string::npos) {
                                roofNames[ri] = act.stringParam.substr(rStart, rPipe - rStart);
                                rStart = rPipe + 1;
                            } else {
                                roofNames[ri] = act.stringParam.substr(rStart);
                            }
                        }
                        ss << "place_roof(\"" << roofNames[0] << "\", \"" << roofNames[1]
                           << "\", \"" << roofNames[2] << "\", \"" << roofNames[3] << "\"";
                        if (act.boolParam || act.floatParam != 2.0f) {
                            ss << ", " << (act.boolParam ? "true" : "false");
                            if (act.floatParam != 2.0f) {
                                ss << ", " << act.floatParam;
                            }
                        }
                        ss << ")\n";
                        break;
                    }
                    case ActionType::PLACE_WALL: {
                        // stringParam = "postA|postB"
                        size_t wPipe = act.stringParam.find('|');
                        std::string wA = (wPipe != std::string::npos) ? act.stringParam.substr(0, wPipe) : act.stringParam;
                        std::string wB = (wPipe != std::string::npos) ? act.stringParam.substr(wPipe + 1) : "";
                        ss << "place_wall(\"" << wA << "\", \"" << wB << "\"";
                        if (act.boolParam || act.floatParam != 2.0f) {
                            ss << ", " << (act.boolParam ? "true" : "false");
                            if (act.floatParam != 2.0f) {
                                ss << ", " << act.floatParam;
                            }
                        }
                        ss << ")\n";
                        break;
                    }
                    case ActionType::MOVE_TO:
                        ss << "move_to(vec3(" << act.vec3Param.x << ", "
                           << act.vec3Param.y << ", " << act.vec3Param.z << "))\n";
                        break;
                    case ActionType::WAIT:
                        ss << "wait(" << act.floatParam << ")\n";
                        break;
                    case ActionType::ROTATE_TO:
                        ss << "rotate_to(vec3(" << act.vec3Param.x << ", "
                           << act.vec3Param.y << ", " << act.vec3Param.z << "))\n";
                        break;
                    case ActionType::TURN_TO:
                        ss << "turn_to(vec3(" << act.vec3Param.x << ", "
                           << act.vec3Param.y << ", " << act.vec3Param.z << "))\n";
                        break;
                    case ActionType::SET_VISIBLE:
                        ss << "set_visible(" << (act.boolParam ? "true" : "false") << ")\n";
                        break;
                    case ActionType::PLAY_SOUND:
                        ss << "play_anim(\"" << act.stringParam << "\")\n";
                        break;
                    case ActionType::SEND_SIGNAL:
                        ss << "send_signal(\"" << act.stringParam << "\")\n";
                        break;
                    case ActionType::FOLLOW_PATH:
                        ss << "follow_path(\"" << act.stringParam << "\")\n";
                        break;
                    default:
                        ss << "-- unsupported action type " << static_cast<int>(act.type) << "\n";
                        break;
                }
            }

            ss << "\nbot_loop(" << (beh->loop ? "true" : "false") << ")\n";
            ss << "bot_run()\n";

            std::string script = ss.str();

            // Save to bot's scripts folder (both source and build)
            std::string filename = behaviorName + ".grove";
            std::string dir = "scripts/" + target->getName();
            std::filesystem::create_directories(dir);
            std::string path = dir + "/" + filename;

            std::ofstream out(path);
            if (out.is_open()) {
                out << script;
                out.close();
                std::cout << "[Grove] Saved script: " << path << " (" << script.size() << " bytes)" << std::endl;
            } else {
                std::cerr << "[Grove] Failed to save: " << path << std::endl;
            }
        });

        std::cout << "Grove scripting VM initialized\n";
    }

    void loadGroveLogoTexture() {
        const char* logoPath = "grove_logo.png";
        int w, h, channels;
        unsigned char* pixels = stbi_load(logoPath, &w, &h, &channels, STBI_rgb_alpha);
        if (!pixels) {
            std::cout << "Grove logo not found at: " << logoPath << " (optional)\n";
            return;
        }

        VkDevice device = getContext().getDevice();
        VkDeviceSize imageSize = w * h * 4;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = getContext().findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
        vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

        void* data;
        vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, imageSize);
        vkUnmapMemory(device, stagingMemory);
        stbi_image_free(pixels);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        vkCreateImage(device, &imageInfo, nullptr, &m_groveLogoImage);

        vkGetImageMemoryRequirements(device, m_groveLogoImage, &memReq);
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = getContext().findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &m_groveLogoMemory);
        vkBindImageMemory(device, m_groveLogoImage, m_groveLogoMemory, 0);

        VkCommandBuffer cmd = getContext().beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_groveLogoImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, m_groveLogoImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        getContext().endSingleTimeCommands(cmd);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_groveLogoImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &viewInfo, nullptr, &m_groveLogoView);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &samplerInfo, nullptr, &m_groveLogoSampler);

        m_groveLogoDescriptor = ImGui_ImplVulkan_AddTexture(m_groveLogoSampler, m_groveLogoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_groveLogoLoaded = true;
    }

    void cleanupGroveLogoTexture() {
        if (!m_groveLogoLoaded) return;
        VkDevice device = getContext().getDevice();
        if (m_groveLogoDescriptor) ImGui_ImplVulkan_RemoveTexture(m_groveLogoDescriptor);
        if (m_groveLogoSampler) vkDestroySampler(device, m_groveLogoSampler, nullptr);
        if (m_groveLogoView) vkDestroyImageView(device, m_groveLogoView, nullptr);
        if (m_groveLogoImage) vkDestroyImage(device, m_groveLogoImage, nullptr);
        if (m_groveLogoMemory) vkFreeMemory(device, m_groveLogoMemory, nullptr);
        m_groveLogoLoaded = false;
    }

    void loadBuildingTextures() {
        // Try multiple paths: CWD, then project source dir
        std::string dir = "textures/building";
        if (!std::filesystem::exists(dir)) {
            // Try the source directory (for running from build/ directly)
            dir = std::string(CMAKE_SOURCE_DIR) + "/textures/building";
        }
        if (!std::filesystem::exists(dir)) {
            std::cout << "Building textures directory not found" << std::endl;
            return;
        }
        std::cout << "Loading building textures from: " << dir << std::endl;

        VkDevice device = getContext().getDevice();
        std::vector<EditorUI::BuildingTextureInfo> uiTextures;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") continue;

            int w, h, channels;
            unsigned char* pixels = stbi_load(entry.path().c_str(), &w, &h, &channels, STBI_rgb_alpha);
            if (!pixels) continue;

            BuildingTexture tex;
            tex.name = entry.path().stem().string();
            tex.width = w;
            tex.height = h;
            tex.pixels.assign(pixels, pixels + w * h * 4);

            // Create Vulkan image for ImGui preview (thumbnail)
            VkDeviceSize imageSize = w * h * 4;

            // Staging buffer
            VkBuffer stagingBuffer;
            VkDeviceMemory stagingMemory;
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = imageSize;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);

            VkMemoryRequirements memReq;
            vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = getContext().findMemoryType(memReq.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
            vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

            void* data;
            vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
            memcpy(data, pixels, imageSize);
            vkUnmapMemory(device, stagingMemory);
            stbi_image_free(pixels);

            // Create image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
            imageInfo.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkCreateImage(device, &imageInfo, nullptr, &tex.image);

            vkGetImageMemoryRequirements(device, tex.image, &memReq);
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = getContext().findMemoryType(memReq.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &allocInfo, nullptr, &tex.memory);
            vkBindImageMemory(device, tex.image, tex.memory, 0);

            // Transition + copy
            VkCommandBuffer cmd = getContext().beginSingleTimeCommands();

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
            vkCmdCopyBufferToImage(cmd, stagingBuffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            getContext().endSingleTimeCommands(cmd);
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);

            // Create view + sampler
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = tex.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCreateImageView(device, &viewInfo, nullptr, &tex.view);

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            vkCreateSampler(device, &samplerInfo, nullptr, &tex.sampler);

            tex.descriptor = ImGui_ImplVulkan_AddTexture(tex.sampler, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            EditorUI::BuildingTextureInfo uiTex;
            uiTex.name = tex.name;
            uiTex.descriptor = tex.descriptor;
            uiTex.width = tex.width;
            uiTex.height = tex.height;
            uiTextures.push_back(uiTex);

            m_buildingTextures.push_back(std::move(tex));
            std::cout << "Loaded building texture: " << entry.path().filename().string()
                      << " (" << w << "x" << h << ")" << std::endl;
        }

        m_editorUI.setBuildingTextures(uiTextures);
        std::cout << "Loaded " << m_buildingTextures.size() << " building textures" << std::endl;
    }

    void cleanupBuildingTextures() {
        VkDevice device = getContext().getDevice();
        for (auto& tex : m_buildingTextures) {
            if (tex.descriptor) ImGui_ImplVulkan_RemoveTexture(tex.descriptor);
            if (tex.sampler) vkDestroySampler(device, tex.sampler, nullptr);
            if (tex.view) vkDestroyImageView(device, tex.view, nullptr);
            if (tex.image) vkDestroyImage(device, tex.image, nullptr);
            if (tex.memory) vkFreeMemory(device, tex.memory, nullptr);
        }
        m_buildingTextures.clear();
    }

    void renderLoadingScreen() {
        uint32_t imageIndex;
        if (!beginFrame(imageIndex)) return;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (m_splashLoaded) {
            float windowW = static_cast<float>(getWindow().getWidth());
            float windowH = static_cast<float>(getWindow().getHeight());

            float imgAspect = static_cast<float>(m_splashWidth) / m_splashHeight;
            float screenAspect = windowW / windowH;

            float displayW, displayH;
            float offsetX = 0, offsetY = 0;

            if (screenAspect > imgAspect) {
                displayW = windowW;
                displayH = windowW / imgAspect;
                offsetY = (windowH - displayH) / 2;
            } else {
                displayH = windowH;
                displayW = windowH * imgAspect;
                offsetX = (windowW - displayW) / 2;
            }

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(windowW, windowH));
            ImGui::Begin("##SplashBG", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);

            ImGui::SetCursorPos(ImVec2(offsetX, offsetY));
            ImGui::Image((ImTextureID)m_splashDescriptor, ImVec2(displayW, displayH));

            ImGui::End();
        }

        ImVec2 windowSize(400, 100);
        ImVec2 windowPos((getWindow().getWidth() - windowSize.x) / 2,
                         (getWindow().getHeight() - windowSize.y) / 2);
        ImGui::SetNextWindowPos(windowPos);
        ImGui::SetNextWindowSize(windowSize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.85f));
        ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        ImGui::Text("Loading Terrain...");
        ImGui::Spacing();

        float progress = m_totalChunks > 0 ? static_cast<float>(m_chunksLoaded) / m_totalChunks : 0.0f;
        ImGui::ProgressBar(progress, ImVec2(-1, 0));
        ImGui::Text("%d / %d chunks", m_chunksLoaded, m_totalChunks);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::Render();

        VkCommandBuffer cmd = getCurrentCommandBuffer();
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = getSwapchain().getRenderPass();
        renderPassInfo.framebuffer = getSwapchain().getFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = getSwapchain().getExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        endFrame(imageIndex);
    }

    void handleCameraInput(float deltaTime) {
        // Play mode: right-click toggles cursor visibility for UI interaction
        if (m_isPlayMode && !m_inConversation) {
            // Check for right-click to toggle cursor (and open filesystem context menu)
            static bool wasRightClickDown = false;
            bool rightClickDown = Input::isMouseButtonDown(Input::MOUSE_RIGHT);
            if (rightClickDown && !wasRightClickDown) {
                if (m_filesystemBrowser.isActive()) {
                    // In filesystem mode: show cursor and open context menu
                    m_playModeCursorVisible = true;
                    Input::setMouseCaptured(false);
                    m_fsContextMenuOpen = true;
                } else {
                    m_playModeCursorVisible = !m_playModeCursorVisible;
                    Input::setMouseCaptured(!m_playModeCursorVisible);
                }
            }
            wasRightClickDown = rightClickDown;

            // Only do mouse look when cursor is hidden
            if (!m_playModeCursorVisible) {
                glm::vec2 mouseDelta = Input::getMouseDelta();
                m_camera.processMouse(mouseDelta.x, -mouseDelta.y);

                if (!Input::isMouseCaptured()) {
                    Input::setMouseCaptured(true);
                }
            }
        } else if (m_inConversation) {
            // During conversation: same right-click toggle, but default is cursor visible
            // Right-click toggles between cursor mode (for chat UI) and mouse look
            static bool wasRightClickDown = false;
            bool rightClickDown = Input::isMouseButtonDown(Input::MOUSE_RIGHT);
            if (rightClickDown && !wasRightClickDown) {
                m_playModeCursorVisible = !m_playModeCursorVisible;
                Input::setMouseCaptured(!m_playModeCursorVisible);
            }
            wasRightClickDown = rightClickDown;

            // Mouse look when cursor is hidden (toggled via right-click)
            if (!m_playModeCursorVisible) {
                glm::vec2 mouseDelta = Input::getMouseDelta();
                m_camera.processMouse(mouseDelta.x, -mouseDelta.y);
            }
        } else {
            // Editor mode: LIME-style orbit/pan/zoom navigation
            bool mouseOverImGui = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::GetIO().WantCaptureMouse;

            // F key: frame selected object (move orbit target to object, reposition camera)
            if (!ImGui::GetIO().WantCaptureKeyboard && Input::isKeyPressed(Input::KEY_F)) {
                if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                    SceneObject* obj = m_sceneObjects[m_selectedObjectIndex].get();
                    AABB bounds = obj->getWorldBounds();
                    glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
                    glm::vec3 extents = bounds.max - bounds.min;
                    float radius = glm::length(extents) * 0.5f;
                    float frameDist = std::max(radius * 2.5f, 5.0f);  // Back off enough to see it

                    m_orbitTarget = center;
                    // Keep current look direction, just reposition at proper distance
                    glm::vec3 camDir = glm::normalize(m_camera.getPosition() - center);
                    m_camera.setPosition(center + camDir * frameDist);

                    // Update camera to look at target
                    glm::vec3 lookDir = glm::normalize(center - m_camera.getPosition());
                    m_camera.setYaw(glm::degrees(atan2(lookDir.z, lookDir.x)));
                    m_camera.setPitch(glm::degrees(asin(glm::clamp(lookDir.y, -1.0f, 1.0f))));
                }
            }

            // Scroll wheel zoom (dolly toward/away from orbit target)
            float scroll = Input::getScrollDelta();
            if (scroll != 0 && !mouseOverImGui) {
                float orbitDistance = glm::length(m_camera.getPosition() - m_orbitTarget);
                if (orbitDistance < 0.01f) orbitDistance = 5.0f;
                float dollySpeed = std::max(orbitDistance * 0.08f, 2.0f);
                glm::vec3 forward = glm::normalize(m_orbitTarget - m_camera.getPosition());
                glm::vec3 move = forward * scroll * dollySpeed;
                glm::vec3 newPos = m_camera.getPosition() + move;
                // Check if we passed through or got too close to the orbit target
                glm::vec3 newToTarget = m_orbitTarget - newPos;
                if (glm::dot(newToTarget, forward) < 2.0f) {
                    // Target is behind us or very close — push it ahead
                    m_orbitTarget = newPos + forward * 2.0f;
                }
                m_camera.setPosition(newPos);
            }

            if (mouseOverImGui) {
                m_isTumbling = false;
                m_isPanning = false;
            } else {
                // RMB: start tumble
                if (Input::isMouseButtonPressed(Input::MOUSE_RIGHT)) {
                    m_isTumbling = true;
                }
                // MMB: start pan
                if (Input::isMouseButtonPressed(Input::MOUSE_MIDDLE)) {
                    m_isPanning = true;
                }
            }

            // Stop when buttons released
            if (!Input::isMouseButtonDown(Input::MOUSE_RIGHT)) m_isTumbling = false;
            if (!Input::isMouseButtonDown(Input::MOUSE_MIDDLE)) m_isPanning = false;

            glm::vec2 mouseDelta = Input::getMouseDelta();

            float orbitDistance = glm::length(m_camera.getPosition() - m_orbitTarget);
            if (orbitDistance < 0.01f) orbitDistance = 5.0f;

            // RMB Tumble: orbit camera around m_orbitTarget
            if (m_isTumbling) {
                if (!m_wasTumbling) {
                    // First frame of tumble: set up state but don't move camera
                    // This avoids orientation snap when camera isn't pointing at orbit target

                    // Orbit around a point along the camera's look direction at the
                    // same distance as the orbit target.  This prevents the camera
                    // from snapping its orientation to face m_orbitTarget when the
                    // user isn't already looking at it (e.g. after a gizmo move).
                    {
                        float dist = glm::length(m_camera.getPosition() - m_orbitTarget);
                        if (dist < 0.5f) dist = 10.0f;
                        glm::vec3 camFront = m_camera.getFront();
                        m_orbitTarget = m_camera.getPosition() + camFront * dist;
                    }
                    m_tumbleOrbitTarget = m_orbitTarget;
                    m_tumbleOrbitDistance = glm::length(m_camera.getPosition() - m_tumbleOrbitTarget);
                    if (m_tumbleOrbitDistance < 0.5f) m_tumbleOrbitDistance = 5.0f;

                    // Derive orbit angles from camera position relative to target
                    glm::vec3 offset = m_camera.getPosition() - m_tumbleOrbitTarget;
                    m_orbitYaw = glm::degrees(atan2(offset.z, offset.x));
                    m_orbitPitch = glm::degrees(asin(glm::clamp(offset.y / m_tumbleOrbitDistance, -1.0f, 1.0f)));
                } else {
                    // Subsequent frames: apply mouse delta and reposition camera
                    glm::vec2 mouseDelta2 = Input::getMouseDelta();
                    float sensitivity = 0.25f;
                    m_orbitYaw += mouseDelta2.x * sensitivity;
                    m_orbitPitch += mouseDelta2.y * sensitivity;
                    m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);

                    float yawRad = glm::radians(m_orbitYaw);
                    float pitchRad = glm::radians(m_orbitPitch);

                    glm::vec3 offset;
                    offset.x = m_tumbleOrbitDistance * cos(pitchRad) * cos(yawRad);
                    offset.y = m_tumbleOrbitDistance * sin(pitchRad);
                    offset.z = m_tumbleOrbitDistance * cos(pitchRad) * sin(yawRad);

                    m_camera.setPosition(m_tumbleOrbitTarget + offset);

                    // Make camera look at target
                    glm::vec3 lookDir = glm::normalize(m_tumbleOrbitTarget - m_camera.getPosition());
                    float camYaw = glm::degrees(atan2(lookDir.z, lookDir.x));
                    float camPitch = glm::degrees(asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
                    m_camera.setYaw(camYaw);
                    m_camera.setPitch(camPitch);
                }
            }

            // MMB Pan: translate camera and orbit target together
            static bool wasPanningPrev = false;
            if (m_isPanning) {
                if (!wasPanningPrev) {
                    // First frame: suppress accumulated delta to avoid jump
                    mouseDelta = glm::vec2(0.0f);
                }
                // Use distance to orbit target (which tracks selected object during orbit)
                float panDist = glm::length(m_camera.getPosition() - m_orbitTarget);
                if (panDist < 1.0f) panDist = 5.0f;
                bool hasSelection = m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size());
                float panSpeed = std::max(panDist * (hasSelection ? 0.0015f : 0.003f), 0.1f);
                glm::vec3 right = m_camera.getRight();
                glm::vec3 up = m_camera.getUp();
                glm::vec3 panOffset = -right * mouseDelta.x * panSpeed + up * mouseDelta.y * panSpeed;
                m_camera.setPosition(m_camera.getPosition() + panOffset);
                m_orbitTarget += panOffset;
            }
            wasPanningPrev = m_isPanning;

            m_wasTumbling = m_isTumbling;

            // Capture/release mouse during tumble/pan
            bool wantCapture = m_isTumbling || m_isPanning;
            if (wantCapture && !m_isLooking) {
                Input::setMouseCaptured(true);
            } else if (!wantCapture && m_isLooking) {
                Input::setMouseCaptured(false);
            }
            m_isLooking = wantCapture;
        }

        // During conversation: use arrow keys for movement (WASD needed for typing)
        // Outside conversation: normal WASD movement
        // When ImGui wants keyboard (text fields, menus): suppress all movement keys
        bool imguiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;

        float speedMult = (!imguiWantsKeyboard && Input::isKeyDown(Input::KEY_LEFT_CONTROL)) ? 3.0f : 1.0f;
        glm::vec3 camPos = m_camera.getPosition();
        float groundHeight = m_terrain.getHeightAt(camPos.x, camPos.z);

        // In play mode, also check for non-Bullet objects we can stand on for jump detection
        // Bullet objects only block movement, they don't provide ground to stand on
        if (m_isPlayMode) {
            const float playerRadius = 0.15f;
            for (const auto& obj : m_sceneObjects) {
                if (!obj || !obj->isVisible() || !obj->hasCollision()) continue;
                if (obj->hasBulletCollision()) continue;  // Bullet objects block, not support
                AABB bounds = obj->getWorldBounds();
                if (camPos.x >= bounds.min.x - playerRadius && camPos.x <= bounds.max.x + playerRadius &&
                    camPos.z >= bounds.min.z - playerRadius && camPos.z <= bounds.max.z + playerRadius) {
                    if (bounds.max.y > groundHeight && bounds.max.y < camPos.y) {
                        groundHeight = bounds.max.y;
                    }
                }
            }
        }

        // Space key fly/walk toggle disabled in editor mode (orbit/pan/zoom navigation)
        // In play mode without character controller, space still toggles fly mode
        // (Character controller path handles its own jump via Jolt)

        // Update moving platforms BEFORE collision checks
        if (m_isPlayMode) {
            for (auto& objPtr : m_sceneObjects) {
                // Update MOVE_TO animation if active
                if (objPtr && objPtr->isMovingTo()) {
                    objPtr->updateMoveTo(deltaTime);
                }

                // Update ALL kinematic platforms every frame (not just when moving)
                // This ensures platforms are tracked during WAIT periods with zero velocity
                if (objPtr && objPtr->hasJoltBody() && m_characterController) {
                    glm::vec3 position = objPtr->getTransform().getPosition();
                    glm::quat rotation = objPtr->getTransform().getRotation();
                    glm::vec3 localOffset = objPtr->getPhysicsOffset();
                    glm::vec3 worldOffset = rotation * localOffset;
                    glm::vec3 center = position + worldOffset;

                    // Get exact velocity from behavior system (calculated analytically)
                    // Returns zero when not moving (during WAIT) - this is correct!
                    glm::vec3 velocity = objPtr->getMoveVelocity();

                    m_characterController->updatePlatformTransform(objPtr->getJoltBodyId(), center, rotation, velocity, deltaTime);
                }
            }

            // Update kinematic platform debug visualization if enabled
            if (m_playModeDebug && m_aiNodeRenderer) {
                m_aiNodeRenderer->clearCollisionAABBs();
                for (const auto& obj : m_sceneObjects) {
                    if (!obj || !obj->isVisible() || !obj->isKinematicPlatform()) continue;

                    AABB bounds = obj->getWorldBounds();
                    m_aiNodeRenderer->addCollisionAABB(bounds.min, bounds.max, glm::vec3(0.3f, 0.5f, 1.0f));
                }
            }
        }

        // Height query function - includes terrain, AABB objects, and Bullet raycasts
        auto heightQuery = [this](float x, float z) {
            float height = m_terrain.getHeightAt(x, z);
            const float playerRadius = 0.15f;
            glm::vec3 camPos = m_camera.getPosition();

            // Check AABB objects we can stand on
            for (const auto& obj : m_sceneObjects) {
                if (!obj || !obj->isVisible() || !obj->hasCollision()) continue;
                if (obj->hasBulletCollision()) continue;  // Handle separately with raycast

                AABB bounds = obj->getWorldBounds();
                if (x >= bounds.min.x - playerRadius && x <= bounds.max.x + playerRadius &&
                    z >= bounds.min.z - playerRadius && z <= bounds.max.z + playerRadius) {
                    // Only count as ground if we're above it
                    if (bounds.max.y > height && bounds.max.y < camPos.y + 0.5f) {
                        height = bounds.max.y;
                    }
                }
            }

            // Raycast down for Bullet collision objects (mesh/convex hull)
            if (m_physicsWorld && m_isPlayMode) {
                glm::vec3 rayStart(x, camPos.y + 1.0f, z);
                glm::vec3 rayEnd(x, height - 1.0f, z);
                auto result = m_physicsWorld->raycast(rayStart, rayEnd);
                if (result.hit && result.hitPoint.y > height) {
                    height = result.hitPoint.y;
                }
            }

            return height;
        };

        glm::vec3 oldCameraPos = m_camera.getPosition();

        // Double-tap space toggles fly/walk mode
        if (m_isPlayMode && !imguiWantsKeyboard && !m_inConversation && !m_quickChatMode) {
            if (Input::isKeyPressed(Input::KEY_SPACE)) {
                float groundHeight = heightQuery(m_camera.getPosition().x, m_camera.getPosition().z);
                m_camera.onSpacePressed(groundHeight);
            }
        }

        // Use character controller for play mode walk (skip in filesystem browser — camera handles movement directly)
        bool useCharacterController = m_isPlayMode && m_characterController &&
                                m_camera.getMovementMode() == MovementMode::Walk &&
                                !m_filesystemBrowser.isActive();

        if (useCharacterController) {
            // Calculate desired velocity from input
            float yaw = glm::radians(m_camera.getYaw());
            glm::vec3 forward(std::cos(yaw), 0.0f, std::sin(yaw));
            glm::vec3 right(-std::sin(yaw), 0.0f, std::cos(yaw));

            glm::vec3 desiredVelocity(0.0f);

            // Use speed from Character Controller settings
            float baseSpeed = m_editorUI.getCharacterSpeed();
            float sprintMult = Input::isKeyDown(Input::KEY_LEFT_CONTROL)
                ? m_editorUI.getCharacterSprintMultiplier() : 1.0f;
            float speed = baseSpeed * sprintMult;

            // During conversation or quick chat: arrow keys move (WASD for typing)
            // Otherwise: WASD moves
            // When ImGui wants keyboard: no movement at all
            if (imguiWantsKeyboard) {
                // No movement — keys are for ImGui
            } else if (m_inConversation || m_quickChatMode) {
                if (Input::isKeyDown(Input::KEY_UP)) desiredVelocity += forward * speed;
                if (Input::isKeyDown(Input::KEY_DOWN)) desiredVelocity -= forward * speed;
                if (Input::isKeyDown(Input::KEY_RIGHT)) desiredVelocity += right * speed;
                if (Input::isKeyDown(Input::KEY_LEFT)) desiredVelocity -= right * speed;
            } else {
                if (Input::isKeyDown(Input::KEY_W)) desiredVelocity += forward * speed;
                if (Input::isKeyDown(Input::KEY_S)) desiredVelocity -= forward * speed;
                if (Input::isKeyDown(Input::KEY_D)) desiredVelocity += right * speed;
                if (Input::isKeyDown(Input::KEY_A)) desiredVelocity -= right * speed;
            }

            // During conversation or quick chat: right ctrl for jump (spacebar is for typing)
            // When ImGui wants keyboard: no jump
            bool jump = imguiWantsKeyboard ? false :
                (m_inConversation || m_quickChatMode) ?
                Input::isKeyPressed(Input::KEY_RIGHT_CONTROL) :
                Input::isKeyPressed(Input::KEY_SPACE);
            float jumpVelocity = m_editorUI.getCharacterJumpVelocity();

            // Update character controller - terrain height handled via heightQuery
            float terrainHeight = heightQuery(m_characterController->getPosition().x,
                                               m_characterController->getPosition().z);

            glm::vec3 charPos = m_characterController->extendedUpdate(
                deltaTime, desiredVelocity, jump && m_characterController->isOnGround(), jumpVelocity
            );

            // Ensure we don't go below terrain
            // Character controller returns CENTER position (half height above feet)
            const float characterHeight = 0.9f;  // Full character height (halved)
            const float halfHeight = characterHeight * 0.5f;  // Center above feet
            const float eyeHeight = 0.85f;       // Eye level from ground
            const float centerToEye = eyeHeight - halfHeight;

            float feetY = charPos.y - halfHeight;
            if (feetY < terrainHeight) {
                charPos.y = terrainHeight + halfHeight;
                m_characterController->setPosition(charPos);
            }

            // Camera positioning based on camera mode
            auto cameraMode = m_editorUI.getCameraMode();
            // Store character eye-level position and hull dimensions for collision hull drawing
            m_thirdPersonPlayerPos = glm::vec3(charPos.x, charPos.y + centerToEye, charPos.z);
            m_collisionHullHeight = eyeHeight;
            m_collisionHullRadius = m_editorUI.getCharacterRadius();
            if (cameraMode == EditorUI::CameraMode::FirstPerson) {
                // First person: camera at eye level (center + 0.8m = 1.7m above feet)
                m_camera.setPosition(glm::vec3(charPos.x, charPos.y + centerToEye, charPos.z));
            } else {
                // Third person: camera behind and above character
                float distance = m_editorUI.getThirdPersonDistance();
                float height = m_editorUI.getThirdPersonHeight();
                float pitch = glm::radians(m_camera.getPitch());

                // Calculate camera offset based on yaw and pitch
                glm::vec3 cameraOffset(
                    -std::cos(yaw) * std::cos(pitch) * distance,
                    height + std::sin(pitch) * distance,
                    -std::sin(yaw) * std::cos(pitch) * distance
                );

                glm::vec3 lookAtPos = charPos + glm::vec3(0, m_editorUI.getThirdPersonLookAtHeight(), 0);
                m_camera.setPosition(lookAtPos + cameraOffset);
            }
        } else {
            // In third-person, restore player position before movement
            // (camera was offset last frame for the view)
            bool isThirdPerson = !useCharacterController &&
                m_camera.getMovementMode() == MovementMode::Walk &&
                m_editorUI.getCameraMode() == EditorUI::CameraMode::ThirdPerson;
            if (isThirdPerson && m_thirdPersonPlayerPos != glm::vec3(0)) {
                m_camera.setPosition(m_thirdPersonPlayerPos);
            }

            // WASD movement only in play mode (editor mode uses orbit/pan/zoom above)
            if (m_isPlayMode) {
                // During conversation or quick chat: arrow keys, otherwise WASD
                // When ImGui wants keyboard: no movement at all
                if (imguiWantsKeyboard) {
                    m_camera.updateMovement(
                        deltaTime * speedMult,
                        false, false, false, false, false, false,
                        heightQuery
                    );
                } else if (m_inConversation || m_quickChatMode) {
                    m_camera.updateMovement(
                        deltaTime * speedMult,
                        Input::isKeyDown(Input::KEY_UP),
                        Input::isKeyDown(Input::KEY_DOWN),
                        Input::isKeyDown(Input::KEY_LEFT),
                        Input::isKeyDown(Input::KEY_RIGHT),
                        false,  // No jump during text input
                        false,  // No crouch during text input
                        heightQuery
                    );
                } else {
                    m_camera.updateMovement(
                        deltaTime * speedMult,
                        Input::isKeyDown(Input::KEY_W),
                        Input::isKeyDown(Input::KEY_S),
                        Input::isKeyDown(Input::KEY_A),
                        Input::isKeyDown(Input::KEY_D),
                        Input::isKeyDown(Input::KEY_SPACE),
                        Input::isKeyDown(Input::KEY_LEFT_SHIFT),
                        heightQuery
                    );
                }
            }
        }

        // Third-person camera for editor walk mode (non-character-controller path)
        if (!useCharacterController && m_camera.getMovementMode() == MovementMode::Walk &&
            m_editorUI.getCameraMode() == EditorUI::CameraMode::ThirdPerson) {
            glm::vec3 playerPos = m_camera.getPosition();  // This is the "player" (eye level)
            m_thirdPersonPlayerPos = playerPos;  // Store for collision hull drawing
            m_collisionHullHeight = m_camera.getEyeHeight();
            m_collisionHullRadius = 0.5f;  // Camera collision radius

            float yaw = glm::radians(m_camera.getYaw());
            float pitch = glm::radians(m_camera.getPitch());
            float distance = m_editorUI.getThirdPersonDistance();
            float height = m_editorUI.getThirdPersonHeight();

            glm::vec3 cameraOffset(
                -std::cos(yaw) * std::cos(pitch) * distance,
                height + std::sin(pitch) * distance,
                -std::sin(yaw) * std::cos(pitch) * distance
            );

            glm::vec3 lookAtPos = playerPos + glm::vec3(0, m_editorUI.getThirdPersonLookAtHeight() - m_camera.getEyeHeight(), 0);
            m_camera.setPosition(lookAtPos + cameraOffset);
        } else if (!useCharacterController) {
            m_thirdPersonPlayerPos = m_camera.getPosition();
        }

        // Post-movement AABB collision for play mode walk (fallback for non-Jolt objects)
        if (m_isPlayMode && m_camera.getMovementMode() == MovementMode::Walk && !useCharacterController) {
            glm::vec3 newPos = m_camera.getPosition();
            const float playerRadius = 0.25f;
            const float playerHeight = 0.85f;

            for (const auto& obj : m_sceneObjects) {
                if (!obj || !obj->isVisible()) continue;

                // Skip AABB if object has Bullet collision (Bullet takes priority)
                if (obj->hasBulletCollision()) continue;

                // Check for AABB collision (fast box-based) - only if no Bullet collision
                if (obj->hasAABBCollision()) {
                    AABB bounds = obj->getWorldBounds();
                    // Expand bounds slightly for player radius
                    bounds.min -= glm::vec3(playerRadius, 0, playerRadius);
                    bounds.max += glm::vec3(playerRadius, 0, playerRadius);

                    // Check if player feet to head intersects object bounds
                    glm::vec3 playerMin(newPos.x - playerRadius, newPos.y - playerHeight, newPos.z - playerRadius);
                    glm::vec3 playerMax(newPos.x + playerRadius, newPos.y + 0.1f, newPos.z + playerRadius);

                    // AABB vs AABB intersection
                    if (playerMax.x > bounds.min.x && playerMin.x < bounds.max.x &&
                        playerMax.y > bounds.min.y && playerMin.y < bounds.max.y &&
                        playerMax.z > bounds.min.z && playerMin.z < bounds.max.z) {
                        // Collision! Try to slide along the object
                        glm::vec3 slidePos = newPos;

                        // Try X movement only
                        slidePos = glm::vec3(newPos.x, oldCameraPos.y, oldCameraPos.z);
                        playerMin = glm::vec3(slidePos.x - playerRadius, slidePos.y - playerHeight, slidePos.z - playerRadius);
                        playerMax = glm::vec3(slidePos.x + playerRadius, slidePos.y + 0.1f, slidePos.z + playerRadius);
                        bool xOk = !(playerMax.x > bounds.min.x && playerMin.x < bounds.max.x &&
                                     playerMax.y > bounds.min.y && playerMin.y < bounds.max.y &&
                                     playerMax.z > bounds.min.z && playerMin.z < bounds.max.z);

                        // Try Z movement only
                        slidePos = glm::vec3(oldCameraPos.x, oldCameraPos.y, newPos.z);
                        playerMin = glm::vec3(slidePos.x - playerRadius, slidePos.y - playerHeight, slidePos.z - playerRadius);
                        playerMax = glm::vec3(slidePos.x + playerRadius, slidePos.y + 0.1f, slidePos.z + playerRadius);
                        bool zOk = !(playerMax.x > bounds.min.x && playerMin.x < bounds.max.x &&
                                     playerMax.y > bounds.min.y && playerMin.y < bounds.max.y &&
                                     playerMax.z > bounds.min.z && playerMin.z < bounds.max.z);

                        if (xOk && !zOk) {
                            newPos = glm::vec3(newPos.x, newPos.y, oldCameraPos.z);
                        } else if (!xOk && zOk) {
                            newPos = glm::vec3(oldCameraPos.x, newPos.y, newPos.z);
                        } else {
                            // Both blocked, revert completely
                            newPos = glm::vec3(oldCameraPos.x, newPos.y, oldCameraPos.z);
                        }
                        m_camera.setPosition(newPos);
                        break;  // Only handle one collision per frame
                    }
                }

                // Polygon collision (ray-triangle intersection)
                if (obj->hasPolygonCollision() && obj->hasMeshData()) {
                    glm::vec3 movement = newPos - oldCameraPos;
                    float moveDist = glm::length(movement);

                    if (moveDist > 0.001f) {
                        glm::vec3 moveDir = movement / moveDist;

                        // Cast rays from multiple heights (feet, knees, waist, chest)
                        float checkHeights[] = {-playerHeight + 0.1f, -playerHeight * 0.5f, -playerHeight * 0.25f, -0.3f};
                        bool blocked = false;

                        for (float heightOffset : checkHeights) {
                            glm::vec3 rayOrigin = oldCameraPos + glm::vec3(0, heightOffset, 0);
                            auto hit = obj->raycast(rayOrigin, moveDir);

                            if (hit.hit && hit.distance < moveDist + playerRadius) {
                                blocked = true;
                                break;
                            }
                        }

                        if (blocked) {
                            // Try sliding along X axis
                            glm::vec3 xMove = glm::vec3(movement.x, 0, 0);
                            float xDist = glm::length(xMove);
                            bool xBlocked = false;

                            if (xDist > 0.001f) {
                                glm::vec3 xDir = xMove / xDist;
                                for (float heightOffset : checkHeights) {
                                    glm::vec3 rayOrigin = oldCameraPos + glm::vec3(0, heightOffset, 0);
                                    auto hit = obj->raycast(rayOrigin, xDir);
                                    if (hit.hit && hit.distance < xDist + playerRadius) {
                                        xBlocked = true;
                                        break;
                                    }
                                }
                            }

                            // Try sliding along Z axis
                            glm::vec3 zMove = glm::vec3(0, 0, movement.z);
                            float zDist = glm::length(zMove);
                            bool zBlocked = false;

                            if (zDist > 0.001f) {
                                glm::vec3 zDir = zMove / zDist;
                                for (float heightOffset : checkHeights) {
                                    glm::vec3 rayOrigin = oldCameraPos + glm::vec3(0, heightOffset, 0);
                                    auto hit = obj->raycast(rayOrigin, zDir);
                                    if (hit.hit && hit.distance < zDist + playerRadius) {
                                        zBlocked = true;
                                        break;
                                    }
                                }
                            }

                            // Apply sliding
                            if (!xBlocked && zBlocked) {
                                newPos = glm::vec3(newPos.x, newPos.y, oldCameraPos.z);
                            } else if (xBlocked && !zBlocked) {
                                newPos = glm::vec3(oldCameraPos.x, newPos.y, newPos.z);
                            } else if (xBlocked && zBlocked) {
                                newPos = glm::vec3(oldCameraPos.x, newPos.y, oldCameraPos.z);
                            }

                            m_camera.setPosition(newPos);
                            break;  // Only handle one collision per frame
                        }
                    }
                }
            }
        }

        // Track movement mode changes (engine hum disabled for now)
        m_lastMovementMode = m_camera.getMovementMode();

        if (m_terrain.getConfig().wrapWorld) {
            glm::vec3 wrapped = m_terrain.wrapWorldPosition(m_camera.getPosition());
            if (wrapped != m_camera.getPosition()) {
                m_camera.setPosition(wrapped);
            }
        }
    }

    void handleKeyboardShortcuts(float deltaTime) {
        // Ctrl+` (backtick) — toggle terminal
        {
            static bool wasBacktick = false;
            bool backtick = Input::isKeyDown(96); // GLFW_KEY_GRAVE_ACCENT = 96
            bool ctrl = Input::isKeyDown(Input::KEY_LEFT_CONTROL) || Input::isKeyDown(Input::KEY_RIGHT_CONTROL);
            if (backtick && ctrl && !wasBacktick) {
                m_editorUI.showTerminal() = !m_editorUI.showTerminal();
                // Lazy-init terminal on first open
                if (m_editorUI.showTerminal() && !m_terminalInitialized) {
                    m_terminal.init(120, 40);
                    m_terminalInitialized = true;
                }
            }
            wasBacktick = backtick;
        }

        // Escape key handling
        static bool wasEscapeDown = false;
        bool escapeDown = Input::isKeyDown(Input::KEY_ESCAPE);
        if (escapeDown && !wasEscapeDown) {
            if (m_quickChatMode) {
                // Close quick chat
                m_quickChatMode = false;
                m_quickChatBuffer[0] = '\0';
                wasEscapeDown = escapeDown;
                return;
            } else if (m_inConversation) {
                endConversation();
                wasEscapeDown = escapeDown;
                return; // Don't process other shortcuts this frame
            } else if (m_isPlayMode) {
                exitPlayMode();
            }
        }
        wasEscapeDown = escapeDown;

        // F6 — run Grove test script (fast iteration, no LLM needed)
        // Works in ANY mode: play, conversation, quick chat
        {
            static bool wasF6 = false;
            bool f6 = Input::isKeyDown(295); // GLFW_KEY_F6 = 295
            if (f6 && !wasF6 && m_isPlayMode) {
                std::string script = "run_file(\"test_wall_panels.grove\")";
                addChatMessage("System", "[F6] Running test...");
                std::cout << "[F6] Executing: " << script << std::endl;
                m_groveOutputAccum.clear();
                int32_t ret = grove_eval(m_groveVm, script.c_str());
                if (ret != 0) {
                    const char* err = grove_last_error(m_groveVm);
                    int line = static_cast<int>(grove_last_error_line(m_groveVm));
                    std::string errMsg = std::string("Error (line ") + std::to_string(line) + "): " + (err ? err : "unknown");
                    std::cout << "[F6] " << errMsg << std::endl;
                    addChatMessage("System", errMsg);
                } else if (!m_groveOutputAccum.empty()) {
                    addChatMessage("System", m_groveOutputAccum);
                }
            }
            wasF6 = f6;
        }

        // F9 — toggle filesystem browser (spawn ~/  or clear)
        {
            static bool wasF9 = false;
            bool f9 = Input::isKeyDown(298); // GLFW_KEY_F9 = 298
            if (f9 && !wasF9 && m_isPlayMode) {
                if (m_filesystemBrowser.isActive()) {
                    m_filesystemBrowser.clearFilesystemObjects();
                    m_camera.setNoClip(false); // Restore terrain collision
                    std::cout << "[F9] Filesystem browser dismissed" << std::endl;
                } else {
                    const char* home = getenv("HOME");
                    std::string homePath = home ? home : "/";
                    glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 8.0f;
                    m_filesystemBrowser.setSpawnOrigin(spawnPos);
                    m_filesystemBrowser.navigate(homePath);
                    // Teleport to center of new gallery
                    glm::vec3 galleryCam = spawnPos;
                    galleryCam.y += 2.0f;
                    m_camera.setPosition(galleryCam);
                    if (m_characterController) {
                        m_characterController->setPosition(galleryCam);
                    }
                    m_camera.setNoClip(true); // Disable terrain collision in filesystem silo
                    std::cout << "[F9] Filesystem browser opened: " << homePath << std::endl;
                }
            }
            wasF9 = f9;
        }

        // V key: push-to-talk (hold to record, release to transcribe + send to nearest NPC)
        if (m_isPlayMode && !ImGui::GetIO().WantTextInput) {
            bool vDown = Input::isKeyDown(Input::KEY_V);
            if (vDown && !m_pttRecording && !m_pttProcessing) {
                // Start recording
                if (Audio::getInstance().startRecording()) {
                    m_pttRecording = true;
                    std::cout << "[PTT] Recording started (hold V to talk)" << std::endl;
                }
            } else if (!vDown && m_pttRecording) {
                // Stop recording and transcribe
                m_pttRecording = false;
                std::string wavPath = "/tmp/eden_ptt.wav";
                if (Audio::getInstance().stopRecording(wavPath)) {
                    m_pttProcessing = true;
                    std::cout << "[PTT] Transcribing..." << std::endl;

                    m_httpClient->requestSTT(wavPath,
                        [this](const AsyncHttpClient::Response& resp) {
                            m_pttProcessing = false;
                            if (!resp.success) {
                                std::cerr << "[PTT] STT request failed" << std::endl;
                                return;
                            }
                            try {
                                auto json = nlohmann::json::parse(resp.body);
                                std::string text = json.value("text", "");
                                if (text.empty()) {
                                    std::cout << "[PTT] No speech detected" << std::endl;
                                    return;
                                }
                                std::cout << "[PTT] You said: \"" << text << "\"" << std::endl;

                                // Send as quick chat to nearest NPC
                                handleVoiceMessage(text);
                            } catch (const std::exception& e) {
                                std::cerr << "[PTT] Parse error: " << e.what() << std::endl;
                            }
                        });
                }
            }
        }

        // Skip all other shortcuts when in conversation or quick chat (only Escape works)
        if (m_inConversation || m_quickChatMode) return;

        // Slash key to open quick chat (talk to nearest AI without approaching)
        static bool wasSlashKeyDown = false;
        bool slashKeyDown = Input::isKeyDown(Input::KEY_SLASH);
        if (slashKeyDown && !wasSlashKeyDown && !ImGui::GetIO().WantTextInput && m_isPlayMode) {
            m_quickChatMode = true;
            m_quickChatBuffer[0] = '\0';
            // Clear any typed characters (the '/' that triggered this)
            Input::clearTypedChars();
            // Mouse look stays active - you can look around while typing!
        }
        wasSlashKeyDown = slashKeyDown;

        // Tab key to toggle world chat history window
        static bool wasTabKeyDown = false;
        bool tabKeyDown = Input::isKeyDown(Input::KEY_TAB);
        if (tabKeyDown && !wasTabKeyDown && !ImGui::GetIO().WantTextInput && !m_quickChatMode) {
            m_showWorldChatHistory = !m_showWorldChatHistory;
        }
        wasTabKeyDown = tabKeyDown;

        // E key to start conversation (only when not already in one)
        static bool wasEKeyDown = false;
        bool eKeyDown = Input::isKeyDown(Input::KEY_E);
        if (eKeyDown && !wasEKeyDown && !ImGui::GetIO().WantTextInput && !m_inConversation) {
            glm::vec3 camPos = m_camera.getPosition();
            m_actionSystem.playerInteract(camPos, 10.0f);

            // Check for nearby scene objects and start conversation
            tryInteractWithNearbyObject(camPos);
        }
        wasEKeyDown = eKeyDown;

        bool ctrlDown = Input::isKeyDown(Input::KEY_LEFT_CONTROL);
        static bool wasNKeyDown = false;
        static bool wasSKeyDown = false;
        static bool wasOKeyDown = false;
        bool nKeyDown = Input::isKeyDown(Input::KEY_N);
        bool sKeyDown = Input::isKeyDown(Input::KEY_S);
        bool oKeyDown = Input::isKeyDown(Input::KEY_O);

        if (!m_isPlayMode) {
            if (ctrlDown && nKeyDown && !wasNKeyDown && !ImGui::GetIO().WantTextInput) {
                newLevel();
            }
            if (ctrlDown && sKeyDown && !wasSKeyDown && !ImGui::GetIO().WantTextInput) {
                showSaveDialog();
            }
            if (ctrlDown && oKeyDown && !wasOKeyDown && !ImGui::GetIO().WantTextInput) {
                showLoadDialog();
            }
        }
        wasNKeyDown = nKeyDown;
        wasSKeyDown = sKeyDown;
        wasOKeyDown = oKeyDown;

        static bool wasF5Down = false;
        bool f5Down = Input::isKeyDown(Input::KEY_F5);
        if (f5Down && !wasF5Down && !ImGui::GetIO().WantTextInput) {
            runGame();
        }
        wasF5Down = f5Down;

        // F3 toggles debug visuals in play mode (waypoints, AI nodes, collision hulls, etc.)
        static bool wasF3Down = false;
        bool f3Down = Input::isKeyDown(Input::KEY_F3);
        if (f3Down && !wasF3Down && m_isPlayMode) {
            m_playModeDebug = !m_playModeDebug;
            if (m_aiNodeRenderer) {
                m_aiNodeRenderer->setVisible(m_playModeDebug);

                // Update collision hull debug rendering
                m_aiNodeRenderer->clearCollisionAABBs();
                if (m_playModeDebug) {
                    for (const auto& obj : m_sceneObjects) {
                        if (!obj || !obj->isVisible()) continue;

                        // Show kinematic platform collision box
                        if (obj->isKinematicPlatform()) {
                            AABB bounds = obj->getWorldBounds();
                            m_aiNodeRenderer->addCollisionAABB(bounds.min, bounds.max, glm::vec3(0.3f, 0.5f, 1.0f));
                            continue; // Don't show other collision types for kinematic platforms
                        }

                        if (!obj->hasCollision()) continue;

                        // Show AABB bounds for objects with AABB or polygon collision (green=AABB, orange=polygon)
                        if (obj->hasAABBCollision() || obj->hasPolygonCollision()) {
                            AABB bounds = obj->getWorldBounds();
                            glm::vec3 aabbColor = obj->hasAABBCollision() ? glm::vec3(0, 1, 0) : glm::vec3(1, 0.5f, 0);
                            m_aiNodeRenderer->addCollisionAABB(bounds.min, bounds.max, aabbColor);
                        }

                        // Show Bullet collision shapes if enabled (cyan=box, magenta=convex, yellow=mesh)
                        if (obj->hasBulletCollision() && m_physicsWorld) {
                            auto vertices = m_physicsWorld->getCollisionShapeVertices(obj.get());
                            glm::vec3 bulletColor;
                            switch (obj->getBulletCollisionType()) {
                                case BulletCollisionType::BOX: bulletColor = glm::vec3(0, 1, 1); break;
                                case BulletCollisionType::CONVEX_HULL: bulletColor = glm::vec3(1, 0, 1); break;
                                case BulletCollisionType::MESH: bulletColor = glm::vec3(1, 1, 0); break;
                                default: bulletColor = glm::vec3(1, 1, 1); break;
                            }
                            for (size_t i = 0; i + 1 < vertices.size(); i += 2) {
                                m_aiNodeRenderer->addCollisionLine(vertices[i], vertices[i + 1], bulletColor);
                            }
                        }
                    }
                }
            }
            std::cout << "Play mode debug: " << (m_playModeDebug ? "ON" : "OFF") << std::endl;
        }
        wasF3Down = f3Down;

        // M key - toggle zone map
        static bool wasMKeyDown = false;
        bool mKeyDown = Input::isKeyDown(Input::KEY_M);
        if (mKeyDown && !wasMKeyDown && !ImGui::GetIO().WantTextInput) {
            m_showZoneMap = !m_showZoneMap;
        }
        wasMKeyDown = mKeyDown;

        // P key - toggle planet info panel (play mode only)
        static bool wasPKeyDown = false;
        bool pKeyDown = Input::isKeyDown(Input::KEY_P);
        if (pKeyDown && !wasPKeyDown && m_isPlayMode && !ImGui::GetIO().WantTextInput) {
            m_showPlanetInfo = !m_showPlanetInfo;
        }
        wasPKeyDown = pKeyDown;

        // Y key - snap selected object vertically (stack on top/bottom of another object)
        static bool wasYKeyDown = false;
        bool yKeyDown = Input::isKeyDown(Input::KEY_Y);
        if (yKeyDown && !wasYKeyDown && !ImGui::GetIO().WantTextInput && !m_isPlayMode) {
            snapToNearestVerticalEdge();
        }
        wasYKeyDown = yKeyDown;

        // N key to place AI node below camera (uses currently selected type from AI panel)
        static bool wasNKeyDownForNode = false;
        bool nKeyDownForNode = Input::isKeyDown(Input::KEY_N);
        if (nKeyDownForNode && !wasNKeyDownForNode && !ctrlDown && !ImGui::GetIO().WantTextInput && !m_isPlayMode) {
            glm::vec3 camPos = m_camera.getPosition();
            float terrainHeight = m_terrain.getHeightAt(camPos.x, camPos.z);
            glm::vec3 nodePos(camPos.x, terrainHeight, camPos.z);
            addAINode(nodePos, static_cast<AINodeType>(m_aiPlacementType));
        }
        wasNKeyDownForNode = nKeyDownForNode;

        // F1 to toggle help window
        static bool wasF1Down = false;
        bool f1Down = Input::isKeyDown(Input::KEY_F1);
        if (f1Down && !wasF1Down && !ImGui::GetIO().WantTextInput) {
            m_editorUI.showHelp() = !m_editorUI.showHelp();
        }
        wasF1Down = f1Down;

        // (Module panel moved — M is now zone map)

        static bool wasDeleteDown = false;
        bool deleteDown = Input::isKeyDown(Input::KEY_DELETE);
        if (deleteDown && !wasDeleteDown && !ImGui::GetIO().WantTextInput && !m_isPlayMode) {
            if (m_selectedObjectIndex >= 0) {
                deleteObject(m_selectedObjectIndex);
            }
        }
        wasDeleteDown = deleteDown;

        // V - duplicate selected object(s) (editor mode only)
        static bool wasVKeyDown = false;
        bool vKeyDown = Input::isKeyDown(Input::KEY_V);
        if (vKeyDown && !wasVKeyDown && !ImGui::GetIO().WantTextInput && !m_isPlayMode) {
            if (m_selectedObjectIndices.size() > 1) {
                // Multi-duplicate: duplicate all selected objects, then select the new ones
                std::vector<int> toClone(m_selectedObjectIndices.begin(), m_selectedObjectIndices.end());
                std::sort(toClone.begin(), toClone.end());
                std::set<int> newIndices;
                for (int idx : toClone) {
                    int newIdx = duplicateObjectSilent(idx);
                    if (newIdx >= 0) {
                        newIndices.insert(newIdx);
                    }
                }
                if (!newIndices.empty()) {
                    // Select all the new duplicates
                    m_selectedObjectIndices = newIndices;
                    m_selectedObjectIndex = *newIndices.begin();
                    m_editorUI.setSelectedObjectIndices(m_selectedObjectIndices);
                    m_editorUI.setSelectedObjectIndex(m_selectedObjectIndex);
                    updateSceneObjectsList();
                    std::cout << "Duplicated " << newIndices.size() << " objects" << std::endl;
                }
            } else if (m_selectedObjectIndex >= 0) {
                duplicateObject(m_selectedObjectIndex);
            }
        }
        wasVKeyDown = vKeyDown;

        static bool wasFKeyDown = false;
        bool fKeyDown = Input::isKeyDown(Input::KEY_F);
        if (!m_isPlayMode && fKeyDown && !wasFKeyDown && !ImGui::GetIO().WantTextInput) {
            focusOnSelectedObject();
        }
        wasFKeyDown = fKeyDown;

        // Q/W/E/R: switch transform mode (W/E/R auto-enter MoveObject brush mode)
        if (!m_isPlayMode && !ImGui::GetIO().WantTextInput) {
            if (Input::isKeyPressed(Input::KEY_Q)) {
                m_transformMode = TransformMode::Select;
                if (m_editorUI.getBrushMode() == BrushMode::MoveObject) {
                    m_editorUI.setBrushMode(m_prevBrushMode);
                }
            }
            if (Input::isKeyPressed(Input::KEY_W) || Input::isKeyPressed(Input::KEY_E) || Input::isKeyPressed(Input::KEY_R)) {
                if (Input::isKeyPressed(Input::KEY_W)) m_transformMode = TransformMode::Move;
                if (Input::isKeyPressed(Input::KEY_E)) m_transformMode = TransformMode::Rotate;
                if (Input::isKeyPressed(Input::KEY_R)) m_transformMode = TransformMode::Scale;
                if (m_editorUI.getBrushMode() != BrushMode::MoveObject) {
                    m_prevBrushMode = m_editorUI.getBrushMode();
                    m_editorUI.setBrushMode(BrushMode::MoveObject);
                }
            }
        }

        // X key - snap selected object to nearest horizontal edge of another object
        static bool wasXKeyDown = false;
        bool xKeyDown = Input::isKeyDown(Input::KEY_X);
        if (!m_isPlayMode && xKeyDown && !wasXKeyDown && !ImGui::GetIO().WantTextInput) {
            snapToNearestEdge();
        }
        wasXKeyDown = xKeyDown;

        // Z key - full 3D snap to align with nearest object surface
        static bool wasZKeyDown = false;
        bool zKeyDown = Input::isKeyDown(Input::KEY_Z);
        if (!m_isPlayMode && zKeyDown && !wasZKeyDown && !ImGui::GetIO().WantTextInput && !ctrlDown) {
            snapFullAlign();
        }
        wasZKeyDown = zKeyDown;

        // C key - snap selected object to terrain surface
        static bool wasCKeyDown = false;
        bool cKeyDown = Input::isKeyDown(Input::KEY_C);
        if (!m_isPlayMode && cKeyDown && !wasCKeyDown && !ImGui::GetIO().WantTextInput && !ctrlDown) {
            snapToTerrain();
        }
        wasCKeyDown = cKeyDown;

        // G key - group selected objects
        static bool wasGKeyDown = false;
        bool gKeyDown = Input::isKeyDown(Input::KEY_G);
        if (!m_isPlayMode && gKeyDown && !wasGKeyDown && !ImGui::GetIO().WantTextInput && !ctrlDown) {
            if (m_editorUI.getSelectedObjectIndices().size() > 1) {
                m_editorUI.showGroupNamePopup();
            }
        }
        wasGKeyDown = gKeyDown;

    }

    void trackFPS(float deltaTime) {
        m_frameTimeAccum += deltaTime;
        m_frameCount++;
        if (m_frameTimeAccum >= 0.5f) {
            m_fps = m_frameCount / m_frameTimeAccum;
            m_frameTimeAccum = 0;
            m_frameCount = 0;
        }
    }

    void updatePlayMode(float deltaTime) {
        m_gizmo.setVisible(false);
        m_splineRenderer->setVisible(false);
        m_brushRing->setVisible(false);

        // Update game module
        if (m_gameModule) {
            m_gameModule->update(deltaTime);
            m_gameModule->setPlayerPosition(m_camera.getPosition());
        }
        
        // Update AI motor control actions (look_around, turn_to, etc.)
        updateAIAction(deltaTime);

        // Update player avatar position to track camera
        updatePlayerAvatar();

        // EDEN companions track the player — turn to face camera
        {
            glm::vec3 playerPos = m_camera.getPosition();
            for (auto& obj : m_sceneObjects) {
                if (!obj || obj->getBeingType() != BeingType::EDEN_COMPANION) continue;
                // Skip if NPC is doing a motor action (look_around, move_to, etc.)
                if (m_aiActionActive && m_currentInteractObject == obj.get()) continue;

                glm::vec3 npcPos = obj->getTransform().getPosition();
                glm::vec3 toPlayer = playerPos - npcPos;
                toPlayer.y = 0.0f;  // only rotate on Y axis

                if (glm::length(toPlayer) < 0.1f) continue;  // too close, skip

                float targetYaw = glm::degrees(atan2(toPlayer.x, toPlayer.z));
                glm::vec3 euler = obj->getEulerRotation();

                // Smooth rotation towards player
                float diff = targetYaw - euler.y;
                // Normalize to [-180, 180]
                while (diff > 180.0f) diff -= 360.0f;
                while (diff < -180.0f) diff += 360.0f;

                float rotSpeed = 90.0f; // degrees per second
                float step = rotSpeed * deltaTime;
                if (std::abs(diff) < step) {
                    euler.y = targetYaw;
                } else {
                    euler.y += (diff > 0 ? step : -step);
                }
                obj->setEulerRotation(euler);
            }
        }

        // Update game time
        int previousMinute = static_cast<int>(m_gameTimeMinutes);
        m_gameTimeMinutes += deltaTime * m_gameTimeScale;

        // Wrap around at midnight (1440 minutes = 24 hours)
        if (m_gameTimeMinutes >= 1440.0f) {
            m_gameTimeMinutes -= 1440.0f;
        }

        int currentMinute = static_cast<int>(m_gameTimeMinutes);

        // Check for time-based triggers if minute changed
        if (currentMinute != previousMinute) {
            checkGameTimeTriggers(previousMinute, currentMinute);
        }

        // Reset per-frame update flags (prevents double-updates from collision + behavior code)
        for (auto& objPtr : m_sceneObjects) {
            if (objPtr) objPtr->resetMoveUpdateFlag();
        }

        for (auto& objPtr : m_sceneObjects) {
            if (!objPtr) continue;

            // Skip all behavior/patrol updates for any AI follow target (follow system manages them)
            bool isFollowTarget = std::any_of(m_aiFollowers.begin(), m_aiFollowers.end(),
                [&objPtr](const AIFollowState& fs) { return fs.npc == objPtr.get(); });

            // Always update behaviors (even for invisible objects - they may SET_VISIBLE themselves)
            if (!isFollowTarget) {
                objPtr->updateBehaviors(deltaTime);
            }

            // Update active behavior actions (SET_VISIBLE, MOVE_TO, etc.)
            if (!isFollowTarget && objPtr->hasActiveBehavior()) {
                updateActiveBehavior(objPtr.get(), deltaTime);
            }

            // Skip movement/patrol updates for invisible objects or objects in conversation
            if (!objPtr->isVisible()) continue;

            bool isInConversation = m_inConversation && (objPtr.get() == m_currentInteractObject);
            if (isInConversation || isFollowTarget) continue;

            // Legacy patrol support (node-based) - only for visible objects without active behavior
            if (!objPtr->hasActiveBehavior() && objPtr->hasPatrolPath() && !objPtr->isPatrolPaused()) {
                updatePatrol(objPtr.get(), deltaTime);
            }
        }

        // Process any pending spawns/destroys (after behavior loop to avoid iterator invalidation)
        processPendingSpawns();
        flushGroveSpawns();
        processPendingDestroys();

        // Update AI follow AFTER scene loop so follow position isn't overwritten by patrol/behaviors
        updateAIFollow(deltaTime);

        // Update carried items (position on NPC shoulder)
        updateCarriedItems();

        // Smoothly rotate NPC to face player during conversation
        if (m_inConversation && m_currentInteractObject && m_hasConversationTargetYaw) {
            glm::vec3 euler = m_currentInteractObject->getEulerRotation();
            float currentYaw = euler.y;

            // Calculate shortest rotation path
            float diff = m_conversationTargetYaw - currentYaw;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;

            // Rotate at a fixed speed
            float turnSpeed = 120.0f;  // degrees per second (slower for conversation)
            float maxTurn = turnSpeed * deltaTime;

            if (std::abs(diff) <= maxTurn) {
                euler.y = m_conversationTargetYaw;
            } else {
                euler.y += (diff > 0 ? maxTurn : -maxTurn);
            }

            // Normalize
            while (euler.y > 180.0f) euler.y -= 360.0f;
            while (euler.y < -180.0f) euler.y += 360.0f;

            m_currentInteractObject->setEulerRotation(euler);
        }

        // Update economy and trading systems
        updateEconomySystems(deltaTime);

        // Left click — crosshair raycast to interact with filesystem objects + drag-and-drop
        m_shootCooldown -= deltaTime;
        bool leftDown = Input::isMouseButtonDown(Input::MOUSE_LEFT) && !ImGui::GetIO().WantCaptureMouse;
        bool leftPressed = Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !ImGui::GetIO().WantCaptureMouse;
        bool leftReleased = m_fsLeftWasDown && !leftDown;

        if (m_filesystemBrowser.isActive() && !m_inConversation) {
            // Helper: crosshair raycast
            auto doCrosshairRay = [&](glm::vec3& rayO, glm::vec3& rayD) {
                float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();
                glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 5000.0f);
                glm::mat4 view = m_camera.getViewMatrix();
                glm::mat4 invVP = glm::inverse(proj * view);
                glm::vec4 nearPt = invVP * glm::vec4(0, 0, -1, 1); nearPt /= nearPt.w;
                glm::vec4 farPt  = invVP * glm::vec4(0, 0,  1, 1); farPt  /= farPt.w;
                rayO = glm::vec3(nearPt);
                rayD = glm::normalize(glm::vec3(farPt - nearPt));
            };

            auto raycastFS = [&](const glm::vec3& rayO, const glm::vec3& rayD) -> SceneObject* {
                float closestDist = std::numeric_limits<float>::max();
                SceneObject* closestHit = nullptr;
                for (auto& obj : m_sceneObjects) {
                    if (!obj) continue;
                    const auto& bt = obj->getBuildingType();
                    if (bt != "filesystem" && bt != "filesystem_wall") continue;
                    float dist = obj->getWorldBounds().intersect(rayO, rayD);
                    if (dist < 0 || dist >= 200.0f) continue;
                    // Walls' rotated AABBs inflate and can shadow doors behind them.
                    // Penalize wall hits so doors/files at similar distances always win.
                    float effectiveDist = dist;
                    if (bt == "filesystem_wall") effectiveDist += 3.0f;
                    if (effectiveDist < closestDist) {
                        closestDist = effectiveDist;
                        closestHit = obj.get();
                    }
                }
                return closestHit;
            };

            // Mouse just pressed — start potential drag or immediate action
            if (leftPressed && m_shootCooldown <= 0.0f) {
                glm::vec3 rayO, rayD;
                doCrosshairRay(rayO, rayD);
                SceneObject* hit = raycastFS(rayO, rayD);

                if (hit && hit->isDoor()) {
                    // Door — navigate immediately
                    std::string target = hit->getTargetLevel();
                    if (target.rfind("fs://", 0) == 0) {
                        glm::vec3 doorPos = hit->getTransform().getPosition();
                        m_filesystemBrowser.setSpawnOrigin(doorPos);
                        m_filesystemBrowser.navigate(target.substr(5));
                        glm::vec3 camPos = doorPos;
                        camPos.y += 2.0f;
                        m_camera.setPosition(camPos);
                        if (m_characterController) {
                            m_characterController->setPosition(camPos);
                        }
                    }
                    m_shootCooldown = 0.2f;
                } else if (hit && hit->getBuildingType() == "filesystem" && !hit->isDoor()) {
                    // Non-door file — start drag candidate
                    m_fsDragObject = hit;
                    m_fsDragHoldTime = 0.0f;
                    m_fsDragActive = false;
                    m_fsDragHoverWall = nullptr;
                } else if (hit && hit->getBuildingType() == "filesystem_wall") {
                    // Wall click — select it, and start drag if wall has an item on it
                    bool ctrlHeld = Input::isKeyDown(Input::KEY_LEFT_CONTROL) || Input::isKeyDown(Input::KEY_RIGHT_CONTROL);
                    if (!ctrlHeld) {
                        for (auto& obj : m_sceneObjects) {
                            if (!obj) continue;
                            const auto& bt = obj->getBuildingType();
                            if (bt == "filesystem" || bt == "filesystem_wall")
                                obj->setSelected(false);
                        }
                    }
                    hit->setSelected(!hit->isSelected());
                    // If wall has an item, allow drag (for moving files/folders)
                    std::string wt = hit->getTargetLevel();
                    if (wt.rfind("fs://", 0) == 0 && wt.size() > 5) {
                        m_fsDragObject = hit;
                        m_fsDragHoldTime = 0.0f;
                        m_fsDragActive = false;
                        m_fsDragHoverWall = nullptr;
                    } else {
                        m_shootCooldown = 0.2f;
                    }
                } else {
                    // Nothing hit — clear all selections
                    for (auto& obj : m_sceneObjects) {
                        if (!obj) continue;
                        const auto& bt = obj->getBuildingType();
                        if (bt == "filesystem" || bt == "filesystem_wall")
                            obj->setSelected(false);
                    }
                    m_shootCooldown = 0.2f;
                }
            }

            // Mouse held — update drag state
            if (leftDown && m_fsDragObject) {
                m_fsDragHoldTime += deltaTime;
                static constexpr float DRAG_THRESHOLD = 0.35f;

                if (m_fsDragHoldTime >= DRAG_THRESHOLD) {
                    m_fsDragActive = true;

                    // Continuous raycast to find hovered wall
                    glm::vec3 rayO, rayD;
                    doCrosshairRay(rayO, rayD);

                    // Clear previous hover highlight
                    if (m_fsDragHoverWall) {
                        m_fsDragHoverWall->setSelected(false);
                        m_fsDragHoverWall = nullptr;
                    }

                    // Find drop target under crosshair (skip the dragged object itself)
                    // Includes walls, folders, and files — walls are penalized so
                    // objects mounted in front of them always win the raycast.
                    float closestDist = std::numeric_limits<float>::max();
                    SceneObject* hoverHit = nullptr;
                    for (auto& obj : m_sceneObjects) {
                        if (!obj || obj.get() == m_fsDragObject) continue;
                        const auto& bt = obj->getBuildingType();
                        if (bt != "filesystem" && bt != "filesystem_wall") continue;
                        float dist = obj->getWorldBounds().intersect(rayO, rayD);
                        if (dist < 0 || dist >= 200.0f) continue;
                        float effectiveDist = dist;
                        if (bt == "filesystem_wall") effectiveDist += 3.0f;
                        if (effectiveDist < closestDist) {
                            closestDist = effectiveDist;
                            hoverHit = obj.get();
                        }
                    }

                    if (hoverHit) {
                        bool isFolder = (hoverHit->getBuildingType() == "filesystem" && hoverHit->isDoor());
                        bool isWall = (hoverHit->getBuildingType() == "filesystem_wall");
                        // Only highlight valid drop targets (folders and empty walls, not other files)
                        if (isFolder || isWall) {
                            hoverHit->setSelected(true);
                            m_fsDragHoverWall = hoverHit;
                        }
                        // If hovering another file, block the drop (no highlight, no target)
                    }
                }
            }

            // Mouse released — complete drag or fall back to click-select
            if (leftReleased && m_fsDragObject) {
                if (m_fsDragActive && m_fsDragHoverWall) {
                    // Get source path — works for both file objects and occupied walls
                    std::string srcPath;
                    std::string target = m_fsDragObject->getTargetLevel();
                    if (target.rfind("fs://", 0) == 0) {
                        srcPath = target.substr(5);
                    }
                    bool dragFromWall = (m_fsDragObject->getBuildingType() == "filesystem_wall");

                    bool droppedOnFolder = (m_fsDragHoverWall->getBuildingType() == "filesystem" && m_fsDragHoverWall->isDoor());

                    // Helper: remove the file/folder scene object for the given path
                    auto removeSceneObjectByPath = [&](const std::string& fsPath) {
                        std::string tgt = "fs://" + fsPath;
                        for (auto it = m_sceneObjects.begin(); it != m_sceneObjects.end(); ++it) {
                            if (*it && (*it)->getBuildingType() == "filesystem" &&
                                (*it)->getTargetLevel() == tgt) {
                                uint32_t handle = (*it)->getBufferHandle();
                                if (handle != 0) m_modelRenderer->destroyModel(handle);
                                m_sceneObjects.erase(it);
                                break;
                            }
                        }
                    };

                    if (!srcPath.empty() && droppedOnFolder) {
                        // Drop onto folder — move file/folder into that directory
                        std::string folderTarget = m_fsDragHoverWall->getTargetLevel();
                        if (folderTarget.rfind("fs://", 0) == 0) {
                            namespace fs = std::filesystem;
                            fs::path src(srcPath);
                            fs::path destDir(folderTarget.substr(5));
                            fs::path dst = destDir / src.filename();
                            if (fs::exists(dst)) {
                                std::string stem = dst.stem().string();
                                std::string ext = dst.extension().string();
                                int n = 1;
                                do {
                                    dst = destDir / (stem + "_" + std::to_string(n) + ext);
                                    n++;
                                } while (fs::exists(dst));
                            }
                            std::error_code ec;
                            fs::rename(src, dst, ec);
                            if (ec) {
                                std::cerr << "[FS] Drag-to-folder failed: " << ec.message() << std::endl;
                            } else {
                                // Remove the visible file/folder object
                                removeSceneObjectByPath(srcPath);
                                // Clear the source wall's item reference
                                if (dragFromWall) {
                                    m_fsDragObject->setTargetLevel("");
                                }
                            }
                        }
                    } else if (!srcPath.empty() && !droppedOnFolder) {
                        // Drop onto wall — move the file object to this wall slot
                        glm::vec3 wallPos = m_fsDragHoverWall->getTransform().getPosition();
                        glm::vec3 wallScale = m_fsDragHoverWall->getTransform().getScale();
                        float wallYaw = m_fsDragHoverWall->getEulerRotation().y;

                        // Remove the visible file/folder object
                        removeSceneObjectByPath(srcPath);
                        // Clear source wall's item reference if dragged from a wall
                        if (dragFromWall) {
                            m_fsDragObject->setTargetLevel("");
                        }

                        // Update destination wall's item reference
                        m_fsDragHoverWall->setTargetLevel("fs://" + srcPath);

                        // Spawn at new wall position
                        m_filesystemBrowser.spawnFileAtWall(srcPath, wallPos, wallScale, wallYaw);
                    }

                    m_fsDragHoverWall->setSelected(false);
                } else if (!m_fsDragActive) {
                    // Short click — select the object
                    bool ctrlHeld = Input::isKeyDown(Input::KEY_LEFT_CONTROL) || Input::isKeyDown(Input::KEY_RIGHT_CONTROL);
                    if (!ctrlHeld) {
                        for (auto& obj : m_sceneObjects) {
                            if (!obj) continue;
                            const auto& bt = obj->getBuildingType();
                            if (bt == "filesystem" || bt == "filesystem_wall")
                                obj->setSelected(false);
                        }
                    }
                    if (m_fsDragObject) m_fsDragObject->setSelected(!m_fsDragObject->isSelected());
                }

                // Clear drag state
                if (m_fsDragHoverWall) m_fsDragHoverWall->setSelected(false);
                m_fsDragObject = nullptr;
                m_fsDragHoverWall = nullptr;
                m_fsDragActive = false;
                m_fsDragHoldTime = 0.0f;
                m_shootCooldown = 0.2f;
            }

            // Cancel drag if object disappeared (e.g. navigation happened)
            if (!leftDown && m_fsDragObject) {
                if (m_fsDragHoverWall) m_fsDragHoverWall->setSelected(false);
                m_fsDragObject = nullptr;
                m_fsDragHoverWall = nullptr;
                m_fsDragActive = false;
            }
            // Per-frame hover raycast for filename preview under crosshair
            if (!m_fsDragActive && !m_playModeCursorVisible) {
                glm::vec3 rayO, rayD;
                doCrosshairRay(rayO, rayD);
                SceneObject* hover = raycastFS(rayO, rayD);
                if (hover && hover->getBuildingType() == "filesystem") {
                    m_fsHoverName = hover->getDescription();
                } else {
                    m_fsHoverName.clear();
                }
            } else {
                m_fsHoverName.clear();
            }
        } else {
            m_fsHoverName.clear();
            if (leftDown && m_shootCooldown <= 0.0f && !m_inConversation) {
                // Non-filesystem left click (existing shoot cooldown behavior)
                m_shootCooldown = 0.2f;
            }
        }
        m_fsLeftWasDown = leftDown;

        // Update projectiles (for any still in flight)
        updateProjectiles(deltaTime);

        // Update pirate AI (scans for targets, sets up attacks)
        updatePirates(deltaTime);

        // Update dogfight AI (handles combat)
        updateDogfighters(deltaTime);

        // E key for interaction
        if (Input::isKeyPressed(Input::KEY_E) && !m_inConversation) {
            interactWithCrosshair();
        }
    }

    void updateEconomySystems(float deltaTime) {
        // Update economy simulation
        if (m_economySystem) {
            m_economySystem->update(deltaTime, m_gameTimeMinutes);
        }

        // Update city governor (builds city autonomously)
        if (m_cityGovernor) {
            m_cityGovernor->update(deltaTime, m_gameTimeMinutes);
        }

        // Update model-based traders and sync their positions to the models
        for (auto& trader : m_modelTraders) {
            if (trader) {
                // Find the model linked to this trader
                bool skipTraderUpdate = false;
                for (auto& obj : m_sceneObjects) {
                    if (obj && obj->getTraderId() == trader->getId()) {
                        glm::vec3 modelPos = obj->getTransform().getPosition();

                        // Check DogfightAI state - skip trading if in any combat state
                        DogfightAI* fighterAI = nullptr;
                        for (auto& fighter : m_dogfighters) {
                            if (fighter->getSceneObject() == obj.get()) {
                                fighterAI = fighter.get();
                                break;
                            }
                        }

                        if (fighterAI) {
                            DogfightState state = fighterAI->getState();
                            // Debug: print state changes (track last state per object)
                            static std::unordered_map<SceneObject*, DogfightState> lastStates;
                            if (lastStates[obj.get()] != state) {
                                std::cout << "[" << obj->getName() << "] State: " << fighterAI->getStateName() << std::endl;
                                lastStates[obj.get()] = state;
                            }

                            // Skip trading if in any combat state (not IDLE or PATROL)
                            if (state != DogfightState::IDLE && state != DogfightState::PATROL) {
                                skipTraderUpdate = true;
                                break;
                            }
                        }

                        // Check if under attack - DogfightAI handles combat, just skip trading
                        if (obj->isUnderAttack()) {
                            // Update attacker position for DogfightAI to track
                            glm::vec3 attackerPos = m_camera.getPosition();
                            obj->setUnderAttack(true, attackerPos);

                            // Check distance to attacker - return to trading if far away
                            float distToAttacker = glm::distance(modelPos, attackerPos);
                            if (distToAttacker > 200.0f) {
                                obj->clearAttackState();
                                std::cout << obj->getName() << " lost sight of attacker, returning to trading" << std::endl;
                            } else {
                                // DogfightAI handles movement/combat, skip trader movement
                                skipTraderUpdate = true;
                                break;
                            }
                        }

                        // Normal trading behavior
                        glm::vec3 traderTarget = trader->getPosition();

                        // Calculate direction to trader's current target
                        glm::vec3 toTarget = traderTarget - modelPos;
                        float distXZ = glm::length(glm::vec2(toTarget.x, toTarget.z));

                        float dist3D = glm::length(toTarget);
                        if (dist3D > 0.5f) {
                            // Calculate target yaw (horizontal) and pitch (vertical)
                            // Negative pitch = nose up (when target is higher, toTarget.y > 0)
                            float targetYaw = glm::degrees(std::atan2(toTarget.x, toTarget.z));
                            float targetPitch = -glm::degrees(std::atan2(toTarget.y, distXZ));
                            glm::vec3 euler = obj->getEulerRotation();

                            // Calculate yaw difference (normalized to ±180°)
                            float yawDiff = targetYaw - euler.y;
                            while (yawDiff > 180.0f) yawDiff -= 360.0f;
                            while (yawDiff < -180.0f) yawDiff += 360.0f;

                            // Calculate pitch difference
                            float pitchDiff = targetPitch - euler.x;

                            float turnSpeed = 120.0f * deltaTime;
                            bool needsYawTurn = std::abs(yawDiff) > 5.0f;
                            bool needsPitchTurn = std::abs(pitchDiff) > 5.0f;

                            if (needsYawTurn || needsPitchTurn) {
                                // Still turning - rotate yaw and pitch simultaneously
                                if (needsYawTurn) {
                                    if (std::abs(yawDiff) <= turnSpeed) {
                                        euler.y = targetYaw;
                                    } else {
                                        euler.y += (yawDiff > 0 ? turnSpeed : -turnSpeed);
                                    }
                                }
                                if (needsPitchTurn) {
                                    if (std::abs(pitchDiff) <= turnSpeed) {
                                        euler.x = targetPitch;
                                    } else {
                                        euler.x += (pitchDiff > 0 ? turnSpeed : -turnSpeed);
                                    }
                                }
                                obj->setEulerRotation(euler);
                            } else {
                                // Facing target - now move toward it in 3D
                                float moveSpeed = trader->getSpeed() * deltaTime;
                                float moveAmount = std::min(moveSpeed, dist3D);

                                glm::vec3 dir = glm::normalize(toTarget);
                                glm::vec3 newPos = modelPos + dir * moveAmount;

                                obj->getTransform().setPosition(newPos);

                                // Keep facing direction updated
                                euler.y = targetYaw;
                                euler.x = targetPitch;
                                obj->setEulerRotation(euler);
                            }
                        }
                        break;
                    }
                }

                // Update trader AI (it calculates paths, trades, etc.) - skip if under attack
                if (!skipTraderUpdate) {
                    trader->update(deltaTime, m_gameTimeMinutes);
                }
            }
        }

        // Update AI node renderer (including trader visualization)
        updateAINodeRenderer();
    }

    void syncEconomyNodes() {
        if (!m_economySystem) return;

        // Clear existing economy nodes
        m_economySystem->clearNodes();

        int registeredCount = 0;

        // Register all GRAPH nodes with the economy system
        for (const auto& node : m_aiNodes) {
            if (!node || node->getType() != AINodeType::GRAPH) continue;
            if (!node->isVisible()) continue;

            GraphCategory category = node->getCategory();

            // First try to find a building definition by name
            const BuildingDef* buildingDef = findBuildingDef(node->getName());

            // Skip nodes with no category AND no building definition (navigation-only)
            if (category == GraphCategory::NONE && !buildingDef) continue;

            EconomyNode econNode;
            econNode.graphNodeId = node->getId();
            econNode.name = node->getName();
            if (buildingDef) {
                // Use building definition
                for (const auto& output : buildingDef->outputs) {
                    econNode.produces.push_back({output.good, output.rate, 1.0f});
                    econNode.inventory[output.good] = buildingDef->baseInventoryCapacity * 0.25f;
                    econNode.maxInventory[output.good] = buildingDef->baseInventoryCapacity;
                    // Producers sell at 80% of market price
                    econNode.sellPriceModifier[output.good] = 0.8f;
                }
                for (const auto& input : buildingDef->inputs) {
                    econNode.consumes.push_back({input.good, input.rate, 1.2f}); // Priority 1.2
                    econNode.maxInventory[input.good] = buildingDef->baseInventoryCapacity * 0.5f;
                    // Consumers buy at 130% of market price
                    econNode.buyPriceModifier[input.good] = 1.3f;
                }
            } else {
                // Fallback: set up based on category
                switch (category) {
                    case GraphCategory::FACTORY:
                        // Generic factory produces chemicals
                        econNode.produces.push_back({GoodType::CHEMICALS, 10.0f, 1.0f});
                        econNode.inventory[GoodType::CHEMICALS] = 50.0f;
                        econNode.maxInventory[GoodType::CHEMICALS] = 200.0f;
                        break;

                    case GraphCategory::WAREHOUSE:
                        // Warehouses store everything, buy low
                        for (int i = 0; i < static_cast<int>(GoodType::COUNT); i++) {
                            GoodType g = static_cast<GoodType>(i);
                            econNode.inventory[g] = 20.0f;
                            econNode.maxInventory[g] = 500.0f;
                            econNode.consumes.push_back({g, 5.0f, 0.8f});
                        }
                        break;

                    case GraphCategory::MARKET:
                        // Markets buy and sell at market prices
                        for (int i = 0; i < static_cast<int>(GoodType::COUNT); i++) {
                            GoodType g = static_cast<GoodType>(i);
                            econNode.inventory[g] = 30.0f;
                            econNode.maxInventory[g] = 100.0f;
                            econNode.produces.push_back({g, 0.0f, 1.0f});
                            econNode.consumes.push_back({g, 2.0f, 1.0f});
                        }
                        break;

                    case GraphCategory::REFUEL:
                        econNode.produces.push_back({GoodType::FUEL, 20.0f, 1.0f});
                        econNode.inventory[GoodType::FUEL] = 100.0f;
                        econNode.maxInventory[GoodType::FUEL] = 500.0f;
                        break;

                    case GraphCategory::RESIDENCE:
                        econNode.consumes.push_back({GoodType::FOOD, 5.0f, 1.2f});
                        econNode.consumes.push_back({GoodType::FURS, 2.0f, 1.0f});
                        econNode.maxInventory[GoodType::FOOD] = 50.0f;
                        econNode.maxInventory[GoodType::FURS] = 20.0f;
                        break;

                    case GraphCategory::RESTAURANT:
                        econNode.consumes.push_back({GoodType::FOOD, 10.0f, 1.1f});
                        econNode.produces.push_back({GoodType::FOOD, 5.0f, 1.5f});
                        econNode.inventory[GoodType::FOOD] = 20.0f;
                        econNode.maxInventory[GoodType::FOOD] = 100.0f;
                        break;

                    default:
                        continue;
                }
            }

            m_economySystem->registerNode(econNode);
            registeredCount++;
        }

        std::cout << "=== Synced " << registeredCount << " economy nodes ===" << std::endl;
    }

    void placeTraderAtRandomNode(TraderAI* trader) {
        if (!trader) return;

        // Find all GRAPH nodes
        std::vector<AINode*> graphNodes;
        for (const auto& node : m_aiNodes) {
            if (node && node->getType() == AINodeType::GRAPH && node->isVisible()) {
                graphNodes.push_back(node.get());
            }
        }

        if (graphNodes.empty()) return;

        // Pick a random node
        int index = rand() % graphNodes.size();
        AINode* startNode = graphNodes[index];

        trader->setCurrentNodeId(startNode->getId());
        trader->setPosition(startNode->getPosition());

        std::cout << "  " << trader->getName() << " placed at " << startNode->getName() << std::endl;
    }

    void spawnJettisonedCargo(const glm::vec3& position, float value) {
        JettisonedCargo cargo;
        cargo.position = position;
        cargo.velocity = glm::vec3(
            (rand() % 100 - 50) * 0.1f,  // Random horizontal spread
            5.0f,                          // Slight upward
            (rand() % 100 - 50) * 0.1f
        );
        cargo.value = value;
        cargo.lifetime = 60.0f;

        // Create a small cube to represent the cargo
        auto cargoObj = std::make_unique<SceneObject>("Cargo_" + std::to_string(m_jettisonedCargo.size()));
        cargoObj->getTransform().setPosition(position);
        cargoObj->getTransform().setScale(glm::vec3(1.0f));  // 1m cube
        cargoObj->setHueShift(60.0f);  // Yellow-ish
        cargoObj->setBrightness(1.5f);

        // Use primitive mesh builder for a simple cube
        auto meshData = PrimitiveMeshBuilder::createCube(1.0f);
        uint32_t bufferHandle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        cargoObj->setBufferHandle(bufferHandle);
        cargoObj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        cargoObj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));

        cargo.sceneObjectIndex = static_cast<int>(m_sceneObjects.size());
        m_sceneObjects.push_back(std::move(cargoObj));
        m_jettisonedCargo.push_back(cargo);

        std::cout << "Cargo jettisoned worth $" << value << std::endl;
    }

    void spawnEjectedPilot(const glm::vec3& position, const glm::vec3& velocity) {
        EjectedPilot pilot;
        pilot.position = position;
        pilot.velocity = velocity;
        pilot.lifetime = 120.0f;
        pilot.hasParachute = false;

        // Create a small sphere/cube to represent the pilot
        auto pilotObj = std::make_unique<SceneObject>("EjectedPilot_" + std::to_string(m_ejectedPilots.size()));
        pilotObj->getTransform().setPosition(position);
        pilotObj->getTransform().setScale(glm::vec3(0.5f));  // Small
        pilotObj->setHueShift(0.0f);
        pilotObj->setSaturation(0.5f);  // Desaturated
        pilotObj->setBrightness(1.2f);

        // Use primitive mesh builder for a simple cube
        auto meshData = PrimitiveMeshBuilder::createCube(0.5f);
        uint32_t bufferHandle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        pilotObj->setBufferHandle(bufferHandle);
        pilotObj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        pilotObj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));

        pilot.sceneObjectIndex = static_cast<int>(m_sceneObjects.size());
        m_sceneObjects.push_back(std::move(pilotObj));
        m_ejectedPilots.push_back(pilot);

        std::cout << "Pilot ejected!" << std::endl;
    }

    void updateJettisonedCargo(float deltaTime) {
        const float gravity = 9.8f;

        for (auto it = m_jettisonedCargo.begin(); it != m_jettisonedCargo.end(); ) {
            it->lifetime -= deltaTime;
            it->velocity.y -= gravity * deltaTime;
            it->position += it->velocity * deltaTime;

            // Check terrain collision
            float terrainHeight = m_terrain.getHeightAt(it->position.x, it->position.z);
            if (it->position.y < terrainHeight) {
                it->position.y = terrainHeight;
                it->velocity = glm::vec3(0);  // Stop moving
            }

            // Update scene object position
            if (it->sceneObjectIndex >= 0 && it->sceneObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                m_sceneObjects[it->sceneObjectIndex]->getTransform().setPosition(it->position);
            }

            // Remove if expired
            if (it->lifetime <= 0) {
                // Could remove the scene object too, but for now just mark as invisible
                if (it->sceneObjectIndex >= 0 && it->sceneObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                    m_sceneObjects[it->sceneObjectIndex]->setVisible(false);
                }
                it = m_jettisonedCargo.erase(it);
            } else {
                ++it;
            }
        }
    }

    void updateEjectedPilots(float deltaTime) {
        const float gravity = 9.8f;
        const float parachuteOpenHeight = 50.0f;  // Open chute at this height above terrain
        const float parachuteDrag = 0.95f;

        for (auto it = m_ejectedPilots.begin(); it != m_ejectedPilots.end(); ) {
            it->lifetime -= deltaTime;

            float terrainHeight = m_terrain.getHeightAt(it->position.x, it->position.z);
            float heightAboveTerrain = it->position.y - terrainHeight;

            // Open parachute when low enough
            if (!it->hasParachute && heightAboveTerrain < parachuteOpenHeight && it->velocity.y < 0) {
                it->hasParachute = true;
                std::cout << "Parachute deployed!" << std::endl;
            }

            // Apply physics
            if (it->hasParachute) {
                // Slow descent with parachute
                it->velocity.y = std::max(it->velocity.y, -3.0f);  // Max fall speed 3 m/s
                it->velocity.x *= parachuteDrag;
                it->velocity.z *= parachuteDrag;
            } else {
                it->velocity.y -= gravity * deltaTime;
            }

            it->position += it->velocity * deltaTime;

            // Check terrain collision
            if (it->position.y < terrainHeight) {
                it->position.y = terrainHeight;
                it->velocity = glm::vec3(0);  // Landed
            }

            // Update scene object position
            if (it->sceneObjectIndex >= 0 && it->sceneObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                m_sceneObjects[it->sceneObjectIndex]->getTransform().setPosition(it->position);
            }

            // Remove if expired
            if (it->lifetime <= 0) {
                if (it->sceneObjectIndex >= 0 && it->sceneObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                    m_sceneObjects[it->sceneObjectIndex]->setVisible(false);
                }
                it = m_ejectedPilots.erase(it);
            } else {
                ++it;
            }
        }
    }

    void updatePirates(float deltaTime) {
        if (m_pirates.empty()) return;

        for (auto& pirate : m_pirates) {
            // Find the pirate's DogfightAI
            DogfightAI* pirateAI = nullptr;
            for (auto& fighter : m_dogfighters) {
                if (fighter->getSceneObject() == pirate.sceneObject) {
                    pirateAI = fighter.get();
                    break;
                }
            }
            if (!pirateAI) continue;

            // Skip if pirate is dead or fleeing
            DogfightState state = pirateAI->getState();
            if (state == DogfightState::DEAD || state == DogfightState::EJECTING) {
                continue;
            }

            // Check if current target has jettisoned cargo - mission accomplished
            if (pirate.targetTrader && pirate.waitingForCargoJettison) {
                if (pirate.targetTrader->hasJettisonedCargo()) {
                    std::cout << "[PIRATE] Target " << pirate.targetTrader->getName()
                              << " has jettisoned cargo! Breaking off attack." << std::endl;

                    // Clear trader's target too so it stops fighting
                    for (auto& fighter : m_dogfighters) {
                        if (fighter->getSceneObject() == pirate.targetTrader) {
                            fighter->clearTarget();
                            pirate.targetTrader->clearAttackState();
                            break;
                        }
                    }

                    pirateAI->clearTarget();
                    pirate.targetTrader = nullptr;
                    pirate.waitingForCargoJettison = false;
                }
            }

            // Check if current target is dead - find new one
            if (pirate.targetTrader && pirate.targetTrader->isDead()) {
                std::cout << "[PIRATE] Target destroyed! Looking for new prey." << std::endl;
                pirateAI->clearTarget();
                pirate.targetTrader = nullptr;
                pirate.waitingForCargoJettison = false;
            }

            // Check if pirate is dead/fleeing - clear trader's target
            if (pirate.targetTrader && (state == DogfightState::FLEEING || state == DogfightState::DEAD)) {
                for (auto& fighter : m_dogfighters) {
                    if (fighter->getSceneObject() == pirate.targetTrader) {
                        fighter->clearTarget();
                        pirate.targetTrader->clearAttackState();
                        std::cout << "[TRADER] Pirate fled/destroyed - returning to trading." << std::endl;
                        break;
                    }
                }
                pirate.targetTrader = nullptr;
                pirate.waitingForCargoJettison = false;
            }

            // Update scan timer
            pirate.scanTimer -= deltaTime;
            if (pirate.scanTimer <= 0 && !pirate.targetTrader) {
                pirate.scanTimer = pirate.scanInterval;

                // Scan for traders with cargo
                glm::vec3 piratePos = pirateAI->getPosition();
                std::cout << "[PIRATE] " << pirate.sceneObject->getName() << " scanning for targets..." << std::endl;
                SceneObject* bestTarget = nullptr;
                float bestDistance = pirate.scanRange;

                for (auto& obj : m_sceneObjects) {
                    if (!obj || !obj->isTrader() || obj->isDead()) continue;

                    glm::vec3 tpos = obj->getTransform().getPosition();

                    // Skip traders who already jettisoned cargo - unless they have new cargo now!
                    if (obj->hasJettisonedCargo()) {
                        // Check if trader has restocked - reset jettison flag
                        uint32_t tid = obj->getTraderId();
                        for (auto& t : m_modelTraders) {
                            if (t && t->getId() == tid && (!t->getCargo().empty() || t->getCredits() > 2000.0f)) {
                                obj->setJettisonedCargo(false);  // They have new cargo, fair game again!
                                std::cout << "[PIRATE] " << obj->getName() << " has restocked - fair game again!" << std::endl;
                                break;
                            }
                        }
                        if (obj->hasJettisonedCargo()) {
                            std::cout << "[PIRATE] Skipping " << obj->getName() << " - already jettisoned cargo pos=(" << (int)tpos.x << "," << (int)tpos.y << "," << (int)tpos.z << ")" << std::endl;
                            continue;
                        }
                    }

                    // Check if trader has cargo (via linked TraderAI)
                    uint32_t traderId = obj->getTraderId();
                    bool hasCargo = false;
                    float credits = 0;
                    for (auto& trader : m_modelTraders) {
                        if (trader && trader->getId() == traderId) {
                            hasCargo = !trader->getCargo().empty() || trader->getCredits() > 1000.0f;
                            credits = trader->getCredits();
                            break;
                        }
                    }

                    if (!hasCargo) {
                        std::cout << "[PIRATE] Skipping " << obj->getName() << " - no cargo (credits=" << (int)credits << ") pos=(" << (int)tpos.x << "," << (int)tpos.y << "," << (int)tpos.z << ")" << std::endl;
                        continue;
                    }

                    // Check distance
                    glm::vec3 traderPos = obj->getTransform().getPosition();
                    float dist = glm::distance(piratePos, traderPos);

                    if (dist < bestDistance) {
                        bestDistance = dist;
                        bestTarget = obj.get();
                    }
                }

                if (bestTarget) {
                    // Found a target! Set up attack
                    pirate.targetTrader = bestTarget;
                    pirate.waitingForCargoJettison = false;

                    // Find the trader's DogfightAI for mutual targeting
                    DogfightAI* traderAI = nullptr;
                    for (auto& fighter : m_dogfighters) {
                        if (fighter->getSceneObject() == bestTarget) {
                            traderAI = fighter.get();
                            break;
                        }
                    }

                    if (traderAI) {
                        // MUTUAL TARGETING - both are aggressive!
                        pirateAI->setTarget(traderAI);
                        traderAI->setTarget(pirateAI);

                        std::cout << "[PIRATE] " << pirate.sceneObject->getName()
                                  << " hunting " << bestTarget->getName()
                                  << " (dist: " << (int)bestDistance << "m)" << std::endl;
                    }

                    // Also mark under attack for state tracking
                    bestTarget->setUnderAttack(true, piratePos);
                } else {
                    std::cout << "[PIRATE] " << pirate.sceneObject->getName() << " no valid targets found" << std::endl;
                }
            }

            // If we have a target and they're low on health, wait for jettison
            if (pirate.targetTrader && !pirate.waitingForCargoJettison) {
                float targetHealth = pirate.targetTrader->getHealthPercent();
                if (targetHealth <= 0.35f) {
                    pirate.waitingForCargoJettison = true;
                    std::cout << "[PIRATE] Target " << pirate.targetTrader->getName()
                              << " is weakened (" << (int)(targetHealth * 100) << "%), waiting for cargo jettison..." << std::endl;
                }
            }

            // Update attacker position for target's AI to track
            if (pirate.targetTrader) {
                pirate.targetTrader->setUnderAttack(true, pirateAI->getPosition());
            }
        }
    }

    void updateDogfighters(float deltaTime) {
        // Update all dogfight AIs
        for (auto& fighter : m_dogfighters) {
            SceneObject* obj = fighter->getSceneObject();
            if (!obj) continue;

            bool isPirate = obj->hasScript("pirate");
            bool isTrader = obj->isTrader();

            // Pirates should ignore player attacks - they only hunt traders
            if (isPirate) {
                if (!fighter->hasTarget()) {
                    // Clear any attack state if pirate has no trader target
                    obj->clearAttackState();
                }
                // Don't update pirate's attacker to player position
            }
            // Traders: check if a pirate is targeting them
            else if (isTrader && obj->isUnderAttack()) {
                // Check if any pirate is targeting this trader
                bool pirateAttacking = false;
                for (const auto& pirate : m_pirates) {
                    if (pirate.targetTrader == obj) {
                        pirateAttacking = true;
                        // Attacker position already set by updatePirates
                        break;
                    }
                }
                // Only set player as attacker if no pirate is attacking
                if (!pirateAttacking) {
                    obj->setUnderAttack(true, m_camera.getPosition());
                }
            }
            // Other fighters (non-trader, non-pirate): player is attacker
            else if (obj->isUnderAttack()) {
                obj->setUnderAttack(true, m_camera.getPosition());
            }

            fighter->update(deltaTime);

            // Clamp fighter position above terrain
            if (obj) {
                glm::vec3 pos = obj->getTransform().getPosition();
                float terrainHeight = m_terrain.getHeightAt(pos.x, pos.z);
                float minHeight = terrainHeight + 2.0f;  // At least 2m above terrain
                if (pos.y < minHeight) {
                    pos.y = minHeight;
                    obj->getTransform().setPosition(pos);
                }

                // Check if fighter is firing and spawn projectile
                if (fighter->isFiring()) {
                    glm::vec3 dir = fighter->getLastShotDirection();
                    spawnEnemyProjectile(pos, dir);

                    // Play shooting sound for enemy
                    Audio::getInstance().playSound("sounds/tir.mp3", 0.15f);
                }
            }
        }

        // Update cargo and pilots
        updateJettisonedCargo(deltaTime);
        updateEjectedPilots(deltaTime);
    }

    void updatePatrol(SceneObject* obj, float deltaTime) {
        if (!obj || !obj->hasPatrolPath()) return;

        static bool debugOnce = true;
        if (debugOnce) {
            std::cout << "Patrol active for " << obj->getName()
                      << " with " << obj->getPatrolPath().size() << " waypoints" << std::endl;
            debugOnce = false;
        }

        uint32_t targetWaypointId = obj->getCurrentWaypointId();

        // Find the target waypoint
        AINode* targetNode = nullptr;
        for (const auto& node : m_aiNodes) {
            if (node && node->getId() == targetWaypointId) {
                targetNode = node.get();
                break;
            }
        }

        if (!targetNode) return;

        glm::vec3 currentPos = obj->getTransform().getPosition();
        glm::vec3 targetPos = targetNode->getPosition();

        // Keep Y the same or follow terrain
        targetPos.y = currentPos.y;

        glm::vec3 toTarget = targetPos - currentPos;
        float distance = glm::length(toTarget);

        const float arrivalThreshold = 1.0f;

        if (distance < arrivalThreshold) {
            // Arrived at waypoint, advance to next
            obj->advanceWaypoint();
        } else {
            // Move toward waypoint
            glm::vec3 direction = glm::normalize(toTarget);
            float moveDistance = obj->getPatrolSpeed() * deltaTime;

            if (moveDistance > distance) {
                moveDistance = distance;
            }

            glm::vec3 newPos = currentPos + direction * moveDistance;

            // Follow terrain height with model offset
            float terrainHeight = m_terrain.getHeightAt(newPos.x, newPos.z);

            // Calculate offset from model's bounding box - place feet on ground
            // Account for object scale
            const AABB& bounds = obj->getLocalBounds();
            float scaleY = obj->getTransform().getScale().y;
            float modelBottomOffset = -bounds.min.y * scaleY;  // How far below origin the model extends
            newPos.y = terrainHeight + modelBottomOffset;

            obj->getTransform().setPosition(newPos);

            // Smoothly rotate to face movement direction
            float targetYaw = glm::degrees(atan2(direction.x, direction.z));
            glm::vec3 euler = obj->getEulerRotation();
            float currentYaw = euler.y;

            // Calculate shortest rotation path (handle wrap-around)
            float diff = targetYaw - currentYaw;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;

            // Rotate at a fixed speed (degrees per second)
            float turnSpeed = 180.0f;  // degrees per second
            float maxTurn = turnSpeed * deltaTime;

            if (std::abs(diff) <= maxTurn) {
                euler.y = targetYaw;
            } else {
                euler.y += (diff > 0 ? maxTurn : -maxTurn);
            }

            // Normalize to -180 to 180
            while (euler.y > 180.0f) euler.y -= 360.0f;
            while (euler.y < -180.0f) euler.y += 360.0f;

            obj->setEulerRotation(euler);
        }
    }

    // Parse and execute a GROVE_COMMAND action's pipe-delimited command string
    void executeGroveCommand(const std::string& cmd, const glm::vec3& pos) {
        std::cout << "[Grove CMD] Executing: " << cmd << " at (" << pos.x << "," << pos.y << "," << pos.z << ")" << std::endl;
        try {
        // Split by '|'
        std::vector<std::string> parts;
        std::istringstream ss(cmd);
        std::string part;
        while (std::getline(ss, part, '|')) {
            parts.push_back(part);
        }
        if (parts.empty()) return;

        const std::string& type = parts[0];

        if (type == "cube" && parts.size() >= 6) {
            // cube|name|size|r|g|b
            std::string name = parts[1];
            float size = std::stof(parts[2]);
            float r = std::stof(parts[3]);
            float g = std::stof(parts[4]);
            float b = std::stof(parts[5]);
            glm::vec4 color(r, g, b, 1.0f);

            auto meshData = PrimitiveMeshBuilder::createCube(size, color);
            auto obj = std::make_unique<SceneObject>(name);
            uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
            obj->setLocalBounds(meshData.bounds);
            obj->setModelPath("");
            obj->setMeshData(meshData.vertices, meshData.indices);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setPrimitiveSize(size);
            obj->setPrimitiveColor(color);

            float terrainY = m_terrain.getHeightAt(pos.x, pos.z);
            obj->getTransform().setPosition(glm::vec3(pos.x, terrainY + size * 0.5f, pos.z));
            m_pendingGroveSpawns.push_back(std::move(obj));
            std::cout << "[Grove CMD] Spawned cube '" << name << "'" << std::endl;
        }
        else if (type == "cylinder" && parts.size() >= 7) {
            // cylinder|name|radius|height|r|g|b
            std::string name = parts[1];
            float radius = std::stof(parts[2]);
            float height = std::stof(parts[3]);
            float r = std::stof(parts[4]);
            float g = std::stof(parts[5]);
            float b = std::stof(parts[6]);
            glm::vec4 color(r, g, b, 1.0f);

            auto meshData = PrimitiveMeshBuilder::createCylinder(radius, height, 12, color);
            auto obj = std::make_unique<SceneObject>(name);
            uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
            obj->setLocalBounds(meshData.bounds);
            obj->setModelPath("");
            obj->setMeshData(meshData.vertices, meshData.indices);
            obj->setPrimitiveType(PrimitiveType::Cylinder);
            obj->setPrimitiveRadius(radius);
            obj->setPrimitiveHeight(height);
            obj->setPrimitiveSegments(12);
            obj->setPrimitiveColor(color);

            float terrainY = m_terrain.getHeightAt(pos.x, pos.z);
            obj->getTransform().setPosition(glm::vec3(pos.x, terrainY, pos.z));
            m_pendingGroveSpawns.push_back(std::move(obj));
            std::cout << "[Grove CMD] Spawned cylinder '" << name << "'" << std::endl;
        }
        else if (type == "beam" && parts.size() >= 9) {
            // beam|name|p2x|p2y|p2z|thickness|r|g|b
            // pos (vec3Param) holds pos1, parts hold pos2
            std::string name = parts[1];
            float p2x = std::stof(parts[2]);
            float p2y = std::stof(parts[3]);
            float p2z = std::stof(parts[4]);
            float thickness = std::stof(parts[5]);
            float r = std::stof(parts[6]);
            float g = std::stof(parts[7]);
            float b = std::stof(parts[8]);
            glm::vec4 color(r, g, b, 1.0f);

            // Compute endpoint positions (Y = terrain + offset)
            float x1 = pos.x, z1 = pos.z;
            float y1 = m_terrain.getHeightAt(x1, z1) + pos.y;
            float x2 = p2x, z2 = p2z;
            float y2 = m_terrain.getHeightAt(x2, z2) + p2y;

            float dx = x2 - x1, dy = y2 - y1, dz = z2 - z1;
            float length = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (length < 0.001f) { std::cout << "[Grove CMD] Beam too short, skipped" << std::endl; }
            else {
                float midX = (x1 + x2) * 0.5f;
                float midY = (y1 + y2) * 0.5f;
                float midZ = (z1 + z2) * 0.5f;
                float rotY = std::atan2(dx, dz) * 180.0f / 3.14159265f;
                float horizDist = std::sqrt(dx * dx + dz * dz);
                float rotX = -std::atan2(dy, horizDist) * 180.0f / 3.14159265f;

                auto meshData = PrimitiveMeshBuilder::createCube(1.0f, color);
                auto obj = std::make_unique<SceneObject>(name);
                uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
                obj->setBufferHandle(handle);
                obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
                obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
                obj->setLocalBounds(meshData.bounds);
                obj->setModelPath("");
                obj->setMeshData(meshData.vertices, meshData.indices);
                obj->setPrimitiveType(PrimitiveType::Cube);
                obj->setPrimitiveSize(1.0f);
                obj->setPrimitiveColor(color);

                obj->getTransform().setPosition(glm::vec3(midX, midY, midZ));
                obj->getTransform().setScale(glm::vec3(thickness, thickness, length));
                obj->setEulerRotation(glm::vec3(rotX, rotY, 0.0f));

                m_pendingGroveSpawns.push_back(std::move(obj));
                std::cout << "[Grove CMD] Spawned beam '" << name << "' length=" << length << std::endl;
            }
        }
        else if (type == "beam_model" && parts.size() >= 6) {
            // beam_model|name|path|p2x|p2y|p2z  (pos1 in vec3Param)
            std::string name = parts[1];
            std::string modelPath = parts[2];
            float p2x = std::stof(parts[3]);
            float p2y = std::stof(parts[4]);
            float p2z = std::stof(parts[5]);

            // Resolve path with multi-path search
            if (!modelPath.empty() && modelPath[0] != '/') {
                std::string resolved;
                std::vector<std::string> searchPaths;
                if (!m_currentLevelPath.empty()) {
                    size_t lastSlash = m_currentLevelPath.find_last_of("/\\");
                    if (lastSlash != std::string::npos)
                        searchPaths.push_back(m_currentLevelPath.substr(0, lastSlash + 1) + modelPath);
                }
                searchPaths.push_back("levels/" + modelPath);
                searchPaths.push_back(modelPath);
                for (const auto& candidate : searchPaths) {
                    std::ifstream test(candidate);
                    if (test.good()) { resolved = candidate; break; }
                }
                if (!resolved.empty()) modelPath = resolved;
            }

            // Compute beam geometry (same math as primitive beam)
            float x1 = pos.x, z1 = pos.z;
            float y1 = m_terrain.getHeightAt(x1, z1) + pos.y;
            float x2 = p2x, z2 = p2z;
            float y2 = m_terrain.getHeightAt(x2, z2) + p2y;

            float dx = x2 - x1, dy = y2 - y1, dz = z2 - z1;
            float length = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (length < 0.001f) {
                std::cout << "[Grove CMD] Beam model too short, skipped" << std::endl;
            } else {
                float midX = (x1 + x2) * 0.5f;
                float midY = (y1 + y2) * 0.5f;
                float midZ = (z1 + z2) * 0.5f;
                float rotY = std::atan2(dx, dz) * 180.0f / 3.14159265f;
                float horizDist = std::sqrt(dx * dx + dz * dz);
                float rotX = -std::atan2(dy, horizDist) * 180.0f / 3.14159265f;

                // Load model (with caching)
                std::unique_ptr<SceneObject> obj;
                auto cacheIt = m_modelCache.find(modelPath);

                if (cacheIt != m_modelCache.end()) {
                    auto& cached = cacheIt->second;
                    obj = std::make_unique<SceneObject>(name);
                    obj->setBufferHandle(cached.bufferHandle);
                    obj->setIndexCount(cached.indexCount);
                    obj->setVertexCount(cached.vertexCount);
                    obj->setMeshData(cached.vertices, cached.indices);
                    obj->setLocalBounds(cached.bounds);
                    obj->getTransform().setScale(cached.scale);
                    obj->setEulerRotation(cached.rotation);
                } else {
                    bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";
                    if (isLime) {
                        auto loadResult = LimeLoader::load(modelPath);
                        if (loadResult.success)
                            obj = LimeLoader::createSceneObject(loadResult.mesh, *m_modelRenderer);
                    } else {
                        auto loadResult = GLBLoader::load(modelPath);
                        if (loadResult.success && !loadResult.meshes.empty())
                            obj = GLBLoader::createSceneObject(loadResult.meshes[0], *m_modelRenderer);
                    }
                    if (obj) {
                        CachedModel cached;
                        cached.bufferHandle = obj->getBufferHandle();
                        cached.indexCount = obj->getIndexCount();
                        cached.vertexCount = obj->getVertexCount();
                        if (obj->hasMeshData()) {
                            cached.vertices = obj->getVertices();
                            cached.indices = obj->getIndices();
                        }
                        cached.bounds = obj->getLocalBounds();
                        cached.scale = obj->getTransform().getScale();
                        cached.rotation = obj->getEulerRotation();
                        m_modelCache[modelPath] = std::move(cached);
                    }
                }

                if (obj) {
                    obj->setName(name);
                    obj->setModelPath(modelPath);
                    obj->getTransform().setPosition(glm::vec3(midX, midY, midZ));
                    // UnitBeam is 1m along Z — scale Z to match span length
                    obj->getTransform().setScale(glm::vec3(1.0f, 1.0f, length));
                    obj->setEulerRotation(glm::vec3(rotX, rotY, 0.0f));
                    m_pendingGroveSpawns.push_back(std::move(obj));
                    std::cout << "[Grove CMD] Spawned beam model '" << name << "' length=" << length << std::endl;
                } else {
                    std::cout << "[Grove CMD] Failed to load beam model: " << modelPath << std::endl;
                }
            }
        }
        else if (type == "wall_panel" && parts.size() >= 6) {
            // wall_panel|name|path|p2x|p2y|p2z  (pos1 in vec3Param)
            std::string name = parts[1];
            std::string modelPath = parts[2];
            float p2x = std::stof(parts[3]);
            float p2y = std::stof(parts[4]);
            float p2z = std::stof(parts[5]);

            // Resolve path with multi-path search
            if (!modelPath.empty() && modelPath[0] != '/') {
                std::string resolved;
                std::vector<std::string> searchPaths;
                if (!m_currentLevelPath.empty()) {
                    size_t lastSlash = m_currentLevelPath.find_last_of("/\\");
                    if (lastSlash != std::string::npos)
                        searchPaths.push_back(m_currentLevelPath.substr(0, lastSlash + 1) + modelPath);
                }
                searchPaths.push_back("levels/" + modelPath);
                searchPaths.push_back(modelPath);
                for (const auto& candidate : searchPaths) {
                    std::ifstream test(candidate);
                    if (test.good()) { resolved = candidate; break; }
                }
                if (!resolved.empty()) modelPath = resolved;
            }

            // Compute wall geometry from two post positions
            float x1 = pos.x, z1 = pos.z;
            float x2 = p2x, z2 = p2z;

            float dx = x2 - x1;
            float dz = z2 - z1;
            float distance = std::sqrt(dx * dx + dz * dz);
            if (distance < 0.001f) {
                std::cout << "[Grove CMD] Wall panel too short, skipped" << std::endl;
            } else {
                float midX = (x1 + x2) * 0.5f;
                float midZ = (z1 + z2) * 0.5f;
                float terrainY = m_terrain.getHeightAt(midX, midZ);

                // Rotation: align panel's X-axis with direction from pos1 to pos2
                // X-axis after Y-rotation = (cos(θ), 0, -sin(θ)), so θ = atan2(-dz, dx)
                float rotY = std::atan2(-dz, dx) * 180.0f / 3.14159265f;

                // Load model (with caching)
                std::unique_ptr<SceneObject> obj;
                auto cacheIt = m_modelCache.find(modelPath);

                if (cacheIt != m_modelCache.end()) {
                    auto& cached = cacheIt->second;
                    obj = std::make_unique<SceneObject>(name);
                    obj->setBufferHandle(cached.bufferHandle);
                    obj->setIndexCount(cached.indexCount);
                    obj->setVertexCount(cached.vertexCount);
                    obj->setMeshData(cached.vertices, cached.indices);
                    obj->setLocalBounds(cached.bounds);
                    obj->getTransform().setScale(cached.scale);
                    obj->setEulerRotation(cached.rotation);
                } else {
                    bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";
                    if (isLime) {
                        auto loadResult = LimeLoader::load(modelPath);
                        if (loadResult.success)
                            obj = LimeLoader::createSceneObject(loadResult.mesh, *m_modelRenderer);
                    } else {
                        auto loadResult = GLBLoader::load(modelPath);
                        if (loadResult.success && !loadResult.meshes.empty())
                            obj = GLBLoader::createSceneObject(loadResult.meshes[0], *m_modelRenderer);
                    }
                    if (obj) {
                        CachedModel cached;
                        cached.bufferHandle = obj->getBufferHandle();
                        cached.indexCount = obj->getIndexCount();
                        cached.vertexCount = obj->getVertexCount();
                        if (obj->hasMeshData()) {
                            cached.vertices = obj->getVertices();
                            cached.indices = obj->getIndices();
                        }
                        cached.bounds = obj->getLocalBounds();
                        cached.scale = obj->getTransform().getScale();
                        cached.rotation = obj->getEulerRotation();
                        m_modelCache[modelPath] = std::move(cached);
                    }
                }

                if (obj) {
                    obj->setName(name);
                    obj->setModelPath(modelPath);

                    // Find min Y vertex and account for model's native scale
                    float minVertexY = 0.0f;
                    if (obj->hasMeshData()) {
                        const auto& verts = obj->getVertices();
                        if (!verts.empty()) {
                            minVertexY = verts[0].position.y;
                            for (const auto& v : verts)
                                if (v.position.y < minVertexY) minVertexY = v.position.y;
                        }
                    }
                    glm::vec3 scale = obj->getTransform().getScale();
                    float bottomOffset = -minVertexY * scale.y;

                    // Place at midpoint, native scale, only rotate to face between posts
                    obj->getTransform().setPosition(glm::vec3(midX, terrainY + bottomOffset, midZ));
                    obj->setEulerRotation(glm::vec3(0.0f, rotY, 0.0f));

                    m_pendingGroveSpawns.push_back(std::move(obj));
                    std::cout << "[Grove CMD] Spawned wall panel '" << name
                              << "' rotY=" << rotY << std::endl;
                } else {
                    std::cout << "[Grove CMD] Failed to load wall panel model: " << modelPath << std::endl;
                }
            }
        }
        else if (type == "model" && parts.size() >= 3) {
            // model|name|path
            std::string name = parts[1];
            std::string modelPath = parts[2];
            std::cout << "[Grove CMD] model command: name='" << name << "' rawPath='" << modelPath << "'" << std::endl;

            // Resolve relative paths — search multiple locations
            if (!modelPath.empty() && modelPath[0] != '/') {
                std::string resolved;
                std::vector<std::string> searchPaths;
                if (!m_currentLevelPath.empty()) {
                    size_t lastSlash = m_currentLevelPath.find_last_of("/\\");
                    if (lastSlash != std::string::npos)
                        searchPaths.push_back(m_currentLevelPath.substr(0, lastSlash + 1) + modelPath);
                }
                searchPaths.push_back("levels/" + modelPath);
                searchPaths.push_back(modelPath);
                for (const auto& candidate : searchPaths) {
                    std::ifstream test(candidate);
                    if (test.good()) { resolved = candidate; break; }
                }
                if (!resolved.empty()) modelPath = resolved;
                else std::cout << "[Grove CMD] Model not found in any search path for: " << modelPath << std::endl;
            }

            // Check model cache — reuse GPU buffer if same file was already loaded
            std::unique_ptr<SceneObject> obj;
            auto cacheIt = m_modelCache.find(modelPath);

            if (cacheIt != m_modelCache.end()) {
                // Cache hit — create SceneObject sharing the existing GPU buffer
                auto& cached = cacheIt->second;
                obj = std::make_unique<SceneObject>(name);
                obj->setBufferHandle(cached.bufferHandle);
                obj->setIndexCount(cached.indexCount);
                obj->setVertexCount(cached.vertexCount);
                obj->setMeshData(cached.vertices, cached.indices);
                obj->setLocalBounds(cached.bounds);
                obj->getTransform().setScale(cached.scale);
                obj->setEulerRotation(cached.rotation);
                std::cout << "[Grove CMD] model cache hit: " << modelPath << std::endl;
            } else {
                // Cache miss — load from disk
                bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";
                if (isLime) {
                    auto loadResult = LimeLoader::load(modelPath);
                    if (loadResult.success)
                        obj = LimeLoader::createSceneObject(loadResult.mesh, *m_modelRenderer);
                } else {
                    auto loadResult = GLBLoader::load(modelPath);
                    if (loadResult.success && !loadResult.meshes.empty())
                        obj = GLBLoader::createSceneObject(loadResult.meshes[0], *m_modelRenderer);
                }
                // Store in cache for future reuse
                if (obj) {
                    CachedModel cached;
                    cached.bufferHandle = obj->getBufferHandle();
                    cached.indexCount = obj->getIndexCount();
                    cached.vertexCount = obj->getVertexCount();
                    if (obj->hasMeshData()) {
                        cached.vertices = obj->getVertices();
                        cached.indices = obj->getIndices();
                    }
                    cached.bounds = obj->getLocalBounds();
                    cached.scale = obj->getTransform().getScale();
                    cached.rotation = obj->getEulerRotation();
                    m_modelCache[modelPath] = std::move(cached);
                    std::cout << "[Grove CMD] model cached: " << modelPath << std::endl;
                }
            }

            if (obj) {
                obj->setName(name);
                obj->setModelPath(modelPath);

                // Position bottom on terrain
                float terrainY = m_terrain.getHeightAt(pos.x, pos.z);
                glm::vec3 scale = obj->getTransform().getScale();
                float minVertexY = 0.0f;
                if (obj->hasMeshData()) {
                    const auto& verts = obj->getVertices();
                    if (!verts.empty()) {
                        minVertexY = verts[0].position.y;
                        for (const auto& v : verts) {
                            if (v.position.y < minVertexY) minVertexY = v.position.y;
                        }
                    }
                }
                float bottomOffset = -minVertexY * scale.y;
                obj->getTransform().setPosition(glm::vec3(pos.x, terrainY + bottomOffset, pos.z));

                m_pendingGroveSpawns.push_back(std::move(obj));
                std::cout << "[Grove CMD] Spawned model '" << name << "' from " << modelPath << std::endl;
            } else {
                std::cout << "[Grove CMD] Failed to load model: " << modelPath << std::endl;
            }
        }
        else if (type == "set_rotation" && parts.size() >= 5) {
            // set_rotation|name|rx|ry|rz
            std::string name = parts[1];
            float rx = std::stof(parts[2]);
            float ry = std::stof(parts[3]);
            float rz = std::stof(parts[4]);
            bool found = false;
            // Search m_sceneObjects first
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == name) {
                    o->setEulerRotation(glm::vec3(rx, ry, rz));
                    found = true; break;
                }
            }
            // Also check pending spawns (objects spawned in same behavior sequence)
            if (!found) {
                for (auto& o : m_pendingGroveSpawns) {
                    if (o && o->getName() == name) {
                        o->setEulerRotation(glm::vec3(rx, ry, rz));
                        found = true; break;
                    }
                }
            }
            if (found) std::cout << "[Grove CMD] Set rotation on '" << name << "'" << std::endl;
            else std::cout << "[Grove CMD] set_rotation: object '" << name << "' not found" << std::endl;
        }
        else if (type == "set_scale" && parts.size() >= 5) {
            // set_scale|name|sx|sy|sz
            std::string name = parts[1];
            float sx = std::stof(parts[2]);
            float sy = std::stof(parts[3]);
            float sz = std::stof(parts[4]);
            bool found = false;
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == name) {
                    o->getTransform().setScale(glm::vec3(sx, sy, sz));
                    found = true; break;
                }
            }
            if (!found) {
                for (auto& o : m_pendingGroveSpawns) {
                    if (o && o->getName() == name) {
                        o->getTransform().setScale(glm::vec3(sx, sy, sz));
                        found = true; break;
                    }
                }
            }
            if (found) std::cout << "[Grove CMD] Set scale on '" << name << "'" << std::endl;
            else std::cout << "[Grove CMD] set_scale: object '" << name << "' not found" << std::endl;
        }
        else if (type == "delete" && parts.size() >= 2) {
            // delete|name — defer to destroy queue to avoid iterator invalidation
            std::string name = parts[1];
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == name) {
                    m_objectsToDestroy.push_back(o.get());
                    std::cout << "[Grove CMD] Queued delete '" << name << "'" << std::endl;
                    break;
                }
            }
        }
        } catch (const std::exception& e) {
            std::cerr << "[Grove CMD] EXCEPTION: " << e.what() << std::endl;
            std::cerr.flush();
        }
    }

    void updateActiveBehavior(SceneObject* obj, float deltaTime) {
        if (!obj || !obj->hasActiveBehavior()) return;

        int behaviorIdx = obj->getActiveBehaviorIndex();
        auto& behaviors = obj->getBehaviors();
        if (behaviorIdx < 0 || behaviorIdx >= static_cast<int>(behaviors.size())) {
            obj->clearActiveBehavior();
            return;
        }

        Behavior& behavior = behaviors[behaviorIdx];
        int actionIdx = obj->getActiveActionIndex();
        if (actionIdx < 0 || actionIdx >= static_cast<int>(behavior.actions.size())) {
            // Behavior complete - all actions done
            if (behavior.loop && !behavior.actions.empty()) {
                // Restart from first action
                obj->setActiveActionIndex(0);
                actionIdx = 0;
                obj->clearPathWaypoints();
                obj->resetPathComplete();
                obj->setCurrentWaypointIndex(0);
                // Immediately load the first action if it's a path
                if (behavior.actions[0].type == ActionType::FOLLOW_PATH) {
                    loadPathForAction(obj, behavior.actions[0]);
                }
            } else {
                obj->clearActiveBehavior();
                return;
            }
        }

        // Safety: re-validate actionIdx after potential modification
        if (actionIdx < 0 || actionIdx >= static_cast<int>(behavior.actions.size())) {
            obj->clearActiveBehavior();
            return;
        }

        Action& currentAction = behavior.actions[actionIdx];

        if (currentAction.type == ActionType::FOLLOW_PATH) {
            // Load path waypoints if not already loaded or path name changed
            std::string pathName = currentAction.stringParam;
            if (obj->getCurrentPathName() != pathName || !obj->hasPathWaypoints()) {
                loadPathForAction(obj, currentAction);
            }

            // Move along the path
            updatePathPatrol(obj, deltaTime);

            // Check if path is complete
            if (obj->isPathComplete()) {
                obj->clearPathWaypoints();

                // Check exit condition ON_PATH_COMPLETE
                if (behavior.exitCondition == ExitCondition::ON_PATH_COMPLETE) {
                    obj->clearActiveBehavior();
                    std::cout << "Path complete, exit condition met for " << obj->getName() << std::endl;
                    return;
                }

                obj->setActiveActionIndex(actionIdx + 1);
                std::cout << "Completed path: " << pathName << ", advancing to next action" << std::endl;

                // Load next action's path if it's also FOLLOW_PATH
                int nextIdx = obj->getActiveActionIndex();
                if (nextIdx < static_cast<int>(behavior.actions.size())) {
                    if (behavior.actions[nextIdx].type == ActionType::FOLLOW_PATH) {
                        loadPathForAction(obj, behavior.actions[nextIdx]);
                    }
                }
            }
        }
        else if (currentAction.type == ActionType::WAIT) {
            // Initialize wait timer if needed
            if (obj->getWaitTimer() <= 0.0f) {
                obj->setWaitTimer(currentAction.duration);

                // Play animation if specified (for skinned models)
                if (obj->isSkinned() && !currentAction.stringParam.empty()) {
                    m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(),
                                                         currentAction.stringParam, true);
                    obj->setCurrentAnimation(currentAction.stringParam);
                    std::cout << "WAIT: Playing '" << currentAction.stringParam
                              << "' for " << currentAction.duration << " seconds" << std::endl;
                } else {
                    std::cout << "WAIT: Waiting for " << currentAction.duration << " seconds" << std::endl;
                }
            }

            // Decrement timer
            obj->decrementWaitTimer(deltaTime);

            // Check if wait is complete
            if (obj->getWaitTimer() <= 0.0f) {
                obj->setActiveActionIndex(actionIdx + 1);
                std::cout << "WAIT: Complete, advancing to next action" << std::endl;

                // Load next action's path if it's FOLLOW_PATH
                int nextIdx = obj->getActiveActionIndex();
                if (nextIdx < static_cast<int>(behavior.actions.size())) {
                    if (behavior.actions[nextIdx].type == ActionType::FOLLOW_PATH) {
                        loadPathForAction(obj, behavior.actions[nextIdx]);
                    }
                }
            }
        }
        else if (currentAction.type == ActionType::SPAWN_ENTITY) {
            // Spawn a model - use stringParam as model path, or clone self if empty
            std::string modelPath = currentAction.stringParam;
            if (modelPath.empty()) {
                modelPath = obj->getModelPath();  // Clone self
            }

            if (!modelPath.empty()) {
                // Queue the spawn (don't modify m_sceneObjects while iterating)
                SpawnRequest spawn;
                spawn.modelPath = modelPath;
                spawn.position = obj->getTransform().getPosition() + currentAction.vec3Param;
                spawn.rotation = obj->getEulerRotation();
                spawn.scale = obj->getTransform().getScale();
                m_pendingSpawns.push_back(spawn);

                std::cout << "Queued spawn: " << modelPath << " at offset "
                          << currentAction.vec3Param.x << ", "
                          << currentAction.vec3Param.y << ", "
                          << currentAction.vec3Param.z << std::endl;
            }

            // Advance to next action
            obj->setActiveActionIndex(actionIdx + 1);
            int nextIdx = obj->getActiveActionIndex();
            if (nextIdx < static_cast<int>(behavior.actions.size())) {
                if (behavior.actions[nextIdx].type == ActionType::FOLLOW_PATH) {
                    loadPathForAction(obj, behavior.actions[nextIdx]);
                }
            }
        }
        else if (currentAction.type == ActionType::SET_VISIBLE) {
            // Toggle visibility
            obj->setVisible(currentAction.boolParam);
            std::cout << "SET_VISIBLE: " << (currentAction.boolParam ? "true" : "false") << std::endl;

            // Advance immediately
            obj->setActiveActionIndex(actionIdx + 1);
        }
        else if (currentAction.type == ActionType::MOVE_TO) {
            // Smooth movement to target position
            glm::vec3 targetPos = currentAction.vec3Param;
            glm::vec3 currentPos = obj->getTransform().getPosition();

            // Calculate duration - either from speed (floatParam) or fixed duration
            float duration;
            if (currentAction.floatParam > 0.0f) {
                // Speed-based: floatParam is speed in units/second
                float distance = glm::length(targetPos - currentPos);
                duration = distance / currentAction.floatParam;
                if (duration < 0.1f) duration = 0.1f;  // Minimum duration
            } else {
                // Duration-based: use duration field directly
                duration = currentAction.duration > 0.0f ? currentAction.duration : 1.0f;
            }

            // Initialize move if just started
            if (!obj->isMovingTo()) {
                bool useLinear = (currentAction.easing == Action::Easing::LINEAR);
                obj->startMoveTo(currentPos, targetPos, duration, useLinear);

                // Play animation if specified (for skinned models)
                if (obj->isSkinned() && !currentAction.animationParam.empty()) {
                    m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(),
                                                         currentAction.animationParam, true);
                    obj->setCurrentAnimation(currentAction.animationParam);
                }

                if (currentAction.floatParam > 0.0f) {
                    std::cout << "MOVE_TO: Starting move to " << targetPos.x << ", " << targetPos.y << ", " << targetPos.z
                              << " at speed " << currentAction.floatParam << " (" << duration << "s)"
                              << (useLinear ? " [linear]" : " [eased]") << std::endl;
                } else {
                    std::cout << "MOVE_TO: Starting move to " << targetPos.x << ", " << targetPos.y << ", " << targetPos.z
                              << " over " << duration << "s"
                              << (useLinear ? " [linear]" : " [eased]") << std::endl;
                }
            }

            // Update movement
            obj->updateMoveTo(deltaTime);

            // Check if complete
            if (!obj->isMovingTo()) {
                obj->getTransform().setPosition(targetPos);  // Snap to final position
                obj->setActiveActionIndex(actionIdx + 1);
                std::cout << "MOVE_TO: Complete" << std::endl;

                // Load next action's path if needed
                int nextIdx = obj->getActiveActionIndex();
                if (nextIdx < static_cast<int>(behavior.actions.size())) {
                    if (behavior.actions[nextIdx].type == ActionType::FOLLOW_PATH) {
                        loadPathForAction(obj, behavior.actions[nextIdx]);
                    }
                }
            }
        }
        else if (currentAction.type == ActionType::MOVE) {
            // Relative movement by offset over duration
            glm::vec3 offset = currentAction.vec3Param;
            float duration = currentAction.duration > 0.0f ? currentAction.duration : 1.0f;

            // Initialize move if just started
            if (!obj->isMovingTo()) {
                glm::vec3 startPos = obj->getTransform().getPosition();
                glm::vec3 targetPos = startPos + offset;
                obj->startMoveTo(startPos, targetPos, duration);
                std::cout << "MOVE: Starting relative move by " << offset.x << ", " << offset.y << ", " << offset.z
                          << " over " << duration << "s" << std::endl;
            }

            // Update movement
            obj->updateMoveTo(deltaTime);

            // Check if complete
            if (!obj->isMovingTo()) {
                obj->setActiveActionIndex(actionIdx + 1);
                std::cout << "MOVE: Complete" << std::endl;

                int nextIdx = obj->getActiveActionIndex();
                if (nextIdx < static_cast<int>(behavior.actions.size())) {
                    if (behavior.actions[nextIdx].type == ActionType::FOLLOW_PATH) {
                        loadPathForAction(obj, behavior.actions[nextIdx]);
                    }
                }
            }
        }
        else if (currentAction.type == ActionType::TURN_TO) {
            // Turn to face a position (yaw only)
            glm::vec3 targetPos = currentAction.vec3Param;
            glm::vec3 currentPos = obj->getTransform().getPosition();
            float duration = currentAction.duration > 0.0f ? currentAction.duration : 0.5f;

            // Calculate target yaw angle
            glm::vec3 direction = targetPos - currentPos;
            direction.y = 0.0f;  // Only horizontal direction
            float targetYaw = 0.0f;
            if (glm::length(direction) > 0.001f) {
                direction = glm::normalize(direction);
                targetYaw = glm::degrees(atan2(direction.x, direction.z));
            }

            // Initialize turn if just started
            if (!obj->isTurning()) {
                glm::vec3 currentRot = obj->getEulerRotation();
                float currentYaw = currentRot.y;

                // Find shortest rotation path
                float deltaYaw = targetYaw - currentYaw;
                while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                while (deltaYaw < -180.0f) deltaYaw += 360.0f;

                obj->startTurnTo(currentYaw, currentYaw + deltaYaw, duration);
                std::cout << "TURN_TO: Turning from " << currentYaw << " to " << (currentYaw + deltaYaw)
                          << " over " << duration << "s" << std::endl;
            }

            // Update turn interpolation
            obj->updateTurnTo(deltaTime);

            // Check if complete
            if (!obj->isTurning()) {
                obj->setActiveActionIndex(actionIdx + 1);
                std::cout << "TURN_TO: Complete, now facing " << obj->getEulerRotation().y << " degrees" << std::endl;

                int nextIdx = obj->getActiveActionIndex();
                if (nextIdx < static_cast<int>(behavior.actions.size())) {
                    if (behavior.actions[nextIdx].type == ActionType::FOLLOW_PATH) {
                        loadPathForAction(obj, behavior.actions[nextIdx]);
                    }
                }
            }
        }
        else if (currentAction.type == ActionType::PICKUP) {
            // Three-phase: turn to face → walk to target → pick up
            // Resolve target position at runtime (not from baked vec3Param) since objects may have moved
            glm::vec3 targetPos = currentAction.vec3Param;
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == currentAction.stringParam && o->isVisible()) {
                    targetPos = o->getTransform().getPosition();
                    break;
                }
            }
            glm::vec3 currentPos = obj->getTransform().getPosition();
            bool useGravity = currentAction.boolParam;
            float speed = currentAction.floatParam > 0.0f ? currentAction.floatParam : 2.0f;

            // Phase 1: Turn to face target
            if (!obj->isTurning() && !obj->isMovingTo()) {
                glm::vec3 dir = targetPos - currentPos;
                dir.y = 0.0f;
                if (glm::length(dir) > 0.01f) {
                    dir = glm::normalize(dir);
                    float targetYaw = glm::degrees(atan2(dir.x, dir.z));
                    float currentYaw = obj->getEulerRotation().y;
                    float deltaYaw = targetYaw - currentYaw;
                    while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                    while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                    if (std::abs(deltaYaw) > 1.0f) {
                        obj->startTurnTo(currentYaw, currentYaw + deltaYaw, 0.3f);
                        std::cout << "PICKUP: Turning to face '" << currentAction.stringParam << "'" << std::endl;
                        return;  // Wait for turn to complete
                    }
                }
                // No turn needed or target too close — start walking immediately
                float distance = glm::length(targetPos - currentPos);
                float duration = distance / speed;
                if (duration < 0.1f) duration = 0.1f;
                obj->startMoveTo(currentPos, targetPos, duration, true);
                if (obj->isSkinned()) {
                    m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                    obj->setCurrentAnimation("walk");
                }
                std::cout << "PICKUP: Walking to '" << currentAction.stringParam
                          << "' speed=" << speed << (useGravity ? " [ground]" : " [fly]") << std::endl;
            }

            // Phase 1 continued: update turn
            if (obj->isTurning()) {
                obj->updateTurnTo(deltaTime);
                if (!obj->isTurning()) {
                    // Turn done — start walking
                    currentPos = obj->getTransform().getPosition();
                    float distance = glm::length(targetPos - currentPos);
                    float duration = distance / speed;
                    if (duration < 0.1f) duration = 0.1f;
                    obj->startMoveTo(currentPos, targetPos, duration, true);
                    if (obj->isSkinned()) {
                        m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                        obj->setCurrentAnimation("walk");
                    }
                    std::cout << "PICKUP: Walking to '" << currentAction.stringParam
                              << "' speed=" << speed << (useGravity ? " [ground]" : " [fly]") << std::endl;
                }
                return;
            }

            // Phase 2: Walk
            if (obj->isMovingTo()) {
                obj->updateMoveTo(deltaTime);
                if (useGravity) {
                    glm::vec3 pos = obj->getTransform().getPosition();
                    pos.y = m_terrain.getHeightAt(pos.x, pos.z);
                    obj->getTransform().setPosition(pos);
                }
            }

            // Phase 3: Arrived — pick up
            if (!obj->isMovingTo() && !obj->isTurning()) {
                glm::vec3 finalPos = targetPos;
                if (useGravity) finalPos.y = m_terrain.getHeightAt(finalPos.x, finalPos.z);
                obj->getTransform().setPosition(finalPos);
                std::string itemName = currentAction.stringParam;

                SceneObject* target = nullptr;
                for (auto& o : m_sceneObjects) {
                    if (o && o->getName() == itemName && o->isVisible()) {
                        target = o.get();
                        break;
                    }
                }

                if (target && !obj->isCarrying()) {
                    target->setVisible(false);
                    obj->setCarriedItem(itemName, target);
                    std::cout << "PICKUP: Picked up '" << itemName << "'" << std::endl;
                } else if (!target) {
                    std::cout << "PICKUP: Target '" << itemName << "' not found or not visible" << std::endl;
                }

                obj->setActiveActionIndex(actionIdx + 1);
            }
        }
        else if (currentAction.type == ActionType::PLACE_VERTICAL) {
            // Three-phase: turn to face → walk to target → place item
            // Resolve target position at runtime (not from baked vec3Param) since objects may have moved
            glm::vec3 targetPos = currentAction.vec3Param;
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == currentAction.stringParam) {
                    targetPos = o->getTransform().getPosition();
                    break;
                }
            }
            glm::vec3 currentPos = obj->getTransform().getPosition();
            bool useGravity = currentAction.boolParam;
            float speed = currentAction.floatParam > 0.0f ? currentAction.floatParam : 2.0f;

            // Phase 1: Turn to face target
            if (!obj->isTurning() && !obj->isMovingTo()) {
                glm::vec3 dir = targetPos - currentPos;
                dir.y = 0.0f;
                if (glm::length(dir) > 0.01f) {
                    dir = glm::normalize(dir);
                    float targetYaw = glm::degrees(atan2(dir.x, dir.z));
                    float currentYaw = obj->getEulerRotation().y;
                    float deltaYaw = targetYaw - currentYaw;
                    while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                    while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                    if (std::abs(deltaYaw) > 1.0f) {
                        obj->startTurnTo(currentYaw, currentYaw + deltaYaw, 0.3f);
                        std::cout << "PLACE_VERTICAL: Turning to face '" << currentAction.stringParam << "'" << std::endl;
                        return;
                    }
                }
                float distance = glm::length(targetPos - currentPos);
                float duration = distance / speed;
                if (duration < 0.1f) duration = 0.1f;
                obj->startMoveTo(currentPos, targetPos, duration, true);
                if (obj->isSkinned()) {
                    m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                    obj->setCurrentAnimation("walk");
                }
                std::cout << "PLACE_VERTICAL: Walking to '" << currentAction.stringParam
                          << "' speed=" << speed << (useGravity ? " [ground]" : " [fly]") << std::endl;
            }

            if (obj->isTurning()) {
                obj->updateTurnTo(deltaTime);
                if (!obj->isTurning()) {
                    currentPos = obj->getTransform().getPosition();
                    float distance = glm::length(targetPos - currentPos);
                    float duration = distance / speed;
                    if (duration < 0.1f) duration = 0.1f;
                    obj->startMoveTo(currentPos, targetPos, duration, true);
                    if (obj->isSkinned()) {
                        m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                        obj->setCurrentAnimation("walk");
                    }
                    std::cout << "PLACE_VERTICAL: Walking to '" << currentAction.stringParam
                              << "' speed=" << speed << (useGravity ? " [ground]" : " [fly]") << std::endl;
                }
                return;
            }

            // Phase 2: Walk
            if (obj->isMovingTo()) {
                obj->updateMoveTo(deltaTime);
                if (useGravity) {
                    glm::vec3 pos = obj->getTransform().getPosition();
                    pos.y = m_terrain.getHeightAt(pos.x, pos.z);
                    obj->getTransform().setPosition(pos);
                }
            }

            // Phase 3: Arrived — place item
            if (!obj->isMovingTo() && !obj->isTurning()) {
                glm::vec3 finalPos = targetPos;
                if (useGravity) finalPos.y = m_terrain.getHeightAt(finalPos.x, finalPos.z);
                obj->getTransform().setPosition(finalPos);
                std::string placeName = currentAction.stringParam;

                SceneObject* placeTarget = nullptr;
                for (auto& o : m_sceneObjects) {
                    if (o && o->getName() == placeName) {
                        placeTarget = o.get();
                        break;
                    }
                }

                if (placeTarget && obj->isCarrying()) {
                    std::string carriedName = obj->getCarriedItemName();
                    placeCarriedItemAt(obj, placeTarget);
                    std::cout << "PLACE_VERTICAL: Placed '" << carriedName
                              << "' into '" << placeName << "'" << std::endl;
                } else if (!obj->isCarrying()) {
                    std::cout << "PLACE_VERTICAL: Not carrying anything" << std::endl;
                } else {
                    std::cout << "PLACE_VERTICAL: Target '" << placeName << "' not found" << std::endl;
                }

                obj->setActiveActionIndex(actionIdx + 1);
            }
        }
        else if (currentAction.type == ActionType::PLACE_AT) {
            // Three-phase: turn to face → walk to position → place carried item on terrain
            glm::vec3 targetPos = currentAction.vec3Param;
            // Resolve Y to terrain height
            targetPos.y = m_terrain.getHeightAt(targetPos.x, targetPos.z);
            glm::vec3 currentPos = obj->getTransform().getPosition();
            bool useGravity = currentAction.boolParam;
            float speed = currentAction.floatParam > 0.0f ? currentAction.floatParam : 2.0f;

            // Phase 1: Turn to face target
            if (!obj->isTurning() && !obj->isMovingTo()) {
                glm::vec3 dir = targetPos - currentPos;
                dir.y = 0.0f;
                if (glm::length(dir) > 0.01f) {
                    dir = glm::normalize(dir);
                    float targetYaw = glm::degrees(atan2(dir.x, dir.z));
                    float currentYaw = obj->getEulerRotation().y;
                    float deltaYaw = targetYaw - currentYaw;
                    while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                    while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                    if (std::abs(deltaYaw) > 1.0f) {
                        obj->startTurnTo(currentYaw, currentYaw + deltaYaw, 0.3f);
                        std::cout << "PLACE_AT: Turning to face target position" << std::endl;
                        return;
                    }
                }
                float distance = glm::length(targetPos - currentPos);
                float duration = distance / speed;
                if (duration < 0.1f) duration = 0.1f;
                obj->startMoveTo(currentPos, targetPos, duration, true);
                if (obj->isSkinned()) {
                    m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                    obj->setCurrentAnimation("walk");
                }
                std::cout << "PLACE_AT: Walking to position" << std::endl;
            }

            if (obj->isTurning()) {
                obj->updateTurnTo(deltaTime);
                if (!obj->isTurning()) {
                    currentPos = obj->getTransform().getPosition();
                    float distance = glm::length(targetPos - currentPos);
                    float duration = distance / speed;
                    if (duration < 0.1f) duration = 0.1f;
                    obj->startMoveTo(currentPos, targetPos, duration, true);
                    if (obj->isSkinned()) {
                        m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                        obj->setCurrentAnimation("walk");
                    }
                    std::cout << "PLACE_AT: Walking to position" << std::endl;
                }
                return;
            }

            // Phase 2: Walk
            if (obj->isMovingTo()) {
                obj->updateMoveTo(deltaTime);
                if (useGravity) {
                    glm::vec3 pos = obj->getTransform().getPosition();
                    pos.y = m_terrain.getHeightAt(pos.x, pos.z);
                    obj->getTransform().setPosition(pos);
                }
            }

            // Phase 3: Arrived — place carried item on terrain
            if (!obj->isMovingTo() && !obj->isTurning()) {
                glm::vec3 finalPos = targetPos;
                finalPos.y = m_terrain.getHeightAt(finalPos.x, finalPos.z);
                obj->getTransform().setPosition(finalPos);

                if (obj->isCarrying()) {
                    std::string carriedName = obj->getCarriedItemName();
                    // Find the carried object and make it visible at this position
                    for (auto& o : m_sceneObjects) {
                        if (o && o->getName() == carriedName) {
                            // Place bottom on terrain using min-vertex-Y offset
                            glm::vec3 scale = o->getTransform().getScale();
                            float minVertexY = 0.0f;
                            if (o->hasMeshData()) {
                                const auto& verts = o->getVertices();
                                for (const auto& v : verts) {
                                    if (v.position.y < minVertexY) minVertexY = v.position.y;
                                }
                            }
                            float bottomOffset = -minVertexY * scale.y;
                            o->getTransform().setPosition(glm::vec3(finalPos.x, finalPos.y + bottomOffset, finalPos.z));
                            o->setEulerRotation(glm::vec3(0));  // Upright
                            o->setVisible(true);
                            std::cout << "PLACE_AT: Placed '" << carriedName << "' at ("
                                      << finalPos.x << ", " << finalPos.y << ", " << finalPos.z << ")" << std::endl;
                            break;
                        }
                    }
                    obj->clearCarriedItem();
                } else {
                    std::cout << "PLACE_AT: Not carrying anything" << std::endl;
                }

                obj->setActiveActionIndex(actionIdx + 1);
            }
        }
        else if (currentAction.type == ActionType::PLACE_HORIZONTAL) {
            // Three-phase: turn to face midpoint → walk there → place carried item as horizontal beam
            // stringParam = "nameA|nameB" (pipe-delimited target names)
            // Resolve target positions at runtime (objects may have moved since script was parsed)
            std::string params = currentAction.stringParam;
            size_t pipePos = params.find('|');
            std::string nameA = (pipePos != std::string::npos) ? params.substr(0, pipePos) : params;
            std::string nameB = (pipePos != std::string::npos) ? params.substr(pipePos + 1) : "";
            glm::vec3 posA(0), posB(0);
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == nameA) posA = o->getTransform().getPosition();
                if (o && o->getName() == nameB) posB = o->getTransform().getPosition();
            }
            glm::vec3 targetPos = (posA + posB) * 0.5f;
            targetPos.y = m_terrain.getHeightAt(targetPos.x, targetPos.z);
            glm::vec3 currentPos = obj->getTransform().getPosition();
            bool useGravity = currentAction.boolParam;
            float speed = currentAction.floatParam > 0.0f ? currentAction.floatParam : 2.0f;

            // Phase 1: Turn to face midpoint
            if (!obj->isTurning() && !obj->isMovingTo()) {
                glm::vec3 dir = targetPos - currentPos;
                dir.y = 0.0f;
                if (glm::length(dir) > 0.01f) {
                    dir = glm::normalize(dir);
                    float targetYaw = glm::degrees(atan2(dir.x, dir.z));
                    float currentYaw = obj->getEulerRotation().y;
                    float deltaYaw = targetYaw - currentYaw;
                    while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                    while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                    if (std::abs(deltaYaw) > 1.0f) {
                        obj->startTurnTo(currentYaw, currentYaw + deltaYaw, 0.3f);
                        std::cout << "PLACE_HORIZONTAL: Turning to face midpoint" << std::endl;
                        return;
                    }
                }
                float distance = glm::length(targetPos - currentPos);
                float duration = distance / speed;
                if (duration < 0.1f) duration = 0.1f;
                obj->startMoveTo(currentPos, targetPos, duration, true);
                if (obj->isSkinned()) {
                    m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                    obj->setCurrentAnimation("walk");
                }
                std::cout << "PLACE_HORIZONTAL: Walking to midpoint" << std::endl;
            }

            if (obj->isTurning()) {
                obj->updateTurnTo(deltaTime);
                if (!obj->isTurning()) {
                    currentPos = obj->getTransform().getPosition();
                    float distance = glm::length(targetPos - currentPos);
                    float duration = distance / speed;
                    if (duration < 0.1f) duration = 0.1f;
                    obj->startMoveTo(currentPos, targetPos, duration, true);
                    if (obj->isSkinned()) {
                        m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                        obj->setCurrentAnimation("walk");
                    }
                    std::cout << "PLACE_HORIZONTAL: Walking to midpoint" << std::endl;
                }
                return;
            }

            // Phase 2: Walk
            if (obj->isMovingTo()) {
                obj->updateMoveTo(deltaTime);
                if (useGravity) {
                    glm::vec3 pos = obj->getTransform().getPosition();
                    pos.y = m_terrain.getHeightAt(pos.x, pos.z);
                    obj->getTransform().setPosition(pos);
                }
            }

            // Phase 3: Arrived — place carried item as horizontal beam between the two targets
            if (!obj->isMovingTo() && !obj->isTurning()) {
                glm::vec3 finalPos = targetPos;
                finalPos.y = m_terrain.getHeightAt(finalPos.x, finalPos.z);
                obj->getTransform().setPosition(finalPos);

                if (obj->isCarrying()) {
                    // Re-resolve positions at placement time (already have nameA/nameB from top)
                    posA = glm::vec3(0); posB = glm::vec3(0);
                    for (auto& o : m_sceneObjects) {
                        if (o && o->getName() == nameA) posA = o->getTransform().getPosition();
                        if (o && o->getName() == nameB) posB = o->getTransform().getPosition();
                    }

                    std::string carriedName = obj->getCarriedItemName();
                    placeCarriedItemHorizontal(obj, posA, posB);
                    std::cout << "PLACE_HORIZONTAL: Placed '" << carriedName
                              << "' between '" << nameA << "' and '" << nameB << "'" << std::endl;
                } else {
                    std::cout << "PLACE_HORIZONTAL: Not carrying anything" << std::endl;
                }

                obj->setActiveActionIndex(actionIdx + 1);
            }
        }
        else if (currentAction.type == ActionType::PLACE_ROOF) {
            // Three-phase: turn to face center → walk there → place carried item as roof on top
            // stringParam = "c1|c2|c3|c4" (pipe-delimited corner names)
            // Resolve corner positions and center at runtime
            std::string params = currentAction.stringParam;
            std::string cornerNames[4];
            glm::vec3 cornerPos[4] = {};
            {
                size_t start = 0;
                for (int i = 0; i < 4; i++) {
                    size_t pipePos = params.find('|', start);
                    if (pipePos != std::string::npos) {
                        cornerNames[i] = params.substr(start, pipePos - start);
                        start = pipePos + 1;
                    } else {
                        cornerNames[i] = params.substr(start);
                    }
                }
            }
            for (auto& o : m_sceneObjects) {
                if (!o) continue;
                for (int i = 0; i < 4; i++) {
                    if (o->getName() == cornerNames[i]) cornerPos[i] = o->getTransform().getPosition();
                }
            }
            glm::vec3 targetPos = (cornerPos[0] + cornerPos[1] + cornerPos[2] + cornerPos[3]) * 0.25f;
            targetPos.y = m_terrain.getHeightAt(targetPos.x, targetPos.z);
            glm::vec3 currentPos = obj->getTransform().getPosition();
            bool useGravity = currentAction.boolParam;
            float speed = currentAction.floatParam > 0.0f ? currentAction.floatParam : 2.0f;

            // Phase 1: Turn to face center
            if (!obj->isTurning() && !obj->isMovingTo()) {
                glm::vec3 dir = targetPos - currentPos;
                dir.y = 0.0f;
                if (glm::length(dir) > 0.01f) {
                    dir = glm::normalize(dir);
                    float targetYaw = glm::degrees(atan2(dir.x, dir.z));
                    float currentYaw = obj->getEulerRotation().y;
                    float deltaYaw = targetYaw - currentYaw;
                    while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                    while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                    if (std::abs(deltaYaw) > 1.0f) {
                        obj->startTurnTo(currentYaw, currentYaw + deltaYaw, 0.3f);
                        std::cout << "PLACE_ROOF: Turning to face center" << std::endl;
                        return;
                    }
                }
                float distance = glm::length(targetPos - currentPos);
                float duration = distance / speed;
                if (duration < 0.1f) duration = 0.1f;
                obj->startMoveTo(currentPos, targetPos, duration, true);
                if (obj->isSkinned()) {
                    m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                    obj->setCurrentAnimation("walk");
                }
                std::cout << "PLACE_ROOF: Walking to center" << std::endl;
            }

            if (obj->isTurning()) {
                obj->updateTurnTo(deltaTime);
                if (!obj->isTurning()) {
                    currentPos = obj->getTransform().getPosition();
                    float distance = glm::length(targetPos - currentPos);
                    float duration = distance / speed;
                    if (duration < 0.1f) duration = 0.1f;
                    obj->startMoveTo(currentPos, targetPos, duration, true);
                    if (obj->isSkinned()) {
                        m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                        obj->setCurrentAnimation("walk");
                    }
                    std::cout << "PLACE_ROOF: Walking to center" << std::endl;
                }
                return;
            }

            // Phase 2: Walk
            if (obj->isMovingTo()) {
                obj->updateMoveTo(deltaTime);
                if (useGravity) {
                    glm::vec3 pos = obj->getTransform().getPosition();
                    pos.y = m_terrain.getHeightAt(pos.x, pos.z);
                    obj->getTransform().setPosition(pos);
                }
            }

            // Phase 3: Arrived — place roof on top of frame
            if (!obj->isMovingTo() && !obj->isTurning()) {
                glm::vec3 finalPos = targetPos;
                finalPos.y = m_terrain.getHeightAt(finalPos.x, finalPos.z);
                obj->getTransform().setPosition(finalPos);

                if (obj->isCarrying()) {
                    SceneObject* carried = obj->getCarriedItemObject();
                    if (carried) {
                        // Re-resolve corner positions
                        for (int i = 0; i < 4; i++) cornerPos[i] = glm::vec3(0);
                        for (auto& o : m_sceneObjects) {
                            if (!o) continue;
                            for (int i = 0; i < 4; i++) {
                                if (o->getName() == cornerNames[i]) cornerPos[i] = o->getTransform().getPosition();
                            }
                        }
                        glm::vec3 center = (cornerPos[0] + cornerPos[1] + cornerPos[2] + cornerPos[3]) * 0.25f;

                        // Find top Y by scanning objects near corners
                        float topY = cornerPos[0].y;
                        for (auto& sceneObj : m_sceneObjects) {
                            if (!sceneObj || !sceneObj->isVisible()) continue;
                            glm::vec3 objPos = sceneObj->getTransform().getPosition();
                            for (int i = 0; i < 4; i++) {
                                float dist = glm::length(glm::vec2(objPos.x - cornerPos[i].x, objPos.z - cornerPos[i].z));
                                if (dist < 1.5f) {
                                    AABB bounds = sceneObj->getWorldBounds();
                                    topY = std::max(topY, bounds.max.y);
                                }
                            }
                        }

                        // Derive front direction from corner0→corner1 edge
                        glm::vec3 frontEdgeMid = (cornerPos[0] + cornerPos[1]) * 0.5f;
                        glm::vec3 frontDir = frontEdgeMid - center;
                        frontDir.y = 0.0f;
                        float frontYaw = 0.0f;
                        if (glm::length(frontDir) > 0.01f) {
                            frontDir = glm::normalize(frontDir);
                            frontYaw = glm::degrees(atan2(frontDir.x, frontDir.z));
                        }

                        // Position roof at center, on top of structure
                        glm::vec3 placePos = center;
                        placePos.y = topY;

                        // Offset toward front for asymmetric overhang
                        float frontOffset = 0.5f;
                        placePos += frontDir * frontOffset;

                        // Rotate 90° so the longer dimension runs front-to-back
                        carried->setEulerRotation(glm::vec3(0.0f, frontYaw + 90.0f, 0.0f));
                        carried->getTransform().setPosition(placePos);
                        carried->setVisible(true);

                        std::string carriedName = obj->getCarriedItemName();
                        obj->clearCarriedItem();
                        std::cout << "PLACE_ROOF: Placed '" << carriedName << "' at ("
                                  << placePos.x << ", " << placePos.y << ", " << placePos.z
                                  << ") frontYaw=" << frontYaw << std::endl;
                    }
                } else {
                    std::cout << "PLACE_ROOF: Not carrying anything" << std::endl;
                }

                obj->setActiveActionIndex(actionIdx + 1);
            }
        }
        else if (currentAction.type == ActionType::PLACE_WALL) {
            // Three-phase: turn to face midpoint → walk there → place carried item as wall panel
            // stringParam = "postA|postB" (pipe-delimited post names)
            // Resolve positions at runtime
            std::string params = currentAction.stringParam;
            size_t pipePos = params.find('|');
            std::string nameA = (pipePos != std::string::npos) ? params.substr(0, pipePos) : params;
            std::string nameB = (pipePos != std::string::npos) ? params.substr(pipePos + 1) : "";
            glm::vec3 posA(0), posB(0);
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == nameA) posA = o->getTransform().getPosition();
                if (o && o->getName() == nameB) posB = o->getTransform().getPosition();
            }
            glm::vec3 targetPos = (posA + posB) * 0.5f;
            targetPos.y = m_terrain.getHeightAt(targetPos.x, targetPos.z);
            glm::vec3 currentPos = obj->getTransform().getPosition();
            bool useGravity = currentAction.boolParam;
            float speed = currentAction.floatParam > 0.0f ? currentAction.floatParam : 2.0f;

            // Phase 1: Turn to face midpoint
            if (!obj->isTurning() && !obj->isMovingTo()) {
                glm::vec3 dir = targetPos - currentPos;
                dir.y = 0.0f;
                if (glm::length(dir) > 0.01f) {
                    dir = glm::normalize(dir);
                    float targetYaw = glm::degrees(atan2(dir.x, dir.z));
                    float currentYaw = obj->getEulerRotation().y;
                    float deltaYaw = targetYaw - currentYaw;
                    while (deltaYaw > 180.0f) deltaYaw -= 360.0f;
                    while (deltaYaw < -180.0f) deltaYaw += 360.0f;
                    if (std::abs(deltaYaw) > 1.0f) {
                        obj->startTurnTo(currentYaw, currentYaw + deltaYaw, 0.3f);
                        std::cout << "PLACE_WALL: Turning to face midpoint" << std::endl;
                        return;
                    }
                }
                float distance = glm::length(targetPos - currentPos);
                float duration = distance / speed;
                if (duration < 0.1f) duration = 0.1f;
                obj->startMoveTo(currentPos, targetPos, duration, true);
                if (obj->isSkinned()) {
                    m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                    obj->setCurrentAnimation("walk");
                }
                std::cout << "PLACE_WALL: Walking to midpoint" << std::endl;
            }

            if (obj->isTurning()) {
                obj->updateTurnTo(deltaTime);
                if (!obj->isTurning()) {
                    currentPos = obj->getTransform().getPosition();
                    float distance = glm::length(targetPos - currentPos);
                    float duration = distance / speed;
                    if (duration < 0.1f) duration = 0.1f;
                    obj->startMoveTo(currentPos, targetPos, duration, true);
                    if (obj->isSkinned()) {
                        m_skinnedModelRenderer->playAnimation(obj->getSkinnedModelHandle(), "walk", true);
                        obj->setCurrentAnimation("walk");
                    }
                    std::cout << "PLACE_WALL: Walking to midpoint" << std::endl;
                }
                return;
            }

            // Phase 2: Walk
            if (obj->isMovingTo()) {
                obj->updateMoveTo(deltaTime);
                if (useGravity) {
                    glm::vec3 pos = obj->getTransform().getPosition();
                    pos.y = m_terrain.getHeightAt(pos.x, pos.z);
                    obj->getTransform().setPosition(pos);
                }
            }

            // Phase 3: Arrived — place wall panel between the two posts
            if (!obj->isMovingTo() && !obj->isTurning()) {
                glm::vec3 finalPos = targetPos;
                finalPos.y = m_terrain.getHeightAt(finalPos.x, finalPos.z);
                obj->getTransform().setPosition(finalPos);

                if (obj->isCarrying()) {
                    SceneObject* carried = obj->getCarriedItemObject();
                    if (carried) {
                        // Re-resolve post positions at placement time
                        posA = glm::vec3(0); posB = glm::vec3(0);
                        for (auto& o : m_sceneObjects) {
                            if (o && o->getName() == nameA) posA = o->getTransform().getPosition();
                            if (o && o->getName() == nameB) posB = o->getTransform().getPosition();
                        }

                        // Position at midpoint of the edge
                        glm::vec3 placePos = (posA + posB) * 0.5f;

                        // Terrain height + bottom offset
                        float terrainY = m_terrain.getHeightAt(placePos.x, placePos.z);
                        glm::vec3 scale = carried->getTransform().getScale();
                        float minVertexY = 0.0f;
                        if (carried->hasMeshData()) {
                            const auto& verts = carried->getVertices();
                            for (const auto& v : verts) {
                                if (v.position.y < minVertexY) minVertexY = v.position.y;
                            }
                        }
                        float bottomOffset = -minVertexY * scale.y;
                        placePos.y = terrainY + bottomOffset;

                        // Compute outward normal (left perpendicular for clockwise winding)
                        glm::vec3 edgeDir = posB - posA;
                        edgeDir.y = 0.0f;
                        if (glm::length(edgeDir) > 0.01f) {
                            edgeDir = glm::normalize(edgeDir);
                        }
                        glm::vec3 outward(edgeDir.z, 0.0f, -edgeDir.x);
                        float wallYaw = glm::degrees(atan2(outward.x, outward.z));

                        carried->setEulerRotation(glm::vec3(0.0f, wallYaw, 0.0f));
                        carried->getTransform().setPosition(placePos);
                        carried->setVisible(true);

                        std::string carriedName = obj->getCarriedItemName();
                        obj->clearCarriedItem();
                        std::cout << "PLACE_WALL: Placed '" << carriedName << "' between '"
                                  << nameA << "' and '" << nameB << "' yaw=" << wallYaw << std::endl;
                    }
                } else {
                    std::cout << "PLACE_WALL: Not carrying anything" << std::endl;
                }

                obj->setActiveActionIndex(actionIdx + 1);
            }
        }
        else if (currentAction.type == ActionType::GROVE_COMMAND) {
            // Parse and execute a construction command from stringParam
            // Format: "type|name|param1|param2|..."
            executeGroveCommand(currentAction.stringParam, currentAction.vec3Param);
            obj->setActiveActionIndex(actionIdx + 1);
        }
        else if (currentAction.type == ActionType::DESTROY_SELF) {
            // Mark for destruction (will be cleaned up after behavior loop)
            obj->setVisible(false);  // Hide immediately
            obj->clearActiveBehavior();
            m_objectsToDestroy.push_back(obj);
            std::cout << "DESTROY_SELF: " << obj->getName() << std::endl;
        }
        else {
            // Unknown action type, skip
            obj->setActiveActionIndex(actionIdx + 1);
        }
    }

    void processPendingSpawns() {
        if (m_pendingSpawns.empty()) return;

        for (const auto& spawn : m_pendingSpawns) {
            auto result = GLBLoader::load(spawn.modelPath);
            if (!result.success || result.meshes.empty()) {
                std::cerr << "SPAWN_ENTITY: Failed to load model: " << spawn.modelPath << std::endl;
                continue;
            }

            const auto& mesh = result.meshes[0];
            auto obj = GLBLoader::createSceneObject(mesh, *m_modelRenderer);
            if (!obj) {
                std::cerr << "SPAWN_ENTITY: Failed to create scene object" << std::endl;
                continue;
            }

            obj->setModelPath(spawn.modelPath);
            obj->getTransform().setPosition(spawn.position);
            obj->setEulerRotation(spawn.rotation);
            obj->getTransform().setScale(spawn.scale);

            std::cout << "Spawned: " << obj->getName() << " at "
                      << spawn.position.x << ", " << spawn.position.y << ", " << spawn.position.z << std::endl;

            m_sceneObjects.push_back(std::move(obj));
        }

        m_pendingSpawns.clear();
    }

    void flushGroveSpawns() {
        if (m_pendingGroveSpawns.empty()) return;
        std::cout << "[Grove] Flushing " << m_pendingGroveSpawns.size() << " spawns into scene (total was " << m_sceneObjects.size() << ")" << std::endl;
        for (auto& obj : m_pendingGroveSpawns) {
            std::cout << "[Grove] Adding '" << obj->getName() << "' handle=" << obj->getBufferHandle() << std::endl;
            m_sceneObjects.push_back(std::move(obj));
        }
        m_pendingGroveSpawns.clear();
        std::cout << "[Grove] Scene now has " << m_sceneObjects.size() << " objects" << std::endl;
    }

    void processPendingDestroys() {
        if (m_objectsToDestroy.empty()) return;

        for (SceneObject* obj : m_objectsToDestroy) {
            // Find and remove from m_sceneObjects
            auto it = std::find_if(m_sceneObjects.begin(), m_sceneObjects.end(),
                [obj](const std::unique_ptr<SceneObject>& ptr) { return ptr.get() == obj; });

            if (it != m_sceneObjects.end()) {
                // Clear selection if this was selected
                int idx = static_cast<int>(std::distance(m_sceneObjects.begin(), it));
                if (m_selectedObjectIndex == idx) {
                    m_selectedObjectIndex = -1;
                } else if (m_selectedObjectIndex > idx) {
                    m_selectedObjectIndex--;
                }

                // Remove from followers if destroyed
                m_aiFollowers.erase(
                    std::remove_if(m_aiFollowers.begin(), m_aiFollowers.end(),
                        [obj](const AIFollowState& fs) { return fs.npc == obj; }),
                    m_aiFollowers.end());

                std::cout << "Destroyed: " << (*it)->getName() << std::endl;
                m_sceneObjects.erase(it);
            }
        }

        m_objectsToDestroy.clear();
    }

    void updatePathPatrol(SceneObject* obj, float deltaTime) {
        if (!obj || !obj->hasPathWaypoints()) return;

        glm::vec3 currentPos = obj->getTransform().getPosition();
        glm::vec3 targetPos = obj->getCurrentWaypointPosition();

        // Keep Y at terrain level
        targetPos.y = currentPos.y;

        glm::vec3 toTarget = targetPos - currentPos;
        toTarget.y = 0;
        float distance = glm::length(toTarget);

        const float arrivalThreshold = 1.5f;

        if (distance < arrivalThreshold) {
            // Arrived at waypoint, advance to next
            obj->advanceWaypoint();
        } else {
            // Move toward waypoint
            glm::vec3 direction = glm::normalize(toTarget);
            float moveDistance = obj->getPatrolSpeed() * deltaTime;

            if (moveDistance > distance) {
                moveDistance = distance;
            }

            glm::vec3 newPos = currentPos + direction * moveDistance;

            // Follow terrain height with model offset
            float terrainHeight = m_terrain.getHeightAt(newPos.x, newPos.z);
            const AABB& bounds = obj->getLocalBounds();
            float scaleY = obj->getTransform().getScale().y;
            float modelBottomOffset = -bounds.min.y * scaleY;
            newPos.y = terrainHeight + modelBottomOffset;

            obj->getTransform().setPosition(newPos);

            // Smoothly rotate to face movement direction
            float targetYaw = glm::degrees(atan2(direction.x, direction.z));
            glm::vec3 euler = obj->getEulerRotation();
            float currentYaw = euler.y;

            float diff = targetYaw - currentYaw;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;

            float turnSpeed = 180.0f;
            float maxTurn = turnSpeed * deltaTime;

            if (std::abs(diff) <= maxTurn) {
                euler.y = targetYaw;
            } else {
                euler.y += (diff > 0 ? maxTurn : -maxTurn);
            }

            while (euler.y > 180.0f) euler.y -= 360.0f;
            while (euler.y < -180.0f) euler.y += 360.0f;

            obj->setEulerRotation(euler);
        }
    }

    void updateEditorMode(float deltaTime) {
        // Terrain brush/deform tools — only active when terrain tools checkbox is on
        if (m_editorUI.isTerrainToolsEnabled()) {
            m_brushTool->setMode(m_editorUI.getBrushMode());
            m_brushTool->setRadius(m_editorUI.getBrushRadius());
            m_brushTool->setStrength(m_editorUI.getBrushStrength());
            m_brushTool->setFalloff(m_editorUI.getBrushFalloff());
            m_brushTool->setPaintColor(m_editorUI.getPaintColor());
            m_brushTool->setTextureIndex(m_editorUI.getSelectedTexture());
            m_brushTool->setTextureHSB(
                m_editorUI.getSelectedTexHue(),
                m_editorUI.getSelectedTexSaturation(),
                m_editorUI.getSelectedTexBrightness()
            );
            m_brushTool->setTargetElevation(m_editorUI.getPathElevation());

            glm::vec2 mousePos = Input::getMousePosition();
            float normalizedX = mousePos.x / getWindow().getWidth();
            float normalizedY = mousePos.y / getWindow().getHeight();
            float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();

            m_brushTool->updatePreview(normalizedX, normalizedY, aspect);

            // Update brush shape from UI
            m_brushTool->setShape(m_editorUI.getBrushShape());
            m_brushTool->setShapeAspectRatio(m_editorUI.getBrushShapeAspectRatio());
            m_brushTool->setShapeRotation(m_editorUI.getBrushShapeRotation());

            // Update brush ring visualization
            if (m_brushTool->hasValidPosition() && !m_isSpaceLevel && m_editorUI.getShowBrushRing()) {
                m_brushRing->update(m_brushTool->getPosition(), m_editorUI.getBrushRadius(),
                                   m_terrain, m_brushTool->getShapeParams());
                m_brushRing->setVisible(true);
            } else {
                m_brushRing->setVisible(false);
            }

            // Update triangulation mode
            static int lastTriMode = -1;
            int triMode = m_editorUI.getTriangulationMode();
            if (triMode != lastTriMode) {
                m_terrain.setTriangulationMode(static_cast<TriangulationMode>(triMode));
                m_chunkManager->updateModifiedChunks(m_terrain);
                lastTriMode = triMode;
            }

            bool leftMouseDown = Input::isMouseButtonDown(Input::MOUSE_LEFT) && !ImGui::GetIO().WantCaptureMouse;

            if (m_editorUI.getBrushMode() == BrushMode::PathMode) {
                m_pathTool->updatePreview(normalizedX, normalizedY, aspect);

                if (leftMouseDown && !m_wasLeftMouseDown) {
                    if (m_pathTool->hasValidPreviewPos()) {
                        m_pathTool->addPoint(m_pathTool->getPreviewPos());
                    }
                }

                auto samples = m_pathTool->sampleSpline(16);
                m_splineRenderer->update(m_pathTool->getControlPoints(), samples, m_terrain);
                m_splineRenderer->setVisible(true);

                m_editorUI.setPathPointCount(m_pathTool->getPointCount());

                m_wasGrabbing = false;
            } else if (m_editorUI.getBrushMode() == BrushMode::MoveObject) {
                m_wasGrabbing = false;
                m_splineRenderer->setVisible(false);
            } else if (m_editorUI.getBrushMode() == BrushMode::Grab) {
                if (leftMouseDown && !m_wasGrabbing) {
                    m_brushTool->beginGrab();
                    m_lastGrabMouseY = mousePos.y;
                    m_wasGrabbing = true;
                } else if (leftMouseDown && m_wasGrabbing) {
                    float deltaY = (m_lastGrabMouseY - mousePos.y) * 0.5f;
                    m_brushTool->updateGrab(deltaY);
                    m_lastGrabMouseY = mousePos.y;
                    m_chunkManager->updateModifiedChunks(m_terrain);
                } else if (!leftMouseDown && m_wasGrabbing) {
                    m_brushTool->endGrab();
                    m_wasGrabbing = false;
                }
                m_splineRenderer->setVisible(false);
            } else {
                if (leftMouseDown) {
                    m_brushTool->apply(deltaTime);
                    m_chunkManager->updateModifiedChunks(m_terrain);
                }
                m_wasGrabbing = false;
                m_splineRenderer->setVisible(false);
            }

            m_wasLeftMouseDown = leftMouseDown;
        } else {
            // Terrain tools off — hide brush ring and spline
            m_brushRing->setVisible(false);
            m_splineRenderer->setVisible(false);

            // Raycast from mouse to terrain for zone info + zone painting
            if (!ImGui::GetIO().WantCaptureMouse) {
                glm::vec2 mpos = Input::getMousePosition();
                float nx = mpos.x / getWindow().getWidth();
                float ny = mpos.y / getWindow().getHeight();
                float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();
                glm::vec3 rayOrig = m_camera.getPosition();
                glm::vec3 rayDir = m_camera.screenToWorldRay(nx, ny, aspect);

                float t = 0.0f;
                bool hit = false;
                glm::vec3 hitPos;
                for (int step = 0; step < 500; step++) {
                    t += 2.0f;
                    glm::vec3 p = rayOrig + rayDir * t;
                    float h = m_terrain.getHeightAt(p.x, p.z);
                    if (p.y <= h) {
                        hitPos = p;
                        hitPos.y = h;
                        hit = true;
                        break;
                    }
                }

                if (hit) {
                    // Update brush position so zone panel shows info at cursor
                    m_editorUI.setBrushPosition(hitPos, true);

                    // Zone painting on left-click
                    if (m_editorUI.isZonePaintMode() && m_zoneSystem &&
                        Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
                        auto g = m_zoneSystem->worldToGrid(hitPos.x, hitPos.z);
                        int paintType = m_editorUI.getZonePaintType();
                        if (paintType == 6 && m_editorUI.getZonePaintResource() > 0) {
                            m_zoneSystem->setResource(g.x, g.y,
                                static_cast<ResourceType>(m_editorUI.getZonePaintResource()),
                                m_editorUI.getZonePaintDensity());
                        } else {
                            m_zoneSystem->setZoneType(g.x, g.y, static_cast<ZoneType>(paintType));
                        }
                    }
                }
            }
        }

        // === Wall Draw / Foundation Tool (preview drawn in recordCommandBuffer after ImGui::NewFrame) ===
        bool inBuildTool = (m_editorUI.getBrushMode() == BrushMode::WallDraw ||
                            m_editorUI.getBrushMode() == BrushMode::Foundation);
        if (inBuildTool && !ImGui::GetIO().WantCaptureMouse) {
            glm::vec2 mpos = Input::getMousePosition();
            float nx = mpos.x / getWindow().getWidth();
            float ny = mpos.y / getWindow().getHeight();
            float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();
            glm::vec3 rayOrig = m_camera.getPosition();
            glm::vec3 rayDir = m_camera.screenToWorldRay(nx, ny, aspect);

            float t = 0.0f;
            bool hit = false;
            glm::vec3 hitPos;
            for (int step = 0; step < 500; step++) {
                t += 2.0f;
                glm::vec3 p = rayOrig + rayDir * t;
                float h = m_terrain.getHeightAt(p.x, p.z);
                if (p.y <= h) {
                    hitPos = p;
                    hitPos.y = h;
                    hit = true;
                    break;
                }
            }

            if (hit) {
                // Snap hit position to 1m grid for clean block alignment
                glm::vec3 snapped = hitPos;
                snapped.x = std::round(hitPos.x);
                snapped.z = std::round(hitPos.z);

                if (Input::isMouseButtonPressed(Input::MOUSE_LEFT)) {
                    m_wallCorner1 = snapped;
                    m_wallCorner1.y = hitPos.y;  // keep raw Y for floor height
                    m_wallCorner2 = m_wallCorner1;
                    m_wallDrawing = true;
                }
                if (m_wallDrawing) {
                    m_wallCorner2 = snapped;
                    m_wallCorner2.y = hitPos.y;
                }
            }

            if (m_wallDrawing && !Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
                m_wallDrawing = false;
                float dx = std::abs(m_wallCorner2.x - m_wallCorner1.x);
                float dz = std::abs(m_wallCorner2.z - m_wallCorner1.z);
                if (dx > 0.5f && dz > 0.5f) {
                    float floorY = std::min(m_wallCorner1.y, m_wallCorner2.y);
                    glm::vec2 c1(m_wallCorner1.x, m_wallCorner1.z);
                    glm::vec2 c2(m_wallCorner2.x, m_wallCorner2.z);

                    // Helper to create a SceneObject from MeshData
                    auto createBuildingObj = [&](const std::string& objName, const PrimitiveMeshBuilder::MeshData& md) {
                        auto obj = std::make_unique<SceneObject>(objName);
                        uint32_t handle = m_modelRenderer->createModel(md.vertices, md.indices);
                        obj->setBufferHandle(handle);
                        obj->setIndexCount(static_cast<uint32_t>(md.indices.size()));
                        obj->setVertexCount(static_cast<uint32_t>(md.vertices.size()));
                        obj->setLocalBounds(md.bounds);
                        obj->setModelPath("");
                        obj->setMeshData(md.vertices, md.indices);
                        m_sceneObjects.push_back(std::move(obj));
                    };

                    if (m_editorUI.getBrushMode() == BrushMode::WallDraw) {
                        glm::vec4 wallColor(0.75f, 0.72f, 0.68f, 1.0f);
                        float wallH = m_editorUI.getWallHeight();

                        std::string prefix = "Building_" + std::to_string(m_buildingCounter++);

                        // Track all object indices for auto-grouping
                        std::set<int> groupIndices;

                        // Pre-build the shared cube mesh once (all blocks are 1x1x1)
                        auto cubeMesh = PrimitiveMeshBuilder::createCube(1.0f, wallColor);

                        // Helper to spawn a 1x1x1 block at a position
                        auto spawnBlock = [&](const std::string& name, glm::vec3 pos) {
                            auto obj = std::make_unique<SceneObject>(name);
                            uint32_t handle = m_modelRenderer->createModel(cubeMesh.vertices, cubeMesh.indices);
                            obj->setBufferHandle(handle);
                            obj->setIndexCount(static_cast<uint32_t>(cubeMesh.indices.size()));
                            obj->setVertexCount(static_cast<uint32_t>(cubeMesh.vertices.size()));
                            obj->setLocalBounds(cubeMesh.bounds);
                            obj->setModelPath("");
                            obj->setMeshData(cubeMesh.vertices, cubeMesh.indices);
                            obj->setPrimitiveType(PrimitiveType::Cube);
                            obj->setPrimitiveSize(1.0f);
                            obj->setPrimitiveColor(wallColor);
                            obj->getTransform().setPosition(pos);
                            int idx = static_cast<int>(m_sceneObjects.size());
                            groupIndices.insert(idx);
                            m_sceneObjects.push_back(std::move(obj));
                        };

                        // Corners are already snapped to 1m grid
                        float x1s = std::min(c1.x, c2.x);
                        float x2s = std::max(c1.x, c2.x);
                        float z1s = std::min(c1.y, c2.y);
                        float z2s = std::max(c1.y, c2.y);
                        float snappedFloorY = std::round(floorY);

                        int countX = std::max(1, static_cast<int>(std::round(x2s - x1s)));
                        int countZ = std::max(1, static_cast<int>(std::round(z2s - z1s)));
                        int countH = std::max(1, static_cast<int>(std::round(wallH)));

                        int blockNum = 0;

                        // Walls: place 1x1x1 blocks along perimeter, stacked vertically
                        for (int row = 0; row < countH; row++) {
                            float cy = snappedFloorY + row;

                            // North wall (z = z1s)
                            for (int i = 0; i < countX; i++) {
                                spawnBlock(prefix + "_Block_" + std::to_string(blockNum++),
                                           glm::vec3(x1s + i + 0.5f, cy, z1s + 0.5f));
                            }
                            // South wall (z = z2s)
                            for (int i = 0; i < countX; i++) {
                                spawnBlock(prefix + "_Block_" + std::to_string(blockNum++),
                                           glm::vec3(x1s + i + 0.5f, cy, z2s - 0.5f));
                            }
                            // West wall (x = x1s), skip corners
                            for (int i = 1; i < countZ - 1; i++) {
                                spawnBlock(prefix + "_Block_" + std::to_string(blockNum++),
                                           glm::vec3(x1s + 0.5f, cy, z1s + i + 0.5f));
                            }
                            // East wall (x = x2s), skip corners
                            for (int i = 1; i < countZ - 1; i++) {
                                spawnBlock(prefix + "_Block_" + std::to_string(blockNum++),
                                           glm::vec3(x2s - 0.5f, cy, z1s + i + 0.5f));
                            }
                        }

                        // Floor: 1x1x1 blocks spanning full rectangle
                        for (int ix = 0; ix < countX; ix++) {
                            for (int iz = 0; iz < countZ; iz++) {
                                spawnBlock(prefix + "_Floor_" + std::to_string(blockNum++),
                                           glm::vec3(x1s + ix + 0.5f, snappedFloorY - 1.0f, z1s + iz + 0.5f));
                            }
                        }

                        // Ceiling: 1x1x1 blocks spanning full rectangle
                        float ceilingY = snappedFloorY + countH;
                        for (int ix = 0; ix < countX; ix++) {
                            for (int iz = 0; iz < countZ; iz++) {
                                spawnBlock(prefix + "_Ceil_" + std::to_string(blockNum++),
                                           glm::vec3(x1s + ix + 0.5f, ceilingY, z1s + iz + 0.5f));
                            }
                        }

                        // Auto-group all blocks
                        EditorUI::ObjectGroup group;
                        group.name = prefix;
                        group.objectIndices = groupIndices;
                        group.expanded = true;
                        m_objectGroups.push_back(group);
                        m_editorUI.setObjectGroups(m_objectGroups);
                    } else {
                        auto meshData = PrimitiveMeshBuilder::createFoundation(
                            c1, c2, floorY, m_editorUI.getFoundationHeight(),
                            glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));
                        createBuildingObj("Foundation_" + std::to_string(m_sceneObjects.size()), meshData);
                    }

                    m_selectedObjectIndex = static_cast<int>(m_sceneObjects.size()) - 1;
                }
            }
        } else if (!Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
            m_wallDrawing = false;
        }

        bool inMoveObjectMode = m_editorUI.getBrushMode() == BrushMode::MoveObject;
        bool inTransformMode = inMoveObjectMode && m_transformMode != TransformMode::Select;
        bool hasSelection = m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size());

        // Ray-axis distance helper (same algorithm as model editor)
        auto rayAxisDist = [](const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                              const glm::vec3& axisOrigin, const glm::vec3& axisDir, float axisLen) -> float {
            glm::vec3 w0 = rayOrigin - axisOrigin;
            float a = glm::dot(rayDir, rayDir);
            float b2 = glm::dot(rayDir, axisDir);
            float c = glm::dot(axisDir, axisDir);
            float d = glm::dot(rayDir, w0);
            float e = glm::dot(axisDir, w0);
            float denom = a * c - b2 * b2;
            if (std::abs(denom) < 0.0001f) return FLT_MAX;
            float t = (b2 * e - c * d) / denom;
            float s = (a * e - b2 * d) / denom;
            s = std::clamp(s, 0.0f, axisLen);
            return glm::length((rayOrigin + rayDir * t) - (axisOrigin + axisDir * s));
        };

        // Pick gizmo axis from ray (matching model editor)
        auto pickAxis = [&](const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                            const glm::vec3& gizmoPos, float size) -> GizmoAxis {
            float threshold = 0.15f * size;

            if (m_transformMode == TransformMode::Rotate) {
                float circleRadius = size * 0.9f;
                float ringThreshold = threshold * 1.5f;
                auto checkCircle = [&](const glm::vec3& normal) -> float {
                    float denom = glm::dot(rayDir, normal);
                    if (std::abs(denom) < 0.0001f) return 999.0f;
                    float t = glm::dot(gizmoPos - rayOrigin, normal) / denom;
                    if (t < 0) return 999.0f;
                    glm::vec3 hitPoint = rayOrigin + rayDir * t;
                    return std::abs(glm::length(hitPoint - gizmoPos) - circleRadius);
                };
                float dX = checkCircle(glm::vec3(1,0,0));
                float dY = checkCircle(glm::vec3(0,1,0));
                float dZ = checkCircle(glm::vec3(0,0,1));
                float minD = std::min({dX, dY, dZ});
                if (minD > ringThreshold) return GizmoAxis::None;
                if (minD == dX) return GizmoAxis::X;
                if (minD == dY) return GizmoAxis::Y;
                return GizmoAxis::Z;
            }

            // Move/Scale: pick axis lines (positive and negative directions)
            float dX = std::min(rayAxisDist(rayOrigin, rayDir, gizmoPos, glm::vec3(1,0,0), size),
                                rayAxisDist(rayOrigin, rayDir, gizmoPos, glm::vec3(-1,0,0), size));
            float dY = std::min(rayAxisDist(rayOrigin, rayDir, gizmoPos, glm::vec3(0,1,0), size),
                                rayAxisDist(rayOrigin, rayDir, gizmoPos, glm::vec3(0,-1,0), size));
            float dZ = std::min(rayAxisDist(rayOrigin, rayDir, gizmoPos, glm::vec3(0,0,1), size),
                                rayAxisDist(rayOrigin, rayDir, gizmoPos, glm::vec3(0,0,-1), size));
            float minD = std::min({dX, dY, dZ});
            if (minD > threshold) return GizmoAxis::None;
            if (minD == dX) return GizmoAxis::X;
            if (minD == dY) return GizmoAxis::Y;
            return GizmoAxis::Z;
        };

        if (inMoveObjectMode && inTransformMode && hasSelection) {
            SceneObject* selected = m_sceneObjects[m_selectedObjectIndex].get();
            if (selected) {
                AABB wb2 = selected->getWorldBounds();
                glm::vec3 gizmoPos((wb2.min.x + wb2.max.x) * 0.5f, wb2.max.y, (wb2.min.z + wb2.max.z) * 0.5f);
                float dist = glm::length(m_camera.getPosition() - gizmoPos);
                float gizmoSize = dist * 0.08f;

                float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();
                glm::vec2 mousePos = Input::getMousePosition();
                float normalizedX = (mousePos.x / getWindow().getWidth()) * 2.0f - 1.0f;
                float normalizedY = 1.0f - (mousePos.y / getWindow().getHeight()) * 2.0f;

                glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 5000.0f);
                glm::mat4 view = m_camera.getViewMatrix();
                glm::mat4 invVP = glm::inverse(proj * view);

                glm::vec4 nearPoint = invVP * glm::vec4(normalizedX, normalizedY, -1.0f, 1.0f);
                glm::vec4 farPoint = invVP * glm::vec4(normalizedX, normalizedY, 1.0f, 1.0f);
                nearPoint /= nearPoint.w;
                farPoint /= farPoint.w;

                glm::vec3 rayOrigin = glm::vec3(nearPoint);
                glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

                // Update hover every frame for visual feedback
                if (!m_gizmoDragging) {
                    m_gizmoHoveredAxis = pickAxis(rayOrigin, rayDir, gizmoPos, gizmoSize);
                }

                bool leftMousePressed = Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !ImGui::GetIO().WantCaptureMouse;

                if (leftMousePressed && !m_gizmoDragging) {
                    GizmoAxis picked = pickAxis(rayOrigin, rayDir, gizmoPos, gizmoSize);
                    if (picked != GizmoAxis::None) {
                        m_gizmoDragging = true;
                        m_gizmoActiveAxis = picked;
                        m_lastMousePos = mousePos;
                        m_gizmoDragRawPos = selected->getTransform().getPosition();
                        m_gizmoDragRawEuler = selected->getEulerRotation();
                    } else {
                        if (Input::isKeyDown(Input::KEY_LEFT_ALT)) {
                            pickFaceAtMouse();
                        } else {
                            pickObjectAtMouse();
                        }
                    }
                } else if (m_gizmoDragging && Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
                    glm::vec2 mouseDelta = mousePos - m_lastMousePos;
                    m_lastMousePos = mousePos;

                    if (m_transformMode == TransformMode::Move) {
                        // Project mouse delta onto the active axis in screen space
                        glm::vec3 axisDir(0);
                        if (m_gizmoActiveAxis == GizmoAxis::X) axisDir = glm::vec3(1,0,0);
                        else if (m_gizmoActiveAxis == GizmoAxis::Y) axisDir = glm::vec3(0,1,0);
                        else if (m_gizmoActiveAxis == GizmoAxis::Z) axisDir = glm::vec3(0,0,1);

                        // Project axis to screen space to get movement direction
                        glm::mat4 vpMat = proj * view;
                        glm::vec4 screenPos = vpMat * glm::vec4(gizmoPos, 1.0f);
                        glm::vec4 screenEnd = vpMat * glm::vec4(gizmoPos + axisDir, 1.0f);
                        glm::vec2 screenDir = glm::vec2(screenEnd.x/screenEnd.w - screenPos.x/screenPos.w,
                                                         screenEnd.y/screenEnd.w - screenPos.y/screenPos.w);
                        float screenLen = glm::length(screenDir);
                        if (screenLen > 0.0001f) {
                            screenDir /= screenLen;
                            // Map mouse pixel delta to normalized coords
                            glm::vec2 normalizedDelta(mouseDelta.x / getWindow().getWidth() * 2.0f,
                                                       -mouseDelta.y / getWindow().getHeight() * 2.0f);
                            float axisDelta = glm::dot(normalizedDelta, screenDir) / screenLen;
                            m_gizmoDragRawPos += axisDir * axisDelta;
                            glm::vec3 newPos = m_gizmoDragRawPos;
                            if (m_editorUI.getSnapMove()) {
                                float snap = m_editorUI.getSnapMoveSize();
                                if (m_gizmoActiveAxis == GizmoAxis::X) newPos.x = std::round(newPos.x / snap) * snap;
                                else if (m_gizmoActiveAxis == GizmoAxis::Y) newPos.y = std::round(newPos.y / snap) * snap;
                                else if (m_gizmoActiveAxis == GizmoAxis::Z) newPos.z = std::round(newPos.z / snap) * snap;
                            }
                            // Snap to nearby object edges (AABB face matching)
                            if (m_editorUI.getSnapToObject()) {
                                // Temporarily set position so getWorldBounds() is up to date
                                glm::vec3 prevPos = selected->getTransform().getPosition();
                                selected->getTransform().setPosition(newPos);
                                AABB selBounds = selected->getWorldBounds();
                                float threshold = m_editorUI.getSnapToObjectDist();
                                float bestDist = threshold;
                                float snapOffset = 0.0f;
                                // Check each face of selected against opposite faces of other objects
                                for (size_t oi = 0; oi < m_sceneObjects.size(); oi++) {
                                    if (static_cast<int>(oi) == m_selectedObjectIndex) continue;
                                    SceneObject* other = m_sceneObjects[oi].get();
                                    if (!other || !other->isVisible()) continue;
                                    AABB ob = other->getWorldBounds();
                                    if (m_gizmoActiveAxis == GizmoAxis::X) {
                                        // selected +X face vs other -X face
                                        float d1 = std::abs(selBounds.max.x - ob.min.x);
                                        if (d1 < bestDist) { bestDist = d1; snapOffset = ob.min.x - selBounds.max.x + SNAP_OVERLAP; }
                                        // selected -X face vs other +X face
                                        float d2 = std::abs(selBounds.min.x - ob.max.x);
                                        if (d2 < bestDist) { bestDist = d2; snapOffset = ob.max.x - selBounds.min.x - SNAP_OVERLAP; }
                                    } else if (m_gizmoActiveAxis == GizmoAxis::Y) {
                                        float d1 = std::abs(selBounds.max.y - ob.min.y);
                                        if (d1 < bestDist) { bestDist = d1; snapOffset = ob.min.y - selBounds.max.y + SNAP_OVERLAP; }
                                        float d2 = std::abs(selBounds.min.y - ob.max.y);
                                        if (d2 < bestDist) { bestDist = d2; snapOffset = ob.max.y - selBounds.min.y - SNAP_OVERLAP; }
                                    } else if (m_gizmoActiveAxis == GizmoAxis::Z) {
                                        float d1 = std::abs(selBounds.max.z - ob.min.z);
                                        if (d1 < bestDist) { bestDist = d1; snapOffset = ob.min.z - selBounds.max.z + SNAP_OVERLAP; }
                                        float d2 = std::abs(selBounds.min.z - ob.max.z);
                                        if (d2 < bestDist) { bestDist = d2; snapOffset = ob.max.z - selBounds.min.z - SNAP_OVERLAP; }
                                    }
                                }
                                if (bestDist < threshold) {
                                    if (m_gizmoActiveAxis == GizmoAxis::X) newPos.x += snapOffset;
                                    else if (m_gizmoActiveAxis == GizmoAxis::Y) newPos.y += snapOffset;
                                    else if (m_gizmoActiveAxis == GizmoAxis::Z) newPos.z += snapOffset;
                                }
                                // Restore position (will be set properly below)
                                selected->getTransform().setPosition(prevPos);
                            }
                            glm::vec3 oldPos = selected->getTransform().getPosition();
                            glm::vec3 moveDelta = newPos - oldPos;
                            selected->getTransform().setPosition(newPos);
                            // Move all other selected objects by the same delta
                            if (m_selectedObjectIndices.size() > 1) {
                                for (int idx : m_selectedObjectIndices) {
                                    if (idx == m_selectedObjectIndex) continue;
                                    if (idx >= 0 && idx < static_cast<int>(m_sceneObjects.size()) && m_sceneObjects[idx]) {
                                        glm::vec3 p = m_sceneObjects[idx]->getTransform().getPosition();
                                        m_sceneObjects[idx]->getTransform().setPosition(p + moveDelta);
                                    }
                                }
                            }
                            // Keep orbit target at object center (avoids drift from snap jumps)
                            AABB wb = selected->getWorldBounds();
                            m_orbitTarget = (wb.min + wb.max) * 0.5f;
                        }
                    } else if (m_transformMode == TransformMode::Rotate) {
                        float angleDelta = mouseDelta.x * 0.5f;
                        if (m_gizmoActiveAxis == GizmoAxis::X) m_gizmoDragRawEuler.x += angleDelta;
                        else if (m_gizmoActiveAxis == GizmoAxis::Y) m_gizmoDragRawEuler.y += angleDelta;
                        else if (m_gizmoActiveAxis == GizmoAxis::Z) m_gizmoDragRawEuler.z += angleDelta;
                        glm::vec3 euler = m_gizmoDragRawEuler;
                        if (m_editorUI.getSnapRotate()) {
                            float snap = m_editorUI.getSnapRotateAngle();
                            if (m_gizmoActiveAxis == GizmoAxis::X) euler.x = std::round(euler.x / snap) * snap;
                            else if (m_gizmoActiveAxis == GizmoAxis::Y) euler.y = std::round(euler.y / snap) * snap;
                            else if (m_gizmoActiveAxis == GizmoAxis::Z) euler.z = std::round(euler.z / snap) * snap;
                        }
                        selected->setEulerRotation(euler);
                    } else if (m_transformMode == TransformMode::Scale) {
                        float scaleFactor = 1.0f + mouseDelta.x * 0.005f;
                        if (scaleFactor < 0.01f) scaleFactor = 0.01f;
                        glm::vec3 s = selected->getTransform().getScale();
                        if (m_gizmoActiveAxis == GizmoAxis::X) s.x *= scaleFactor;
                        else if (m_gizmoActiveAxis == GizmoAxis::Y) s.y *= scaleFactor;
                        else if (m_gizmoActiveAxis == GizmoAxis::Z) s.z *= scaleFactor;
                        selected->getTransform().setScale(s);
                    }
                } else if (m_gizmoDragging && !Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
                    m_gizmoDragging = false;
                    m_gizmoActiveAxis = GizmoAxis::None;
                }
            }
        } else if (inMoveObjectMode) {
            // Select mode or no selection: allow picking
            m_gizmoDragging = false;
            m_gizmoHoveredAxis = GizmoAxis::None;
            m_gizmoActiveAxis = GizmoAxis::None;

            bool leftMousePressed = Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !ImGui::GetIO().WantCaptureMouse;
            if (leftMousePressed) {
                if (Input::isKeyDown(Input::KEY_LEFT_ALT)) {
                    pickFaceAtMouse();
                } else {
                    pickObjectAtMouse();
                }
            }
        } else {
            m_gizmoDragging = false;
            m_gizmoHoveredAxis = GizmoAxis::None;
            m_gizmoActiveAxis = GizmoAxis::None;
        }

        m_editorUI.setFPS(m_fps);
        m_editorUI.setCameraPosition(m_camera.getPosition());
        m_editorUI.setCameraSpeed(m_cameraSpeed);
        m_editorUI.setMovementMode(m_camera.getMovementMode());
        m_editorUI.setOnGround(m_camera.isOnGround());

        updateSceneObjectsList();
        m_editorUI.setSelectedObjectIndex(m_selectedObjectIndex);

        m_editorUI.setBrushPosition(m_brushTool->getPosition(), m_brushTool->hasValidPosition());
        m_editorUI.setHasSelection(m_terrain.hasAnySelection());

        // Update AI nodes
        updateAINodeRenderer();
        updateAINodeList();
        m_editorUI.setSelectedAINodeIndex(m_selectedAINodeIndex);
    }

    void renderConversationUI() {
        if (!m_currentInteractObject) return;

        float windowWidth = static_cast<float>(getWindow().getWidth());
        float windowHeight = static_cast<float>(getWindow().getHeight());
        float chatWidth = 500.0f;
        float chatHeight = 400.0f;
        float padding = 20.0f;

        // Position chat window on the right side
        ImGui::SetNextWindowPos(ImVec2(windowWidth - chatWidth - padding, (windowHeight - chatHeight) * 0.5f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(chatWidth, chatHeight));

        std::string windowTitle = "Conversation - " + m_currentInteractObject->getName();

        if (ImGui::Begin(windowTitle.c_str(), nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {

            // Chat history area (scrollable)
            float inputAreaHeight = 60.0f;
            float historyHeight = ImGui::GetContentRegionAvail().y - inputAreaHeight;

            ImGui::BeginChild("ChatHistory", ImVec2(0, historyHeight), true);

            for (const auto& msg : m_conversationHistory) {
                if (msg.isPlayer) {
                    // Player messages - green color
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                    ImGui::TextWrapped("[You]: %s", msg.text.c_str());
                    ImGui::PopStyleColor();
                } else {
                    // NPC messages - cyan color
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                    ImGui::TextWrapped("[%s]: %s", msg.sender.c_str(), msg.text.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();
            }

            // Show thinking indicator when waiting for AI
            if (m_waitingForAIResponse) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::TextWrapped("...");
                ImGui::PopStyleColor();
            }

            // Auto-scroll to bottom when new messages added
            if (m_scrollToBottom) {
                ImGui::SetScrollHereY(1.0f);
                m_scrollToBottom = false;
            }

            ImGui::EndChild();

            ImGui::Separator();

            // Input area
            if (m_waitingForAIResponse) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1.0f), "Waiting for response...");
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Your message:");
            }

            // Auto-focus the input
            static bool needsFocus = true;
            if (needsFocus && !m_waitingForAIResponse) {
                ImGui::SetKeyboardFocusHere();
                needsFocus = false;
            }

            // Disable input while waiting
            if (m_waitingForAIResponse) {
                ImGui::BeginDisabled();
            }

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
            bool enterPressed = ImGui::InputText("##chatinput", m_responseBuffer, sizeof(m_responseBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::SameLine();
            bool sendClicked = ImGui::Button("Send", ImVec2(60, 0));

            if (m_waitingForAIResponse) {
                ImGui::EndDisabled();
            }

            if ((enterPressed || sendClicked) && m_responseBuffer[0] != '\0' && !m_waitingForAIResponse) {
                sendPlayerResponse();
                needsFocus = true;
            }

            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Press Escape to end conversation");
        }
        ImGui::End();
    }

    void sendPlayerResponse() {
        if (!m_currentInteractObject) return;
        if (m_waitingForAIResponse) return;  // Don't send while waiting

        std::string playerMessage = m_responseBuffer;
        std::string npcName = m_currentInteractObject->getName();

        // Add player message to history
        m_conversationHistory.push_back({
            "You",
            playerMessage,
            true
        });
        m_scrollToBottom = true;

        // Clear input buffer
        m_responseBuffer[0] = '\0';

        std::cout << "Player said: " << playerMessage << std::endl;

        // /run command — execute Grove script directly, bypass LLM
        if (playerMessage.size() > 5 && playerMessage.substr(0, 5) == "/run ") {
            std::string script = playerMessage.substr(5);
            addChatMessage("System", "Running: " + script);
            std::cout << "[/run] Executing: " << script << std::endl;
            m_groveOutputAccum.clear();
            int32_t ret = grove_eval(m_groveVm, script.c_str());
            if (ret != 0) {
                const char* err = grove_last_error(m_groveVm);
                int line = static_cast<int>(grove_last_error_line(m_groveVm));
                std::string errMsg = std::string("Error (line ") + std::to_string(line) + "): " + (err ? err : "unknown");
                std::cout << "[/run] " << errMsg << std::endl;
                addChatMessage("System", errMsg);
            } else if (!m_groveOutputAccum.empty()) {
                addChatMessage("System", m_groveOutputAccum);
            }
            return;
        }

        // Send to AI backend
        if (m_httpClient && m_httpClient->isConnected()) {
            m_waitingForAIResponse = true;
            // (TTS overlap prevented by m_ttsInFlight + m_ttsCooldown in speakTTS)
            int beingType = static_cast<int>(m_currentInteractObject->getBeingType());
            
            // For AI NPCs (Xenk, Eve, Robot), include updated perception data and parse actions
            if (m_currentInteractObject->getBeingType() == BeingType::AI_ARCHITECT ||
                m_currentInteractObject->getBeingType() == BeingType::EVE ||
                m_currentInteractObject->getBeingType() == BeingType::ROBOT) {
                // Use full scan results if available from recent look_around, otherwise do fresh scan
                PerceptionData perception;
                if (m_hasFullScanResult) {
                    perception = m_lastFullScanResult;
                    m_hasFullScanResult = false;  // Clear after use
                    std::cout << "  Using full scan result: " << perception.visibleObjects.size() << " objects" << std::endl;
                } else {
                    perception = performScanCone(m_currentInteractObject, 120.0f, 50.0f);
                    std::cout << "  Fresh scan: " << perception.visibleObjects.size() << " objects" << std::endl;
                }
                m_httpClient->sendChatMessageWithPerception(m_currentSessionId, playerMessage,
                    npcName, "", beingType, perception,
                    [this, npcName](const AsyncHttpClient::Response& resp) {
                        m_waitingForAIResponse = false;
                        if (resp.success) {
                            try {
                                auto json = nlohmann::json::parse(resp.body);
                                if (json.contains("session_id")) {
                                    m_currentSessionId = json["session_id"].get<std::string>();
                                }
                                std::string response = json.value("response", "...");
                                m_conversationHistory.push_back({npcName, response, false});
                                std::cout << npcName << " responded: " << response << std::endl;

                                // Speak the response via TTS
                                speakTTS(response, npcName);

                                // Cycle expression on each response
                                if (m_currentInteractObject) {
                                    cycleExpression(m_currentInteractObject);
                                }

                                // Check for and execute AI action
                                if (json.contains("action") && !json["action"].is_null()) {
                                    std::cout << "[AI] Action received: " << json["action"].value("type", "?") << std::endl;
                                    executeAIAction(json["action"]);
                                } else {
                                    std::cout << "[AI] No action in response (dialogue only)" << std::endl;
                                }
                            } catch (const std::exception& e) {
                                std::cerr << "[AI] Exception in response handler: " << e.what() << std::endl;
                                m_conversationHistory.push_back({npcName, "...", false});
                            } catch (...) {
                                std::cerr << "[AI] Unknown exception in response handler" << std::endl;
                                m_conversationHistory.push_back({npcName, "...", false});
                            }
                        } else {
                            m_conversationHistory.push_back({npcName, "(Connection lost)", false});
                        }
                        m_scrollToBottom = true;
                    });
            } else {
                // Standard NPCs without perception
                m_httpClient->sendChatMessage(m_currentSessionId, playerMessage,
                    npcName, "", beingType,
                    [this, npcName](const AsyncHttpClient::Response& resp) {
                        m_waitingForAIResponse = false;
                        if (resp.success) {
                            try {
                                auto json = nlohmann::json::parse(resp.body);
                                if (json.contains("session_id")) {
                                    m_currentSessionId = json["session_id"].get<std::string>();
                                }
                                std::string response = json.value("response", "...");
                                m_conversationHistory.push_back({npcName, response, false});
                                std::cout << npcName << " responded: " << response << std::endl;
                            } catch (...) {
                                m_conversationHistory.push_back({npcName, "...", false});
                            }
                        } else {
                            m_conversationHistory.push_back({npcName, "(Connection lost)", false});
                        }
                        m_scrollToBottom = true;
                    });
            }
        } else {
            // Fallback response when backend not available
            m_conversationHistory.push_back({npcName, "(AI backend not connected)", false});
            m_scrollToBottom = true;
        }
    }

    // Add a message to the Minecraft-style chat log + persistent history
    void addChatMessage(const std::string& sender, const std::string& message) {
        m_chatLog.push_back({sender, message, CHAT_MESSAGE_DURATION});
        while (m_chatLog.size() > MAX_CHAT_LOG_ENTRIES) {
            m_chatLog.erase(m_chatLog.begin());
        }
        // Also add to persistent world chat history
        m_worldChatHistory.push_back({sender, message});
        m_worldChatScrollToBottom = true;
    }

    // Handle transcribed voice message — find nearest NPC and send as quick chat
    void handleVoiceMessage(const std::string& text) {
        glm::vec3 playerPos = m_camera.getPosition();

        // Find nearest sentient NPC
        SceneObject* nearestNPC = nullptr;
        float nearestDist = 100.0f;
        for (auto& obj : m_sceneObjects) {
            if (!obj || !obj->isVisible() || !obj->isSentient()) continue;
            if (obj.get() == m_playerAvatar) continue;
            float dist = glm::length(obj->getTransform().getPosition() - playerPos);
            if (dist < nearestDist) {
                nearestDist = dist;
                nearestNPC = obj.get();
            }
        }

        if (!nearestNPC) {
            addChatMessage("System", "No one nearby to hear you.");
            return;
        }

        // Show player message in chat log
        addChatMessage("You", text);

        std::string npcName = nearestNPC->getName();
        int beingType = static_cast<int>(nearestNPC->getBeingType());
        m_currentInteractObject = nearestNPC;

        // Reuse session
        std::string sessionId;
        auto it = m_quickChatSessionIds.find(npcName);
        if (it != m_quickChatSessionIds.end()) sessionId = it->second;

        // Send with perception
        PerceptionData perception = performScanCone(nearestNPC, 120.0f, 50.0f);
        m_httpClient->sendChatMessageWithPerception(sessionId, text,
            npcName, "", beingType, perception,
            [this, npcName, nearestNPC](const AsyncHttpClient::Response& resp) {
                if (resp.success) {
                    try {
                        auto json = nlohmann::json::parse(resp.body);
                        if (json.contains("session_id")) {
                            m_quickChatSessionIds[npcName] = json["session_id"].get<std::string>();
                        }
                        std::string response = json.value("response", "...");
                        addChatMessage(npcName, response);
                        speakTTS(response, npcName);

                        m_currentInteractObject = nearestNPC;
                        if (json.contains("action") && !json["action"].is_null()) {
                            executeAIAction(json["action"]);
                        }
                    } catch (...) {
                        addChatMessage(npcName, "...");
                    }
                } else {
                    addChatMessage(npcName, "(No response)");
                }
            });
    }

    // Cycle to the next expression texture on an NPC (for testing)
    void cycleExpression(SceneObject* npc) {
        if (!npc || npc->getExpressionCount() == 0) return;
        int next = (npc->getCurrentExpression() + 1) % npc->getExpressionCount();
        if (npc->setExpression(next)) {
            auto& tex = npc->getTextureData();
            m_modelRenderer->updateTexture(
                npc->getBufferHandle(), tex.data(),
                npc->getTextureWidth(), npc->getTextureHeight());
            std::cout << "[Expression] " << npc->getName() << " -> '"
                      << npc->getExpressionName(next) << "'" << std::endl;
        }
    }

    // Request TTS audio and play it via miniaudio
    // Only one TTS can be in-flight or playing at a time — all others are dropped
    void speakTTS(const std::string& text, const std::string& npcName) {
        if (!m_httpClient || text.empty()) return;

        // Block if another TTS is still in-flight or playing
        if (m_ttsInFlight || m_ttsCooldown > 0.0f) {
            std::cout << "[TTS] Skipped (already playing): \"" << text.substr(0, 40) << "...\"" << std::endl;
            return;
        }

        // Pick voice based on NPC
        std::string voice = "en-US-AvaNeural";  // Liora default
        std::string rate;
        bool useRobotVoice = false;
        if (npcName == "Eve") voice = "en-GB-SoniaNeural";
        else if (npcName == "Xenk") voice = "en-US-GuyNeural";
        else if (npcName.find("Robot") != std::string::npos) voice = "en-US-GuyNeural";
        else if ([&]() { std::string lower = npcName; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower); return lower.find("lionel") != std::string::npos || lower.find("unit") != std::string::npos; }()) {
            useRobotVoice = true;
        }

        m_ttsInFlight = true;
        std::cout << "[TTS] Requesting: \"" << text.substr(0, 60) << "...\" (" << (useRobotVoice ? "robot" : voice) << ")" << std::endl;

        m_httpClient->requestTTS(text, voice,
            [this](const AsyncHttpClient::Response& resp) {
                m_ttsInFlight = false;
                if (!resp.success) {
                    std::cerr << "[TTS] Request failed: " << resp.error << " (status " << resp.statusCode << ")" << std::endl;
                    return;
                }
                if (resp.body.empty()) {
                    std::cerr << "[TTS] Empty audio response" << std::endl;
                    return;
                }

                // Estimate audio duration: WAV ~32kB/s (16-bit mono 16kHz), MP3 ~16kB/s
                bool isWav = resp.body.size() >= 4 && resp.body[0] == 'R' && resp.body[1] == 'I' && resp.body[2] == 'F' && resp.body[3] == 'F';
                float estimatedDuration = isWav
                    ? static_cast<float>(resp.body.size()) / 32000.0f
                    : static_cast<float>(resp.body.size()) / 16000.0f;

                // Write audio to temp file and play
                std::string ext = isWav ? ".wav" : ".mp3";
                std::string tempPath = "/tmp/eden_tts_" + std::to_string(m_ttsFileCounter++) + ext;
                std::ofstream out(tempPath, std::ios::binary);
                if (out) {
                    out.write(resp.body.data(), resp.body.size());
                    out.close();

                    if (!m_lastTTSFile.empty()) {
                        std::remove(m_lastTTSFile.c_str());
                    }
                    m_lastTTSFile = tempPath;

                    std::cout << "[TTS] Playing: " << tempPath << " (~" << estimatedDuration << "s)" << std::endl;
                    Audio::getInstance().playSound(tempPath, 0.8f);

                    // Set cooldown — no new TTS until this one finishes
                    m_ttsCooldown = estimatedDuration + 0.5f;
                } else {
                    std::cerr << "[TTS] Failed to write temp file: " << tempPath << std::endl;
                }
            }, rate, useRobotVoice);
    }

    // Update chat log timers (call from update loop)
    void updateChatLog(float deltaTime) {
        for (auto it = m_chatLog.begin(); it != m_chatLog.end(); ) {
            it->timeRemaining -= deltaTime;
            if (it->timeRemaining <= 0) {
                it = m_chatLog.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Get NPC chat color by sender name (shared by overlay and history window)
    ImVec4 getChatColor(const std::string& sender, float alpha = 1.0f) {
        if (sender == "You") {
            return ImVec4(0.8f, 0.8f, 0.8f, alpha);
        } else if (sender == "Eve" || sender.find("Eve") == 0) {
            return ImVec4(0.4f, 1.0f, 0.5f, alpha);
        } else if (sender == "Xenk" || sender.find("Xenk") == 0) {
            return ImVec4(0.4f, 0.6f, 1.0f, alpha);
        } else if (sender.find("Robot") != std::string::npos || sender.find("robot") != std::string::npos) {
            return ImVec4(1.0f, 0.6f, 0.2f, alpha);
        } else if (sender == "Liora" || sender.find("Liora") == 0) {
            return ImVec4(0.9f, 0.5f, 1.0f, alpha);
        } else if (sender == "System") {
            return ImVec4(1.0f, 1.0f, 0.4f, alpha);
        } else {
            return ImVec4(0.4f, 0.9f, 1.0f, alpha);
        }
    }

    // Render the chat log overlay (bottom-left, Minecraft style)
    void renderChatLog() {
        // PTT recording/processing indicator
        if (m_pttRecording || m_pttProcessing) {
            float windowWidth = static_cast<float>(getWindow().getWidth());
            float windowHeight = static_cast<float>(getWindow().getHeight());
            ImGui::SetNextWindowPos(ImVec2(windowWidth * 0.5f - 80.0f, windowHeight - 120.0f));
            ImGui::SetNextWindowBgAlpha(0.7f);
            if (ImGui::Begin("##PTTIndicator", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
                if (m_pttRecording) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    ImGui::Text("  Recording...  ");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
                    ImGui::Text("  Transcribing...  ");
                    ImGui::PopStyleColor();
                }
            }
            ImGui::End();
        }

        if (m_chatLog.empty() && !m_quickChatMode) return;

        float windowWidth = static_cast<float>(getWindow().getWidth());
        float windowHeight = static_cast<float>(getWindow().getHeight());
        float chatX = 10.0f;
        float chatW = std::min(windowWidth * 0.5f, 600.0f);  // Max 50% of screen or 600px
        float chatY = windowHeight - 80.0f;

        // Calculate height needed with wrapping (estimate)
        float estimatedHeight = 0.0f;
        for (const auto& entry : m_chatLog) {
            std::string fullText = "<" + entry.sender + "> " + entry.message;
            float textWidth = ImGui::CalcTextSize(fullText.c_str()).x;
            float lines = std::max(1.0f, std::ceil(textWidth / (chatW - 20.0f)));
            estimatedHeight += lines * ImGui::GetTextLineHeightWithSpacing();
        }

        ImGui::SetNextWindowPos(ImVec2(chatX, chatY - estimatedHeight - 10.0f));
        ImGui::SetNextWindowSize(ImVec2(chatW, 0));
        ImGui::SetNextWindowBgAlpha(0.0f);

        if (ImGui::Begin("##ChatLog", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs)) {

            ImGui::PushTextWrapPos(chatW - 20.0f);
            for (const auto& entry : m_chatLog) {
                float alpha = std::min(1.0f, entry.timeRemaining / 2.0f);
                ImVec4 color = getChatColor(entry.sender, alpha);
                std::string fullText = "<" + entry.sender + "> " + entry.message;
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextWrapped("%s", fullText.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
    }

    // Render the full conversation history window (toggleable with Tab)
    void renderWorldChatHistory() {
        if (!m_showWorldChatHistory) return;

        float windowWidth = static_cast<float>(getWindow().getWidth());
        float windowHeight = static_cast<float>(getWindow().getHeight());
        float histW = std::min(500.0f, windowWidth * 0.4f);
        float histH = std::min(400.0f, windowHeight * 0.5f);

        ImGui::SetNextWindowPos(ImVec2(windowWidth - histW - 10.0f, windowHeight - histH - 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(histW, histH), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.85f);

        if (ImGui::Begin("World Chat", &m_showWorldChatHistory,
            ImGuiWindowFlags_NoFocusOnAppearing)) {

            // Scrollable child region
            ImGui::BeginChild("##ChatScroll", ImVec2(0, 0), false);
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);

            for (const auto& entry : m_worldChatHistory) {
                ImVec4 color = getChatColor(entry.sender);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextWrapped("<%s> %s", entry.sender.c_str(), entry.message.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::PopTextWrapPos();

            // Auto-scroll to bottom when new messages arrive
            if (m_worldChatScrollToBottom) {
                ImGui::SetScrollHereY(1.0f);
                m_worldChatScrollToBottom = false;
            }

            ImGui::EndChild();
        }
        ImGui::End();
    }

    void renderQuickChatUI() {
        // Minecraft-style chat: direct keyboard capture, no mouse needed

        // Handle typed characters directly
        const std::string& typed = Input::getTypedChars();
        if (!typed.empty()) {
            size_t currentLen = strlen(m_quickChatBuffer);
            for (char c : typed) {
                if (currentLen < sizeof(m_quickChatBuffer) - 1) {
                    m_quickChatBuffer[currentLen++] = c;
                    m_quickChatBuffer[currentLen] = '\0';
                }
            }
            Input::clearTypedChars();
        }

        // Handle backspace
        if (Input::isKeyPressed(Input::KEY_BACKSPACE)) {
            size_t len = strlen(m_quickChatBuffer);
            if (len > 0) {
                m_quickChatBuffer[len - 1] = '\0';
            }
        }

        // Handle enter to send
        if (Input::isKeyPressed(Input::KEY_ENTER) && m_quickChatBuffer[0] != '\0') {
            sendQuickChatMessage();
            return;  // Chat mode closed by sendQuickChatMessage
        }

        // Render the chat bar (display only, no input widget)
        float windowWidth = static_cast<float>(getWindow().getWidth());
        float windowHeight = static_cast<float>(getWindow().getHeight());
        float chatBarWidth = 600.0f;
        float chatBarHeight = 40.0f;
        float padding = 20.0f;

        // Position chat bar at bottom center
        ImGui::SetNextWindowPos(ImVec2((windowWidth - chatBarWidth) * 0.5f, windowHeight - chatBarHeight - padding));
        ImGui::SetNextWindowSize(ImVec2(chatBarWidth, chatBarHeight));
        ImGui::SetNextWindowBgAlpha(0.85f);

        if (ImGui::Begin("##QuickChat", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing)) {

            // Show the typed text with a blinking cursor
            static float cursorBlink = 0.0f;
            cursorBlink += ImGui::GetIO().DeltaTime;
            bool showCursor = fmod(cursorBlink, 1.0f) < 0.5f;

            std::string displayText = "/";
            displayText += m_quickChatBuffer;
            if (showCursor) {
                displayText += "_";
            }

            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", displayText.c_str());
        }
        ImGui::End();

        // Show hint above the chat bar
        ImGui::SetNextWindowPos(ImVec2((windowWidth - chatBarWidth) * 0.5f, windowHeight - chatBarHeight - padding - 22));
        ImGui::SetNextWindowBgAlpha(0.0f);
        if (ImGui::Begin("##QuickChatHint", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoFocusOnAppearing)) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "/eve | /xenk | /robot | /liora <msg> — Enter to send, Esc to cancel");
        }
        ImGui::End();
    }

    void sendQuickChatMessage() {
        std::string message = m_quickChatBuffer;

        // /run command — execute Grove script directly, no NPC needed
        {
            std::string lower = message;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.rfind("run ", 0) == 0) {
                std::string script = message.substr(4);
                addChatMessage("System", "Running: " + script);
                std::cout << "[/run] Executing: " << script << std::endl;
                m_groveOutputAccum.clear();
                int32_t ret = grove_eval(m_groveVm, script.c_str());
                if (ret != 0) {
                    const char* err = grove_last_error(m_groveVm);
                    int line = static_cast<int>(grove_last_error_line(m_groveVm));
                    std::string errMsg = std::string("Error (line ") + std::to_string(line) + "): " + (err ? err : "unknown");
                    std::cout << "[/run] " << errMsg << std::endl;
                    addChatMessage("System", errMsg);
                } else if (!m_groveOutputAccum.empty()) {
                    addChatMessage("System", m_groveOutputAccum);
                }
                m_quickChatMode = false;
                m_quickChatBuffer[0] = '\0';
                return;
            }
        }

        glm::vec3 playerPos = m_camera.getPosition();

        // Parse command prefix: /eve or /xenk to target a specific NPC
        BeingType targetType = BeingType::STATIC;  // STATIC = no specific target
        bool hasTargetPrefix = false;
        bool targetTerminal = false;

        // Case-insensitive prefix check
        std::string lowerMsg = message;
        std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);

        if (lowerMsg.rfind("/eve ", 0) == 0 || lowerMsg.rfind("eve ", 0) == 0) {
            targetType = BeingType::EVE;
            hasTargetPrefix = true;
            size_t spacePos = message.find(' ');
            message = message.substr(spacePos + 1);
        } else if (lowerMsg.rfind("/xenk ", 0) == 0 || lowerMsg.rfind("xenk ", 0) == 0) {
            targetType = BeingType::AI_ARCHITECT;
            hasTargetPrefix = true;
            size_t spacePos = message.find(' ');
            message = message.substr(spacePos + 1);
        } else if (lowerMsg.rfind("/robot ", 0) == 0 || lowerMsg.rfind("robot ", 0) == 0) {
            targetType = BeingType::ROBOT;
            hasTargetPrefix = true;
            size_t spacePos = message.find(' ');
            message = message.substr(spacePos + 1);
        } else if (lowerMsg.rfind("/liora ", 0) == 0 || lowerMsg.rfind("liora ", 0) == 0) {
            targetType = BeingType::EDEN_COMPANION;
            hasTargetPrefix = true;
            size_t spacePos = message.find(' ');
            message = message.substr(spacePos + 1);
        } else if (lowerMsg.rfind("/terminal ", 0) == 0 || lowerMsg.rfind("terminal ", 0) == 0 ||
                   lowerMsg.rfind("/console ", 0) == 0 || lowerMsg.rfind("console ", 0) == 0) {
            targetTerminal = true;
            size_t spacePos = message.find(' ');
            message = message.substr(spacePos + 1);
        }

        // Send directly to terminal if /terminal prefix or proximity to terminal_screen
        if (targetTerminal) {
            if (m_terminal.isAlive()) {
                addChatMessage("You → Terminal", message);
                m_terminal.sendCommand(message + "\n");
                m_quickChatMode = false;
                m_quickChatBuffer[0] = '\0';
                return;
            } else {
                addChatMessage("System", "Terminal is not running");
                m_quickChatMode = false;
                m_quickChatBuffer[0] = '\0';
                return;
            }
        }

        SceneObject* closestSentient = nullptr;

        if (hasTargetPrefix) {
            // Find NPC by type (no distance limit)
            for (auto& obj : m_sceneObjects) {
                if (!obj || !obj->isVisible()) continue;
                if (obj.get() == m_playerAvatar) continue;
                if (obj->getBeingType() == targetType) {
                    closestSentient = obj.get();
                    break;
                }
            }
        } else {
            // Proximity-based search — check NPCs and terminal screen
            const float quickChatRadius = 100.0f;
            float closestDist = quickChatRadius;

            // Check if terminal_screen is closest
            if (m_terminalScreenObject && m_terminal.isAlive()) {
                glm::vec3 termPos = m_terminalScreenObject->getTransform().getPosition();
                float termDist = glm::length(termPos - playerPos);
                if (termDist < closestDist) {
                    // Terminal is closest — send command to it
                    addChatMessage("You → Terminal", message);
                    m_terminal.sendCommand(message + "\n");
                    m_quickChatMode = false;
                    m_quickChatBuffer[0] = '\0';
                    return;
                }
            }

            for (auto& obj : m_sceneObjects) {
                if (!obj || !obj->isVisible()) continue;
                if (!obj->isSentient()) continue;
                if (obj.get() == m_playerAvatar) continue;

                glm::vec3 objPos = obj->getTransform().getPosition();
                float dist = glm::length(objPos - playerPos);

                if (dist < closestDist) {
                    closestDist = dist;
                    closestSentient = obj.get();
                }
            }
        }

        if (!closestSentient) {
            // No one to talk to
            std::string errorMsg = hasTargetPrefix ? "No NPC of that type found in scene" : "No one nearby to hear you";
            addChatMessage("System", errorMsg);
            m_quickChatMode = false;
            m_quickChatBuffer[0] = '\0';
            return;
        }

        // Show player message in chat log
        addChatMessage("You", message);

        // Send message to AI
        std::string npcName = closestSentient->getName();
        int beingType = static_cast<int>(closestSentient->getBeingType());

        std::cout << "Quick chat to " << npcName << ": " << message << std::endl;

        // Set interact object so AI motor actions (look_around, move_to) have a target
        m_currentInteractObject = closestSentient;
        // (TTS overlap prevented by m_ttsInFlight + m_ttsCooldown in speakTTS)

        if (m_httpClient && m_httpClient->isConnected()) {
            // Reuse session for same NPC, or start fresh
            std::string sessionId = "";
            auto it = m_quickChatSessionIds.find(npcName);
            if (it != m_quickChatSessionIds.end()) {
                sessionId = it->second;
            }

            // For AI NPCs (Xenk, Eve, Robot), include perception data and parse actions
            if (closestSentient->getBeingType() == BeingType::AI_ARCHITECT ||
                closestSentient->getBeingType() == BeingType::EVE ||
                closestSentient->getBeingType() == BeingType::ROBOT ||
                closestSentient->getBeingType() == BeingType::EDEN_COMPANION) {
                PerceptionData perception;
                if (m_hasFullScanResult) {
                    perception = m_lastFullScanResult;
                    m_hasFullScanResult = false;
                    std::cout << "  Quick chat using full scan result: " << perception.visibleObjects.size() << " objects" << std::endl;
                } else {
                    perception = performScanCone(closestSentient, 120.0f, 50.0f);
                    std::cout << "  Quick chat fresh scan: " << perception.visibleObjects.size() << " objects" << std::endl;
                }

                m_httpClient->sendChatMessageWithPerception(sessionId, message,
                    npcName, "", beingType, perception,
                    [this, npcName, closestSentient](const AsyncHttpClient::Response& resp) {
                        if (resp.success) {
                            try {
                                auto json = nlohmann::json::parse(resp.body);
                                if (json.contains("session_id")) {
                                    m_quickChatSessionIds[npcName] = json["session_id"].get<std::string>();
                                }
                                std::string response = json.value("response", "...");
                                addChatMessage(npcName, response);
                                std::cout << npcName << " says: " << response << std::endl;

                                // Speak the response via TTS
                                speakTTS(response, npcName);

                                // Cycle expression on each response
                                cycleExpression(closestSentient);

                                // Restore interact object for this NPC (may have been cleared since send)
                                m_currentInteractObject = closestSentient;

                                // Execute AI action if present
                                if (json.contains("action") && !json["action"].is_null()) {
                                    executeAIAction(json["action"]);
                                }
                            } catch (...) {
                                addChatMessage(npcName, "...");
                            }
                        } else {
                            addChatMessage(npcName, "(No response)");
                        }
                    });
            } else {
                // Standard NPCs without perception
                m_httpClient->sendChatMessage(sessionId, message,
                    npcName, "", beingType,
                    [this, npcName](const AsyncHttpClient::Response& resp) {
                        if (resp.success) {
                            try {
                                auto json = nlohmann::json::parse(resp.body);
                                if (json.contains("session_id")) {
                                    m_quickChatSessionIds[npcName] = json["session_id"].get<std::string>();
                                }
                                std::string response = json.value("response", "...");
                                addChatMessage(npcName, response);
                                std::cout << npcName << " says: " << response << std::endl;
                            } catch (...) {
                                addChatMessage(npcName, "...");
                            }
                        } else {
                            addChatMessage(npcName, "(No response)");
                        }
                    });
            }
        } else {
            // Fallback when backend not available
            addChatMessage("System", "AI backend not connected");
        }

        // Close quick chat mode (mouse state unchanged - stays in whatever mode it was)
        m_quickChatMode = false;
        m_quickChatBuffer[0] = '\0';
    }

    void renderPlanetInfoPanel() {
        if (!m_showPlanetInfo || !m_worldGenerated || m_planetData.empty()) return;

        ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("Planet Info [P]", &m_showPlanetInfo,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {

            std::string name = m_planetData.value("name", "Unknown");
            std::string biome = m_planetData.value("biome_name", m_planetData.value("biome", "?"));
            std::string species = m_planetData.value("species_name", "Unknown");
            std::string govt = m_planetData.value("government_name", "Unknown");
            int techLevel = m_planetData.value("tech_level", 0);
            std::string techName = m_planetData.value("tech_name", "");
            int population = m_planetData.value("population", 0);
            std::string temp = m_planetData.value("temperature", "?");
            std::string veg = m_planetData.value("vegetation", "?");

            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "%s", name.c_str());
            ImGui::Separator();

            ImGui::Text("Biome:       %s", biome.c_str());
            ImGui::Text("Temperature: %s", temp.c_str());
            ImGui::Text("Vegetation:  %s", veg.c_str());
            ImGui::Spacing();

            ImGui::Text("Species:     %s", species.c_str());
            ImGui::Text("Government:  %s", govt.c_str());
            ImGui::Text("Tech Level:  %d (%s)", techLevel, techName.c_str());
            ImGui::Text("Population:  %d", population);
            ImGui::Spacing();

            // Resources
            if (m_planetData.contains("resources_harvestable") && m_planetData["resources_harvestable"].is_array()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Harvestable Resources:");
                for (const auto& r : m_planetData["resources_harvestable"]) {
                    ImGui::BulletText("%s", r.get<std::string>().c_str());
                }
            }

            if (m_planetData.contains("resources_locked") && m_planetData["resources_locked"].is_array()
                && !m_planetData["resources_locked"].empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Locked Resources:");
                for (const auto& r : m_planetData["resources_locked"]) {
                    ImGui::BulletText("%s", r.get<std::string>().c_str());
                }
            }

            // Buildings available
            if (m_planetData.contains("buildings_available") && m_planetData["buildings_available"].is_array()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Available Buildings:");
                std::string bldgList;
                for (size_t i = 0; i < m_planetData["buildings_available"].size(); i++) {
                    if (i > 0) bldgList += ", ";
                    bldgList += m_planetData["buildings_available"][i].get<std::string>();
                }
                ImGui::TextWrapped("%s", bldgList.c_str());
            }
        }
        ImGui::End();
    }

    void renderPlayModeUI() {
        if (m_filesystemBrowser.isActive()) {
            // Filesystem mode: show current directory path centered at top
            std::string dirPath = m_filesystemBrowser.getCurrentPath();
            if (m_monoFont) ImGui::PushFont(m_monoFont);
            ImVec2 textSize = ImGui::CalcTextSize(dirPath.c_str());
            float windowW = static_cast<float>(getWindow().getWidth());
            float padX = 20.0f;
            float winW = textSize.x + padX * 2.0f;
            if (m_monoFont) ImGui::PopFont();
            ImGui::SetNextWindowPos(ImVec2((windowW - winW) * 0.5f, 10));
            ImGui::SetNextWindowBgAlpha(0.6f);
            if (ImGui::Begin("##FSPath", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings)) {
                if (m_monoFont) ImGui::PushFont(m_monoFont);
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%s", dirPath.c_str());
                if (m_monoFont) ImGui::PopFont();
            }
            ImGui::End();
        } else {
            // Normal play mode: show hint + HUD
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowBgAlpha(0.3f);
            if (ImGui::Begin("##PlayModeHint", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::Text("PLAY MODE - Press Escape or F5 to exit");
                if (m_playModeCursorVisible) {
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Right-click to resume mouse look");
                } else {
                    ImGui::Text("Right-click to show cursor for UI");
                }
            }
            ImGui::End();

            // Credits + City Credits + Game time display (upper right corner)
            std::string timeStr = formatGameTimeDisplay(m_gameTimeMinutes);
            char creditsStr[64];
            snprintf(creditsStr, sizeof(creditsStr), "%d CR", static_cast<int>(m_playerCredits));
            char cityCreditsStr[64];
            snprintf(cityCreditsStr, sizeof(cityCreditsStr), "City: %d CR", static_cast<int>(m_cityCredits));
            ImVec2 timeSize = ImGui::CalcTextSize(timeStr.c_str());
            ImVec2 creditsSize = ImGui::CalcTextSize(creditsStr);
            ImVec2 cityCreditsSize = ImGui::CalcTextSize(cityCreditsStr);
            float hudWindowWidth = creditsSize.x + 20.0f + cityCreditsSize.x + 20.0f + timeSize.x + 20.0f;
            ImGui::SetNextWindowPos(ImVec2(getWindow().getWidth() - hudWindowWidth - 10, 10));
            ImGui::SetNextWindowBgAlpha(0.5f);
            if (ImGui::Begin("##GameHUD", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", creditsStr);
                ImGui::SameLine(0, 20.0f);
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", cityCreditsStr);
                ImGui::SameLine(0, 20.0f);
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "%s", timeStr.c_str());
            }
            ImGui::End();
        }

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        float cx = getWindow().getWidth() * 0.5f;
        float cy = getWindow().getHeight() * 0.5f;
        float size = 10.0f;
        float thickness = 2.0f;
        ImU32 color = IM_COL32(255, 255, 255, 200);

        drawList->AddLine(ImVec2(cx - size, cy), ImVec2(cx + size, cy), color, thickness);
        drawList->AddLine(ImVec2(cx, cy - size), ImVec2(cx, cy + size), color, thickness);

        // Filename preview under crosshair when hovering a filesystem object
        if (!m_fsHoverName.empty()) {
            if (m_monoFont) ImGui::PushFont(m_monoFont);
            ImVec2 labelSize = ImGui::CalcTextSize(m_fsHoverName.c_str());
            float labelX = cx - labelSize.x * 0.5f;
            float labelY = cy + size + 6.0f;
            // Shadow
            drawList->AddText(ImVec2(labelX + 1, labelY + 1), IM_COL32(0, 0, 0, 180), m_fsHoverName.c_str());
            // Text
            drawList->AddText(ImVec2(labelX, labelY), IM_COL32(255, 255, 255, 230), m_fsHoverName.c_str());
            if (m_monoFont) ImGui::PopFont();
        }

        // (collision hull drawing moved to common render path below)

        // Player health bar - HIDDEN for now (was overlapping chat text)
        // TODO: re-enable when UI layout is finalized

        // Render planet info panel (P key toggle)
        renderPlanetInfoPanel();

        // Render trading UI panels
        renderTradingUI();

        // Render game module UI
        if (m_gameModule) {
            float width = static_cast<float>(getWindow().getWidth());
            float height = static_cast<float>(getWindow().getHeight());
            m_gameModule->renderUI(width, height);
        }

        // Render AI mind map in play mode (toggled by Unit 42's show_mind_map action)
        if (m_editorUI.showMindMap()) {
            m_editorUI.renderMindMapWindow();
        }

        // Filesystem right-click context menu
        if (m_fsContextMenuOpen) {
            // Position popup at screen center (crosshair)
            float cx = getWindow().getWidth() * 0.5f;
            float cy = getWindow().getHeight() * 0.5f;
            ImGui::SetNextWindowPos(ImVec2(cx, cy));
            ImGui::OpenPopup("##FSContextMenu");
            m_fsContextMenuOpen = false;
        }
        bool fsPopupOpen = ImGui::BeginPopup("##FSContextMenu");
        bool fsNewFolderModalOpen = ImGui::IsPopupOpen("New Folder##FSNewFolder");
        bool fsRenameModalOpen = ImGui::IsPopupOpen("Rename##FSRename");
        if (!fsPopupOpen && m_fsContextMenuWasOpen && !m_fsNewFolderPopup && !fsNewFolderModalOpen && !m_fsRenamePopup && !fsRenameModalOpen) {
            // Popup just closed — return to mouse look (unless opening New Folder dialog)
            m_playModeCursorVisible = false;
            Input::setMouseCaptured(true);
        }
        m_fsContextMenuWasOpen = fsPopupOpen;
        if (fsPopupOpen) {
            // Separate selected file objects from selected wall (blank slate) objects
            std::vector<SceneObject*> selectedFiles;
            SceneObject* selectedWall = nullptr;
            for (auto& obj : m_sceneObjects) {
                if (!obj || !obj->isSelected()) continue;
                if (obj->getBuildingType() == "filesystem" && !obj->isDoor()) {
                    selectedFiles.push_back(obj.get());
                } else if (obj->getBuildingType() == "filesystem_wall") {
                    selectedWall = obj.get(); // use last selected wall
                }
            }

            bool hasClipboard = !m_fsClipboard.empty();

            if (!selectedFiles.empty()) {
                // File(s) selected — offer Copy, Cut, Delete
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "%zu file(s) selected", selectedFiles.size());
                ImGui::Separator();
                if (ImGui::MenuItem("Copy")) {
                    m_fsClipboard.clear();
                    m_fsClipboardIsCut = false;
                    for (auto* obj : selectedFiles) {
                        std::string target = obj->getTargetLevel();
                        if (target.rfind("fs://", 0) == 0) {
                            m_fsClipboard.push_back(target.substr(5));
                        }
                    }
                }
                if (ImGui::MenuItem("Cut")) {
                    m_fsClipboard.clear();
                    m_fsClipboardIsCut = true;
                    for (auto* obj : selectedFiles) {
                        std::string target = obj->getTargetLevel();
                        if (target.rfind("fs://", 0) == 0) {
                            m_fsClipboard.push_back(target.substr(5));
                        }
                    }
                }
                ImGui::Separator();
                if (selectedFiles.size() == 1) {
                    if (ImGui::MenuItem("Rename")) {
                        std::string target = selectedFiles[0]->getTargetLevel();
                        if (target.rfind("fs://", 0) == 0) {
                            m_fsRenameOldPath = target.substr(5);
                            std::string filename = std::filesystem::path(m_fsRenameOldPath).filename().string();
                            snprintf(m_fsRenameName, sizeof(m_fsRenameName), "%s", filename.c_str());
                            m_fsRenamePopup = true;
                        }
                    }
                }
                if (ImGui::MenuItem("Delete (Trash)")) {
                    std::string destDir = m_filesystemBrowser.getCurrentPath();
                    for (auto* obj : selectedFiles) {
                        std::string target = obj->getTargetLevel();
                        if (target.rfind("fs://", 0) == 0) {
                            std::string path = target.substr(5);
                            std::string cmd = "gio trash " + shellEscapeFS(path);
                            int ret = system(cmd.c_str());
                            if (ret != 0) {
                                std::cerr << "[FS] Trash failed: " << path << std::endl;
                            }
                        }
                    }
                    // Refresh view
                    m_filesystemBrowser.navigate(destDir);
                }
            } else if (selectedWall) {
                const std::string& wallDesc = selectedWall->getDescription();
                std::string wallTarget = selectedWall->getTargetLevel();
                bool wallHasItem = (wallTarget.rfind("fs://", 0) == 0 && wallTarget.size() > 5);
                std::string wallItemPath = wallHasItem ? wallTarget.substr(5) : "";
                std::string wallItemName = wallHasItem ? std::filesystem::path(wallItemPath).filename().string() : "";

                std::string wallType;
                if (wallDesc == "wall_image")      wallType = "image";
                else if (wallDesc == "wall_video")  wallType = "video";
                else if (wallDesc == "wall_folder") wallType = "folder";
                else                                wallType = "other";

                if (wallHasItem) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "%s", wallItemName.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy")) {
                        m_fsClipboard.clear();
                        m_fsClipboardIsCut = false;
                        m_fsClipboard.push_back(wallItemPath);
                    }
                    if (ImGui::MenuItem("Cut")) {
                        m_fsClipboard.clear();
                        m_fsClipboardIsCut = true;
                        m_fsClipboard.push_back(wallItemPath);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Rename")) {
                        m_fsRenameOldPath = wallItemPath;
                        snprintf(m_fsRenameName, sizeof(m_fsRenameName), "%s", wallItemName.c_str());
                        m_fsRenamePopup = true;
                    }
                    if (ImGui::MenuItem("Delete (Trash)")) {
                        std::string cmd = "gio trash " + shellEscapeFS(wallItemPath);
                        int ret = system(cmd.c_str());
                        if (ret != 0) {
                            std::cerr << "[FS] Trash failed: " << wallItemPath << std::endl;
                        }
                        m_filesystemBrowser.navigate(m_filesystemBrowser.getCurrentPath());
                    }
                    // If this is a folder and clipboard has content, offer paste-into
                    if (wallDesc == "wall_folder" && hasClipboard) {
                        ImGui::Separator();
                        const char* pasteLabel = m_fsClipboardIsCut ? "Move into folder" : "Paste into folder";
                        if (ImGui::MenuItem(pasteLabel)) {
                            namespace fs = std::filesystem;
                            fs::path destDir(wallItemPath);
                            for (const auto& srcPath : m_fsClipboard) {
                                fs::path src(srcPath);
                                fs::path dst = destDir / src.filename();
                                if (fs::exists(dst)) {
                                    std::string stem = dst.stem().string();
                                    std::string ext = dst.extension().string();
                                    int n = 1;
                                    do {
                                        dst = destDir / (stem + "_" + std::to_string(n) + ext);
                                        n++;
                                    } while (fs::exists(dst));
                                }
                                std::error_code ec;
                                if (m_fsClipboardIsCut) {
                                    fs::rename(src, dst, ec);
                                } else if (fs::is_directory(src)) {
                                    fs::copy(src, dst, fs::copy_options::recursive, ec);
                                } else {
                                    fs::copy_file(src, dst, ec);
                                }
                                if (ec) {
                                    std::cerr << "[FS] Paste into folder failed: " << ec.message() << std::endl;
                                }
                            }
                            if (m_fsClipboardIsCut) {
                                m_fsClipboard.clear();
                                // Refresh to remove moved items from view
                                m_filesystemBrowser.navigate(m_filesystemBrowser.getCurrentPath());
                            }
                            selectedWall->setSelected(false);
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s slot", wallType.c_str());
                }
                ImGui::Separator();

                // Paste option if clipboard has content
                if (hasClipboard) {
                    const char* pasteLabel = m_fsClipboardIsCut ? "Move here" : "Paste";
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%zu file(s) %s", m_fsClipboard.size(),
                                       m_fsClipboardIsCut ? "to move" : "in clipboard");
                    if (ImGui::MenuItem(pasteLabel)) {
                        std::string destDir = m_filesystemBrowser.getCurrentPath();
                        glm::vec3 wallPos = selectedWall->getTransform().getPosition();
                        glm::vec3 wallScale = selectedWall->getTransform().getScale();
                        float wallYaw = selectedWall->getEulerRotation().y;

                        // Helper to remove an old scene object by its filesystem path
                        auto removeOldFsObject = [&](const std::string& fsPath) {
                            std::string targetLevel = "fs://" + fsPath;
                            for (auto it = m_sceneObjects.begin(); it != m_sceneObjects.end(); ++it) {
                                if (*it && (*it)->getBuildingType() == "filesystem" &&
                                    (*it)->getTargetLevel() == targetLevel) {
                                    uint32_t handle = (*it)->getBufferHandle();
                                    if (handle != 0) m_modelRenderer->destroyModel(handle);
                                    m_sceneObjects.erase(it);
                                    break;
                                }
                            }
                        };

                        for (const auto& srcPath : m_fsClipboard) {
                            namespace fs = std::filesystem;
                            fs::path src(srcPath);
                            fs::path dst = fs::path(destDir) / src.filename();

                            // Check if source is already in the destination directory
                            bool sameDir = fs::equivalent(src.parent_path(), fs::path(destDir));

                            if (sameDir && m_fsClipboardIsCut) {
                                // Cut within same directory — remove old object, spawn at new wall
                                removeOldFsObject(srcPath);
                                m_filesystemBrowser.spawnFileAtWall(srcPath, wallPos, wallScale, wallYaw);
                                selectedWall->setTargetLevel("fs://" + srcPath);
                            } else {
                                if (fs::exists(dst) && !sameDir) {
                                    std::string stem = dst.stem().string();
                                    std::string ext = dst.extension().string();
                                    fs::path parent = dst.parent_path();
                                    int n = 1;
                                    do {
                                        dst = parent / (stem + "_" + std::to_string(n) + ext);
                                        n++;
                                    } while (fs::exists(dst));
                                } else if (fs::exists(dst) && sameDir) {
                                    // Copy within same dir — always auto-rename
                                    std::string stem = dst.stem().string();
                                    std::string ext = dst.extension().string();
                                    fs::path parent = dst.parent_path();
                                    int n = 1;
                                    do {
                                        dst = parent / (stem + "_" + std::to_string(n) + ext);
                                        n++;
                                    } while (fs::exists(dst));
                                }
                                std::error_code ec;
                                if (m_fsClipboardIsCut) {
                                    fs::rename(src, dst, ec);
                                } else if (fs::is_directory(src)) {
                                    fs::copy(src, dst, fs::copy_options::recursive, ec);
                                } else {
                                    fs::copy_file(src, dst, ec);
                                }
                                if (ec) {
                                    std::cerr << "[FS] " << (m_fsClipboardIsCut ? "Move" : "Copy") << " failed: " << srcPath << " -> " << dst.string() << ": " << ec.message() << std::endl;
                                } else {
                                    m_filesystemBrowser.spawnFileAtWall(dst.string(), wallPos, wallScale, wallYaw);
                                    selectedWall->setTargetLevel("fs://" + dst.string());
                                }
                            }
                        }
                        if (m_fsClipboardIsCut) m_fsClipboard.clear();
                        selectedWall->setSelected(false);
                    }
                }

                // New Folder on folder-type wall
                if (wallDesc == "wall_folder") {
                    if (ImGui::MenuItem("New Folder")) {
                        snprintf(m_fsNewFolderName, sizeof(m_fsNewFolderName), "New Folder");
                        m_fsNewFolderOnWall = true;
                        m_fsNewFolderWallPos = selectedWall->getTransform().getPosition();
                        m_fsNewFolderWallScale = selectedWall->getTransform().getScale();
                        m_fsNewFolderWallYaw = selectedWall->getEulerRotation().y;
                        selectedWall->setSelected(false);
                        m_fsNewFolderPopup = true;
                    }
                }
            } else {
                // Nothing selected
                if (hasClipboard) {
                    const char* pasteLabel = m_fsClipboardIsCut ? "Move here" : "Paste here";
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%zu file(s) %s", m_fsClipboard.size(),
                                       m_fsClipboardIsCut ? "to move" : "in clipboard");
                    if (ImGui::MenuItem(pasteLabel)) {
                        std::string destDir = m_filesystemBrowser.getCurrentPath();
                        bool anyChanged = false;
                        for (const auto& srcPath : m_fsClipboard) {
                            namespace fs = std::filesystem;
                            fs::path src(srcPath);
                            bool sameDir = fs::equivalent(src.parent_path(), fs::path(destDir));

                            if (sameDir && m_fsClipboardIsCut) {
                                // Cut within same directory — nothing to do on disk
                                continue;
                            }

                            fs::path dst = fs::path(destDir) / src.filename();
                            if (fs::exists(dst)) {
                                std::string stem = dst.stem().string();
                                std::string ext = dst.extension().string();
                                fs::path parent = dst.parent_path();
                                int n = 1;
                                do {
                                    dst = parent / (stem + "_" + std::to_string(n) + ext);
                                    n++;
                                } while (fs::exists(dst));
                            }
                            std::error_code ec;
                            if (m_fsClipboardIsCut) {
                                fs::rename(src, dst, ec);
                            } else if (fs::is_directory(src)) {
                                fs::copy(src, dst, fs::copy_options::recursive, ec);
                            } else {
                                fs::copy_file(src, dst, ec);
                            }
                            if (ec) {
                                std::cerr << "[FS] " << (m_fsClipboardIsCut ? "Move" : "Copy") << " failed: " << srcPath << " -> " << dst.string() << ": " << ec.message() << std::endl;
                            } else {
                                anyChanged = true;
                            }
                        }
                        if (m_fsClipboardIsCut) m_fsClipboard.clear();
                        if (anyChanged) m_filesystemBrowser.navigate(destDir);
                    }
                    ImGui::Separator();
                }
                if (ImGui::MenuItem("New Folder")) {
                    snprintf(m_fsNewFolderName, sizeof(m_fsNewFolderName), "New Folder");
                    m_fsNewFolderOnWall = false;
                    m_fsNewFolderPopup = true;
                }
            }
            ImGui::EndPopup();
        }

        // "New Folder" naming modal
        if (m_fsNewFolderPopup) {
            ImGui::OpenPopup("New Folder##FSNewFolder");
            m_fsNewFolderPopup = false;
        }
        if (ImGui::BeginPopupModal("New Folder##FSNewFolder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            // Keep cursor visible while modal is open
            if (!m_playModeCursorVisible) {
                m_playModeCursorVisible = true;
                Input::setMouseCaptured(false);
            }
            ImGui::Text("Folder name:");
            bool enter = ImGui::InputText("##foldername", m_fsNewFolderName, sizeof(m_fsNewFolderName),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter || ImGui::Button("Create", ImVec2(120, 0))) {
                std::string destDir = m_filesystemBrowser.getCurrentPath();
                namespace fs = std::filesystem;
                fs::path newDir = fs::path(destDir) / m_fsNewFolderName;
                // Auto-rename if exists
                if (fs::exists(newDir)) {
                    std::string base = m_fsNewFolderName;
                    int n = 1;
                    do {
                        newDir = fs::path(destDir) / (base + "_" + std::to_string(n));
                        n++;
                    } while (fs::exists(newDir));
                }
                std::error_code ec;
                fs::create_directory(newDir, ec);
                if (ec) {
                    std::cerr << "[FS] Failed to create folder: " << newDir.string() << ": " << ec.message() << std::endl;
                } else {
                    if (m_fsNewFolderOnWall) {
                        m_filesystemBrowser.spawnFileAtWall(newDir.string(), m_fsNewFolderWallPos, m_fsNewFolderWallScale, m_fsNewFolderWallYaw);
                    } else {
                        m_filesystemBrowser.navigate(destDir);
                    }
                }
                ImGui::CloseCurrentPopup();
                m_playModeCursorVisible = false;
                Input::setMouseCaptured(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                m_playModeCursorVisible = false;
                Input::setMouseCaptured(true);
            }
            ImGui::EndPopup();
        }

        // "Rename" modal
        if (m_fsRenamePopup) {
            ImGui::OpenPopup("Rename##FSRename");
            m_fsRenamePopup = false;
        }
        if (ImGui::BeginPopupModal("Rename##FSRename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!m_playModeCursorVisible) {
                m_playModeCursorVisible = true;
                Input::setMouseCaptured(false);
            }
            ImGui::Text("New name:");
            bool enter = ImGui::InputText("##renamefield", m_fsRenameName, sizeof(m_fsRenameName),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter || ImGui::Button("Rename", ImVec2(120, 0))) {
                namespace fs = std::filesystem;
                fs::path oldPath(m_fsRenameOldPath);
                fs::path newPath = oldPath.parent_path() / m_fsRenameName;
                if (newPath != oldPath) {
                    std::error_code ec;
                    fs::rename(oldPath, newPath, ec);
                    if (ec) {
                        std::cerr << "[FS] Rename failed: " << ec.message() << std::endl;
                    } else {
                        m_filesystemBrowser.navigate(m_filesystemBrowser.getCurrentPath());
                    }
                }
                ImGui::CloseCurrentPopup();
                m_playModeCursorVisible = false;
                Input::setMouseCaptured(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                m_playModeCursorVisible = false;
                Input::setMouseCaptured(true);
            }
            ImGui::EndPopup();
        }
    }

    void renderZoneOverlay() {
        if (!m_editorUI.isZoneOverlayEnabled() || !m_zoneSystem) return;

        VkExtent2D extent = getSwapchain().getExtent();
        float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 5000.0f);
        glm::mat4 vp = proj * view;
        float screenW = static_cast<float>(extent.width);
        float screenH = static_cast<float>(extent.height);
        glm::vec4 viewport(0, 0, screenW, screenH);

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        glm::vec3 camPos = m_camera.getPosition();

        // Only render cells within a reasonable distance from camera (performance)
        float renderDist = 500.0f;
        float cellSize = m_zoneSystem->getCellSize();

        glm::ivec2 camGrid = m_zoneSystem->worldToGrid(camPos.x, camPos.z);
        int cellRange = static_cast<int>(renderDist / cellSize) + 1;

        int minGX = std::max(0, camGrid.x - cellRange);
        int maxGX = std::min(m_zoneSystem->getGridWidth() - 1, camGrid.x + cellRange);
        int minGZ = std::max(0, camGrid.y - cellRange);
        int maxGZ = std::min(m_zoneSystem->getGridHeight() - 1, camGrid.y + cellRange);

        auto projectToScreen = [&](const glm::vec3& worldPos, glm::vec2& screenPos) -> bool {
            glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.001f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            // Vulkan NDC: x [-1,1], y [-1,1] (y points down after proj flip)
            screenPos.x = (ndc.x * 0.5f + 0.5f) * screenW;
            screenPos.y = (ndc.y * -0.5f + 0.5f) * screenH; // Flip Y for Vulkan
            return ndc.z > 0.0f && ndc.z < 1.0f;
        };

        for (int gz = minGZ; gz <= maxGZ; gz++) {
            for (int gx = minGX; gx <= maxGX; gx++) {
                glm::vec2 worldCenter = m_zoneSystem->gridToWorld(gx, gz);
                float half = cellSize * 0.5f;

                // Get cell data
                float wx = worldCenter.x;
                float wz = worldCenter.y;
                const ZoneCell* cell = m_zoneSystem->getCell(wx, wz);
                if (!cell || cell->type == ZoneType::Wilderness) continue;

                // Color by zone type (alpha ~47% for clear visibility)
                ImU32 color;
                switch (cell->type) {
                    case ZoneType::Battlefield: color = IM_COL32(220, 50, 50, 120); break;
                    case ZoneType::SpawnSafe:   color = IM_COL32(50, 220, 50, 120); break;
                    case ZoneType::Residential: color = IM_COL32(50, 100, 220, 120); break;
                    case ZoneType::Commercial:  color = IM_COL32(220, 220, 50, 120); break;
                    case ZoneType::Industrial:  color = IM_COL32(220, 140, 50, 120); break;
                    case ZoneType::Resource:    color = IM_COL32(180, 50, 220, 120); break;
                    default: continue;
                }

                // Get terrain height at corners
                float y00 = m_terrain.getHeightAt(wx - half, wz - half) + 0.3f;
                float y10 = m_terrain.getHeightAt(wx + half, wz - half) + 0.3f;
                float y11 = m_terrain.getHeightAt(wx + half, wz + half) + 0.3f;
                float y01 = m_terrain.getHeightAt(wx - half, wz + half) + 0.3f;

                glm::vec3 corners[4] = {
                    {wx - half, y00, wz - half},
                    {wx + half, y10, wz - half},
                    {wx + half, y11, wz + half},
                    {wx - half, y01, wz + half}
                };

                glm::vec2 screen[4];
                bool allVisible = true;
                for (int i = 0; i < 4; i++) {
                    if (!projectToScreen(corners[i], screen[i])) {
                        allVisible = false;
                        break;
                    }
                }

                if (allVisible) {
                    drawList->AddQuadFilled(
                        ImVec2(screen[0].x, screen[0].y),
                        ImVec2(screen[1].x, screen[1].y),
                        ImVec2(screen[2].x, screen[2].y),
                        ImVec2(screen[3].x, screen[3].y),
                        color
                    );
                }
            }
        }
    }

    // Per-resource unique color based on resource name
    static ImU32 resourceNameColor(const std::string& name) {
        // Hand-picked colors for known resources
        if (name == "Water")             return IM_COL32(30, 100, 200, 255);
        if (name == "Water Ice")         return IM_COL32(140, 200, 240, 255);
        if (name == "Salt Compounds")    return IM_COL32(200, 200, 180, 255);
        if (name == "Marine Biomass")    return IM_COL32(20, 140, 130, 255);
        if (name == "Oxygen")            return IM_COL32(160, 220, 240, 255);
        if (name == "Nitrogen")          return IM_COL32(100, 160, 220, 255);
        if (name == "Hydrogen")          return IM_COL32(220, 220, 140, 255);
        if (name == "Helium")            return IM_COL32(240, 180, 200, 255);
        if (name == "Methane")           return IM_COL32(120, 140, 80, 255);
        if (name == "Ammonia")           return IM_COL32(140, 200, 140, 255);
        if (name == "Carbon Dioxide")    return IM_COL32(160, 160, 160, 255);
        if (name == "Helium-3")          return IM_COL32(220, 100, 220, 255);
        if (name == "Iron")              return IM_COL32(160, 90, 60, 255);
        if (name == "Carbon")            return IM_COL32(80, 80, 80, 255);
        if (name == "Limestone")         return IM_COL32(190, 180, 150, 255);
        if (name == "Silicon")           return IM_COL32(170, 180, 200, 255);
        if (name == "Nickel")            return IM_COL32(140, 160, 130, 255);
        if (name == "Aluminum")          return IM_COL32(180, 190, 210, 255);
        if (name == "Sulfur")            return IM_COL32(220, 210, 50, 255);
        if (name == "Titanium")          return IM_COL32(120, 140, 170, 255);
        if (name == "Silver")            return IM_COL32(210, 210, 220, 255);
        if (name == "Diamond")           return IM_COL32(230, 240, 255, 255);
        if (name == "Platinum")          return IM_COL32(200, 200, 210, 255);
        if (name == "Gold")              return IM_COL32(240, 200, 50, 255);
        if (name == "Uranium")           return IM_COL32(80, 200, 80, 255);
        if (name == "Organic Matter")    return IM_COL32(100, 130, 50, 255);
        if (name == "Wood")              return IM_COL32(60, 140, 40, 255);
        if (name == "Rare Flora")        return IM_COL32(200, 80, 160, 255);
        if (name == "Mineral Deposits")  return IM_COL32(150, 120, 80, 255);
        if (name == "Geothermal Energy") return IM_COL32(240, 120, 30, 255);
        if (name == "Oil")               return IM_COL32(40, 40, 40, 255);
        if (name == "Rare Crystals")     return IM_COL32(180, 80, 240, 255);
        if (name == "Dark Matter")       return IM_COL32(60, 20, 80, 255);
        if (name == "Exotic Matter")     return IM_COL32(240, 40, 180, 255);
        if (name == "Ancient Artifacts") return IM_COL32(200, 160, 60, 255);
        // Fallback: hash-based color
        uint32_t h = 0;
        for (char c : name) h = h * 31 + static_cast<uint8_t>(c);
        return IM_COL32(80 + (h % 160), 80 + ((h >> 8) % 160), 80 + ((h >> 16) % 160), 255);
    }

    // Short label for zone map cells
    static const char* resourceNameLabel(const std::string& name) {
        if (name == "Water")             return "H2O";
        if (name == "Water Ice")         return "Ice";
        if (name == "Salt Compounds")    return "Sal";
        if (name == "Marine Biomass")    return "Mar";
        if (name == "Oxygen")            return "O2";
        if (name == "Nitrogen")          return "N2";
        if (name == "Hydrogen")          return "H2";
        if (name == "Helium")            return "He";
        if (name == "Methane")           return "CH4";
        if (name == "Ammonia")           return "NH3";
        if (name == "Carbon Dioxide")    return "CO2";
        if (name == "Helium-3")          return "He3";
        if (name == "Iron")              return "Fe";
        if (name == "Carbon")            return "C";
        if (name == "Limestone")         return "ite";
        if (name == "Silicon")           return "Si";
        if (name == "Nickel")            return "Ni";
        if (name == "Aluminum")          return "Al";
        if (name == "Sulfur")            return "S";
        if (name == "Titanium")          return "Ti";
        if (name == "Silver")            return "Ag";
        if (name == "Diamond")           return "Dia";
        if (name == "Platinum")          return "Pt";
        if (name == "Gold")              return "Au";
        if (name == "Uranium")           return "U";
        if (name == "Organic Matter")    return "Org";
        if (name == "Wood")              return "Wd";
        if (name == "Rare Flora")        return "Flo";
        if (name == "Mineral Deposits")  return "Min";
        if (name == "Geothermal Energy") return "Geo";
        if (name == "Oil")               return "Oil";
        if (name == "Rare Crystals")     return "Cry";
        if (name == "Dark Matter")       return "DM";
        if (name == "Exotic Matter")     return "EM";
        if (name == "Ancient Artifacts") return "Art";
        if (name.size() >= 2) return name.c_str(); // fallback: full name (truncated by cell)
        return "?";
    }

    void renderZoneMap() {
        if (!m_zoneSystem) return;

        VkExtent2D extent = getSwapchain().getExtent();
        float screenW = static_cast<float>(extent.width);
        float screenH = static_cast<float>(extent.height);

        // Darken background
        ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
        bgDraw->AddRectFilled(ImVec2(0, 0), ImVec2(screenW, screenH), IM_COL32(0, 0, 0, 160));

        // Map window — centered, large
        float mapPad = 40.0f;
        float mapW = screenW - mapPad * 2.0f;
        float mapH = screenH - mapPad * 2.0f - 30.0f; // leave room for title

        ImGui::SetNextWindowPos(ImVec2(mapPad, mapPad), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(mapW + 16, mapH + 60), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.92f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
        if (!ImGui::Begin("Zone Map", &m_showZoneMap, flags)) {
            ImGui::End();
            return;
        }

        // Legend
        auto legendColor = [](ImU32 col, const char* label) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), col);
            ImGui::Dummy(ImVec2(14, 12));
            ImGui::SameLine();
            ImGui::Text("%s", label);
            ImGui::SameLine(0, 16);
        };
        legendColor(IM_COL32(220, 50, 50, 255), "Battlefield");
        legendColor(IM_COL32(50, 220, 50, 255), "Spawn");
        legendColor(IM_COL32(50, 100, 220, 255), "Residential");
        legendColor(IM_COL32(220, 220, 50, 255), "Commercial");
        legendColor(IM_COL32(220, 140, 50, 255), "Industrial");
        legendColor(IM_COL32(180, 50, 220, 255), "Resource");
        legendColor(IM_COL32(60, 60, 60, 255), "Wilderness");
        ImGui::NewLine();

        // Building icon legend
        auto legendDiamond = [](ImU32 col, const char* label) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 c(p.x + 6, p.y + 6);
            ImGui::GetWindowDrawList()->AddQuadFilled(
                ImVec2(c.x, c.y - 5), ImVec2(c.x + 5, c.y),
                ImVec2(c.x, c.y + 5), ImVec2(c.x - 5, c.y), col);
            ImGui::GetWindowDrawList()->AddQuad(
                ImVec2(c.x, c.y - 5), ImVec2(c.x + 5, c.y),
                ImVec2(c.x, c.y + 5), ImVec2(c.x - 5, c.y), IM_COL32(0, 0, 0, 255));
            ImGui::Dummy(ImVec2(14, 12));
            ImGui::SameLine();
            ImGui::Text("%s", label);
            ImGui::SameLine(0, 16);
        };
        ImGui::Text("Buildings:");
        ImGui::SameLine(0, 8);
        legendDiamond(IM_COL32(230, 204, 51, 255), "Housing");
        legendDiamond(IM_COL32(77, 204, 51, 255), "Food");
        legendDiamond(IM_COL32(153, 102, 51, 255), "Resource");
        legendDiamond(IM_COL32(128, 128, 128, 255), "Industry");
        legendDiamond(IM_COL32(51, 128, 204, 255), "Commercial");
        ImGui::NewLine();

        // Zoom controls
        ImGui::Text("Zoom: %.1fx", m_zoneMapZoom);
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) m_zoneMapZoom = std::min(m_zoneMapZoom * 1.5f, 10.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) m_zoneMapZoom = std::max(m_zoneMapZoom / 1.5f, 0.3f);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) { m_zoneMapZoom = 1.0f; m_zoneMapPan = {0, 0}; }
        ImGui::SameLine(0, 20);
        ImGui::TextDisabled("Scroll to zoom, drag to pan, M to close");

        // Map drawing area
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        if (canvasSize.x < 50 || canvasSize.y < 50) { ImGui::End(); return; }

        ImGui::InvisibleButton("zone_map_canvas", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool canvasHovered = ImGui::IsItemHovered();

        // Handle scroll zoom
        if (canvasHovered) {
            float scroll = ImGui::GetIO().MouseWheel;
            if (scroll != 0.0f) {
                float oldZoom = m_zoneMapZoom;
                m_zoneMapZoom *= (scroll > 0) ? 1.2f : (1.0f / 1.2f);
                m_zoneMapZoom = std::max(0.3f, std::min(m_zoneMapZoom, 10.0f));

                // Zoom toward mouse position
                ImVec2 mousePos = ImGui::GetMousePos();
                float mx = (mousePos.x - canvasPos.x - canvasSize.x * 0.5f);
                float my = (mousePos.y - canvasPos.y - canvasSize.y * 0.5f);
                float zoomRatio = m_zoneMapZoom / oldZoom;
                m_zoneMapPan.x = mx - (mx - m_zoneMapPan.x) * zoomRatio;
                m_zoneMapPan.y = my - (my - m_zoneMapPan.y) * zoomRatio;
            }
        }

        // Handle pan drag
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_zoneMapDragging = true;
            m_zoneMapDragStart = {ImGui::GetMousePos().x - m_zoneMapPan.x,
                                  ImGui::GetMousePos().y - m_zoneMapPan.y};
        }
        if (m_zoneMapDragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_zoneMapPan.x = ImGui::GetMousePos().x - m_zoneMapDragStart.x;
                m_zoneMapPan.y = ImGui::GetMousePos().y - m_zoneMapDragStart.y;
            } else {
                m_zoneMapDragging = false;
            }
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        int gridW = m_zoneSystem->getGridWidth();
        int gridH = m_zoneSystem->getGridHeight();

        // Calculate cell size to fit grid in canvas, then apply zoom
        float fitScale = std::min(canvasSize.x / gridW, canvasSize.y / gridH);
        float cellPx = fitScale * m_zoneMapZoom;

        // Total map size in pixels
        float totalW = gridW * cellPx;
        float totalH = gridH * cellPx;

        // Origin: centered in canvas + pan offset
        float originX = canvasPos.x + canvasSize.x * 0.5f - totalW * 0.5f + m_zoneMapPan.x;
        float originY = canvasPos.y + canvasSize.y * 0.5f - totalH * 0.5f + m_zoneMapPan.y;

        // Only draw visible cells (clipping for performance)
        int minGX = std::max(0, static_cast<int>((canvasPos.x - originX) / cellPx));
        int maxGX = std::min(gridW - 1, static_cast<int>((canvasPos.x + canvasSize.x - originX) / cellPx));
        int minGZ = std::max(0, static_cast<int>((canvasPos.y - originY) / cellPx));
        int maxGZ = std::min(gridH - 1, static_cast<int>((canvasPos.y + canvasSize.y - originY) / cellPx));

        // Draw grid background
        drawList->AddRectFilled(
            ImVec2(originX, originY),
            ImVec2(originX + totalW, originY + totalH),
            IM_COL32(35, 40, 35, 255));

        // Draw zone cells
        for (int gz = minGZ; gz <= maxGZ; gz++) {
            for (int gx = minGX; gx <= maxGX; gx++) {
                glm::vec2 wc = m_zoneSystem->gridToWorld(gx, gz);
                const ZoneCell* cell = m_zoneSystem->getCell(wc.x, wc.y);
                if (!cell) continue;

                ImU32 color;
                switch (cell->type) {
                    case ZoneType::Wilderness:  color = IM_COL32(45, 55, 45, 255); break;
                    case ZoneType::Battlefield: color = IM_COL32(180, 40, 40, 255); break;
                    case ZoneType::SpawnSafe:   color = IM_COL32(40, 180, 40, 255); break;
                    case ZoneType::Residential: color = IM_COL32(40, 80, 180, 255); break;
                    case ZoneType::Commercial:  color = IM_COL32(180, 180, 40, 255); break;
                    case ZoneType::Industrial:  color = IM_COL32(180, 110, 40, 255); break;
                    case ZoneType::Resource: {
                        // Tint by individual resource name (unique color per resource)
                        color = resourceNameColor(cell->resourceName);
                        break;
                    }
                    default: color = IM_COL32(45, 55, 45, 255); break;
                }

                float x0 = originX + gx * cellPx;
                float y0 = originY + gz * cellPx;
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cellPx, y0 + cellPx), color);

                // Grid lines at higher zoom levels
                if (cellPx >= 4.0f) {
                    drawList->AddRect(ImVec2(x0, y0), ImVec2(x0 + cellPx, y0 + cellPx),
                                      IM_COL32(0, 0, 0, 40));
                }
            }
        }

        // Draw resource labels at high zoom
        if (cellPx >= 16.0f) {
            for (int gz = minGZ; gz <= maxGZ; gz++) {
                for (int gx = minGX; gx <= maxGX; gx++) {
                    glm::vec2 wc = m_zoneSystem->gridToWorld(gx, gz);
                    const ZoneCell* cell = m_zoneSystem->getCell(wc.x, wc.y);
                    if (!cell || cell->resource == ResourceType::None) continue;

                    float x0 = originX + gx * cellPx;
                    float y0 = originY + gz * cellPx;
                    const char* label = resourceNameLabel(cell->resourceName);
                    ImVec2 textSize = ImGui::CalcTextSize(label);
                    drawList->AddText(ImVec2(x0 + (cellPx - textSize.x) * 0.5f,
                                             y0 + (cellPx - textSize.y) * 0.5f),
                                      IM_COL32(255, 255, 255, 200), label);
                }
            }
        }

        // Draw building icons as colored diamonds
        {
            auto bldgDiamondColor = [](const std::string& btype) -> ImU32 {
                const ::CityBuildingDef* d = ::findCityBuildingDef(btype);
                if (!d) return IM_COL32(200, 200, 200, 255);
                const std::string& cat = d->category;
                if (cat == "housing")    return IM_COL32(230, 204, 51, 255);
                if (cat == "food")       return IM_COL32(77, 204, 51, 255);
                if (cat == "resource")   return IM_COL32(153, 102, 51, 255);
                if (cat == "industry")   return IM_COL32(128, 128, 128, 255);
                if (cat == "commercial") return IM_COL32(51, 128, 204, 255);
                return IM_COL32(200, 200, 200, 255);
            };

            auto bldgLabel = [](const std::string& btype) -> const char* {
                if (btype == "shack")       return "S";
                if (btype == "farm")        return "F";
                if (btype == "lumber_mill") return "L";
                if (btype == "quarry")      return "Q";
                if (btype == "mine")        return "M";
                if (btype == "workshop")    return "W";
                if (btype == "market")      return "Mk";
                if (btype == "warehouse")   return "Wh";
                return "?";
            };

            for (auto& obj : m_sceneObjects) {
                const std::string& bt = obj->getBuildingType();
                if (bt.empty() || bt.substr(0, 10) == "worker_at_") continue;

                glm::vec3 pos = obj->getTransform().getPosition();
                glm::ivec2 gp = m_zoneSystem->worldToGrid(pos.x, pos.z);
                float cx = originX + (gp.x + 0.5f) * cellPx;
                float cy = originY + (gp.y + 0.5f) * cellPx;

                // Skip if off-screen
                if (cx < canvasPos.x - 10 || cx > canvasPos.x + canvasSize.x + 10 ||
                    cy < canvasPos.y - 10 || cy > canvasPos.y + canvasSize.y + 10) continue;

                float ds = std::max(3.0f, cellPx * 0.4f); // diamond half-size
                ImU32 col = bldgDiamondColor(bt);

                drawList->AddQuadFilled(
                    ImVec2(cx, cy - ds), ImVec2(cx + ds, cy),
                    ImVec2(cx, cy + ds), ImVec2(cx - ds, cy), col);
                drawList->AddQuad(
                    ImVec2(cx, cy - ds), ImVec2(cx + ds, cy),
                    ImVec2(cx, cy + ds), ImVec2(cx - ds, cy), IM_COL32(0, 0, 0, 255));

                // Text label at higher zoom
                if (cellPx >= 24.0f) {
                    const char* lbl = bldgLabel(bt);
                    ImVec2 ts = ImGui::CalcTextSize(lbl);
                    drawList->AddText(ImVec2(cx - ts.x * 0.5f, cy + ds + 1.0f),
                                      IM_COL32(255, 255, 255, 220), lbl);
                }
            }
        }

        // Draw player position marker
        glm::vec3 camPos = m_camera.getPosition();
        glm::ivec2 playerGrid = m_zoneSystem->worldToGrid(camPos.x, camPos.z);
        float px = originX + (playerGrid.x + 0.5f) * cellPx;
        float py = originY + (playerGrid.y + 0.5f) * cellPx;
        float markerSize = std::max(4.0f, cellPx * 0.6f);

        // Player triangle (pointing in camera direction)
        float yaw = glm::radians(m_camera.getYaw());
        // Map: +X is right, +Z is down, yaw 0 = facing +X
        float dirX = std::cos(yaw);
        float dirZ = -std::sin(yaw); // Negate because screen Y is inverted
        float perpX = -dirZ;
        float perpZ = dirX;

        ImVec2 tip(px + dirX * markerSize, py + dirZ * markerSize);
        ImVec2 left(px - dirX * markerSize * 0.4f + perpX * markerSize * 0.5f,
                     py - dirZ * markerSize * 0.4f + perpZ * markerSize * 0.5f);
        ImVec2 right(px - dirX * markerSize * 0.4f - perpX * markerSize * 0.5f,
                      py - dirZ * markerSize * 0.4f - perpZ * markerSize * 0.5f);
        drawList->AddTriangleFilled(tip, left, right, IM_COL32(255, 255, 255, 255));
        drawList->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 255), 2.0f);

        // Tooltip on hover
        if (canvasHovered) {
            ImVec2 mousePos = ImGui::GetMousePos();
            int hoverGX = static_cast<int>((mousePos.x - originX) / cellPx);
            int hoverGZ = static_cast<int>((mousePos.y - originY) / cellPx);
            if (hoverGX >= 0 && hoverGX < gridW && hoverGZ >= 0 && hoverGZ < gridH) {
                glm::vec2 wc = m_zoneSystem->gridToWorld(hoverGX, hoverGZ);
                const ZoneCell* cell = m_zoneSystem->getCell(wc.x, wc.y);
                if (cell) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Grid: %d, %d", hoverGX, hoverGZ);
                    ImGui::Text("World: %.0f, %.0f", wc.x, wc.y);
                    ImGui::Text("Zone: %s", ZoneSystem::zoneTypeName(cell->type));
                    if (cell->resource != ResourceType::None) {
                        if (!cell->resourceName.empty())
                            ImGui::Text("Resource: %s (%.0f%%)",
                                        cell->resourceName.c_str(),
                                        cell->resourceDensity * 100.0f);
                        else
                            ImGui::Text("Resource: %s (%.0f%%)",
                                        ZoneSystem::resourceTypeName(cell->resource),
                                        cell->resourceDensity * 100.0f);
                    }
                    if (cell->ownerPlayerId != 0)
                        ImGui::Text("Owner: Player %u", cell->ownerPlayerId);
                    ImGui::Text("Price: $%.0f", cell->purchasePrice);

                    // List buildings in this grid cell
                    bool headerShown = false;
                    for (auto& bobj : m_sceneObjects) {
                        const std::string& bt = bobj->getBuildingType();
                        if (bt.empty() || bt.substr(0, 10) == "worker_at_") continue;
                        glm::vec3 bpos = bobj->getTransform().getPosition();
                        glm::ivec2 bgp = m_zoneSystem->worldToGrid(bpos.x, bpos.z);
                        if (bgp.x == hoverGX && bgp.y == hoverGZ) {
                            if (!headerShown) {
                                ImGui::Separator();
                                ImGui::Text("Buildings:");
                                headerShown = true;
                            }
                            const ::CityBuildingDef* bd = ::findCityBuildingDef(bt);
                            ImGui::Text("  %s (%s)", bobj->getName().c_str(),
                                        bd ? bd->name.c_str() : bt.c_str());
                        }
                    }
                    ImGui::EndTooltip();
                }
            }
        }

        drawList->PopClipRect();
        ImGui::End();
    }

    void renderModulePanel() {
        if (!m_showModulePanel) return;

        ImGui::SetNextWindowPos(ImVec2(getWindow().getWidth() / 2 - 150, 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Game Modules", &m_showModulePanel)) {
            ImGui::Text("Load a game module to enable");
            ImGui::Text("game-specific UI during play mode.");
            ImGui::Separator();

            // Current module status
            if (m_gameModule) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Loaded: %s", m_gameModule->getName());
                ImGui::Text("%s", m_gameModule->getStatusMessage().c_str());

                if (ImGui::Button("Unload Module")) {
                    m_gameModule->shutdown();
                    m_gameModule.reset();
                }
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "No module loaded");
            }

            ImGui::Separator();
            ImGui::Text("Available Modules:");

            // List available modules
            auto modules = eden::GameModuleFactory::getAvailableModules();
            for (const auto& moduleName : modules) {
                bool isLoaded = m_gameModule && std::string(m_gameModule->getName()) == moduleName;

                if (isLoaded) {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  %s (loaded)", moduleName.c_str());
                } else {
                    if (ImGui::Button(moduleName.c_str())) {
                        // Unload current module if any
                        if (m_gameModule) {
                            m_gameModule->shutdown();
                            m_gameModule.reset();
                        }

                        // Load new module
                        m_gameModule = eden::GameModuleFactory::create(moduleName);
                        if (m_gameModule) {
                            m_gameModule->initialize();
                            std::cout << "Loaded game module: " << moduleName << std::endl;
                        }
                    }
                }
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Press M to toggle this panel");
        }
        ImGui::End();
    }

    void renderTradingUI() {
        if (m_modelTraders.empty()) return;

        // Trader status panel (bottom left)
        ImGui::SetNextWindowPos(ImVec2(10, getWindow().getHeight() - 120));
        ImGui::SetNextWindowBgAlpha(0.7f);
        if (ImGui::Begin("##TraderStatus", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings)) {

            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "MODEL TRADERS: %zu", m_modelTraders.size());
            ImGui::Separator();

            if (ImGui::Button("Trading Panel")) {
                m_showTraderPanel = !m_showTraderPanel;
            }
            ImGui::SameLine();
            if (ImGui::Button("Economy")) {
                m_showEconomyPanel = !m_showEconomyPanel;
            }
        }
        ImGui::End();

        // Detailed trading panel
        if (m_showTraderPanel) {
            renderTraderPanel();
        }

        // Economy overview panel
        if (m_showEconomyPanel) {
            renderEconomyPanel();
        }
    }

    void renderTraderPanel() {
        ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Trading", &m_showTraderPanel)) {
            if (m_modelTraders.empty()) {
                ImGui::Text("No traders - add 'trader' script to a model");
                ImGui::End();
                return;
            }

            // Model traders
            if (ImGui::CollapsingHeader("Model Traders", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto& trader : m_modelTraders) {
                    if (!trader) continue;
                    ImGui::PushID(trader.get());

                    // Trader name and state
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", trader->getName().c_str());
                    ImGui::Text("  Credits: $%.0f | State: %s",
                        trader->getCredits(),
                        trader->getStateName());

                    // Cargo
                    const auto& cargo = trader->getCargo();
                    if (!cargo.empty()) {
                        for (const auto& item : cargo) {
                            ImGui::Text("  Cargo: %s x%.1f",
                                EconomySystem::getGoodName(item.good),
                                item.quantity);
                        }
                    }

                    ImGui::Separator();
                    ImGui::PopID();
                }
            }

            // Trade opportunities (from first trader)
            if (!m_modelTraders.empty() && ImGui::CollapsingHeader("Trade Opportunities")) {
                auto opportunities = m_modelTraders[0]->findBestTrades(5);
                if (opportunities.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No opportunities found");
                } else {
                    for (const auto& opp : opportunities) {
                        ImGui::PushID(&opp);
                        ImVec4 color = opp.profitMargin > 0.2f ?
                            ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
                        ImGui::TextColored(color, "%s: +%.0f%% margin",
                            EconomySystem::getGoodName(opp.good),
                            opp.profitMargin * 100.0f);
                        ImGui::Text("  Buy: $%.1f -> Sell: $%.1f", opp.buyPrice, opp.sellPrice);
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::End();
    }

    void renderEconomyPanel() {
        ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Economy", &m_showEconomyPanel)) {
            if (!m_economySystem) {
                ImGui::Text("Economy not initialized");
                ImGui::End();
                return;
            }

            // Market prices
            if (ImGui::CollapsingHeader("Market Prices", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int i = 0; i < static_cast<int>(GoodType::COUNT); i++) {
                    GoodType good = static_cast<GoodType>(i);
                    const auto& market = m_economySystem->getMarket(good);
                    float ratio = m_economySystem->getSupplyDemandRatio(good);

                    ImVec4 color;
                    if (ratio < 0.5f) color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);      // Shortage (red)
                    else if (ratio > 2.0f) color = ImVec4(0.3f, 0.8f, 1.0f, 1.0f); // Surplus (blue)
                    else color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);                    // Normal

                    ImGui::TextColored(color, "%-12s $%6.1f  (S/D: %.2f)",
                        EconomySystem::getGoodName(good),
                        market.currentPrice,
                        ratio);
                }
            }

            // City stats
            if (m_cityGovernor && ImGui::CollapsingHeader("City Stats")) {
                const auto& stats = m_cityGovernor->getStats();
                ImGui::Text("Population: %d / %d housing", stats.population, stats.housingCapacity);
                ImGui::Text("Employment: %d / %d unemployed", stats.employed, stats.unemployed);
                ImGui::Text("Happiness: %.0f%%", stats.overallHappiness);
                ImGui::Text("Treasury: $%.0f", stats.treasury);
                ImGui::Text("Tech Level: %s", CityGovernor::getTechLevelName(stats.currentTech));
                ImGui::Text("Buildings: %zu", m_cityGovernor->getBuildings().size());
            }
        }
        ImGui::End();
    }

    void loadEditorConfig() {
        // Load EditorUI config (window visibility, brush settings)
        m_editorUI.loadConfig("editor_ui_config.json");

        std::ifstream file("editor_config.json");
        if (!file.is_open()) return;

        try {
            nlohmann::json config;
            file >> config;

            if (config.contains("camera_speed")) {
                m_camera.setSpeed(config["camera_speed"].get<float>());
            }
        } catch (...) {
        }
    }

    void saveEditorConfig() {
        // Save EditorUI config (window visibility, brush settings)
        m_editorUI.saveConfig("editor_ui_config.json");

        nlohmann::json config;
        config["camera_speed"] = m_cameraSpeed;

        std::ofstream file("editor_config.json");
        if (file.is_open()) {
            file << config.dump(2);
        }
    }

    std::string getLevelsDirectory() {
        // Get absolute path to levels directory
        std::filesystem::path exePath = std::filesystem::current_path();
        std::filesystem::path levelsPath = exePath / "levels";

        // Create if it doesn't exist
        if (!std::filesystem::exists(levelsPath)) {
            std::filesystem::create_directories(levelsPath);
        }

        return levelsPath.string();
    }

    void showSaveDialog() {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = {{"EDEN Level", "eden"}};

        // Default to levels directory (absolute path), or current level path if already set
        std::string levelsDir = getLevelsDirectory();
        const char* defaultDir = m_currentLevelPath.empty() ? levelsDir.c_str() : nullptr;
        const char* defaultPath = m_currentLevelPath.empty() ? nullptr : m_currentLevelPath.c_str();
        nfdresult_t result = NFD_SaveDialog(&outPath, filters, 1, defaultDir, defaultPath);

        if (result == NFD_OKAY) {
            std::string path = outPath;
            if (path.find(".eden") == std::string::npos) {
                path += ".eden";
            }
            saveLevel(path);
            NFD_FreePath(outPath);
        }
    }

    void showLoadDialog() {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = {{"EDEN Level", "eden"}};

        // Default to levels directory (absolute path)
        std::string levelsDir = getLevelsDirectory();
        nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, levelsDir.c_str());

        if (result == NFD_OKAY) {
            loadLevel(outPath);
            preloadAdjacentLevels();  // Preload levels linked by doors
            NFD_FreePath(outPath);
        }
    }

    void showModelImportDialog() {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filters[3] = {
            {"LIME Model", "lime"},
            {"GLB Model", "glb"},
            {"GLTF Model", "gltf"}
        };

        // Default to models directory
        std::filesystem::path modelsPath = std::filesystem::current_path() / "models";
        std::string modelsDir = modelsPath.string();

        nfdresult_t result = NFD_OpenDialog(&outPath, filters, 3, modelsDir.c_str());

        if (result == NFD_OKAY) {
            importModel(outPath);
            NFD_FreePath(outPath);
        }
    }

    void saveLevel(const std::string& filepath) {
        // Remove runtime-only filesystem objects before saving
        m_filesystemBrowser.clearFilesystemObjects();

        // Remove player avatar before saving so it doesn't persist in the level file
        if (m_playerAvatar) {
            for (auto it = m_sceneObjects.begin(); it != m_sceneObjects.end(); ++it) {
                if (it->get() == m_playerAvatar) {
                    m_sceneObjects.erase(it);
                    break;
                }
            }
            m_playerAvatar = nullptr;
        }

        if (m_hasSpawnPoint && m_spawnObjectIndex >= 0 && m_spawnObjectIndex < static_cast<int>(m_sceneObjects.size())) {
            m_spawnPosition = m_sceneObjects[m_spawnObjectIndex]->getTransform().getPosition();
        }

        glm::vec3 saveSpawnPos = m_hasSpawnPoint ? m_spawnPosition : m_camera.getPosition();

        bool success = LevelSerializer::save(
            filepath,
            m_terrain,
            m_sceneObjects,
            m_actionSystem,
            m_aiNodes,
            m_editorUI.getWaterLevel(),
            m_editorUI.getWaterVisible(),
            saveSpawnPos,
            m_skybox->getParameters(),
            m_camera.getPosition(),
            m_camera.getYaw(),
            m_camera.getPitch(),
            m_isTestLevel,
            m_isSpaceLevel,
            static_cast<int>(m_editorUI.getPhysicsBackend()),
            m_gameModule ? m_gameModule->getName() : ""
        );

        if (success) {
            // Append zone data and group data to the saved JSON file
            try {
                std::ifstream inFile(filepath);
                nlohmann::json root = nlohmann::json::parse(inFile);
                inFile.close();

                if (m_zoneSystem) {
                    m_zoneSystem->save(root);
                }

                // Sync group state from EditorUI (it tracks expanded/collapsed)
                m_objectGroups = m_editorUI.getObjectGroups();

                // Save object groups (map indices to object names for stability across reloads)
                if (!m_objectGroups.empty()) {
                    nlohmann::json groupsJson = nlohmann::json::array();
                    for (const auto& group : m_objectGroups) {
                        nlohmann::json g;
                        g["name"] = group.name;
                        g["expanded"] = group.expanded;
                        nlohmann::json members = nlohmann::json::array();
                        for (int idx : group.objectIndices) {
                            if (idx >= 0 && idx < static_cast<int>(m_sceneObjects.size())) {
                                members.push_back(m_sceneObjects[idx]->getName());
                            }
                        }
                        g["objects"] = members;
                        groupsJson.push_back(g);
                    }
                    root["objectGroups"] = groupsJson;
                }

                std::ofstream outFile(filepath);
                outFile << root.dump(2);
                outFile.close();
            } catch (const std::exception& e) {
                std::cerr << "Failed to save extra data: " << e.what() << std::endl;
            }

            m_currentLevelPath = filepath;
            std::cout << "Level saved to: " << filepath << std::endl;

            // Also save binary format for fast loading
            saveBinaryLevel(filepath);
        } else {
            std::cerr << "Failed to save level: " << LevelSerializer::getLastError() << std::endl;
        }
    }

    void saveBinaryLevel(const std::string& edenPath) {
        std::string binPath = BinaryLevelReader::getBinaryPath(edenPath);

        BinaryLevelWriter writer;

        // Add all scene objects and their mesh data
        for (const auto& obj : m_sceneObjects) {
            // Skip skinned models for now (V1 limitation - they'll load from GLB)
            if (obj->isSkinned()) {
                // Add skinned object with no mesh (it will fallback to GLB loading)
                writer.addObject(*obj, -1, obj->getModelPath());
                continue;
            }

            int32_t meshId = -1;

            // Check if object has mesh data stored
            if (obj->hasMeshData()) {
                const unsigned char* texData = nullptr;
                int texW = 0, texH = 0;

                if (obj->hasTextureData()) {
                    texData = obj->getTextureData().data();
                    texW = obj->getTextureWidth();
                    texH = obj->getTextureHeight();
                }

                meshId = writer.addMesh(
                    obj->getVertices(),
                    obj->getIndices(),
                    obj->getLocalBounds(),
                    texData, texW, texH
                );
            }

            writer.addObject(*obj, meshId, obj->getModelPath());
        }

        if (writer.write(binPath)) {
            std::cout << "Binary level saved to: " << binPath << std::endl;
        } else {
            std::cerr << "Failed to save binary level" << std::endl;
        }
    }

    // Try to load objects from binary level file
    // Returns true and populates m_sceneObjects if successful
    bool tryLoadBinaryObjects(const std::string& filepath, const LevelData& levelData) {
        std::string binPath = BinaryLevelReader::getBinaryPath(filepath);

        if (!BinaryLevelReader::exists(binPath)) {
            return false;
        }

        BinaryLevelReader reader;
        BinaryLevelData binData = reader.load(binPath);

        if (!binData.success) {
            std::cerr << "Binary level load failed: " << binData.error << std::endl;
            return false;
        }

        // Verify object counts match
        if (binData.objects.size() != levelData.objects.size()) {
            std::cerr << "Binary/JSON object count mismatch, falling back to JSON" << std::endl;
            return false;
        }

        std::cout << "Loading from binary format (" << binPath << ")" << std::endl;

        // Load each object from binary data
        for (size_t i = 0; i < binData.objects.size(); ++i) {
            const auto& binObj = binData.objects[i];
            const auto& jsonObj = levelData.objects[i];

            std::unique_ptr<SceneObject> obj;

            // Skinned models still need GLB loading
            if (binObj.isSkinned) {
                auto result = SkinnedGLBLoader::load(binObj.modelPath);
                if (!result.success || result.meshes.empty()) {
                    std::cerr << "Failed to load skinned model: " << binObj.modelPath << std::endl;
                    continue;
                }

                const auto& mesh = result.meshes[0];
                uint32_t handle = m_skinnedModelRenderer->createModel(
                    mesh.vertices,
                    mesh.indices,
                    std::make_unique<Skeleton>(*result.skeleton),
                    result.animations,
                    mesh.hasTexture ? mesh.textureData.data() : nullptr,
                    mesh.textureWidth,
                    mesh.textureHeight
                );

                obj = std::make_unique<SceneObject>(mesh.name);
                obj->setSkinnedModelHandle(handle);
                obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));

                auto animNames = m_skinnedModelRenderer->getAnimationNames(handle);
                obj->setAnimationNames(animNames);
                if (!jsonObj.currentAnimation.empty()) {
                    m_skinnedModelRenderer->playAnimation(handle, jsonObj.currentAnimation, true);
                    obj->setCurrentAnimation(jsonObj.currentAnimation);
                } else if (!animNames.empty()) {
                    m_skinnedModelRenderer->playAnimation(handle, animNames[0], true);
                    obj->setCurrentAnimation(animNames[0]);
                }
            }
            // Objects with mesh data in binary
            else if (binObj.meshId >= 0 && binObj.meshId < static_cast<int32_t>(binData.meshes.size())) {
                const auto& meshData = binData.meshes[binObj.meshId];

                obj = std::make_unique<SceneObject>(binObj.name);

                // Upload mesh to GPU with optional texture
                const unsigned char* texData = nullptr;
                int texW = 0, texH = 0;
                if (meshData.textureId >= 0 && meshData.textureId < static_cast<int32_t>(binData.textures.size())) {
                    const auto& tex = binData.textures[meshData.textureId];
                    texData = tex.pixels.data();
                    texW = tex.width;
                    texH = tex.height;
                }

                uint32_t handle = m_modelRenderer->createModel(
                    meshData.vertices,
                    meshData.indices,
                    texData, texW, texH
                );

                obj->setBufferHandle(handle);
                obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
                obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
                obj->setLocalBounds(meshData.bounds);

                // Store mesh data for raycasting
                obj->setMeshData(meshData.vertices, meshData.indices);

                // Store texture data for painting
                if (texData && texW > 0 && texH > 0) {
                    const auto& tex = binData.textures[meshData.textureId];
                    obj->setTextureData(tex.pixels, texW, texH);
                } else {
                    // Create default white texture
                    std::vector<unsigned char> defaultTex(256 * 256 * 4, 255);
                    obj->setTextureData(defaultTex, 256, 256);
                }
            }
            // Fallback: no mesh in binary, try loading from model path or generate primitive
            else if (binObj.isPrimitive) {
                PrimitiveType primType = static_cast<PrimitiveType>(binObj.primitiveType);
                PrimitiveMeshBuilder::MeshData meshData;

                switch (primType) {
                    case PrimitiveType::Cube:
                        meshData = PrimitiveMeshBuilder::createCube(binObj.primitiveSize, binObj.primitiveColor);
                        break;
                    case PrimitiveType::Cylinder:
                        meshData = PrimitiveMeshBuilder::createCylinder(
                            binObj.primitiveRadius, binObj.primitiveHeight,
                            binObj.primitiveSegments, binObj.primitiveColor);
                        break;
                    case PrimitiveType::SpawnMarker:
                        meshData = PrimitiveMeshBuilder::createSpawnMarker(binObj.primitiveSize);
                        break;
                    case PrimitiveType::Door:
                        meshData = PrimitiveMeshBuilder::createCube(binObj.primitiveSize, binObj.primitiveColor);
                        break;
                    default:
                        std::cerr << "Unknown primitive type in binary: " << binObj.primitiveType << std::endl;
                        continue;
                }

                obj = std::make_unique<SceneObject>(binObj.name);
                uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
                obj->setBufferHandle(handle);
                obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
                obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
                obj->setLocalBounds(meshData.bounds);
                obj->setMeshData(meshData.vertices, meshData.indices);
            }
            else if (!binObj.modelPath.empty()) {
                // Load from GLB as fallback
                auto result = GLBLoader::load(binObj.modelPath);
                if (!result.success || result.meshes.empty()) {
                    std::cerr << "Failed to load model: " << binObj.modelPath << std::endl;
                    continue;
                }
                const auto& mesh = result.meshes[0];
                obj = GLBLoader::createSceneObject(mesh, *m_modelRenderer);
                if (!obj) continue;
            }
            else {
                continue;  // Skip invalid objects
            }

            // Apply properties from binary
            obj->setModelPath(binObj.modelPath);
            obj->getTransform().setPosition(binObj.position);
            obj->setEulerRotation(binObj.rotation);
            obj->getTransform().setScale(binObj.scale);
            obj->setHueShift(binObj.hueShift);
            obj->setSaturation(binObj.saturation);
            obj->setBrightness(binObj.brightness);
            obj->setVisible(binObj.visible);
            obj->setAABBCollision(binObj.aabbCollision);
            obj->setPolygonCollision(binObj.polygonCollision);
            obj->setBulletCollisionType(static_cast<BulletCollisionType>(binObj.bulletCollisionType));
            obj->setKinematicPlatform(binObj.kinematicPlatform);
            obj->setBeingType(static_cast<BeingType>(binObj.beingType));
            obj->setDailySchedule(binObj.dailySchedule);
            obj->setPatrolSpeed(binObj.patrolSpeed);
            if (!binObj.description.empty()) {
                obj->setDescription(binObj.description);
            }
            if (!binObj.buildingType.empty()) {
                obj->setBuildingType(binObj.buildingType);
            }

            // Primitive properties
            if (binObj.isPrimitive) {
                obj->setPrimitiveType(static_cast<PrimitiveType>(binObj.primitiveType));
                obj->setPrimitiveSize(binObj.primitiveSize);
                obj->setPrimitiveRadius(binObj.primitiveRadius);
                obj->setPrimitiveHeight(binObj.primitiveHeight);
                obj->setPrimitiveSegments(binObj.primitiveSegments);
                obj->setPrimitiveColor(binObj.primitiveColor);
            }

            // Door properties
            if (binObj.isDoor) {
                obj->setDoorId(binObj.doorId);
                obj->setTargetLevel(binObj.targetLevel);
                obj->setTargetDoorId(binObj.targetDoorId);
            }

            // Frozen transform (already baked in binary, just record it)
            if (binObj.hasFrozenTransform) {
                obj->setFrozenTransform(binObj.frozenRotation, binObj.frozenScale);
            }

            // Add to physics world
            if (obj->hasBulletCollision() && m_physicsWorld) {
                m_physicsWorld->addObject(obj.get(), obj->getBulletCollisionType());
            }

            // Restore behaviors from JSON (not in binary)
            for (const auto& behData : jsonObj.behaviors) {
                Behavior behavior;
                behavior.name = behData.name;
                behavior.trigger = static_cast<TriggerType>(behData.trigger);
                behavior.triggerParam = behData.triggerParam;
                behavior.triggerRadius = behData.triggerRadius;
                behavior.loop = behData.loop;
                behavior.enabled = behData.enabled;

                for (const auto& actData : behData.actions) {
                    Action action;
                    action.type = static_cast<ActionType>(actData.type);
                    action.vec3Param = actData.vec3Param;
                    action.floatParam = actData.floatParam;
                    action.stringParam = actData.stringParam;
                    action.animationParam = actData.animationParam;
                    action.boolParam = actData.boolParam;
                    action.easing = static_cast<Action::Easing>(actData.easing);
                    action.duration = actData.duration;
                    behavior.actions.push_back(action);
                }
                obj->addBehavior(behavior);
            }

            m_sceneObjects.push_back(std::move(obj));
        }

        return true;
    }

    // Load expression textures from assets/textures/expressions/<npcName>/
    // Each .png in the folder becomes a named expression (filename without extension)
    void loadExpressionsForNPC(SceneObject* obj) {
        std::string npcName = obj->getName();
        // Convert to lowercase for folder lookup
        std::string folderName = npcName;
        std::transform(folderName.begin(), folderName.end(), folderName.begin(), ::tolower);

        std::string exprDir = "textures/expressions/" + folderName + "/";

        // Check if directory exists
        DIR* dir = opendir(exprDir.c_str());
        if (!dir) return;

        std::cout << "[Expressions] Loading expressions for " << npcName << " from " << exprDir << std::endl;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            // Only load .png files
            if (filename.size() < 5 || filename.substr(filename.size() - 4) != ".png") continue;

            std::string filepath = exprDir + filename;
            std::string exprName = filename.substr(0, filename.size() - 4);  // strip .png

            int w, h, channels;
            unsigned char* pixels = stbi_load(filepath.c_str(), &w, &h, &channels, STBI_rgb_alpha);
            if (!pixels) {
                std::cerr << "[Expressions] Failed to load: " << filepath << std::endl;
                continue;
            }

            std::vector<unsigned char> pixelData(pixels, pixels + w * h * 4);
            obj->addExpression(exprName, pixelData, w, h);
            stbi_image_free(pixels);

            std::cout << "[Expressions]   Loaded '" << exprName << "' (" << w << "x" << h << ")" << std::endl;
        }
        closedir(dir);

        std::cout << "[Expressions] " << npcName << ": " << obj->getExpressionCount() << " expressions loaded" << std::endl;

        // Set neutral as default if available
        if (obj->getExpressionCount() > 0) {
            obj->setExpressionByName("neutral");
            if (obj->getCurrentExpression() >= 0) {
                auto& tex = obj->getTextureData();
                m_modelRenderer->updateTexture(obj->getBufferHandle(), tex.data(), obj->getTextureWidth(), obj->getTextureHeight());
            }
        }
    }

    void loadLevel(const std::string& filepath) {
        LevelData levelData;

        // Check if level is already cached (preloaded)
        auto cacheIt = m_levelCache.find(filepath);
        if (cacheIt != m_levelCache.end()) {
            levelData = std::move(cacheIt->second);
            m_levelCache.erase(cacheIt);  // Remove from cache after use
        } else {
            // Load from disk
            if (!LevelSerializer::load(filepath, levelData)) {
                std::cerr << "Failed to load level: " << LevelSerializer::getLastError() << std::endl;
                return;
            }
        }

        LevelSerializer::applyToTerrain(levelData, m_terrain);

        // Clear physics world before loading new objects
        if (m_physicsWorld) {
            m_physicsWorld->clear();
        }

        m_sceneObjects.clear();
        m_terminalScreenObject = nullptr;  // Will re-bind after load
        m_terminalScreenBound = false;

        // Try binary loading first for fast load
        if (tryLoadBinaryObjects(filepath, levelData)) {
            // Binary loading succeeded, skip JSON object loop
            // (behaviors were already applied in tryLoadBinaryObjects)
        } else {
            // Fall back to JSON + GLB loading
            for (const auto& objData : levelData.objects) {
                // Skip objects with no model path AND no primitive type
                if (objData.modelPath.empty() && objData.primitiveType == 0) continue;

                std::unique_ptr<SceneObject> obj;

                // Check if this is a primitive object
                if (objData.primitiveType != 0) {
                    PrimitiveType primType = static_cast<PrimitiveType>(objData.primitiveType);
                    PrimitiveMeshBuilder::MeshData meshData;

                    switch (primType) {
                        case PrimitiveType::Cube:
                            meshData = PrimitiveMeshBuilder::createCube(objData.primitiveSize, objData.primitiveColor);
                            break;
                        case PrimitiveType::Cylinder:
                            meshData = PrimitiveMeshBuilder::createCylinder(
                                objData.primitiveRadius, objData.primitiveHeight,
                                objData.primitiveSegments, objData.primitiveColor);
                            break;
                        case PrimitiveType::SpawnMarker:
                            meshData = PrimitiveMeshBuilder::createSpawnMarker(objData.primitiveSize);
                            break;
                        case PrimitiveType::Door:
                            meshData = PrimitiveMeshBuilder::createCube(objData.primitiveSize, objData.primitiveColor);
                            break;
                        default:
                            std::cerr << "Unknown primitive type: " << objData.primitiveType << std::endl;
                            continue;
                    }

                    std::string name = objData.name.empty() ? "Primitive" : objData.name;
                    obj = std::make_unique<SceneObject>(name);
                    uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
                    obj->setBufferHandle(handle);
                    obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
                    obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
                    obj->setLocalBounds(meshData.bounds);

                    // Store primitive info for re-saving
                    obj->setPrimitiveType(primType);
                    obj->setPrimitiveSize(objData.primitiveSize);
                    obj->setPrimitiveRadius(objData.primitiveRadius);
                    obj->setPrimitiveHeight(objData.primitiveHeight);
                    obj->setPrimitiveSegments(objData.primitiveSegments);
                    obj->setPrimitiveColor(objData.primitiveColor);

                    // Apply door properties if this is a door
                    if (primType == PrimitiveType::Door) {
                        obj->setDoorId(objData.doorId);
                        obj->setTargetLevel(objData.targetLevel);
                        obj->setTargetDoorId(objData.targetDoorId);
                    }

                    std::cout << "Loaded primitive: " << name << std::endl;
                }
                // Check if this is a skinned model
                else if (objData.isSkinned) {
                    auto result = SkinnedGLBLoader::load(objData.modelPath);
                    if (!result.success || result.meshes.empty()) {
                        std::cerr << "Failed to load skinned model: " << objData.modelPath << std::endl;
                        continue;
                    }

                    const auto& mesh = result.meshes[0];

                    // Create GPU resources for skinned model
                    uint32_t handle = m_skinnedModelRenderer->createModel(
                        mesh.vertices,
                        mesh.indices,
                        std::make_unique<Skeleton>(*result.skeleton),
                        result.animations,
                        mesh.hasTexture ? mesh.textureData.data() : nullptr,
                        mesh.textureWidth,
                        mesh.textureHeight
                    );

                    obj = std::make_unique<SceneObject>(mesh.name);
                    obj->setSkinnedModelHandle(handle);
                    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));

                    // Store animation info and play
                    auto animNames = m_skinnedModelRenderer->getAnimationNames(handle);
                    obj->setAnimationNames(animNames);
                    if (!objData.currentAnimation.empty()) {
                        m_skinnedModelRenderer->playAnimation(handle, objData.currentAnimation, true);
                        obj->setCurrentAnimation(objData.currentAnimation);
                    } else if (!animNames.empty()) {
                        m_skinnedModelRenderer->playAnimation(handle, animNames[0], true);
                        obj->setCurrentAnimation(animNames[0]);
                    }

                    std::cout << "Loaded skinned model: " << objData.modelPath << std::endl;
                } else {
                    // Static model
                    auto result = GLBLoader::load(objData.modelPath);
                    if (!result.success || result.meshes.empty()) {
                        std::cerr << "Failed to load model: " << objData.modelPath << std::endl;
                        continue;
                    }

                    const auto& mesh = result.meshes[0];
                    obj = GLBLoader::createSceneObject(mesh, *m_modelRenderer);
                    if (!obj) {
                        std::cerr << "Failed to create scene object for: " << objData.modelPath << std::endl;
                        continue;
                    }
                }

                obj->setModelPath(objData.modelPath);
                obj->getTransform().setPosition(objData.position);
                obj->setEulerRotation(objData.rotation);
                obj->getTransform().setScale(objData.scale);
                obj->setHueShift(objData.hueShift);
                obj->setSaturation(objData.saturation);
                obj->setBrightness(objData.brightness);
                obj->setVisible(objData.visible);
                obj->setAABBCollision(objData.aabbCollision);
                obj->setPolygonCollision(objData.polygonCollision);
                obj->setBulletCollisionType(static_cast<BulletCollisionType>(objData.bulletCollisionType));
                obj->setKinematicPlatform(objData.kinematicPlatform);
                obj->setBeingType(static_cast<BeingType>(objData.beingType));
                if (!objData.groveScript.empty()) {
                    obj->setGroveScriptPath(objData.groveScript);
                }

                // Apply frozen transform if saved (re-bake rotation/scale into vertices)
                if (objData.frozenTransform && obj->hasMeshData()) {
                    // Convert euler angles to quaternion
                    glm::vec3 radians = glm::radians(objData.frozenRotation);
                    glm::quat rotation = glm::quat(radians);
                    glm::vec3 scale = objData.frozenScale;

                    // Transform vertices
                    std::vector<ModelVertex> vertices = obj->getVertices();
                    glm::mat4 rotMat = glm::mat4_cast(rotation);
                    glm::mat3 normalMat = glm::mat3(rotMat);

                    glm::vec3 minBounds(FLT_MAX);
                    glm::vec3 maxBounds(-FLT_MAX);

                    for (auto& v : vertices) {
                        glm::vec3 scaledPos = v.position * scale;
                        v.position = glm::vec3(rotMat * glm::vec4(scaledPos, 1.0f));
                        v.normal = glm::normalize(normalMat * v.normal);
                        minBounds = glm::min(minBounds, v.position);
                        maxBounds = glm::max(maxBounds, v.position);
                    }

                    // Update mesh data and bounds
                    obj->setMeshData(vertices, obj->getIndices());
                    AABB newBounds;
                    newBounds.min = minBounds;
                    newBounds.max = maxBounds;
                    obj->setLocalBounds(newBounds);

                    // Update GPU buffer
                    if (obj->getBufferHandle() != UINT32_MAX) {
                        m_modelRenderer->updateModelBuffer(obj->getBufferHandle(), vertices);
                    }

                    // Store frozen transform for future saves
                    obj->setFrozenTransform(objData.frozenRotation, objData.frozenScale);

                    std::cout << "Applied frozen transform for " << objData.name << std::endl;
                }

                // Add to Bullet physics world if it has Bullet collision
                if (obj->hasBulletCollision() && m_physicsWorld) {
                    m_physicsWorld->addObject(obj.get(), obj->getBulletCollisionType());
                }
                obj->setDailySchedule(objData.dailySchedule);
                obj->setPatrolSpeed(objData.patrolSpeed);
                if (!objData.description.empty()) {
                    obj->setDescription(objData.description);
                }
                if (!objData.buildingType.empty()) {
                    obj->setBuildingType(objData.buildingType);
                }

                // Restore behaviors
                for (const auto& behData : objData.behaviors) {
                    Behavior behavior;
                    behavior.name = behData.name;
                    behavior.trigger = static_cast<TriggerType>(behData.trigger);
                    behavior.triggerParam = behData.triggerParam;
                    behavior.triggerRadius = behData.triggerRadius;
                    behavior.loop = behData.loop;
                    behavior.enabled = behData.enabled;

                    for (const auto& actData : behData.actions) {
                        Action action;
                        action.type = static_cast<ActionType>(actData.type);
                        action.vec3Param = actData.vec3Param;
                        action.floatParam = actData.floatParam;
                        action.stringParam = actData.stringParam;
                        action.animationParam = actData.animationParam;
                        action.boolParam = actData.boolParam;
                        action.easing = static_cast<Action::Easing>(actData.easing);
                        action.duration = actData.duration;
                        behavior.actions.push_back(action);
                    }
                    obj->addBehavior(behavior);
                }

                m_sceneObjects.push_back(std::move(obj));
            }
        }  // End of else (JSON+GLB fallback)

        m_chunkManager->updateModifiedChunks(m_terrain);

        // Restore spawn position for play mode (if set)
        if (levelData.spawnPosition != glm::vec3(0)) {
            m_hasSpawnPoint = true;
            m_spawnPosition = levelData.spawnPosition;
        }

        // Restore editor camera position
        m_camera.setPosition(levelData.editorCameraPos);
        m_camera.setYaw(levelData.editorCameraYaw);
        m_camera.setPitch(levelData.editorCameraPitch);

        m_editorUI.setWaterLevel(levelData.waterLevel);
        m_editorUI.setWaterVisible(levelData.waterEnabled);
        m_waterRenderer->setWaterLevel(levelData.waterLevel);
        m_waterRenderer->setVisible(levelData.waterEnabled);

        m_skybox->updateParameters(levelData.skyParams);

        // Restore test level mode
        m_isTestLevel = levelData.isTestLevel;
        m_editorUI.setTestLevelMode(levelData.isTestLevel);
        if (levelData.isTestLevel) {
            m_testFloorSize = 100.0f;  // Default test floor size
            // Floor is now saved as a primitive, so it loads with other objects
        }

        // Restore space level mode
        m_isSpaceLevel = levelData.isSpaceLevel;
        m_editorUI.setSpaceLevelMode(levelData.isSpaceLevel);

        // Restore game module
        if (!levelData.gameModuleName.empty()) {
            // Unload current module if different
            if (m_gameModule && std::string(m_gameModule->getName()) != levelData.gameModuleName) {
                m_gameModule->shutdown();
                m_gameModule.reset();
            }
            // Load the saved module
            if (!m_gameModule) {
                m_gameModule = eden::GameModuleFactory::create(levelData.gameModuleName);
                if (m_gameModule) {
                    m_gameModule->initialize();
                    std::cout << "Loaded game module: " << levelData.gameModuleName << std::endl;
                }
            }
        } else if (m_gameModule) {
            // No module in level, unload current
            m_gameModule->shutdown();
            m_gameModule.reset();
        }

        // Restore physics backend
        m_physicsBackend = static_cast<PhysicsBackend>(levelData.physicsBackend);
        m_editorUI.setPhysicsBackend(m_physicsBackend);

        // Restore AI nodes
        m_aiNodes.clear();
        m_selectedAINodeIndex = -1;
        uint32_t maxId = 0;
        for (const auto& nodeData : levelData.aiNodes) {
            auto node = std::make_unique<AINode>(nodeData.id, nodeData.name);
            node->setPosition(nodeData.position);
            node->setType(static_cast<AINodeType>(nodeData.type));
            node->setRadius(nodeData.radius);
            node->setVisible(nodeData.visible);

            // Restore connections
            for (uint32_t connId : nodeData.connections) {
                node->addConnection(connId);
            }

            // Restore behaviors
            for (const auto& behData : nodeData.behaviors) {
                Behavior behavior;
                behavior.name = behData.name;
                behavior.trigger = static_cast<TriggerType>(behData.trigger);
                behavior.triggerParam = behData.triggerParam;
                behavior.triggerRadius = behData.triggerRadius;
                behavior.loop = behData.loop;
                behavior.enabled = behData.enabled;

                for (const auto& actData : behData.actions) {
                    Action action;
                    action.type = static_cast<ActionType>(actData.type);
                    action.vec3Param = actData.vec3Param;
                    action.floatParam = actData.floatParam;
                    action.stringParam = actData.stringParam;
                    action.animationParam = actData.animationParam;
                    action.boolParam = actData.boolParam;
                    action.easing = static_cast<Action::Easing>(actData.easing);
                    action.duration = actData.duration;
                    behavior.actions.push_back(action);
                }
                node->addBehavior(behavior);
            }

            // Restore properties
            for (const auto& [key, value] : nodeData.properties) {
                node->setProperty(key, value);
            }

            // Restore tags
            for (const auto& tag : nodeData.tags) {
                node->addTag(tag);
            }

            if (nodeData.id > maxId) maxId = nodeData.id;
            m_aiNodes.push_back(std::move(node));
        }
        m_nextAINodeId = maxId + 1;
        updateAINodeList();
        updateAINodeRenderer();

        // Load zone data and object groups from raw JSON
        {
            try {
                std::ifstream zfile(filepath);
                nlohmann::json zroot = nlohmann::json::parse(zfile);
                zfile.close();

                if (m_zoneSystem && levelData.zoneData.hasData) {
                    m_zoneSystem->load(zroot);
                }

                // Load object groups (rebuild indices from object names)
                m_objectGroups.clear();
                if (zroot.contains("objectGroups") && zroot["objectGroups"].is_array()) {
                    // Build name→index map
                    std::map<std::string, int> nameToIndex;
                    for (int i = 0; i < static_cast<int>(m_sceneObjects.size()); i++) {
                        nameToIndex[m_sceneObjects[i]->getName()] = i;
                    }

                    for (const auto& gj : zroot["objectGroups"]) {
                        EditorUI::ObjectGroup group;
                        group.name = gj.value("name", "Group");
                        group.expanded = gj.value("expanded", true);
                        if (gj.contains("objects") && gj["objects"].is_array()) {
                            for (const auto& objName : gj["objects"]) {
                                auto it = nameToIndex.find(objName.get<std::string>());
                                if (it != nameToIndex.end()) {
                                    group.objectIndices.insert(it->second);
                                }
                            }
                        }
                        if (!group.objectIndices.empty()) {
                            m_objectGroups.push_back(group);
                        }
                    }
                    m_editorUI.setObjectGroups(m_objectGroups);
                    if (!m_objectGroups.empty()) {
                        std::cout << "Loaded " << m_objectGroups.size() << " object groups" << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to load extra data: " << e.what() << std::endl;
            }
        }

        m_currentLevelPath = filepath;
        std::cout << "Level loaded from: " << filepath << std::endl;

        // Load expression textures for sentient NPCs
        for (auto& obj : m_sceneObjects) {
            if (obj && obj->isSentient()) {
                loadExpressionsForNPC(obj.get());
            }
        }

        // Auto-load game save if it exists
        loadGame();
    }

    // Get the game save file path derived from the current level path
    std::string getGameSavePath() const {
        if (m_currentLevelPath.empty()) return "";
        // xenk.eden -> xenk.savegame.json
        std::string base = m_currentLevelPath;
        size_t dot = base.rfind('.');
        if (dot != std::string::npos) {
            base = base.substr(0, dot);
        }
        return base + ".savegame.json";
    }

    void saveGame() {
        std::string savePath = getGameSavePath();
        if (savePath.empty()) {
            std::cout << "[SaveGame] No level loaded, cannot save game." << std::endl;
            return;
        }

        try {
            nlohmann::json save;
            save["credits"] = m_playerCredits;
            save["cityCredits"] = m_cityCredits;
            save["gameTimeMinutes"] = m_gameTimeMinutes;

            // Save owned plots from zone system
            nlohmann::json plots = nlohmann::json::array();
            if (m_zoneSystem) {
                int gw = m_zoneSystem->getGridWidth();
                int gh = m_zoneSystem->getGridHeight();
                for (int gz = 0; gz < gh; gz++) {
                    for (int gx = 0; gx < gw; gx++) {
                        glm::vec2 wc = m_zoneSystem->gridToWorld(gx, gz);
                        uint32_t owner = m_zoneSystem->getOwner(wc.x, wc.y);
                        if (owner != 0) {
                            nlohmann::json p;
                            p["x"] = gx;
                            p["z"] = gz;
                            p["owner"] = owner;
                            plots.push_back(p);
                        }
                    }
                }
            }
            save["ownedPlots"] = plots;

            std::ofstream outFile(savePath);
            outFile << save.dump(2);
            outFile.close();

            std::cout << "[SaveGame] Game saved to: " << savePath
                      << " (" << plots.size() << " owned plots, "
                      << static_cast<int>(m_playerCredits) << " CR)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[SaveGame] Failed to save: " << e.what() << std::endl;
        }
    }

    void loadGame() {
        std::string savePath = getGameSavePath();
        if (savePath.empty()) return;

        std::ifstream inFile(savePath);
        if (!inFile.good()) return;  // No save file exists yet

        try {
            nlohmann::json save = nlohmann::json::parse(inFile);
            inFile.close();

            // Restore credits
            if (save.contains("credits")) {
                m_playerCredits = save["credits"].get<float>();
            }
            if (save.contains("cityCredits")) {
                m_cityCredits = save["cityCredits"].get<float>();
            }

            // Restore game time
            if (save.contains("gameTimeMinutes")) {
                m_gameTimeMinutes = save["gameTimeMinutes"].get<float>();
            }

            // Restore plot ownership and spawn boundary posts
            if (save.contains("ownedPlots") && m_zoneSystem) {
                auto& plots = save["ownedPlots"];
                for (auto& p : plots) {
                    int gx = p["x"].get<int>();
                    int gz = p["z"].get<int>();
                    uint32_t owner = p["owner"].get<uint32_t>();
                    m_zoneSystem->setOwner(gx, gz, owner);
                    spawnPlotPosts(gx, gz);
                }

                std::cout << "[SaveGame] Loaded: " << plots.size() << " owned plots, "
                          << static_cast<int>(m_playerCredits) << " CR" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[SaveGame] Failed to load: " << e.what() << std::endl;
        }
    }

    void exportTerrainOBJ() {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = {{"OBJ Mesh", "obj"}};

        nfdresult_t result = NFD_SaveDialog(&outPath, filters, 1, nullptr, "terrain.obj");

        if (result == NFD_OKAY) {
            std::string path = outPath;
            if (path.find(".obj") == std::string::npos) {
                path += ".obj";
            }

            if (m_terrain.exportToOBJ(path)) {
                std::cout << "Terrain exported to: " << path << std::endl;
            } else {
                std::cerr << "Failed to export terrain" << std::endl;
            }

            NFD_FreePath(outPath);
        }
    }

    void newLevel() {
        // Exit play mode if active
        if (m_isPlayMode) {
            exitPlayMode();
        }

        // Clear physics worlds
        if (m_physicsWorld) {
            m_physicsWorld->clear();
        }
        if (m_characterController) {
            m_characterController->clearBodies();
        }

        // Clear scene objects
        for (auto& obj : m_sceneObjects) {
            if (obj) {
                m_modelRenderer->destroyModel(obj->getBufferHandle());
            }
        }
        m_sceneObjects.clear();
        m_selectedObjectIndex = -1;

        // Clear AI nodes
        m_aiNodes.clear();
        m_selectedAINodeIndex = -1;
        if (m_aiNodeRenderer) {
            m_aiNodeRenderer->clearCollisionAABBs();
            m_aiNodeRenderer->update(m_aiNodes, m_terrain);
        }

        // Reset spawn point
        m_hasSpawnPoint = false;
        m_spawnObjectIndex = -1;
        m_spawnPosition = glm::vec3(0.0f);

        // Reset water
        m_editorUI.setWaterLevel(-5.0f);
        m_editorUI.setWaterVisible(false);
        m_waterRenderer->setWaterLevel(-5.0f);
        m_waterRenderer->setVisible(false);

        // Reset sky to defaults
        if (m_skybox) {
            SkyParameters defaultSky;
            m_skybox->updateParameters(defaultSky);
        }

        // Reset camera
        float startHeight = 20.0f;
        m_camera.setPosition({0, startHeight, 0});
        m_camera.setYaw(-90.0f);
        m_camera.setPitch(0.0f);

        // Update EditorUI to clear any stale references
        m_editorUI.setSelectedObjectIndex(-1);
        m_editorUI.setSelectedAINodeIndex(-1);

        // Reset test level state
        m_isTestLevel = false;
        m_editorUI.setTestLevelMode(false);

        // Reset space level state
        m_isSpaceLevel = false;
        m_editorUI.setSpaceLevelMode(false);

        // Reset zone system
        if (m_zoneSystem) {
            m_zoneSystem->generateDefaultLayout();
        }

        m_currentLevelPath.clear();
        std::cout << "New level created" << std::endl;
    }

    void newTestLevel() {
        // First clear everything
        newLevel();

        // Create a large flat floor for testing
        // The floor will be a 100x100 meter platform at Y=0

        // We need to be in play mode for Jolt physics
        // So we'll store the floor config and create it when entering play mode
        m_isTestLevel = true;
        m_testFloorSize = 100.0f;

        // Tell EditorUI we're in test level mode (hides terrain/sky panels)
        m_editorUI.setTestLevelMode(true);

        // Create a visible floor mesh (thin cube)
        float floorSize = m_testFloorSize;
        float floorThickness = 0.1f;
        glm::vec4 floorColor = glm::vec4(0.4f, 0.4f, 0.45f, 1.0f);  // Gray floor
        auto meshData = PrimitiveMeshBuilder::createCube(1.0f, floorColor);

        auto floorObj = std::make_unique<SceneObject>("TestFloor");
        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        floorObj->setBufferHandle(handle);
        floorObj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        floorObj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        floorObj->setLocalBounds(meshData.bounds);

        // Store primitive info for save/load
        floorObj->setPrimitiveType(PrimitiveType::Cube);
        floorObj->setPrimitiveSize(1.0f);  // Base size before scale
        floorObj->setPrimitiveColor(floorColor);

        // Scale to make it a flat floor: 100x0.1x100
        floorObj->getTransform().setScale(glm::vec3(floorSize, floorThickness, floorSize));
        // Position so top surface is at Y=0
        floorObj->getTransform().setPosition(glm::vec3(0.0f, -floorThickness * 0.5f, 0.0f));

        m_sceneObjects.push_back(std::move(floorObj));

        // Update EditorUI with scene objects
        std::vector<SceneObject*> objects;
        for (const auto& obj : m_sceneObjects) {
            objects.push_back(obj.get());
        }
        m_editorUI.setSceneObjects(objects);

        // Position camera above the test floor
        m_camera.setPosition({0, 5.0f, 10.0f});
        m_camera.setYaw(-90.0f);
        m_camera.setPitch(-15.0f);

        // Set a spawn point at the center
        m_hasSpawnPoint = true;
        m_spawnPosition = glm::vec3(0.0f, 2.0f, 0.0f);

        std::cout << "Test level created - " << m_testFloorSize << "x" << m_testFloorSize << "m flat floor" << std::endl;
        std::cout << "Press F5 to enter play mode and test physics" << std::endl;
    }

    void newSpaceLevel() {
        // First clear everything
        newLevel();

        // Set space level mode (no terrain, no floor, just skybox for stars)
        m_isSpaceLevel = true;
        m_isTestLevel = false;

        // Tell EditorUI we're in space level mode (hides terrain panels, keeps sky)
        m_editorUI.setSpaceLevelMode(true);
        m_editorUI.setTestLevelMode(false);

        // Update EditorUI with scene objects (empty)
        std::vector<SceneObject*> objects;
        for (const auto& obj : m_sceneObjects) {
            objects.push_back(obj.get());
        }
        m_editorUI.setSceneObjects(objects);

        // Position camera at origin looking forward
        m_camera.setPosition({0, 0, 10.0f});
        m_camera.setYaw(-90.0f);
        m_camera.setPitch(0.0f);

        // No spawn point by default - user can add one
        m_hasSpawnPoint = false;

        // Enable space mode for sky - stars appear on full sphere
        if (m_skybox) {
            auto& skyParams = m_skybox->getParameters();
            skyParams.spaceMode = true;
            m_skybox->updateParameters(skyParams);
        }

        std::cout << "Space level created - no terrain, just sky/stars" << std::endl;
        std::cout << "Use File > Import Model to add objects" << std::endl;
    }

    void runGame() {
        if (m_isPlayMode) {
            exitPlayMode();
        } else {
            enterPlayMode();
        }
    }

    // ── World Generation ─────────────────────────────────────────────

    // Find an empty plot for a building type (native C++ version of MCP find_empty_plot)
    // Returns {x, z} or {NaN, NaN} if not found
    glm::vec2 findEmptyPlotNative(const std::string& buildingType, float nearX = 0.0f, float nearZ = 0.0f) {
        const ::CityBuildingDef* def = ::findCityBuildingDef(buildingType);
        if (!def || !m_zoneSystem) return glm::vec2(NAN);

        // Collect existing building positions and footprints
        std::vector<std::pair<glm::vec2, float>> existingBuildings;
        for (auto& obj : m_sceneObjects) {
            if (!obj || obj->getBuildingType().empty()) continue;
            auto pos = obj->getTransform().getPosition();
            const ::CityBuildingDef* bd = ::findCityBuildingDef(obj->getBuildingType());
            float fp = bd ? bd->footprint : 10.0f;
            existingBuildings.push_back({{pos.x, pos.z}, fp});
        }

        float cellSize = m_zoneSystem->getCellSize();
        int maxRadius = 50;
        for (int r = 0; r <= maxRadius; r++) {
            for (int dz = -r; dz <= r; dz++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (std::abs(dx) != r && std::abs(dz) != r) continue;
                    float wx = nearX + dx * cellSize;
                    float wz = nearZ + dz * cellSize;

                    // Check zone match
                    ZoneType zt = m_zoneSystem->getZoneType(wx, wz);
                    bool matches = def->zoneReq.empty();
                    if (!matches) {
                        if (def->zoneReq == "residential" && zt == ZoneType::Residential) matches = true;
                        if (def->zoneReq == "commercial"  && zt == ZoneType::Commercial)  matches = true;
                        if (def->zoneReq == "industrial"  && zt == ZoneType::Industrial)  matches = true;
                        if (def->zoneReq == "resource"    && zt == ZoneType::Resource)    matches = true;
                    }
                    if (!matches) continue;

                    // Check resource requirement for extraction buildings
                    if (!def->requires.empty()) {
                        ResourceType rt = m_zoneSystem->getResource(wx, wz);
                        bool resMatch = false;
                        if (def->requires == "wood"      && rt == ResourceType::Wood)      resMatch = true;
                        if (def->requires == "iron"      && rt == ResourceType::Iron)      resMatch = true;
                        if (def->requires == "limestone" && rt == ResourceType::Limestone) resMatch = true;
                        if (def->requires == "oil"       && rt == ResourceType::Oil)       resMatch = true;
                        if (def->requires == "water"     && rt == ResourceType::Water)     resMatch = true;
                        if (def->requires == "gas"       && rt == ResourceType::Gas)       resMatch = true;
                        if (def->requires == "crystal"   && rt == ResourceType::Crystal)   resMatch = true;
                        if (def->requires == "energy"    && rt == ResourceType::Energy)    resMatch = true;
                        if (def->requires == "exotic"    && rt == ResourceType::Exotic)    resMatch = true;
                        if (!resMatch) continue;
                    }

                    // Check no existing building too close
                    bool tooClose = false;
                    for (auto& [bp, bfp] : existingBuildings) {
                        float minDist = (bfp + def->footprint) * 0.5f;
                        if (glm::length(glm::vec2(wx, wz) - bp) < minDist) {
                            tooClose = true;
                            break;
                        }
                    }
                    if (tooClose) continue;

                    return {wx, wz};
                }
            }
        }
        return glm::vec2(NAN);
    }

    // Place a building at a position (returns true if placed)
    bool placeBuildingNative(const std::string& type, float posX, float posZ) {
        const ::CityBuildingDef* def = ::findCityBuildingDef(type);
        if (!def) return false;

        if (m_cityCredits < def->cost) return false;
        m_cityCredits -= def->cost;

        int count = 0;
        for (auto& obj : m_sceneObjects) {
            if (obj && obj->getBuildingType() == type) count++;
        }
        std::string objName = def->name + "_" + std::to_string(count + 1);
        float terrainY = m_terrain.getHeightAt(posX, posZ);

        float size = def->footprint * 0.6f;
        glm::vec4 color(0.7f, 0.7f, 0.7f, 1.0f);
        if (def->category == "housing")         color = glm::vec4(0.9f, 0.8f, 0.2f, 1.0f);
        else if (def->category == "food")       color = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f);
        else if (def->category == "resource")   color = glm::vec4(0.6f, 0.4f, 0.2f, 1.0f);
        else if (def->category == "industry")   color = glm::vec4(0.5f, 0.5f, 0.6f, 1.0f);
        else if (def->category == "commercial") color = glm::vec4(0.2f, 0.5f, 0.9f, 1.0f);

        auto meshData = PrimitiveMeshBuilder::createCube(size, color);
        auto obj = std::make_unique<SceneObject>(objName);
        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);
        obj->setModelPath("");
        obj->setMeshData(meshData.vertices, meshData.indices);
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(size);
        obj->setPrimitiveColor(color);
        obj->getTransform().setPosition(glm::vec3(posX, terrainY, posZ));
        obj->setName(objName);
        obj->setBuildingType(type);
        obj->setDescription(def->name);

        m_sceneObjects.push_back(std::move(obj));
        return true;
    }

    // Build a settlement algorithmically from planet data
    int buildSettlement(const nlohmann::json& planetData) {
        int population = planetData.value("population", 50);
        float startingCredits = planetData.value("starting_credits", 5000.0f);
        m_cityCredits = startingCredits;

        // Collect available buildings from planet data
        std::set<std::string> available;
        if (planetData.contains("buildings_available") && planetData["buildings_available"].is_array()) {
            for (const auto& b : planetData["buildings_available"]) {
                available.insert(b.get<std::string>());
            }
        }
        // Shack is always available
        available.insert("shack");

        int totalPlaced = 0;

        // Phase 1: Housing — 1 shack per 4 people
        if (available.count("shack")) {
            int targetShacks = std::min(population / 4, 50);  // Cap at 50
            for (int i = 0; i < targetShacks; i++) {
                if (m_cityCredits < 50.0f) break;
                glm::vec2 plot = findEmptyPlotNative("shack", m_spawnPosition.x, m_spawnPosition.z);
                if (std::isnan(plot.x)) break;
                if (placeBuildingNative("shack", plot.x, plot.y)) totalPlaced++;
            }
        }

        // Phase 2: Food — 1 farm per 8 people
        if (available.count("farm")) {
            int targetFarms = std::min(population / 8, 20);
            for (int i = 0; i < targetFarms; i++) {
                if (m_cityCredits < 200.0f) break;
                glm::vec2 plot = findEmptyPlotNative("farm", m_spawnPosition.x, m_spawnPosition.z);
                if (std::isnan(plot.x)) break;
                if (placeBuildingNative("farm", plot.x, plot.y)) totalPlaced++;
            }
        }

        // Phase 3: Resource extraction (only if resource zone exists)
        auto placeExtractors = [&](const std::string& type, int count) {
            if (!available.count(type)) return;
            for (int i = 0; i < count; i++) {
                const ::CityBuildingDef* def = ::findCityBuildingDef(type);
                if (!def || m_cityCredits < def->cost) break;
                glm::vec2 plot = findEmptyPlotNative(type, m_spawnPosition.x, m_spawnPosition.z);
                if (std::isnan(plot.x)) break;
                if (placeBuildingNative(type, plot.x, plot.y)) totalPlaced++;
            }
        };
        placeExtractors("lumber_mill", 2);
        placeExtractors("mine", 2);
        placeExtractors("quarry", 2);

        // Phase 4: Industry — workshops if raw materials exist
        if (available.count("workshop")) {
            int targetWorkshops = std::min(2, population / 20);
            for (int i = 0; i < targetWorkshops; i++) {
                if (m_cityCredits < 350.0f) break;
                glm::vec2 plot = findEmptyPlotNative("workshop", m_spawnPosition.x, m_spawnPosition.z);
                if (std::isnan(plot.x)) break;
                if (placeBuildingNative("workshop", plot.x, plot.y)) totalPlaced++;
            }
        }

        // Phase 5: Commerce
        if (available.count("market")) {
            glm::vec2 plot = findEmptyPlotNative("market", m_spawnPosition.x, m_spawnPosition.z);
            if (!std::isnan(plot.x) && m_cityCredits >= 250.0f) {
                if (placeBuildingNative("market", plot.x, plot.y)) totalPlaced++;
            }
        }
        if (available.count("warehouse")) {
            glm::vec2 plot = findEmptyPlotNative("warehouse", m_spawnPosition.x, m_spawnPosition.z);
            if (!std::isnan(plot.x) && m_cityCredits >= 200.0f) {
                if (placeBuildingNative("warehouse", plot.x, plot.y)) totalPlaced++;
            }
        }

        std::cout << "[Settlement] Built " << totalPlaced << " buildings (pop=" << population
                  << ", treasury=" << static_cast<int>(m_cityCredits) << " CR remaining)\n";
        return totalPlaced;
    }

    // Generate a random world: fetch planet data, lay out zones, build settlement
    bool generateRandomWorld() {
        std::cout << "[WorldGen] Generating random world...\n";

        // 1. HTTP POST to backend
        httplib::Client cli("localhost", 8080);
        cli.set_connection_timeout(5);
        auto res = cli.Post("/planet/generate", "{}", "application/json");
        if (!res || res->status != 200) {
            std::cout << "[WorldGen] ERROR: Backend not available (start backend/server.py on port 8080)\n";
            return false;
        }

        try {
            m_planetData = nlohmann::json::parse(res->body);
        } catch (const std::exception& e) {
            std::cout << "[WorldGen] ERROR: Failed to parse planet JSON: " << e.what() << "\n";
            return false;
        }

        std::string planetName = m_planetData.value("name", "Unknown");
        std::string biome = m_planetData.value("biome_name", m_planetData.value("biome", "unknown"));
        int techLevel = m_planetData.value("tech_level", 1);
        int population = m_planetData.value("population", 50);
        float credits = m_planetData.value("starting_credits", 5000.0f);

        std::cout << "[WorldGen] Planet: " << planetName << "\n"
                  << "[WorldGen]   Biome: " << biome << "\n"
                  << "[WorldGen]   Tech Level: " << techLevel << " (" << m_planetData.value("tech_name", "") << ")\n"
                  << "[WorldGen]   Population: " << population << "\n"
                  << "[WorldGen]   Starting Credits: " << static_cast<int>(credits) << " CR\n"
                  << "[WorldGen]   Species: " << m_planetData.value("species_name", "Unknown") << "\n"
                  << "[WorldGen]   Government: " << m_planetData.value("government_name", "Unknown") << "\n";

        // List resources
        if (m_planetData.contains("resources_harvestable") && m_planetData["resources_harvestable"].is_array()) {
            std::cout << "[WorldGen]   Harvestable: ";
            for (size_t i = 0; i < m_planetData["resources_harvestable"].size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << m_planetData["resources_harvestable"][i].get<std::string>();
            }
            std::cout << "\n";
        }

        // 2. Generate zone layout
        if (m_zoneSystem) {
            m_zoneSystem->generatePlanetLayout(m_planetData);
        }

        // 3. Build settlement
        int buildingsPlaced = buildSettlement(m_planetData);

        // 4. Mark as generated
        m_worldGenerated = true;

        std::cout << "[WorldGen] World generation complete — " << buildingsPlaced << " buildings placed\n";
        return true;
    }

    void enterPlayMode() {
        m_isPlayMode = true;
        m_playModeCursorVisible = false;  // Start with cursor hidden (mouse look active)
        m_playModeDebug = false;          // Debug visuals off by default
        m_selectedFaces.clear();
        Input::setMouseCaptured(true);

        // Disable noclip for play mode (terrain collision enabled)
        m_camera.setNoClip(false);

        // Set up signal callback for SceneObject SEND_SIGNAL actions
        SceneObject::setSignalCallback([this](const std::string& signalName, const std::string& targetName, SceneObject* sender) {
            if (targetName.empty()) {
                // Broadcast to all objects
                for (auto& obj : m_sceneObjects) {
                    if (obj && obj.get() != sender) {
                        obj->triggerBehaviorBySignal(signalName);
                    }
                }
            } else {
                // Send to specific target
                for (auto& obj : m_sceneObjects) {
                    if (obj && obj->getName() == targetName) {
                        obj->triggerBehaviorBySignal(signalName);
                        break;
                    }
                }
            }
        });

        // Hide debug visuals (AI nodes, etc.) - F3 can re-enable them
        if (m_aiNodeRenderer) {
            m_aiNodeRenderer->setVisible(false);
        }

        // Save editor camera state - this is where player spawns if no spawn point set
        m_editorCameraPos = m_camera.getPosition();
        m_editorCameraYaw = m_camera.getYaw();
        m_editorCameraPitch = m_camera.getPitch();

        // Only teleport to spawn position if there's an actual spawn marker object
        // (not just a saved camera position from level load)
        bool hasRealSpawnMarker = m_spawnObjectIndex >= 0 &&
                                   m_spawnObjectIndex < static_cast<int>(m_sceneObjects.size());
        if (hasRealSpawnMarker) {
            // Use spawn marker position
            m_spawnPosition = m_sceneObjects[m_spawnObjectIndex]->getTransform().getPosition();
            const float eyeHeight = 1.7f;
            m_camera.setPosition(m_spawnPosition + glm::vec3(0, eyeHeight, 0));
        }
        // Otherwise camera stays at current editor position for quick iteration

        // Trigger procedural world generation if spawn marker exists and world not yet generated
        if (hasRealSpawnMarker && !m_worldGenerated) {
            std::cout << "[PlayMode] Spawn marker detected — triggering world generation\n";
            generateRandomWorld();
        }

        // Initialize game time (start at 6:00 AM)
        m_gameTimeMinutes = 360.0f;  // 0600

        // Reset player health
        m_playerHealth = m_playerMaxHealth;

        // Sync GRAPH nodes with economy system
        syncEconomyNodes();

        // Create player avatar so AI can perceive the player
        createPlayerAvatar();

        // Place model traders at starting nodes (if not already placed)
        for (auto& trader : m_modelTraders) {
            if (trader && trader->getCurrentNodeId() == 0) {
                placeTraderAtRandomNode(trader.get());
            }
        }
        if (!m_modelTraders.empty()) {
            std::cout << "Placed " << m_modelTraders.size() << " model traders at starting nodes" << std::endl;
        }

        // Trigger ON_GAMESTART behaviors for all objects
        for (auto& objPtr : m_sceneObjects) {
            if (objPtr && objPtr->hasBehaviors()) {
                startGameStartBehaviors(objPtr.get());
            }
        }

        // Also check ON_GAME_TIME triggers that match the starting time
        int startMinute = static_cast<int>(m_gameTimeMinutes);
        checkInitialGameTimeTriggers(startMinute);

        // Camera movement mode is controlled by double-tap ALT at runtime
        Input::setMouseCaptured(true);

        // Force walk mode in play mode (no flying)
        m_camera.setMovementMode(MovementMode::Walk);
        m_lastMovementMode = MovementMode::Walk;

        // Snap camera to terrain height if below ground
        {
            glm::vec3 startPos = m_camera.getPosition();
            float terrainHeight = m_terrain.getHeightAt(startPos.x, startPos.z);
            float eyeHeight = 1.7f;
            float minY = terrainHeight + eyeHeight;
            if (startPos.y < minY) {
                startPos.y = minY;
                m_camera.setPosition(startPos);
            }
        }

        // Get physics backend from EditorUI (may have been changed by user)
        m_physicsBackend = m_editorUI.getPhysicsBackend();

        // Initialize character controller based on selected physics backend
        if (m_physicsBackend == PhysicsBackend::Jolt) {
            m_characterController = std::make_unique<JoltCharacter>();
            std::cout << "Using Jolt Physics backend" << std::endl;
        } else {
            m_characterController = std::make_unique<HomebrewCharacter>();
            std::cout << "Using Homebrew Physics backend" << std::endl;
        }
        if (m_characterController->initialize()) {
            if (m_isTestLevel) {
                // Test level: Add a simple flat floor box instead of terrain
                float halfSize = m_testFloorSize * 0.5f;
                float floorThickness = 0.5f;
                glm::vec3 floorHalfExtents(halfSize, floorThickness, halfSize);
                glm::vec3 floorPosition(0.0f, -floorThickness, 0.0f);  // Top surface at Y=0
                m_characterController->addStaticBox(floorHalfExtents, floorPosition);
                std::cout << "Added test floor: " << m_testFloorSize << "x" << m_testFloorSize << "m at Y=0" << std::endl;

                // Position camera for test level (only if real spawn marker exists)
                if (hasRealSpawnMarker) {
                    glm::vec3 startPos = m_spawnPosition + glm::vec3(0, 1.7f, 0);
                    m_camera.setPosition(startPos);
                }
            } else {
                // Normal level: Add terrain collision using heightfield
                const auto& config = m_terrain.getConfig();
                if (config.useFixedBounds) {
                    // Calculate terrain world bounds
                    int chunksX = config.maxChunk.x - config.minChunk.x + 1;
                    int chunksZ = config.maxChunk.y - config.minChunk.y + 1;
                    float chunkSize = config.chunkResolution * config.tileSize;
                    float worldMinX = config.minChunk.x * chunkSize;
                    float worldMinZ = config.minChunk.y * chunkSize;
                    float worldSizeX = chunksX * chunkSize;
                    float worldSizeZ = chunksZ * chunkSize;

                    // Sample count must be power of 2 + 1 for Jolt
                    // Use 2049 samples for accurate terrain collision (captures small features like valleys)
                    const int sampleCount = 2049;
                    float sampleSpacingX = worldSizeX / (sampleCount - 1);
                    float sampleSpacingZ = worldSizeZ / (sampleCount - 1);

                    // Build heightfield by sampling terrain
                    std::vector<float> heightData(sampleCount * sampleCount);
                    for (int z = 0; z < sampleCount; z++) {
                        for (int x = 0; x < sampleCount; x++) {
                            float worldX = worldMinX + x * sampleSpacingX;
                            float worldZ = worldMinZ + z * sampleSpacingZ;
                            heightData[z * sampleCount + x] = m_terrain.getHeightAt(worldX, worldZ);
                        }
                    }

                    // Add heightfield - offset is the corner position, scale is per-sample
                    glm::vec3 offset(worldMinX, 0.0f, worldMinZ);
                    glm::vec3 scale(sampleSpacingX, 1.0f, sampleSpacingZ);
                    m_characterController->addTerrainHeightfield(heightData, sampleCount, offset, scale);
                    std::cout << "Added terrain heightfield to Jolt (" << sampleCount << "x" << sampleCount << " samples)" << std::endl;
                }
            }

            // Add kinematic platforms first (lifts, moving platforms)
            // These are added regardless of other collision settings
            int kinematicCount = 0;
            for (auto& obj : m_sceneObjects) {
                if (!obj || !obj->isVisible()) continue;
                if (!obj->isKinematicPlatform()) continue;

                // Get LOCAL bounds (before rotation) and apply rotation to physics body
                AABB localBounds = obj->getLocalBounds();
                glm::vec3 localHalfExtents = (localBounds.max - localBounds.min) * 0.5f;

                // Calculate local center offset (in case pivot isn't at mesh center)
                glm::vec3 localCenterOffset = (localBounds.min + localBounds.max) * 0.5f;

                // Apply scale to half extents and offset
                glm::vec3 scale = obj->getTransform().getScale();
                localHalfExtents *= scale;
                localCenterOffset *= scale;

                // Get world position and rotation
                glm::vec3 position = obj->getTransform().getPosition();
                glm::quat rotation = obj->getTransform().getRotation();

                // Calculate world center by rotating the local offset
                glm::vec3 worldCenterOffset = rotation * localCenterOffset;
                glm::vec3 center = position + worldCenterOffset;

                // Store the offset for use during updates
                obj->setPhysicsOffset(localCenterOffset);

                // Create physics body with proper rotation
                uint32_t bodyId = m_characterController->addKinematicPlatform(localHalfExtents, center, rotation);
                obj->setJoltBodyId(bodyId);
                std::cout << "Added kinematic platform: " << obj->getName()
                          << " halfExtents=(" << localHalfExtents.x << "," << localHalfExtents.y << "," << localHalfExtents.z << ")"
                          << " offset=(" << localCenterOffset.x << "," << localCenterOffset.y << "," << localCenterOffset.z << ")" << std::endl;
                kinematicCount++;
            }
            if (kinematicCount == 0) {
                std::cout << "No kinematic platforms found in scene" << std::endl;
            }

            // Add collision bodies from scene objects with Bullet collision
            for (auto& obj : m_sceneObjects) {
                if (!obj || !obj->isVisible()) continue;
                if (!obj->hasBulletCollision()) continue;
                if (obj->isKinematicPlatform()) continue;  // Already added above

                // Get mesh data for collision (static)
                if (obj->hasMeshData()) {
                    const auto& modelVerts = obj->getVertices();
                    const auto& indices = obj->getIndices();

                    // Extract just positions from ModelVertex
                    std::vector<glm::vec3> positions;
                    positions.reserve(modelVerts.size());
                    for (const auto& v : modelVerts) {
                        positions.push_back(v.position);
                    }

                    glm::mat4 transform = obj->getTransform().getMatrix();
                    m_characterController->addStaticMesh(positions, indices, transform);
                }
            }

            // Add AABB collision objects as boxes
            for (auto& obj : m_sceneObjects) {
                if (!obj || !obj->isVisible()) continue;
                if (!obj->hasAABBCollision() && !obj->hasCollision()) continue;
                if (obj->hasBulletCollision()) continue;  // Already added as mesh
                if (obj->isKinematicPlatform()) continue;  // Already added above

                AABB bounds = obj->getWorldBounds();
                glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
                glm::vec3 halfExtents = (bounds.max - bounds.min) * 0.5f;
                m_characterController->addStaticBox(halfExtents, center);
            }

            // For Homebrew physics, set up terrain height query as backup
            if (m_physicsBackend == PhysicsBackend::Homebrew) {
                auto* homebrew = dynamic_cast<HomebrewCharacter*>(m_characterController.get());
                if (homebrew && !m_isTestLevel) {
                    homebrew->setHeightQueryFunction([this](float x, float z) {
                        return m_terrain.getHeightAt(x, z);
                    });
                }
            }

            // Create player character at camera position with settings from UI
            glm::vec3 playerPos = m_camera.getPosition();
            float charHeight = m_editorUI.getCharacterHeight();
            float charRadius = m_editorUI.getCharacterRadius();
            playerPos.y -= charHeight * 0.5f;  // Character center is below eye level
            m_characterController->createCharacter(playerPos, charHeight, charRadius);

            // Apply gravity setting from UI
            m_characterController->setGravity(m_editorUI.getCharacterGravity());

            std::cout << "Character controller initialized (height=" << charHeight
                      << "m, radius=" << charRadius << "m, gravity=" << m_editorUI.getCharacterGravity()
                      << "m/s²)" << std::endl;
        }

        std::cout << "Entered PLAY MODE at " << formatGameTimeDisplay(m_gameTimeMinutes)
                  << " (Space=shoot, double-tap Alt=toggle fly/walk)" << std::endl;
    }

    void startGameStartBehaviors(SceneObject* obj) {
        if (!obj) return;

        auto& behaviors = obj->getBehaviors();
        for (size_t i = 0; i < behaviors.size(); i++) {
            Behavior& beh = behaviors[i];
            if (beh.trigger == TriggerType::ON_GAMESTART && beh.enabled && !beh.actions.empty()) {
                // Set this as the active behavior
                obj->setActiveBehaviorIndex(static_cast<int>(i));
                obj->setActiveActionIndex(0);
                obj->resetPathComplete();
                obj->clearPathWaypoints();

                std::cout << "Started ON_GAMESTART behavior for " << obj->getName() << std::endl;

                // If first action is FOLLOW_PATH, load the path
                if (beh.actions[0].type == ActionType::FOLLOW_PATH) {
                    loadPathForAction(obj, beh.actions[0]);
                }
                break;  // Only start the first ON_GAMESTART behavior
            }
        }
    }

    void loadPathForAction(SceneObject* obj, const Action& action) {
        if (action.type != ActionType::FOLLOW_PATH) return;

        std::string pathName = action.stringParam;
        AIPath* path = getPathByName(pathName);
        if (path && path->getWaypointCount() > 0) {
            obj->setCurrentPathName(pathName);
            obj->setCurrentPathWaypoints(path->getWaypoints());
            obj->setCurrentWaypointIndex(0);  // Always start at beginning
            obj->setPatrolLoop(false);  // Don't loop path - behavior.loop handles repetition
            obj->resetPathComplete();
        }
    }

    // Check ON_GAME_TIME triggers at game start (for triggers that match the initial time)
    void checkInitialGameTimeTriggers(int currentMinute) {
        for (auto& objPtr : m_sceneObjects) {
            if (!objPtr || !objPtr->hasBehaviors()) continue;
            if (objPtr->hasActiveBehavior()) continue;  // Already has a behavior from ON_GAMESTART

            auto& behaviors = objPtr->getBehaviors();
            for (size_t i = 0; i < behaviors.size(); i++) {
                Behavior& beh = behaviors[i];

                if (!beh.enabled || beh.actions.empty()) continue;

                if (beh.trigger == TriggerType::ON_GAME_TIME) {
                    int triggerTime = parseTimeString(beh.triggerParam);
                    if (triggerTime < 0) continue;

                    // Check if trigger time matches current time (exact match for game start)
                    if (triggerTime == currentMinute) {
                        std::cout << "ON_GAME_TIME triggered at start for " << objPtr->getName()
                                  << " at " << formatGameTimeDisplay(static_cast<float>(triggerTime)) << std::endl;

                        objPtr->setActiveBehaviorIndex(static_cast<int>(i));
                        objPtr->setActiveActionIndex(0);
                        objPtr->resetPathComplete();
                        objPtr->clearPathWaypoints();

                        if (beh.actions[0].type == ActionType::FOLLOW_PATH) {
                            loadPathForAction(objPtr.get(), beh.actions[0]);
                        }
                        break;  // Only start first matching behavior
                    }
                }
            }
        }
    }

    // Check and trigger ON_GAME_TIME behaviors when game time reaches their trigger time
    void checkGameTimeTriggers(int previousMinute, int currentMinute) {
        // Handle day wrap-around (e.g., 1439 -> 0)
        bool wrapped = currentMinute < previousMinute;

        // At midnight, reset daily schedule NPCs so their behaviors can trigger again
        if (wrapped) {
            for (auto& objPtr : m_sceneObjects) {
                if (objPtr && objPtr->hasDailySchedule()) {
                    objPtr->clearActiveBehavior();
                    objPtr->clearPathWaypoints();
                    std::cout << "New day - reset daily schedule for " << objPtr->getName() << std::endl;
                }
            }
        }

        for (auto& objPtr : m_sceneObjects) {
            if (!objPtr || !objPtr->hasBehaviors()) continue;

            auto& behaviors = objPtr->getBehaviors();

            // FIRST: Check exit conditions for active behaviors (so they clear before new triggers)
            if (objPtr->hasActiveBehavior()) {
                int behaviorIdx = objPtr->getActiveBehaviorIndex();
                if (behaviorIdx >= 0 && behaviorIdx < static_cast<int>(behaviors.size())) {
                    Behavior& activeBeh = behaviors[behaviorIdx];
                    if (activeBeh.exitCondition == ExitCondition::ON_GAME_TIME) {
                        int exitTime = parseTimeString(activeBeh.exitParam);
                        if (exitTime >= 0) {
                            bool shouldExit = false;
                            if (wrapped) {
                                shouldExit = (exitTime > previousMinute) || (exitTime <= currentMinute);
                            } else {
                                shouldExit = (exitTime > previousMinute && exitTime <= currentMinute);
                            }

                            if (shouldExit) {
                                std::cout << "ON_GAME_TIME exit for " << objPtr->getName()
                                          << " at " << formatGameTimeDisplay(static_cast<float>(exitTime)) << std::endl;
                                objPtr->clearActiveBehavior();
                                objPtr->clearPathWaypoints();
                            }
                        }
                    }
                }
            }

            // SECOND: Check for new triggers (now that exit conditions have been processed)
            for (size_t i = 0; i < behaviors.size(); i++) {
                Behavior& beh = behaviors[i];

                // Skip if already has an active behavior or this behavior is disabled
                if (objPtr->hasActiveBehavior()) continue;
                if (!beh.enabled || beh.actions.empty()) continue;

                if (beh.trigger == TriggerType::ON_GAME_TIME) {
                    int triggerTime = parseTimeString(beh.triggerParam);
                    if (triggerTime < 0) continue;

                    // Check if we crossed this trigger time
                    bool shouldTrigger = false;
                    if (wrapped) {
                        // Day wrapped: check if trigger is after previous OR before/at current
                        shouldTrigger = (triggerTime > previousMinute) || (triggerTime <= currentMinute);
                    } else {
                        // Normal: check if trigger is between previous and current (exclusive/inclusive)
                        shouldTrigger = (triggerTime > previousMinute && triggerTime <= currentMinute);
                    }

                    if (shouldTrigger) {
                        std::cout << "ON_GAME_TIME triggered for " << objPtr->getName()
                                  << " at " << formatGameTimeDisplay(static_cast<float>(triggerTime)) << std::endl;

                        objPtr->setActiveBehaviorIndex(static_cast<int>(i));
                        objPtr->setActiveActionIndex(0);
                        objPtr->resetPathComplete();
                        objPtr->clearPathWaypoints();

                        if (beh.actions[0].type == ActionType::FOLLOW_PATH) {
                            loadPathForAction(objPtr.get(), beh.actions[0]);
                        }
                        break;  // Only start one behavior per check
                    }
                }
            }
        }

        // Notify game module we're entering play mode
        if (m_gameModule) {
            m_gameModule->onEnterPlayMode();
        }
    }

    void exitPlayMode() {
        // Auto-save game state before exiting play mode
        saveGame();

        // Notify game module we're exiting play mode
        if (m_gameModule) {
            m_gameModule->onExitPlayMode();
        }

        m_isPlayMode = false;
        m_playModeDebug = false;

        // Enable noclip for editor mode (can go below terrain)
        m_camera.setNoClip(true);

        // Clear Jolt body IDs from scene objects
        for (auto& objPtr : m_sceneObjects) {
            if (objPtr && objPtr->hasJoltBody()) {
                objPtr->clearJoltBody();
            }
        }

        // Destroy Jolt character controller
        m_characterController.reset();

        // Destroy Bullet character controller (legacy)
        if (m_physicsWorld) {
            m_physicsWorld->destroyCharacterController();
        }

        // Stop engine hum
        if (m_engineHumLoopId >= 0) {
            Audio::getInstance().stopLoop(m_engineHumLoopId);
            m_engineHumLoopId = -1;
        }

        // Keep camera at current position (don't restore editor position)
        Input::setMouseCaptured(false);

        // Restore AI node visibility for editor
        if (m_aiNodeRenderer) {
            m_aiNodeRenderer->setVisible(true);
        }

        std::cout << "Exited PLAY MODE" << std::endl;
    }

    void interactWithCrosshair() {
        float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 5000.0f);
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 invVP = glm::inverse(proj * view);

        glm::vec4 nearPoint = invVP * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
        glm::vec4 farPoint = invVP * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        glm::vec3 rayOrigin = glm::vec3(nearPoint);
        glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

        float closestDist = std::numeric_limits<float>::max();
        SceneObject* closestObj = nullptr;

        for (auto& obj : m_sceneObjects) {
            if (!obj || !obj->isVisible()) continue;

            AABB worldBounds = obj->getWorldBounds();
            float dist = worldBounds.intersect(rayOrigin, rayDir);

            if (dist >= 0 && dist < closestDist && dist < 10.0f) {
                closestDist = dist;
                closestObj = obj.get();
            }
        }

        if (closestObj) {
            // Check if this is a door - handle level transition or teleport
            if (closestObj->isDoor()) {
                std::string targetLevel = closestObj->getTargetLevel();
                std::string targetDoorId = closestObj->getTargetDoorId();

                if (!targetLevel.empty() && targetLevel.rfind("fs://", 0) == 0) {
                    // Filesystem navigation — extract path after fs://
                    std::string fsPath = targetLevel.substr(5);
                    m_filesystemBrowser.setSpawnOrigin(closestObj->getTransform().getPosition());
                    m_filesystemBrowser.navigate(fsPath);
                } else if (!targetLevel.empty()) {
                    // Level transition - load new level
                    transitionToLevel(targetLevel, targetDoorId);
                } else if (!targetDoorId.empty()) {
                    // Teleport within same level - find target door
                    teleportToDoor(targetDoorId);
                }
                return;
            }

            closestObj->triggerBehavior(TriggerType::ON_INTERACT);
        }
    }

    void transitionToLevel(const std::string& levelPath, const std::string& targetDoorId) {
        // Resolve level path (relative to current level's directory)
        std::string fullPath = levelPath;
        if (!m_currentLevelPath.empty() && levelPath.find('/') == std::string::npos) {
            size_t lastSlash = m_currentLevelPath.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                fullPath = m_currentLevelPath.substr(0, lastSlash + 1) + levelPath;
            }
        }

        // Store pending transition and start fade out
        m_pendingLevelPath = fullPath;
        m_pendingTargetDoorId = targetDoorId;
        m_fadeState = FadeState::FADING_OUT;
        m_fadeAlpha = 0.0f;
    }

    void teleportToDoor(const std::string& targetDoorId) {
        // Find target door in current level
        for (const auto& obj : m_sceneObjects) {
            if (obj && obj->isDoor() && obj->getDoorId() == targetDoorId) {
                glm::vec3 doorPos = obj->getTransform().getPosition();
                glm::vec3 teleportPos = doorPos + glm::vec3(0, 0.5f, 0);

                // Teleport the player
                if (m_characterController) {
                    m_characterController->setPosition(teleportPos);
                }

                // Quick fade effect for visual feedback
                m_fadeState = FadeState::FADING_IN;
                m_fadeAlpha = 0.5f;  // Start half-faded for quick flash

                std::cout << "Teleported to door: " << targetDoorId << std::endl;
                return;
            }
        }

        std::cerr << "Teleport failed: door not found: " << targetDoorId << std::endl;
    }

    void executeTransition() {
        // Called when fade-out is complete
        m_pendingDoorSpawn = m_pendingTargetDoorId;

        // Exit current play mode
        exitPlayMode();

        // Load the new level
        loadLevel(m_pendingLevelPath);

        // Find the target door and set spawn position
        if (!m_pendingDoorSpawn.empty()) {
            for (const auto& obj : m_sceneObjects) {
                if (obj && obj->isDoor() && obj->getDoorId() == m_pendingDoorSpawn) {
                    glm::vec3 doorPos = obj->getTransform().getPosition();
                    m_spawnPosition = doorPos + glm::vec3(0, 0.5f, 0);
                    m_hasSpawnPoint = true;
                    break;
                }
            }
            m_pendingDoorSpawn.clear();
        }

        // Re-enter play mode at the door position
        enterPlayMode();

        // Preload adjacent levels from this new level's doors
        preloadAdjacentLevels();

        // Clear pending transition
        m_pendingLevelPath.clear();
        m_pendingTargetDoorId.clear();
    }

    void updateFade(float deltaTime) {
        switch (m_fadeState) {
            case FadeState::FADING_OUT:
                m_fadeAlpha += deltaTime / m_fadeDuration;
                if (m_fadeAlpha >= 1.0f) {
                    m_fadeAlpha = 1.0f;
                    m_fadeState = FadeState::LOADING;
                }
                break;

            case FadeState::LOADING:
                // Execute the actual transition (screen is black)
                executeTransition();
                m_fadeState = FadeState::FADING_IN;
                break;

            case FadeState::FADING_IN:
                m_fadeAlpha -= deltaTime / m_fadeDuration;
                if (m_fadeAlpha <= 0.0f) {
                    m_fadeAlpha = 0.0f;
                    m_fadeState = FadeState::NONE;
                }
                break;

            case FadeState::NONE:
            default:
                break;
        }
    }

    void renderFadeOverlay() {
        if (m_fadeState == FadeState::NONE || m_fadeAlpha <= 0.0f) return;

        // Full-screen black overlay using ImGui
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, m_fadeAlpha));

        ImGui::Begin("##FadeOverlay", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    void preloadAdjacentLevels() {
        // Scan all doors and preload their target levels
        std::set<std::string> levelsToPreload;

        for (const auto& obj : m_sceneObjects) {
            if (obj && obj->isDoor()) {
                std::string targetLevel = obj->getTargetLevel();
                if (!targetLevel.empty()) {
                    // Resolve relative path
                    std::string fullPath = targetLevel;
                    if (!m_currentLevelPath.empty() && targetLevel.find('/') == std::string::npos) {
                        size_t lastSlash = m_currentLevelPath.find_last_of("/\\");
                        if (lastSlash != std::string::npos) {
                            fullPath = m_currentLevelPath.substr(0, lastSlash + 1) + targetLevel;
                        }
                    }

                    // Only preload if not already cached
                    if (m_levelCache.find(fullPath) == m_levelCache.end()) {
                        levelsToPreload.insert(fullPath);
                    }
                }
            }
        }

        // Preload each unique level
        for (const auto& levelPath : levelsToPreload) {
            LevelData levelData;
            if (LevelSerializer::load(levelPath, levelData)) {
                m_levelCache[levelPath] = std::move(levelData);
            }
        }
    }

    void focusOnSelectedObject() {
        if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) return;

        SceneObject* obj = m_sceneObjects[m_selectedObjectIndex].get();
        if (!obj) return;

        glm::vec3 objPos = obj->getTransform().getPosition();
        AABB bounds = obj->getWorldBounds();
        float size = glm::length(bounds.max - bounds.min);
        float distance = std::max(size * 2.0f, 5.0f);

        glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
        glm::vec3 camPos = center - m_camera.getFront() * distance;
        m_camera.setPosition(camPos);
        m_orbitTarget = center;

        std::cout << "Focused on: " << obj->getName() << std::endl;
    }

    // Snap selected object's edge to nearest edge on another object
    // Small overlap to eliminate visible seams between snapped tiles
    void snapToTerrain() {
        if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) {
            std::cout << "No object selected for terrain snap" << std::endl;
            return;
        }

        SceneObject* selected = m_sceneObjects[m_selectedObjectIndex].get();
        if (!selected) return;

        glm::vec3 pos = selected->getTransform().getPosition();
        float terrainY = m_terrain.getHeightAt(pos.x, pos.z);

        // Lift so the bottom of the object sits on the terrain
        const AABB& localBounds = selected->getLocalBounds();
        float scaleY = selected->getTransform().getScale().y;
        float bottomOffset = -localBounds.min.y * scaleY;

        pos.y = terrainY + bottomOffset;
        selected->getTransform().setPosition(pos);

        std::cout << "Snapped '" << selected->getName() << "' to terrain at Y=" << pos.y << std::endl;
    }

    static constexpr float SNAP_OVERLAP = 0.005f;

    void snapToNearestEdge() {
        if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) {
            std::cout << "No object selected for edge snap" << std::endl;
            return;
        }

        SceneObject* selected = m_sceneObjects[m_selectedObjectIndex].get();
        if (!selected) return;

        AABB selectedBounds = selected->getWorldBounds();
        glm::vec3 selectedCenter = (selectedBounds.min + selectedBounds.max) * 0.5f;

        // Define edge center points for selected object (at Y center)
        float selectedYCenter = selectedCenter.y;
        struct Edge {
            glm::vec3 point;      // Center point of edge
            int axis;             // 0 = X, 2 = Z
            int sign;             // -1 = min side, +1 = max side
        };

        std::vector<Edge> selectedEdges = {
            {{selectedBounds.max.x, selectedYCenter, selectedCenter.z}, 0, +1},  // +X (right)
            {{selectedBounds.min.x, selectedYCenter, selectedCenter.z}, 0, -1},  // -X (left)
            {{selectedCenter.x, selectedYCenter, selectedBounds.max.z}, 2, +1},  // +Z (front)
            {{selectedCenter.x, selectedYCenter, selectedBounds.min.z}, 2, -1},  // -Z (back)
        };

        float closestDist = FLT_MAX;
        glm::vec3 snapTranslation(0.0f);
        std::string snapInfo;

        // Check all other objects
        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            if (static_cast<int>(i) == m_selectedObjectIndex) continue;

            SceneObject* other = m_sceneObjects[i].get();
            if (!other || !other->isVisible()) continue;

            AABB otherBounds = other->getWorldBounds();
            glm::vec3 otherCenter = (otherBounds.min + otherBounds.max) * 0.5f;
            float otherYCenter = otherCenter.y;

            // Define edge center points for other object
            std::vector<Edge> otherEdges = {
                {{otherBounds.max.x, otherYCenter, otherCenter.z}, 0, +1},  // +X
                {{otherBounds.min.x, otherYCenter, otherCenter.z}, 0, -1},  // -X
                {{otherCenter.x, otherYCenter, otherBounds.max.z}, 2, +1},  // +Z
                {{otherCenter.x, otherYCenter, otherBounds.min.z}, 2, -1},  // -Z
            };

            // Find closest matching opposite edges
            for (const auto& selEdge : selectedEdges) {
                for (const auto& othEdge : otherEdges) {
                    // Only match opposite edges on same axis
                    // Selected +X should match other -X, etc.
                    if (selEdge.axis != othEdge.axis) continue;
                    if (selEdge.sign == othEdge.sign) continue;  // Need opposite signs

                    // Calculate distance between edge centers
                    float dist = glm::distance(selEdge.point, othEdge.point);

                    if (dist < closestDist) {
                        closestDist = dist;

                        // Calculate translation to snap edges together
                        // Move selected so its edge aligns with other's edge
                        snapTranslation = othEdge.point - selEdge.point;

                        // Keep Y unchanged (only snap X/Z)
                        snapTranslation.y = 0.0f;

                        // Add tiny overlap to eliminate seams
                        // Push selected toward other by SNAP_OVERLAP in the snap direction
                        if (selEdge.axis == 0) {  // X axis
                            snapTranslation.x += (selEdge.sign > 0 ? SNAP_OVERLAP : -SNAP_OVERLAP);
                        } else {  // Z axis
                            snapTranslation.z += (selEdge.sign > 0 ? SNAP_OVERLAP : -SNAP_OVERLAP);
                        }

                        const char* axisNames[] = {"X", "", "Z"};
                        const char* sideNames[] = {"min", "", "max"};
                        snapInfo = std::string("Snapping ") + axisNames[selEdge.axis] +
                                   (selEdge.sign > 0 ? "+" : "-") + " to " + other->getName();
                    }
                }
            }
        }

        // Apply snap if we found a close edge (within reasonable distance)
        if (closestDist < 50.0f) {  // Max snap distance
            glm::vec3 currentPos = selected->getTransform().getPosition();
            selected->getTransform().setPosition(currentPos + snapTranslation);
            std::cout << snapInfo << " (distance: " << closestDist << ")" << std::endl;
        } else {
            std::cout << "No nearby edge found to snap to" << std::endl;
        }
    }

    // Snap selected object vertically to stack on top/bottom of another object
    void snapToNearestVerticalEdge() {
        if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) {
            std::cout << "No object selected for vertical snap" << std::endl;
            return;
        }

        SceneObject* selected = m_sceneObjects[m_selectedObjectIndex].get();
        if (!selected) return;

        AABB selectedBounds = selected->getWorldBounds();
        glm::vec3 selectedCenter = (selectedBounds.min + selectedBounds.max) * 0.5f;

        // Define top and bottom face center points
        struct Face {
            glm::vec3 point;      // Center point of face
            int sign;             // -1 = bottom, +1 = top
        };

        std::vector<Face> selectedFaces = {
            {{selectedCenter.x, selectedBounds.max.y, selectedCenter.z}, +1},  // Top
            {{selectedCenter.x, selectedBounds.min.y, selectedCenter.z}, -1},  // Bottom
        };

        float closestDist = FLT_MAX;
        glm::vec3 snapTranslation(0.0f);
        std::string snapInfo;

        // Check all other objects
        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            if (static_cast<int>(i) == m_selectedObjectIndex) continue;

            SceneObject* other = m_sceneObjects[i].get();
            if (!other || !other->isVisible()) continue;

            AABB otherBounds = other->getWorldBounds();
            glm::vec3 otherCenter = (otherBounds.min + otherBounds.max) * 0.5f;

            // Define top and bottom faces for other object
            std::vector<Face> otherFaces = {
                {{otherCenter.x, otherBounds.max.y, otherCenter.z}, +1},  // Top
                {{otherCenter.x, otherBounds.min.y, otherCenter.z}, -1},  // Bottom
            };

            // Find closest matching opposite faces
            for (const auto& selFace : selectedFaces) {
                for (const auto& othFace : otherFaces) {
                    // Only match opposite faces
                    // Selected top should match other bottom (stacking below)
                    // Selected bottom should match other top (stacking on top)
                    if (selFace.sign == othFace.sign) continue;

                    // For vertical stacking, we care about horizontal distance (X/Z)
                    // to find objects that are roughly aligned
                    float horizDist = glm::distance(
                        glm::vec2(selFace.point.x, selFace.point.z),
                        glm::vec2(othFace.point.x, othFace.point.z));

                    // Also consider vertical distance for overall closest
                    float vertDist = std::abs(selFace.point.y - othFace.point.y);
                    float totalDist = horizDist + vertDist * 0.5f;  // Weight vertical less

                    if (totalDist < closestDist) {
                        closestDist = totalDist;

                        // Calculate translation to snap faces together
                        // Only move in Y direction
                        snapTranslation = glm::vec3(0.0f, othFace.point.y - selFace.point.y, 0.0f);

                        // Add tiny overlap to eliminate seams
                        // If selected top (+Y) snaps to other's bottom, push UP to overlap
                        // If selected bottom (-Y) snaps to other's top, push DOWN to overlap
                        snapTranslation.y += (selFace.sign > 0 ? SNAP_OVERLAP : -SNAP_OVERLAP);

                        snapInfo = std::string("Stacking ") +
                                   (selFace.sign > 0 ? "top" : "bottom") + " to " +
                                   other->getName() + "'s " +
                                   (othFace.sign > 0 ? "top" : "bottom");
                    }
                }
            }
        }

        // Apply snap if we found a close face (within reasonable distance)
        if (closestDist < 50.0f) {  // Max snap distance
            glm::vec3 currentPos = selected->getTransform().getPosition();
            selected->getTransform().setPosition(currentPos + snapTranslation);
            std::cout << snapInfo << " (distance: " << closestDist << ")" << std::endl;
        } else {
            std::cout << "No nearby object found to stack on" << std::endl;
        }
    }

    // Full 3D snap - align with nearest surface on any axis
    void snapFullAlign() {
        if (m_selectedObjectIndex < 0 || m_selectedObjectIndex >= static_cast<int>(m_sceneObjects.size())) {
            std::cout << "No object selected for full align" << std::endl;
            return;
        }

        SceneObject* selected = m_sceneObjects[m_selectedObjectIndex].get();
        if (!selected) return;

        AABB selectedBounds = selected->getWorldBounds();
        glm::vec3 selectedCenter = (selectedBounds.min + selectedBounds.max) * 0.5f;

        // Define all 6 face center points
        struct Face {
            glm::vec3 point;      // Center point of face
            int axis;             // 0 = X, 1 = Y, 2 = Z
            int sign;             // -1 = min side, +1 = max side
        };

        std::vector<Face> selectedFaces = {
            {{selectedBounds.max.x, selectedCenter.y, selectedCenter.z}, 0, +1},  // +X
            {{selectedBounds.min.x, selectedCenter.y, selectedCenter.z}, 0, -1},  // -X
            {{selectedCenter.x, selectedBounds.max.y, selectedCenter.z}, 1, +1},  // +Y (top)
            {{selectedCenter.x, selectedBounds.min.y, selectedCenter.z}, 1, -1},  // -Y (bottom)
            {{selectedCenter.x, selectedCenter.y, selectedBounds.max.z}, 2, +1},  // +Z
            {{selectedCenter.x, selectedCenter.y, selectedBounds.min.z}, 2, -1},  // -Z
        };

        float closestDist = FLT_MAX;
        glm::vec3 snapTranslation(0.0f);
        std::string snapInfo;

        // Check all other objects
        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            if (static_cast<int>(i) == m_selectedObjectIndex) continue;

            SceneObject* other = m_sceneObjects[i].get();
            if (!other || !other->isVisible()) continue;

            AABB otherBounds = other->getWorldBounds();
            glm::vec3 otherCenter = (otherBounds.min + otherBounds.max) * 0.5f;

            // Define all 6 faces for other object
            std::vector<Face> otherFaces = {
                {{otherBounds.max.x, otherCenter.y, otherCenter.z}, 0, +1},  // +X
                {{otherBounds.min.x, otherCenter.y, otherCenter.z}, 0, -1},  // -X
                {{otherCenter.x, otherBounds.max.y, otherCenter.z}, 1, +1},  // +Y
                {{otherCenter.x, otherBounds.min.y, otherCenter.z}, 1, -1},  // -Y
                {{otherCenter.x, otherCenter.y, otherBounds.max.z}, 2, +1},  // +Z
                {{otherCenter.x, otherCenter.y, otherBounds.min.z}, 2, -1},  // -Z
            };

            // Find closest matching opposite faces on same axis
            for (const auto& selFace : selectedFaces) {
                for (const auto& othFace : otherFaces) {
                    // Only match opposite faces on same axis
                    if (selFace.axis != othFace.axis) continue;
                    if (selFace.sign == othFace.sign) continue;

                    // Calculate distance between face centers
                    float dist = glm::distance(selFace.point, othFace.point);

                    if (dist < closestDist) {
                        closestDist = dist;

                        // Calculate full 3D translation to align faces
                        snapTranslation = othFace.point - selFace.point;

                        // Add tiny overlap to eliminate seams
                        // Push selected toward other (in direction of snap)
                        float overlapDir = (selFace.sign > 0 ? SNAP_OVERLAP : -SNAP_OVERLAP);
                        if (selFace.axis == 0) snapTranslation.x += overlapDir;
                        else if (selFace.axis == 1) snapTranslation.y += overlapDir;
                        else snapTranslation.z += overlapDir;

                        const char* axisNames[] = {"X", "Y", "Z"};
                        snapInfo = std::string("Aligning ") + axisNames[selFace.axis] +
                                   (selFace.sign > 0 ? "+" : "-") + " to " + other->getName();
                    }
                }
            }
        }

        // Apply snap if we found a close face (within reasonable distance)
        if (closestDist < 50.0f) {
            glm::vec3 currentPos = selected->getTransform().getPosition();
            selected->getTransform().setPosition(currentPos + snapTranslation);
            std::cout << snapInfo << " (distance: " << closestDist << ")" << std::endl;
        } else {
            std::cout << "No nearby surface found to align" << std::endl;
        }
    }

    // Helper: get floor height for object placement
    // If camera is underground (below terrain), use subfloor height
    // Otherwise use terrain height
    float getPlacementFloorHeight(float x, float z) {
        float terrainHeight = m_terrain.getHeightAt(x, z);
        float cameraY = m_camera.getPosition().y;

        // If camera is below terrain, we're editing underground - use subfloor
        if (cameraY < terrainHeight - 1.0f) {
            return SUBFLOOR_HEIGHT;
        }
        return terrainHeight;
    }

    void addSpawnPoint() {
        glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 10.0f;
        const float eyeHeight = 1.7f;
        spawnPos.y = getPlacementFloorHeight(spawnPos.x, spawnPos.z) + eyeHeight;

        if (m_spawnObjectIndex >= 0 && m_spawnObjectIndex < static_cast<int>(m_sceneObjects.size())) {
            m_sceneObjects[m_spawnObjectIndex]->getTransform().setPosition(spawnPos);
            std::cout << "Moved spawn point to: " << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << std::endl;
        } else {
            auto spawnMarker = createSpawnMarkerObject();
            if (spawnMarker) {
                spawnMarker->getTransform().setPosition(spawnPos);
                m_sceneObjects.push_back(std::move(spawnMarker));
                m_spawnObjectIndex = static_cast<int>(m_sceneObjects.size()) - 1;
                selectObject(m_spawnObjectIndex);
                std::cout << "Created spawn point at: " << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << std::endl;
            }
        }

        m_hasSpawnPoint = true;
        m_spawnPosition = spawnPos;
    }

    void addCylinder(float radius = 2.0f, float height = 4.0f, int segments = 32,
                     const glm::vec4& color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f)) {
        auto meshData = PrimitiveMeshBuilder::createCylinder(radius, height, segments, color);

        auto obj = std::make_unique<SceneObject>(generateUniqueName("Cylinder"));

        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);
        obj->setModelPath("");
        obj->setMeshData(meshData.vertices, meshData.indices);

        // Store primitive info for save/load
        obj->setPrimitiveType(PrimitiveType::Cylinder);
        obj->setPrimitiveRadius(radius);
        obj->setPrimitiveHeight(height);
        obj->setPrimitiveSegments(segments);
        obj->setPrimitiveColor(color);

        glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 10.0f;
        spawnPos.y = getPlacementFloorHeight(spawnPos.x, spawnPos.z);
        obj->getTransform().setPosition(spawnPos);

        m_sceneObjects.push_back(std::move(obj));
        selectObject(static_cast<int>(m_sceneObjects.size()) - 1);

        std::cout << "Created cylinder" << std::endl;
    }

    void shootProjectile() {
        // Create a small cube projectile
        float size = 0.3f;
        auto meshData = PrimitiveMeshBuilder::createCube(size);

        auto obj = std::make_unique<SceneObject>("Projectile");
        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);

        // Position at camera, slightly in front
        glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 1.0f;
        obj->getTransform().setPosition(spawnPos);

        // Make it a distinct color (red-ish via hue shift)
        obj->setHueShift(0.0f);
        obj->setSaturation(2.0f);
        obj->setBrightness(1.5f);

        m_sceneObjects.push_back(std::move(obj));
        int objIndex = static_cast<int>(m_sceneObjects.size()) - 1;

        // Create projectile data
        Projectile proj;
        proj.position = spawnPos;
        proj.startPosition = spawnPos;
        proj.velocity = m_camera.getFront() * 200.0f;  // 200 m/s - fast like a bullet
        proj.size = size;
        proj.lifetime = 0.0f;
        proj.sceneObjectIndex = objIndex;
        proj.isEnemy = false;

        m_projectiles.push_back(proj);
    }

    void spawnEnemyProjectile(const glm::vec3& position, const glm::vec3& direction) {
        // Create a small cube projectile for enemy
        float size = 0.25f;
        auto meshData = PrimitiveMeshBuilder::createCube(size);

        auto obj = std::make_unique<SceneObject>("EnemyProjectile");
        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);

        glm::vec3 spawnPos = position + direction * 2.0f;  // Spawn slightly in front
        obj->getTransform().setPosition(spawnPos);

        // Make it green-ish to distinguish from player projectiles
        obj->setHueShift(0.3f);
        obj->setSaturation(2.0f);
        obj->setBrightness(1.5f);

        m_sceneObjects.push_back(std::move(obj));
        int objIndex = static_cast<int>(m_sceneObjects.size()) - 1;

        // Create projectile data
        Projectile proj;
        proj.position = spawnPos;
        proj.startPosition = spawnPos;
        proj.velocity = direction * 150.0f;  // Slightly slower than player
        proj.size = size;
        proj.lifetime = 0.0f;
        proj.sceneObjectIndex = objIndex;
        proj.isEnemy = true;

        m_projectiles.push_back(proj);
    }

    void updateProjectiles(float deltaTime) {
        // Update hit flash timers for all objects
        for (auto& obj : m_sceneObjects) {
            if (obj) {
                obj->updateHitFlash(deltaTime);
            }
        }

        // Update all projectiles and remove ones that hit terrain or reach max range
        for (auto it = m_projectiles.begin(); it != m_projectiles.end(); ) {
            Projectile& proj = *it;
            proj.lifetime += deltaTime;

            // Move projectile (minimal gravity for slight drop over distance)
            proj.position += proj.velocity * deltaTime;
            proj.velocity.y -= 2.0f * deltaTime;  // Very slight gravity

            // Update scene object position
            if (proj.sceneObjectIndex >= 0 && proj.sceneObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                auto& obj = m_sceneObjects[proj.sceneObjectIndex];
                if (obj) {
                    obj->getTransform().setPosition(proj.position);
                }
            }

            // Check if enemy projectile hits player (invisible hitbox)
            bool hitPlayer = false;
            if (proj.isEnemy && m_isPlayMode) {
                glm::vec3 playerPos = m_camera.getPosition();
                float dist = glm::distance(proj.position, playerPos);
                if (dist < m_playerHitboxRadius) {
                    // Player got hit!
                    float damage = 5.0f;
                    m_playerHealth -= damage;
                    if (m_playerHealth < 0) m_playerHealth = 0;
                    hitPlayer = true;
                    std::cout << "Player hit! Health: " << m_playerHealth << "/" << m_playerMaxHealth << std::endl;
                }
            }

            // Check collision with trader/fighter models using bounding box (player projectiles only)
            bool hitTarget = false;
            if (!proj.isEnemy) {
                for (auto& obj : m_sceneObjects) {
                    if (!obj) continue;
                    if (obj->getName() == "Projectile") continue;  // Don't hit other projectiles
                    if (obj->getName() == "EnemyProjectile") continue;
                    if (obj->getName().find("Cargo_") == 0) continue;  // Don't hit cargo
                    if (obj->getName().find("EjectedPilot_") == 0) continue;  // Don't hit pilots

                    // Only check traders and fighters
                    bool isTarget = obj->isTrader() || obj->hasScript("fighter");
                    if (!isTarget) continue;

                    // Get world-space bounding box
                    AABB bounds = obj->getWorldBounds();

                    // Check if projectile is inside the bounding box (with small padding)
                    float padding = 0.5f;
                    bool insideX = proj.position.x >= bounds.min.x - padding && proj.position.x <= bounds.max.x + padding;
                    bool insideY = proj.position.y >= bounds.min.y - padding && proj.position.y <= bounds.max.y + padding;
                    bool insideZ = proj.position.z >= bounds.min.z - padding && proj.position.z <= bounds.max.z + padding;

                    if (insideX && insideY && insideZ) {
                        // Deal damage and flash
                        float damage = 10.0f;  // Player projectile damage
                        obj->takeDamage(damage);
                        obj->setUnderAttack(true, m_camera.getPosition());  // Alert! Track attacker
                        hitTarget = true;

                        std::cout << "Hit " << obj->getName() << " - Health: " << obj->getHealth() << "/" << obj->getMaxHealth();
                        if (obj->isDead()) {
                            std::cout << " - DESTROYED!";
                        }
                        std::cout << std::endl;
                        break;
                    }
                }
            }

            // Check terrain collision
            float terrainHeight = m_terrain.getHeightAt(proj.position.x, proj.position.z);
            bool hitTerrain = proj.position.y <= terrainHeight;

            // Check max range (300 meters)
            float distanceTraveled = glm::distance(proj.position, proj.startPosition);
            bool tooFar = distanceTraveled > 300.0f;

            if (hitTerrain || tooFar || hitTarget || hitPlayer) {
                // Remove the scene object
                if (proj.sceneObjectIndex >= 0 && proj.sceneObjectIndex < static_cast<int>(m_sceneObjects.size())) {
                    // Mark for deletion by setting to nullptr, we'll clean up
                    m_sceneObjects[proj.sceneObjectIndex].reset();
                }
                it = m_projectiles.erase(it);
            } else {
                ++it;
            }
        }

        // Clean up null scene objects (projectiles that were removed)
        m_sceneObjects.erase(
            std::remove_if(m_sceneObjects.begin(), m_sceneObjects.end(),
                [](const std::unique_ptr<SceneObject>& obj) { return !obj; }),
            m_sceneObjects.end()
        );

        // Update projectile indices after cleanup (they may have shifted)
        // This is a simple approach - rebuild indices
        for (auto& proj : m_projectiles) {
            // Find the projectile's scene object by checking position match
            for (int i = 0; i < static_cast<int>(m_sceneObjects.size()); i++) {
                if (m_sceneObjects[i]) {
                    const std::string& name = m_sceneObjects[i]->getName();
                    if (name == "Projectile" || name == "EnemyProjectile") {
                        glm::vec3 objPos = m_sceneObjects[i]->getTransform().getPosition();
                        if (glm::distance(objPos, proj.position) < 0.1f) {
                            proj.sceneObjectIndex = i;
                            break;
                        }
                    }
                }
            }
        }
    }

    void addCube(float size, const glm::vec4& color = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f)) {
        auto meshData = PrimitiveMeshBuilder::createCube(size, color);

        auto obj = std::make_unique<SceneObject>(generateUniqueName("Cube"));

        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);
        obj->setModelPath("");
        obj->setMeshData(meshData.vertices, meshData.indices);

        // Store primitive info for save/load
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(size);
        obj->setPrimitiveColor(color);

        glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 10.0f;
        spawnPos.y = getPlacementFloorHeight(spawnPos.x, spawnPos.z);
        obj->getTransform().setPosition(spawnPos);

        m_sceneObjects.push_back(std::move(obj));
        selectObject(static_cast<int>(m_sceneObjects.size()) - 1);

        std::cout << "Created cube (" << size << "m)" << std::endl;
    }

    // Spawn 4 golden corner posts to mark a purchased plot
    void spawnPlotPosts(int gridX, int gridZ) {
        if (!m_zoneSystem) return;

        // Check if posts already exist (avoid duplicates on reload)
        std::string baseName = "PlotPost_" + std::to_string(gridX) + "_" + std::to_string(gridZ);
        for (const auto& obj : m_sceneObjects) {
            if (obj->getName().find(baseName) == 0) return;  // Already exist
        }

        glm::vec2 center = m_zoneSystem->gridToWorld(gridX, gridZ);
        float half = m_zoneSystem->getCellSize() / 2.0f;

        glm::vec2 corners[4] = {
            {center.x - half, center.y - half},
            {center.x + half, center.y - half},
            {center.x + half, center.y + half},
            {center.x - half, center.y + half}
        };

        float postRadius = 0.15f;
        float postHeight = 4.0f;
        glm::vec4 postColor(1.0f, 0.85f, 0.0f, 1.0f);  // Golden yellow

        for (int i = 0; i < 4; i++) {
            auto meshData = PrimitiveMeshBuilder::createCylinder(postRadius, postHeight, 8, postColor);

            auto obj = std::make_unique<SceneObject>(baseName + "_" + std::to_string(i));
            uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
            obj->setLocalBounds(meshData.bounds);
            obj->setModelPath("");
            obj->setMeshData(meshData.vertices, meshData.indices);

            obj->setPrimitiveType(PrimitiveType::Cylinder);
            obj->setPrimitiveRadius(postRadius);
            obj->setPrimitiveHeight(postHeight);
            obj->setPrimitiveSegments(8);
            obj->setPrimitiveColor(postColor);

            float terrainY = m_terrain.getHeightAt(corners[i].x, corners[i].y);
            obj->getTransform().setPosition(glm::vec3(corners[i].x, terrainY, corners[i].y));

            m_sceneObjects.push_back(std::move(obj));
        }

        std::cout << "[Economy] Placed boundary posts for plot (" << gridX << ", " << gridZ << ")" << std::endl;
    }

    // Remove corner posts for a sold plot
    void removePlotPosts(int gridX, int gridZ) {
        std::string baseName = "PlotPost_" + std::to_string(gridX) + "_" + std::to_string(gridZ);

        for (auto it = m_sceneObjects.begin(); it != m_sceneObjects.end(); ) {
            if ((*it)->getName().find(baseName) == 0) {
                it = m_sceneObjects.erase(it);
            } else {
                ++it;
            }
        }

        std::cout << "[Economy] Removed boundary posts for plot (" << gridX << ", " << gridZ << ")" << std::endl;
    }

    /**
     * Create or update the player avatar - a visible object that follows the camera
     * so AI characters can perceive the player's position
     */
    void createPlayerAvatar() {
        // If avatar already exists, just make sure it's visible
        if (m_playerAvatar) {
            m_playerAvatar->setVisible(true);
            return;
        }

        // Create a small cube to represent the player
        glm::vec4 playerColor(0.2f, 0.8f, 0.2f, 1.0f);  // Green
        auto meshData = PrimitiveMeshBuilder::createCube(0.8f, playerColor);

        auto obj = std::make_unique<SceneObject>("Player");

        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);
        obj->setModelPath("");
        obj->setMeshData(meshData.vertices, meshData.indices);

        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(0.8f);
        obj->setPrimitiveColor(playerColor);

        // Mark as human so AI can perceive it (HUMAN is sentient by default)
        obj->setBeingType(BeingType::HUMAN);

        // Position at camera (will be updated each frame)
        glm::vec3 camPos = m_camera.getPosition();
        obj->getTransform().setPosition(camPos - glm::vec3(0, 1.5f, 0));  // Offset down from eye height

        // Store raw pointer before moving
        m_playerAvatar = obj.get();
        m_sceneObjects.push_back(std::move(obj));

        std::cout << "[Player] Avatar created - AI can now perceive you" << std::endl;
    }

    /**
     * Update player avatar position to follow camera
     */
    void updatePlayerAvatar() {
        if (!m_playerAvatar || !m_isPlayMode) return;

        glm::vec3 camPos = m_camera.getPosition();
        // Position the avatar at feet level (camera is at eye height ~1.7m)
        m_playerAvatar->getTransform().setPosition(camPos - glm::vec3(0, 1.5f, 0));
    }

    void addDoor(float size = 2.0f) {
        // Semi-transparent blue for door trigger zones
        glm::vec4 doorColor(0.3f, 0.5f, 1.0f, 0.4f);
        auto meshData = PrimitiveMeshBuilder::createCube(size, doorColor);

        auto obj = std::make_unique<SceneObject>(generateUniqueName("Door"));

        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);
        obj->setModelPath("");
        obj->setMeshData(meshData.vertices, meshData.indices);

        // Store primitive info for save/load
        obj->setPrimitiveType(PrimitiveType::Door);
        obj->setPrimitiveSize(size);
        obj->setPrimitiveColor(doorColor);

        // Generate a unique door ID
        static int doorIdCounter = 0;
        obj->setDoorId("door_" + std::to_string(++doorIdCounter));

        glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 10.0f;
        spawnPos.y = getPlacementFloorHeight(spawnPos.x, spawnPos.z);
        obj->getTransform().setPosition(spawnPos);

        m_sceneObjects.push_back(std::move(obj));
        selectObject(static_cast<int>(m_sceneObjects.size()) - 1);

        std::cout << "Created door trigger zone (" << size << "m)" << std::endl;
    }

    std::unique_ptr<SceneObject> createSpawnMarkerObject() {
        auto meshData = PrimitiveMeshBuilder::createSpawnMarker(2.0f);

        auto obj = std::make_unique<SceneObject>("Spawn Point");

        uint32_t handle = m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);
        obj->setModelPath("");
        obj->setMeshData(meshData.vertices, meshData.indices);

        return obj;
    }

    void importModel(const std::string& inputPath) {
        std::string path = inputPath;
        if (!path.empty() && path[0] != '/') {
            path = "models/" + path;
            if (path.find(".glb") == std::string::npos &&
                path.find(".gltf") == std::string::npos &&
                path.find(".lime") == std::string::npos) {
                path += ".glb";
            }
        }

        std::cout << "=== Importing model ===" << std::endl;
        std::cout << "Input: " << inputPath << std::endl;
        std::cout << "Resolved path: " << path << std::endl;

        // Check for LIME format
        if (path.size() >= 5 && path.substr(path.size() - 5) == ".lime") {
            importLimeModel(path);
            return;
        }

        // Check if model has skeletal animation data
        if (SkinnedGLBLoader::hasSkeleton(path)) {
            std::cout << "Detected skeletal animation - loading as skinned model" << std::endl;
            importSkinnedModel(path);
            return;
        }

        auto result = GLBLoader::load(path);
        if (!result.success) {
            std::cerr << "!!! Failed to load model: " << result.error << std::endl;
            return;
        }

        std::cout << "Loaded " << result.meshes.size() << " mesh(es)" << std::endl;

        for (const auto& mesh : result.meshes) {
            auto obj = GLBLoader::createSceneObject(mesh, *m_modelRenderer);
            if (obj) {
                glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 10.0f;
                spawnPos.y = getPlacementFloorHeight(spawnPos.x, spawnPos.z) + mesh.bounds.getSize().y * 0.5f;
                obj->getTransform().setPosition(spawnPos);
                obj->setModelPath(path);  // Store the model path for save/load

                std::cout << "Created object: " << obj->getName()
                          << " (" << obj->getVertexCount() << " vertices, "
                          << obj->getIndexCount() << " indices)" << std::endl;

                m_sceneObjects.push_back(std::move(obj));
                selectObject(static_cast<int>(m_sceneObjects.size()) - 1);
            }
        }
    }

    void importSkinnedModel(const std::string& path) {
        auto result = SkinnedGLBLoader::load(path);
        if (!result.success) {
            std::cerr << "!!! Failed to load skinned model: " << result.error << std::endl;
            return;
        }

        std::cout << "Loaded skinned model with " << result.meshes.size() << " mesh(es)" << std::endl;
        if (result.skeleton) {
            std::cout << "  Skeleton: " << result.skeleton->bones.size() << " bones" << std::endl;
        }
        std::cout << "  Animations: " << result.animations.size() << std::endl;
        for (const auto& anim : result.animations) {
            std::cout << "    - " << anim.name << " (" << anim.duration << "s)" << std::endl;
        }

        for (const auto& mesh : result.meshes) {
            // Create GPU resources for skinned model
            uint32_t handle = m_skinnedModelRenderer->createModel(
                mesh.vertices,
                mesh.indices,
                std::make_unique<Skeleton>(*result.skeleton),  // Copy skeleton for each mesh
                result.animations,  // Copy animations
                mesh.hasTexture ? mesh.textureData.data() : nullptr,
                mesh.textureWidth,
                mesh.textureHeight
            );

            // Create SceneObject for the skinned model (gets all the same properties as static models!)
            auto obj = std::make_unique<SceneObject>(mesh.name);
            obj->setSkinnedModelHandle(handle);
            obj->setModelPath(path);
            obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));

            // Blender uses Z-up, we use Y-up, so rotate +90 on X axis
            // Mixamo models are in cm, scale to m
            obj->setEulerRotation(glm::vec3(90.0f, 0.0f, 0.0f));
            obj->getTransform().setScale(glm::vec3(0.012f));

            // Position in front of camera at ground level (or subfloor if underground)
            glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 5.0f;
            spawnPos.y = getPlacementFloorHeight(spawnPos.x, spawnPos.z);
            obj->getTransform().setPosition(spawnPos);

            // Store animation info
            auto animNames = m_skinnedModelRenderer->getAnimationNames(handle);
            obj->setAnimationNames(animNames);

            // Play first animation if available
            if (!animNames.empty()) {
                m_skinnedModelRenderer->playAnimation(handle, animNames[0], true);
                obj->setCurrentAnimation(animNames[0]);
                std::cout << "Auto-playing animation: " << animNames[0] << std::endl;
            }

            std::cout << "Created skinned object: " << obj->getName()
                      << " at position (" << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << ")"
                      << " (" << mesh.vertices.size() << " vertices)" << std::endl;

            m_sceneObjects.push_back(std::move(obj));
            selectObject(static_cast<int>(m_sceneObjects.size()) - 1);
        }
    }

    void importLimeModel(const std::string& path) {
        auto result = LimeLoader::load(path);
        if (!result.success) {
            std::cerr << "!!! Failed to load LIME model: " << result.error << std::endl;
            return;
        }

        auto obj = LimeLoader::createSceneObject(result.mesh, *m_modelRenderer);
        if (obj) {
            // Position in front of camera
            glm::vec3 spawnPos = m_camera.getPosition() + m_camera.getFront() * 10.0f;
            spawnPos.y = getPlacementFloorHeight(spawnPos.x, spawnPos.z) + 1.0f;
            obj->getTransform().setPosition(spawnPos);
            obj->setModelPath(path);

            std::cout << "Created object: " << obj->getName()
                      << " (" << obj->getVertexCount() << " vertices, "
                      << obj->getIndexCount() << " indices)";
            if (result.mesh.hasTexture) {
                std::cout << " with " << result.mesh.textureWidth << "x" << result.mesh.textureHeight << " texture";
            }
            std::cout << std::endl;

            m_sceneObjects.push_back(std::move(obj));
            selectObject(static_cast<int>(m_sceneObjects.size()) - 1);
        }
    }

    void selectObject(int index) {
        if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
            m_sceneObjects[m_selectedObjectIndex]->setSelected(false);
        }

        m_selectedObjectIndex = index;

        if (m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
            m_sceneObjects[m_selectedObjectIndex]->setSelected(true);
            // Set orbit target to selected object center for camera orbiting
            AABB selBounds = m_sceneObjects[m_selectedObjectIndex]->getWorldBounds();
            m_orbitTarget = (selBounds.min + selBounds.max) * 0.5f;
        }

        m_editorUI.setSelectedObjectIndex(m_selectedObjectIndex);
    }

    void deleteObject(int index) {
        if (index < 0 || index >= static_cast<int>(m_sceneObjects.size())) return;

        bool isDeletingSpawn = (index == m_spawnObjectIndex);

        uint32_t bufferHandle = m_sceneObjects[index]->getBufferHandle();
        if (bufferHandle != UINT32_MAX) {
            m_modelRenderer->destroyModel(bufferHandle);
        }

        m_sceneObjects.erase(m_sceneObjects.begin() + index);

        if (isDeletingSpawn) {
            m_spawnObjectIndex = -1;
            m_hasSpawnPoint = false;
            std::cout << "Spawn point deleted" << std::endl;
        } else if (m_spawnObjectIndex > index) {
            m_spawnObjectIndex--;
        }

        if (m_selectedObjectIndex == index) {
            m_selectedObjectIndex = -1;
        } else if (m_selectedObjectIndex > index) {
            m_selectedObjectIndex--;
        }

        m_editorUI.setSelectedObjectIndex(m_selectedObjectIndex);
    }

    // AI Node methods
    void selectAINode(int index) {
        // Deselect previous
        if (m_selectedAINodeIndex >= 0 && m_selectedAINodeIndex < static_cast<int>(m_aiNodes.size())) {
            m_aiNodes[m_selectedAINodeIndex]->setSelected(false);
        }

        m_selectedAINodeIndex = index;

        // Select new
        if (m_selectedAINodeIndex >= 0 && m_selectedAINodeIndex < static_cast<int>(m_aiNodes.size())) {
            m_aiNodes[m_selectedAINodeIndex]->setSelected(true);
        }

        m_editorUI.setSelectedAINodeIndex(m_selectedAINodeIndex);
        updateAINodeRenderer();
    }

    void deleteAINode(int index) {
        if (index < 0 || index >= static_cast<int>(m_aiNodes.size())) return;

        uint32_t deletedId = m_aiNodes[index]->getId();

        // Remove connections to this node from other nodes
        for (auto& node : m_aiNodes) {
            node->removeConnection(deletedId);
        }

        m_aiNodes.erase(m_aiNodes.begin() + index);

        if (m_selectedAINodeIndex == index) {
            m_selectedAINodeIndex = -1;
        } else if (m_selectedAINodeIndex > index) {
            m_selectedAINodeIndex--;
        }

        m_editorUI.setSelectedAINodeIndex(m_selectedAINodeIndex);
        updateAINodeList();
        updateAINodeRenderer();

        std::cout << "Deleted AI node" << std::endl;
    }

    AINode* addAINode(const glm::vec3& position, AINodeType type = AINodeType::WAYPOINT) {
        auto node = std::make_unique<AINode>(m_nextAINodeId++);
        node->setPosition(position);
        node->setType(type);
        node->setName(generateAINodeName(type));

        AINode* ptr = node.get();
        m_aiNodes.push_back(std::move(node));

        updateAINodeList();
        updateAINodeRenderer();

        std::cout << "Added AI node: " << ptr->getName() << std::endl;
        return ptr;
    }

    std::string generateAINodeName(AINodeType type) {
        const char* prefix = AINode::getTypeName(type);
        int maxNum = 0;

        for (const auto& node : m_aiNodes) {
            const std::string& name = node->getName();
            size_t prefixLen = strlen(prefix);
            if (name.size() > prefixLen && name.substr(0, prefixLen) == prefix && name[prefixLen] == '_') {
                std::string suffix = name.substr(prefixLen + 1);
                try {
                    int num = std::stoi(suffix);
                    maxNum = std::max(maxNum, num);
                } catch (...) {}
            }
        }

        return std::string(prefix) + "_" + std::to_string(maxNum + 1);
    }

    void updateAINodeList() {
        std::vector<AINode*> nodePtrs;
        for (const auto& node : m_aiNodes) {
            nodePtrs.push_back(node.get());
        }
        m_editorUI.setAINodes(nodePtrs);
    }

    void updateAINodeRenderer() {
        if (m_aiNodeRenderer) {
            m_aiNodeRenderer->update(m_aiNodes, m_terrain);
        }
    }

    void connectAllGraphNodes() {
        // Find all GRAPH type nodes
        std::vector<size_t> graphNodeIndices;
        for (size_t i = 0; i < m_aiNodes.size(); i++) {
            if (m_aiNodes[i]->getType() == AINodeType::GRAPH) {
                graphNodeIndices.push_back(i);
            }
        }

        // Connect each GRAPH node to all other GRAPH nodes (bidirectional)
        for (size_t i = 0; i < graphNodeIndices.size(); i++) {
            for (size_t j = 0; j < graphNodeIndices.size(); j++) {
                if (i != j) {
                    size_t fromIdx = graphNodeIndices[i];
                    size_t toIdx = graphNodeIndices[j];
                    uint32_t targetId = m_aiNodes[toIdx]->getId();

                    // Only add if not already connected
                    const auto& connections = m_aiNodes[fromIdx]->getConnections();
                    if (std::find(connections.begin(), connections.end(), targetId) == connections.end()) {
                        m_aiNodes[fromIdx]->addConnection(targetId);
                    }
                }
            }
        }

        updateAINodeRenderer();
        std::cout << "Connected " << graphNodeIndices.size() << " GRAPH nodes (all-to-all)" << std::endl;
    }

    void createTestEconomy() {
        // Get camera position as center point
        glm::vec3 center = m_camera.getPosition();
        center.y = 0; // Ground level

        // Test buildings to create - a complete supply chain
        struct TestBuilding {
            const char* name;
            float offsetX;
            float offsetZ;
        };

        TestBuilding buildings[] = {
            // Raw producers
            {"Downtown Chemicals", -100.0f, -100.0f},
            {"Prison Mine", -100.0f, 0.0f},
            {"Charlie's Pizza", -100.0f, 100.0f},

            // Manufacturers
            {"Ore Processing", 0.0f, -100.0f},
            {"Downtown Components", 0.0f, 0.0f},
            {"Waterfront Booze", 0.0f, 100.0f},

            // Consumers (bars)
            {"The After Dark", 100.0f, -50.0f},
            {"Traders Rest", 100.0f, 50.0f},
        };

        std::cout << "Creating test economy nodes around camera..." << std::endl;

        for (const auto& bld : buildings) {
            glm::vec3 pos = center + glm::vec3(bld.offsetX, 0, bld.offsetZ);

            // Get terrain height at position
            float terrainHeight = m_terrain.getHeightAt(pos.x, pos.z);
            pos.y = terrainHeight + 2.0f;

            // Create the node
            auto node = std::make_unique<AINode>(m_nextAINodeId++, bld.name);
            node->setPosition(pos);
            node->setType(AINodeType::GRAPH);
            node->setRadius(10.0f);

            std::cout << "  Created: " << bld.name << " at (" << pos.x << ", " << pos.z << ")" << std::endl;

            m_aiNodes.push_back(std::move(node));
        }

        // Connect all nodes
        connectAllGraphNodes();

        // Update UI
        updateAINodeList();
        updateAINodeRenderer();

        std::cout << "Test economy created with " << m_aiNodes.size() << " nodes. Press F5 to test!" << std::endl;
    }

    // Path management
    glm::vec3 getPathColor() {
        // Assign a unique color based on path count using golden ratio for nice distribution
        float hue = std::fmod(m_aiPaths.size() * 0.618033988749895f, 1.0f);
        glm::vec3 color;
        // Simple HSV to RGB (S=1, V=1)
        float h = hue * 6.0f;
        int i = static_cast<int>(h);
        float f = h - i;
        switch (i % 6) {
            case 0: color = glm::vec3(1, f, 0); break;
            case 1: color = glm::vec3(1-f, 1, 0); break;
            case 2: color = glm::vec3(0, 1, f); break;
            case 3: color = glm::vec3(0, 1-f, 1); break;
            case 4: color = glm::vec3(f, 0, 1); break;
            default: color = glm::vec3(1, 0, 1-f); break;
        }
        return color;
    }

    void createPathFromNodes(const std::string& name, const std::vector<int>& nodeIndices) {
        if (nodeIndices.size() < 2) {
            std::cout << "Need at least 2 nodes to create a path" << std::endl;
            return;
        }

        auto path = std::make_unique<AIPath>(m_nextPathId++, name);
        path->setColor(getPathColor());

        // Add waypoints from the selected nodes in order
        for (int idx : nodeIndices) {
            if (idx >= 0 && idx < static_cast<int>(m_aiNodes.size())) {
                path->addWaypoint(m_aiNodes[idx]->getPosition());
            }
        }

        std::cout << "Created path: " << name << " with " << path->getWaypointCount() << " waypoints" << std::endl;
        m_aiPaths.push_back(std::move(path));
        updatePathList();
    }

    // Create path directly from positions (used by procedural generation)
    AIPath* createPathFromPositions(const std::string& name, const std::vector<glm::vec3>& positions) {
        if (positions.size() < 2) return nullptr;

        auto path = std::make_unique<AIPath>(m_nextPathId++, name);
        path->setColor(getPathColor());

        for (const auto& pos : positions) {
            path->addWaypoint(pos);
        }

        AIPath* pathPtr = path.get();
        m_aiPaths.push_back(std::move(path));
        updatePathList();
        std::cout << "Created path: " << name << " with " << pathPtr->getWaypointCount() << " waypoints" << std::endl;
        return pathPtr;
    }

    void deletePath(int index) {
        if (index >= 0 && index < static_cast<int>(m_aiPaths.size())) {
            std::cout << "Deleted path: " << m_aiPaths[index]->getName() << std::endl;
            m_aiPaths.erase(m_aiPaths.begin() + index);
            if (m_selectedPathIndex >= static_cast<int>(m_aiPaths.size())) {
                m_selectedPathIndex = static_cast<int>(m_aiPaths.size()) - 1;
            }
            updatePathList();
        }
    }

    void selectPath(int index) {
        m_selectedPathIndex = index;
        m_editorUI.setSelectedPathIndex(index);
    }

    void updatePathList() {
        std::vector<AIPath*> pathPtrs;
        for (const auto& path : m_aiPaths) {
            pathPtrs.push_back(path.get());
        }
        m_editorUI.setAIPaths(pathPtrs);
    }

    AIPath* getPathByName(const std::string& name) {
        for (const auto& path : m_aiPaths) {
            if (path->getName() == name) {
                return path.get();
            }
        }
        return nullptr;
    }

    // Convert HHMM string (e.g., "1800") to game minutes (0-1440)
    int parseTimeString(const std::string& timeStr) {
        if (timeStr.length() < 4) return -1;
        try {
            int hours = std::stoi(timeStr.substr(0, 2));
            int minutes = std::stoi(timeStr.substr(2, 2));
            return hours * 60 + minutes;
        } catch (...) {
            return -1;
        }
    }

    // Format game minutes to HHMM string
    std::string formatGameTime(float gameMinutes) {
        int totalMinutes = static_cast<int>(gameMinutes) % 1440;
        if (totalMinutes < 0) totalMinutes += 1440;
        int hours = totalMinutes / 60;
        int minutes = totalMinutes % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d%02d", hours, minutes);
        return std::string(buf);
    }

    // Format for display (HH:MM)
    std::string formatGameTimeDisplay(float gameMinutes) {
        int totalMinutes = static_cast<int>(gameMinutes) % 1440;
        if (totalMinutes < 0) totalMinutes += 1440;
        int hours = totalMinutes / 60;
        int minutes = totalMinutes % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", hours, minutes);
        return std::string(buf);
    }

    void generateAINodes(int pattern, int count, float radius) {
        glm::vec3 center = m_camera.getPosition();
        center.y = m_terrain.getHeightAt(center.x, center.z);

        if (pattern == 0) {
            // Patrol route - circle
            std::vector<glm::vec3> patrolPositions;

            for (int i = 0; i < count; i++) {
                float angle = (static_cast<float>(i) / count) * 2.0f * 3.14159f;
                float x = center.x + radius * std::cos(angle);
                float z = center.z + radius * std::sin(angle);
                float y = m_terrain.getHeightAt(x, z);

                glm::vec3 pos(x, y, z);
                patrolPositions.push_back(pos);

                AINode* node = addAINode(pos, AINodeType::PATROL);

                // Connect to previous node
                if (i > 0) {
                    m_aiNodes[m_aiNodes.size() - 2]->addConnection(node->getId());
                }
            }
            // Connect last to first to complete loop
            if (count > 1) {
                m_aiNodes.back()->addConnection(m_aiNodes[m_aiNodes.size() - count]->getId());
            }

            // Also create an AIPath from these positions
            std::string pathName = "PatrolRoute_" + std::to_string(m_nextPathId);
            AIPath* path = createPathFromPositions(pathName, patrolPositions);
            if (path) {
                path->setLooping(true);  // Patrol routes loop
            }
        } else if (pattern == 1) {
            // Grid
            int side = static_cast<int>(std::sqrt(count));
            float spacing = radius * 2.0f / (side - 1);
            float startX = center.x - radius;
            float startZ = center.z - radius;

            for (int i = 0; i < side && static_cast<int>(m_aiNodes.size()) < count; i++) {
                for (int j = 0; j < side && static_cast<int>(m_aiNodes.size()) < count; j++) {
                    float x = startX + i * spacing;
                    float z = startZ + j * spacing;
                    float y = m_terrain.getHeightAt(x, z);
                    addAINode(glm::vec3(x, y, z), AINodeType::WAYPOINT);
                }
            }
        } else {
            // Scattered
            for (int i = 0; i < count; i++) {
                float angle = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
                float dist = static_cast<float>(rand()) / RAND_MAX * radius;
                float x = center.x + dist * std::cos(angle);
                float z = center.z + dist * std::sin(angle);
                float y = m_terrain.getHeightAt(x, z);
                addAINode(glm::vec3(x, y, z), AINodeType::INTEREST);
            }
        }

        updateAINodeRenderer();
        std::cout << "Generated " << count << " AI nodes" << std::endl;

        // Debug: Print connection info for patrol routes
        if (pattern == 0) {
            size_t startIdx = m_aiNodes.size() - count;
            for (size_t i = startIdx; i < m_aiNodes.size(); i++) {
                const auto& conns = m_aiNodes[i]->getConnections();
                std::cout << "  " << m_aiNodes[i]->getName() << " has " << conns.size() << " connections";
                if (!conns.empty()) {
                    std::cout << ": ";
                    for (uint32_t connId : conns) {
                        // Find name of connected node
                        for (const auto& n : m_aiNodes) {
                            if (n->getId() == connId) {
                                std::cout << n->getName() << " ";
                                break;
                            }
                        }
                    }
                }
                std::cout << std::endl;
            }
        }
    }

    /**
     * Perform a scan cone perception from an NPC's position/facing.
     * Returns a PerceptionData struct with all visible objects within FOV and range.
     */
    PerceptionData performScanCone(SceneObject* npc, float fovDegrees = 120.0f, float range = 50.0f) {
        PerceptionData perception;
        if (!npc) return perception;
        
        glm::vec3 npcPos = npc->getTransform().getPosition();
        perception.posX = npcPos.x;
        perception.posY = npcPos.y;
        perception.posZ = npcPos.z;
        perception.fov = fovDegrees;
        perception.range = range;

        // Include player (camera) position so spatial analysis can show it
        glm::vec3 camPos = m_camera.getPosition();
        perception.playerX = camPos.x;
        perception.playerY = camPos.y;
        perception.playerZ = camPos.z;
        
        // Get NPC facing direction from Y rotation (euler angles)
        glm::vec3 euler = npc->getEulerRotation();
        float yawRad = glm::radians(euler.y);
        glm::vec3 facing(sin(yawRad), 0.0f, cos(yawRad));
        facing = glm::normalize(facing);
        perception.facingX = facing.x;
        perception.facingY = facing.y;
        perception.facingZ = facing.z;
        
        float halfFov = fovDegrees * 0.5f;
        
        // Scan all scene objects
        for (const auto& obj : m_sceneObjects) {
            if (!obj || !obj->isVisible()) continue;
            if (obj.get() == npc) continue;  // Skip self
            
            glm::vec3 objPos = obj->getTransform().getPosition();
            glm::vec3 toObj = objPos - npcPos;
            float dist = glm::length(toObj);
            
            // Skip if out of range
            if (dist > range || dist < 0.1f) continue;
            
            // Calculate angle from facing direction
            glm::vec3 toObjNorm = glm::normalize(toObj);
            float dotProduct = glm::dot(facing, glm::vec3(toObjNorm.x, 0, toObjNorm.z));
            dotProduct = glm::clamp(dotProduct, -1.0f, 1.0f);
            float angleDeg = glm::degrees(acos(dotProduct));
            
            // Skip if outside FOV
            if (angleDeg > halfFov) continue;
            
            // Determine bearing (left/right/ahead)
            glm::vec3 right(facing.z, 0, -facing.x);  // Perpendicular to facing
            float rightDot = glm::dot(right, glm::vec3(toObjNorm.x, 0, toObjNorm.z));
            
            std::string bearing;
            if (angleDeg < 15.0f) {
                bearing = "directly ahead";
            } else if (angleDeg < 45.0f) {
                bearing = rightDot > 0 ? "ahead-right" : "ahead-left";
            } else {
                bearing = rightDot > 0 ? "right" : "left";
            }
            
            // Determine object type
            std::string objType;
            switch (obj->getPrimitiveType()) {
                case PrimitiveType::Cube: objType = "cube"; break;
                case PrimitiveType::Cylinder: objType = "cylinder"; break;
                case PrimitiveType::SpawnMarker: objType = "spawn_marker"; break;
                case PrimitiveType::Door: objType = "door"; break;
                default: objType = "model"; break;
            }
            
            VisibleObject visObj;
            visObj.name = obj->getName();
            visObj.type = objType;
            visObj.distance = dist;
            visObj.angle = angleDeg;
            visObj.bearing = bearing;
            visObj.posX = objPos.x;
            visObj.posY = objPos.y;
            visObj.posZ = objPos.z;
            visObj.isSentient = obj->isSentient();
            if (visObj.isSentient) {
                visObj.beingType = getBeingTypeName(obj->getBeingType());
            }
            visObj.description = obj->getDescription();

            // Append control point info so AI knows about connectable parts
            if (obj->hasControlPoints()) {
                std::string cpInfo = "CPs: ";
                for (size_t ci = 0; ci < obj->getControlPoints().size(); ci++) {
                    if (ci > 0) cpInfo += ", ";
                    cpInfo += obj->getControlPoints()[ci].name;
                }
                if (visObj.description.empty()) {
                    visObj.description = cpInfo;
                } else {
                    visObj.description += " | " + cpInfo;
                }
            }

            perception.visibleObjects.push_back(visObj);
        }
        
        // Sort by distance (closest first)
        std::sort(perception.visibleObjects.begin(), perception.visibleObjects.end(),
            [](const VisibleObject& a, const VisibleObject& b) {
                return a.distance < b.distance;
            });
        
        return perception;
    }

    /**
     * Execute an AI motor control action (look_around, turn_to, move_to, etc.)
     */
    // Send a completion message to the AI after a move_to finishes,
    // so it can issue the next action in a multi-step sequence.
    void sendActionCompleteCallback(SceneObject* npc, const std::string& actionType,
                                     float x, float z) {
        if (!npc || !m_httpClient) return;

        std::string npcName = npc->getName();
        int beingType = static_cast<int>(npc->getBeingType());

        std::string sessionId;
        auto it = m_quickChatSessionIds.find(npcName);
        if (it != m_quickChatSessionIds.end()) sessionId = it->second;
        if (sessionId.empty()) return;  // No active session, nothing to callback

        // Fresh perception from the arrival location
        PerceptionData perception = performScanCone(npc, 360.0f, 50.0f);

        std::string msg = "[ACTION COMPLETE] " + actionType + " finished at ("
                        + std::to_string(x) + ", " + std::to_string(z)
                        + "). If you have a pending task (e.g. a return trip), "
                          "issue the next action now. If not, simply acknowledge.";

        m_httpClient->sendChatMessageWithPerception(sessionId, msg,
            npcName, "", beingType, perception,
            [this, npcName, npc](const AsyncHttpClient::Response& resp) {
                if (!resp.success) return;
                try {
                    auto json = nlohmann::json::parse(resp.body);
                    if (json.contains("session_id")) {
                        m_quickChatSessionIds[npcName] = json["session_id"].get<std::string>();
                    }
                    std::string response = json.value("response", "");
                    if (!response.empty()) {
                        addChatMessage(npcName, response);
                        speakTTS(response, npcName);
                    }
                    // Execute follow-up action if the AI issued one
                    if (json.contains("action") && !json["action"].is_null()) {
                        m_currentInteractObject = npc;
                        executeAIAction(json["action"]);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[ActionComplete] Parse error: " << e.what() << std::endl;
                }
            });
    }

    void executeAIAction(const nlohmann::json& action) {
        if (!m_currentInteractObject) return;
        
        std::string actionType = action.value("type", "");
        float duration = action.value("duration", 2.0f);
        
        std::cout << "[AI Action] Type: " << actionType << ", Duration: " << duration << "s" << std::endl;
        
        if (actionType == "look_around") {
            // Start a 360-degree scan rotation
            m_aiActionActive = true;
            m_aiActionType = "look_around";
            m_aiActionDuration = duration;
            m_aiActionTimer = 0.0f;
            m_aiActionStartYaw = m_currentInteractObject->getEulerRotation().y;
            std::cout << "[AI Action] Starting 360-degree scan from yaw " << m_aiActionStartYaw << std::endl;
        }
        else if (actionType == "turn_to") {
            // Turn to face a specific direction or target
            if (action.contains("target") && action["target"].is_array()) {
                auto target = action["target"];
                glm::vec3 targetPos(target[0].get<float>(), target[1].get<float>(), target[2].get<float>());
                glm::vec3 npcPos = m_currentInteractObject->getTransform().getPosition();
                glm::vec3 toTarget = targetPos - npcPos;
                toTarget.y = 0;
                if (glm::length(toTarget) > 0.01f) {
                    toTarget = glm::normalize(toTarget);
                    float targetYaw = glm::degrees(atan2(toTarget.x, toTarget.z));
                    m_aiActionActive = true;
                    m_aiActionType = "turn_to";
                    m_aiActionDuration = duration;
                    m_aiActionTimer = 0.0f;
                    m_aiActionStartYaw = m_currentInteractObject->getEulerRotation().y;
                    m_aiActionTargetYaw = targetYaw;
                    std::cout << "[AI Action] Turning from " << m_aiActionStartYaw << " to " << targetYaw << std::endl;
                }
            }
            else if (action.contains("angle")) {
                float targetYaw = action["angle"].get<float>();
                m_aiActionActive = true;
                m_aiActionType = "turn_to";
                m_aiActionDuration = duration;
                m_aiActionTimer = 0.0f;
                m_aiActionStartYaw = m_currentInteractObject->getEulerRotation().y;
                m_aiActionTargetYaw = targetYaw;
            }
        }
        // move_to: Move toward a target position at specified speed
        else if (actionType == "move_to") {
            if (action.contains("target")) {
                auto& target = action["target"];
                float x = target.value("x", 0.0f);
                float y = target.value("y", m_currentInteractObject->getTransform().getPosition().y); // default: maintain current height
                float z = target.value("z", 0.0f);
                float speed = action.value("speed", 5.0f);  // units per second
                
                m_aiActionStartPos = m_currentInteractObject->getTransform().getPosition();
                m_aiActionTargetPos = glm::vec3(x, y, z);
                m_aiActionSpeed = speed;
                
                float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                m_aiActionDuration = distance / speed;  // time = distance / speed
                
                if (m_aiActionDuration > 0.01f) {  // Only move if there's meaningful distance
                    m_aiActionActive = true;
                    m_aiActionType = "move_to";
                    m_aiActionTimer = 0.0f;
                    
                    // Calculate yaw to face movement direction
                    glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                    m_aiActionStartYaw = m_currentInteractObject->getEulerRotation().y;
                    
                    std::cout << "[AI Action] Moving from (" << m_aiActionStartPos.x << ", " << m_aiActionStartPos.z 
                              << ") to (" << x << ", " << z << ") at speed " << speed 
                              << " (ETA: " << m_aiActionDuration << "s)" << std::endl;
                } else {
                    std::cout << "[AI Action] Already at target position" << std::endl;
                }
            }
        }
        else if (actionType == "follow") {
            float dist = action.value("distance", 4.0f);
            float spd = action.value("speed", 5.0f);
            // Update existing entry or add new one
            bool found = false;
            for (auto& fs : m_aiFollowers) {
                if (fs.npc == m_currentInteractObject) {
                    fs.distance = dist;
                    fs.speed = spd;
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_aiFollowers.push_back({m_currentInteractObject, dist, spd});
            }
            std::cout << "[AI Action] Follow mode activated for " << m_currentInteractObject->getName()
                      << " (distance: " << dist << ", speed: " << spd
                      << ", total followers: " << m_aiFollowers.size() << ")" << std::endl;
        }
        else if (actionType == "pickup") {
            // Pick up a nearby world object
            std::string targetName = action.value("target", "");
            if (targetName.empty()) {
                std::cout << "[AI Action] pickup: no target specified" << std::endl;
            } else if (m_currentInteractObject->isCarrying()) {
                std::cout << "[AI Action] pickup: already carrying " << m_currentInteractObject->getCarriedItemName() << std::endl;
            } else {
                // Find target object by name
                SceneObject* target = nullptr;
                for (auto& obj : m_sceneObjects) {
                    if (obj && obj->getName() == targetName && obj->isVisible()) {
                        target = obj.get();
                        break;
                    }
                }
                if (!target) {
                    std::cout << "[AI Action] pickup: target '" << targetName << "' not found" << std::endl;
                } else {
                    // Navigate to object then pick up (reuse move_to logic)
                    glm::vec3 targetPos = target->getTransform().getPosition();
                    float speed = 5.0f;
                    m_aiActionStartPos = m_currentInteractObject->getTransform().getPosition();
                    m_aiActionTargetPos = targetPos;
                    m_aiActionTargetPos.y = m_aiActionStartPos.y;  // Maintain height
                    m_aiActionSpeed = speed;

                    float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionDuration = distance / speed;

                    // Store pickup target for completion callback
                    m_aiPickupTarget = target;
                    m_aiPickupTargetName = targetName;

                    if (m_aiActionDuration > 0.01f) {
                        m_aiActionActive = true;
                        m_aiActionType = "pickup";
                        m_aiActionTimer = 0.0f;

                        glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                        m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                        m_aiActionStartYaw = m_currentInteractObject->getEulerRotation().y;

                        std::cout << "[AI Action] Moving to pick up '" << targetName << "'" << std::endl;
                    } else {
                        // Already at target — pick up immediately
                        target->setVisible(false);
                        m_currentInteractObject->setCarriedItem(targetName, target);
                        std::cout << "[AI Action] Picked up '" << targetName << "' (was already nearby)" << std::endl;
                    }
                }
            }
        }
        else if (actionType == "drop") {
            // Drop carried item at bot's feet
            if (!m_currentInteractObject->isCarrying()) {
                std::cout << "[AI Action] drop: not carrying anything" << std::endl;
            } else {
                SceneObject* carried = m_currentInteractObject->getCarriedItemObject();
                if (carried) {
                    glm::vec3 dropPos = m_currentInteractObject->getTransform().getPosition();
                    // Place slightly in front of the NPC
                    float yawRad = glm::radians(m_currentInteractObject->getEulerRotation().y);
                    glm::vec3 forward(sin(yawRad), 0.0f, cos(yawRad));
                    dropPos += forward * 1.5f;
                    dropPos.y = carried->getTransform().getPosition().y;  // Restore original height
                    carried->getTransform().setPosition(dropPos);
                    carried->setVisible(true);
                    std::cout << "[AI Action] Dropped '" << m_currentInteractObject->getCarriedItemName()
                              << "' at (" << dropPos.x << ", " << dropPos.z << ")" << std::endl;
                }
                m_currentInteractObject->clearCarriedItem();
            }
        }
        else if (actionType == "place") {
            // Place carried item into a target object (e.g. timber into posthole)
            std::string targetName = action.value("target", "");
            if (targetName.empty()) {
                std::cout << "[AI Action] place: no target specified" << std::endl;
            } else if (!m_currentInteractObject->isCarrying()) {
                std::cout << "[AI Action] place: not carrying anything" << std::endl;
            } else {
                // Find target object by name
                SceneObject* target = nullptr;
                for (auto& obj : m_sceneObjects) {
                    if (obj && obj->getName() == targetName && obj->isVisible()) {
                        target = obj.get();
                        break;
                    }
                }
                if (!target) {
                    std::cout << "[AI Action] place: target '" << targetName << "' not found" << std::endl;
                } else {
                    // Navigate to target, then place on arrival
                    glm::vec3 targetPos = target->getTransform().getPosition();
                    float speed = 5.0f;
                    m_aiActionStartPos = m_currentInteractObject->getTransform().getPosition();
                    m_aiActionTargetPos = targetPos;
                    m_aiActionTargetPos.y = m_aiActionStartPos.y;
                    m_aiActionSpeed = speed;

                    float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionDuration = distance / speed;

                    m_aiPlaceTarget = target;
                    m_aiPlaceTargetName = targetName;

                    if (m_aiActionDuration > 0.01f) {
                        m_aiActionActive = true;
                        m_aiActionType = "place";
                        m_aiActionTimer = 0.0f;

                        glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                        m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                        m_aiActionStartYaw = m_currentInteractObject->getEulerRotation().y;

                        std::cout << "[AI Action] Moving to place item at '" << targetName << "'" << std::endl;
                    } else {
                        // Already at target — place immediately
                        placeCarriedItemAt(m_currentInteractObject, target);
                    }
                }
            }
        }
        else if (actionType == "run_script") {
            // Execute a Grove script directly (for economy, zone queries, etc.)
            std::string script = action.value("script", "");
            if (script.empty()) {
                std::cout << "[AI Action] run_script: no script provided" << std::endl;
            } else {
                std::cout << "[AI Action] run_script: executing " << script.size() << " bytes" << std::endl;
                std::cout << "[AI Action] script first 200 chars: " << script.substr(0, 200) << std::endl;
                m_groveOutputAccum.clear();
                int32_t ret = grove_eval(m_groveVm, script.c_str());
                std::cout << "[AI Action] grove_eval returned: " << ret << std::endl;
                if (ret != 0) {
                    const char* err = grove_last_error(m_groveVm);
                    int line = static_cast<int>(grove_last_error_line(m_groveVm));
                    std::string errMsg = std::string("Script error (line ") + std::to_string(line) + "): " + (err ? err : "unknown");
                    std::cout << "[AI Action] " << errMsg << std::endl;
                    addChatMessage("System", errMsg);
                } else if (!m_groveOutputAccum.empty()) {
                    // Show Grove output in chat as a system message
                    std::cout << "[Grove output] " << m_groveOutputAccum << std::endl;
                    // Add to conversation so player can see the result
                    std::string output = m_groveOutputAccum;
                    // Trim trailing newline
                    while (!output.empty() && output.back() == '\n') output.pop_back();
                    addChatMessage(m_currentInteractObject ? m_currentInteractObject->getName() : "System", output);
                } else {
                    std::cout << "[AI Action] Script succeeded but produced no output" << std::endl;
                }
            }
        }
        else if (actionType == "program_bot") {
            // Xenk (or another LLM NPC) is programming an AlgoBot with a Grove script
            std::string targetName = action.value("target", "");
            std::string script = action.value("script", "");

            if (targetName.empty() || script.empty()) {
                std::cout << "[AI Action] program_bot: missing target or script" << std::endl;
            } else {
                // Find the target AlgoBot
                SceneObject* targetBot = nullptr;
                for (auto& obj : m_sceneObjects) {
                    if (obj->getName() == targetName) {
                        targetBot = obj.get();
                        break;
                    }
                }

                if (!targetBot) {
                    std::cout << "[AI Action] program_bot: target '" << targetName << "' not found" << std::endl;
                } else if (targetBot->getBeingType() != BeingType::ALGOBOT) {
                    std::cout << "[AI Action] program_bot: '" << targetName << "' is not an AlgoBot (type="
                              << getBeingTypeName(targetBot->getBeingType()) << ")" << std::endl;
                } else {
                    std::cout << "[AI Action] program_bot: programming '" << targetName << "' with "
                              << script.size() << " bytes of Grove code" << std::endl;

                    // Execute the Grove script — it will queue actions via bot_target/move_to/bot_run/etc.
                    m_groveOutputAccum.clear();
                    int32_t ret = grove_eval(m_groveVm, script.c_str());
                    if (ret != 0) {
                        const char* err = grove_last_error(m_groveVm);
                        int line = static_cast<int>(grove_last_error_line(m_groveVm));
                        std::string errMsg = std::string("Script error (line ") + std::to_string(line) + "): " + (err ? err : "unknown");
                        std::cout << "[AI Action] " << errMsg << std::endl;
                        addChatMessage("System", errMsg);
                    } else {
                        if (!m_groveOutputAccum.empty()) {
                            std::cout << "[AI Action] Grove output: " << m_groveOutputAccum;
                        }

                        // If we're in play mode, immediately start the last non-empty behavior
                        // (the one the script just created — name may vary depending on run_file)
                        if (m_isPlayMode && targetBot->hasBehaviors()) {
                            auto& behaviors = targetBot->getBehaviors();
                            for (int i = static_cast<int>(behaviors.size()) - 1; i >= 0; i--) {
                                if (!behaviors[i].actions.empty()) {
                                    targetBot->setActiveBehaviorIndex(i);
                                    targetBot->setActiveActionIndex(0);
                                    targetBot->resetPathComplete();
                                    targetBot->clearPathWaypoints();

                                    // If first action is FOLLOW_PATH, load it
                                    if (behaviors[i].actions[0].type == ActionType::FOLLOW_PATH) {
                                        loadPathForAction(targetBot, behaviors[i].actions[0]);
                                    }

                                    std::cout << "[AI Action] AlgoBot '" << targetName
                                              << "' program started behavior '" << behaviors[i].name
                                              << "' (" << behaviors[i].actions.size()
                                              << " actions, loop=" << (behaviors[i].loop ? "yes" : "no") << ")" << std::endl;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        else if (actionType == "stop") {
            // Remove this NPC from followers
            m_aiFollowers.erase(
                std::remove_if(m_aiFollowers.begin(), m_aiFollowers.end(),
                    [this](const AIFollowState& fs) { return fs.npc == m_currentInteractObject; }),
                m_aiFollowers.end());
            m_aiActionActive = false;
            std::cout << "[AI Action] Stopped for " << m_currentInteractObject->getName()
                      << " (remaining followers: " << m_aiFollowers.size() << ")" << std::endl;
        }
        else if (actionType == "set_expression") {
            // Swap NPC's face texture to a named expression
            std::string exprName = action.value("expression", "");
            if (exprName.empty()) {
                std::cout << "[AI Action] set_expression: no expression name provided" << std::endl;
            } else if (m_currentInteractObject->getExpressionCount() == 0) {
                std::cout << "[AI Action] set_expression: NPC has no expressions loaded" << std::endl;
            } else {
                if (m_currentInteractObject->setExpressionByName(exprName)) {
                    auto& tex = m_currentInteractObject->getTextureData();
                    m_modelRenderer->updateTexture(
                        m_currentInteractObject->getBufferHandle(),
                        tex.data(),
                        m_currentInteractObject->getTextureWidth(),
                        m_currentInteractObject->getTextureHeight());
                    std::cout << "[AI Action] Expression changed to '" << exprName << "'" << std::endl;
                } else {
                    std::cout << "[AI Action] set_expression: '" << exprName << "' not found or already active" << std::endl;
                }
            }
        }
        else if (actionType == "show_mind_map") {
            m_editorUI.showMindMap() = true;
            std::cout << "[AI Action] Mind map opened by " << m_currentInteractObject->getName() << std::endl;
        }
        else if (actionType == "hide_mind_map") {
            m_editorUI.showMindMap() = false;
            std::cout << "[AI Action] Mind map closed by " << m_currentInteractObject->getName() << std::endl;
        }
        else {
            std::cout << "[AI Action] Unknown action type: '" << actionType << "'" << std::endl;
        }
    }

    /**
     * Update AI follow mode for all following NPCs (called each frame)
     */
    void updateAIFollow(float deltaTime) {
        if (m_aiFollowers.empty()) return;

        // Periodic debug log (every ~2 seconds at 60fps)
        static int followDebugCount = 0;
        if (followDebugCount++ % 120 == 0) {
            std::cout << "[AI Follow] " << m_aiFollowers.size() << " NPC(s) following" << std::endl;
        }

        // Get player position and facing (shared for all followers)
        glm::vec3 playerPos = m_camera.getPosition();
        float yawRad = glm::radians(m_camera.getYaw());
        glm::vec3 camDir(sin(yawRad), 0.0f, cos(yawRad));
        glm::vec3 camRight(camDir.z, 0.0f, -camDir.x);

        for (size_t i = 0; i < m_aiFollowers.size(); i++) {
            auto& fs = m_aiFollowers[i];
            if (!fs.npc) continue;

            glm::vec3 npcPlayerPos = playerPos;
            npcPlayerPos.y = fs.npc->getTransform().getPosition().y; // Stay on same Y

            // Offset followers laterally so they don't stack on top of each other
            // First follower: directly behind. Additional: offset left/right
            glm::vec3 lateralOffset(0.0f);
            if (m_aiFollowers.size() > 1) {
                float spread = 2.5f;
                float side = (i % 2 == 0) ? -1.0f : 1.0f;
                float idx = static_cast<float>((i + 1) / 2);
                lateralOffset = camRight * side * idx * spread;
            }

            glm::vec3 targetPos = npcPlayerPos - camDir * fs.distance + lateralOffset;

            glm::vec3 npcPos = fs.npc->getTransform().getPosition();
            glm::vec3 toTarget = targetPos - npcPos;
            toTarget.y = 0;
            float dist = glm::length(toTarget);

            if (dist > 1.0f) {
                glm::vec3 moveDir = glm::normalize(toTarget);
                float moveAmount = std::min(fs.speed * deltaTime, dist);
                glm::vec3 newPos = npcPos + moveDir * moveAmount;
                fs.npc->getTransform().setPosition(newPos);

                float targetYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
                glm::vec3 euler = fs.npc->getEulerRotation();
                float yawDiff = targetYaw - euler.y;
                while (yawDiff > 180.0f) yawDiff -= 360.0f;
                while (yawDiff < -180.0f) yawDiff += 360.0f;
                euler.y += yawDiff * std::min(deltaTime * 8.0f, 1.0f);
                fs.npc->setEulerRotation(euler);
            } else {
                glm::vec3 euler = fs.npc->getEulerRotation();
                float targetYaw = glm::degrees(atan2(camDir.x, camDir.z));
                float yawDiff = targetYaw - euler.y;
                while (yawDiff > 180.0f) yawDiff -= 360.0f;
                while (yawDiff < -180.0f) yawDiff += 360.0f;
                euler.y += yawDiff * std::min(deltaTime * 4.0f, 1.0f);
                fs.npc->setEulerRotation(euler);
            }
        }
    }

    /**
     * Update active AI action (called each frame)
     */
    void updateAIAction(float deltaTime) {
        if (!m_aiActionActive || !m_currentInteractObject) return;
        
        m_aiActionTimer += deltaTime;
        float t = std::min(m_aiActionTimer / m_aiActionDuration, 1.0f);
        
        // Smooth easing
        float easedT = t * t * (3.0f - 2.0f * t);
        
        if (m_aiActionType == "look_around") {
            // Rotate 360 degrees
            float currentYaw = m_aiActionStartYaw + easedT * 360.0f;
            glm::vec3 euler = m_currentInteractObject->getEulerRotation();
            euler.y = currentYaw;
            m_currentInteractObject->setEulerRotation(euler);
            
            if (t >= 1.0f) {
                // Action complete - do final scan and report
                m_aiActionActive = false;
                std::cout << "[AI Action] look_around complete" << std::endl;
                
                // Perform a full 360 scan (no FOV filter)
                PerceptionData fullScan = performScanCone(m_currentInteractObject, 360.0f, 50.0f);
                std::cout << "[AI Action] Full scan found " << fullScan.visibleObjects.size() << " objects" << std::endl;
                
                // Store for next response
                m_lastFullScanResult = fullScan;
                m_hasFullScanResult = true;
            }
        }
        else if (m_aiActionType == "turn_to") {
            // Interpolate to target yaw
            float currentYaw = glm::mix(m_aiActionStartYaw, m_aiActionTargetYaw, easedT);
            glm::vec3 euler = m_currentInteractObject->getEulerRotation();
            euler.y = currentYaw;
            m_currentInteractObject->setEulerRotation(euler);
            
            if (t >= 1.0f) {
                m_aiActionActive = false;
                std::cout << "[AI Action] turn_to complete" << std::endl;
            }
        }
        else if (m_aiActionType == "move_to") {
            // First 15% of time: turn to face target
            // Remaining 85%: move toward target
            const float turnPhase = 0.15f;
            
            if (t < turnPhase) {
                // Turn phase: interpolate yaw to face movement direction
                float turnT = t / turnPhase;
                float turnEased = turnT * turnT * (3.0f - 2.0f * turnT);
                float currentYaw = glm::mix(m_aiActionStartYaw, m_aiActionTargetYaw, turnEased);
                glm::vec3 euler = m_currentInteractObject->getEulerRotation();
                euler.y = currentYaw;
                m_currentInteractObject->setEulerRotation(euler);
            } else {
                // Move phase: linear interpolation of position
                float moveT = (t - turnPhase) / (1.0f - turnPhase);
                glm::vec3 currentPos = glm::mix(m_aiActionStartPos, m_aiActionTargetPos, moveT);
                m_currentInteractObject->getTransform().setPosition(currentPos);
                
                // Ensure facing direction is locked
                glm::vec3 euler = m_currentInteractObject->getEulerRotation();
                euler.y = m_aiActionTargetYaw;
                m_currentInteractObject->setEulerRotation(euler);
            }
            
            if (t >= 1.0f) {
                // Snap to exact target position
                m_currentInteractObject->getTransform().setPosition(m_aiActionTargetPos);
                m_aiActionActive = false;
                std::cout << "[AI Action] move_to complete at ("
                          << m_aiActionTargetPos.x << ", " << m_aiActionTargetPos.z << ")" << std::endl;

                // Send completion callback so the AI can issue the next action
                // in a multi-step sequence (e.g. "go to door then come back")
                sendActionCompleteCallback(m_currentInteractObject, "move_to",
                    m_aiActionTargetPos.x, m_aiActionTargetPos.z);
            }
        }
        else if (m_aiActionType == "pickup") {
            // Same movement logic as move_to
            const float turnPhase = 0.15f;

            if (t < turnPhase) {
                float turnT = t / turnPhase;
                float turnEased = turnT * turnT * (3.0f - 2.0f * turnT);
                float currentYaw = glm::mix(m_aiActionStartYaw, m_aiActionTargetYaw, turnEased);
                glm::vec3 euler = m_currentInteractObject->getEulerRotation();
                euler.y = currentYaw;
                m_currentInteractObject->setEulerRotation(euler);
            } else {
                float moveT = (t - turnPhase) / (1.0f - turnPhase);
                glm::vec3 currentPos = glm::mix(m_aiActionStartPos, m_aiActionTargetPos, moveT);
                m_currentInteractObject->getTransform().setPosition(currentPos);

                glm::vec3 euler = m_currentInteractObject->getEulerRotation();
                euler.y = m_aiActionTargetYaw;
                m_currentInteractObject->setEulerRotation(euler);
            }

            if (t >= 1.0f) {
                m_currentInteractObject->getTransform().setPosition(m_aiActionTargetPos);
                m_aiActionActive = false;

                // Pick up the target object
                if (m_aiPickupTarget && !m_currentInteractObject->isCarrying()) {
                    m_aiPickupTarget->setVisible(false);
                    m_currentInteractObject->setCarriedItem(m_aiPickupTargetName, m_aiPickupTarget);
                    std::cout << "[AI Action] Picked up '" << m_aiPickupTargetName << "'" << std::endl;
                }
                m_aiPickupTarget = nullptr;
                m_aiPickupTargetName.clear();
            }
        }
        else if (m_aiActionType == "place") {
            // Same movement logic as move_to
            const float turnPhase = 0.15f;

            if (t < turnPhase) {
                float turnT = t / turnPhase;
                float turnEased = turnT * turnT * (3.0f - 2.0f * turnT);
                float currentYaw = glm::mix(m_aiActionStartYaw, m_aiActionTargetYaw, turnEased);
                glm::vec3 euler = m_currentInteractObject->getEulerRotation();
                euler.y = currentYaw;
                m_currentInteractObject->setEulerRotation(euler);
            } else {
                float moveT = (t - turnPhase) / (1.0f - turnPhase);
                glm::vec3 currentPos = glm::mix(m_aiActionStartPos, m_aiActionTargetPos, moveT);
                m_currentInteractObject->getTransform().setPosition(currentPos);

                glm::vec3 euler = m_currentInteractObject->getEulerRotation();
                euler.y = m_aiActionTargetYaw;
                m_currentInteractObject->setEulerRotation(euler);
            }

            if (t >= 1.0f) {
                m_currentInteractObject->getTransform().setPosition(m_aiActionTargetPos);
                m_aiActionActive = false;

                // Place carried item into target
                if (m_aiPlaceTarget && m_currentInteractObject->isCarrying()) {
                    placeCarriedItemAt(m_currentInteractObject, m_aiPlaceTarget);
                }
                m_aiPlaceTarget = nullptr;
                m_aiPlaceTargetName.clear();
            }
        }
    }

    /**
     * Place a carried item vertically into a target object (e.g. timber into posthole).
     * Measures both objects via AABB, rotates timber upright, sinks to posthole bottom.
     */
    void placeCarriedItemAt(SceneObject* npc, SceneObject* target) {
        SceneObject* carried = npc->getCarriedItemObject();
        if (!carried) return;

        // Ensure local bounds exist — compute from mesh vertices if needed
        AABB localBounds = carried->getLocalBounds();
        glm::vec3 localSize = localBounds.getSize();
        if (localSize.x <= 0 && localSize.y <= 0 && localSize.z <= 0 && carried->hasMeshData()) {
            const auto& verts = carried->getVertices();
            glm::vec3 vmin(INFINITY), vmax(-INFINITY);
            for (const auto& v : verts) {
                vmin = glm::min(vmin, v.position);
                vmax = glm::max(vmax, v.position);
            }
            localBounds = {vmin, vmax};
            carried->setLocalBounds(localBounds);  // Cache for future use
            localSize = localBounds.getSize();
            std::cout << "[AI Place] Computed bounds from " << verts.size() << " vertices: ("
                      << localSize.x << "," << localSize.y << "," << localSize.z << ")" << std::endl;
        }

        // Apply scale to find actual dimensions
        glm::vec3 scale = carried->getTransform().getScale();
        glm::vec3 scaledSize = localSize * glm::abs(scale);

        // Find longest axis
        int longestAxis = 0;
        float longestLen = scaledSize[0];
        for (int i = 1; i < 3; i++) {
            if (scaledSize[i] > longestLen) {
                longestLen = scaledSize[i];
                longestAxis = i;
            }
        }

        std::cout << "[AI Place] Local=(" << localSize.x << "," << localSize.y << "," << localSize.z
                  << ") Scale=(" << scale.x << "," << scale.y << "," << scale.z
                  << ") Scaled=(" << scaledSize.x << "," << scaledSize.y << "," << scaledSize.z
                  << ") longest axis=" << longestAxis << std::endl;

        // Rotation to make longest axis vertical (Y-up)
        glm::vec3 rotation(0.0f);
        if (longestAxis == 0) {
            rotation.z = 90.0f;   // Roll: X → Y
        } else if (longestAxis == 2) {
            rotation.x = 90.0f;   // Pitch: Z → Y
        }
        // longestAxis == 1: already vertical

        // Get posthole's world position and bounds
        glm::vec3 postholePos = target->getTransform().getPosition();
        AABB postholeBounds = target->getWorldBounds();
        float postholeBottom = postholeBounds.min.y;

        // Position timber so its bottom is at the posthole's bottom
        float timberHalfLength = longestLen * 0.5f;
        glm::vec3 placePos(postholePos.x, postholeBottom + timberHalfLength, postholePos.z);

        carried->setVisible(true);
        carried->setEulerRotation(rotation);
        carried->getTransform().setPosition(placePos);

        std::string itemName = npc->getCarriedItemName();
        npc->clearCarriedItem();

        std::cout << "[AI Action] Placed '" << itemName << "' vertically in '" << target->getName()
                  << "' (longest axis=" << longestAxis << ", len=" << longestLen
                  << ", rotation=(" << rotation.x << "," << rotation.y << "," << rotation.z
                  << "), base Y=" << postholeBottom << ")" << std::endl;
    }

    /**
     * Place a carried item horizontally between two corner positions (top rail beam).
     * Finds the top of the vertical posts at those corners and positions the beam there.
     */
    void placeCarriedItemHorizontal(SceneObject* npc, const glm::vec3& posA, const glm::vec3& posB) {
        SceneObject* carried = npc->getCarriedItemObject();
        if (!carried) return;

        // Ensure local bounds exist — compute from mesh vertices if needed
        AABB localBounds = carried->getLocalBounds();
        glm::vec3 localSize = localBounds.getSize();
        if (localSize.x <= 0 && localSize.y <= 0 && localSize.z <= 0 && carried->hasMeshData()) {
            const auto& verts = carried->getVertices();
            glm::vec3 vmin(INFINITY), vmax(-INFINITY);
            for (const auto& v : verts) {
                vmin = glm::min(vmin, v.position);
                vmax = glm::max(vmax, v.position);
            }
            localBounds = {vmin, vmax};
            carried->setLocalBounds(localBounds);
            localSize = localBounds.getSize();
        }

        glm::vec3 scale = carried->getTransform().getScale();
        glm::vec3 scaledSize = localSize * glm::abs(scale);

        // Find longest axis (this is the axis to align with the beam direction)
        int longestAxis = 0;
        float longestLen = scaledSize[0];
        for (int i = 1; i < 3; i++) {
            if (scaledSize[i] > longestLen) {
                longestLen = scaledSize[i];
                longestAxis = i;
            }
        }

        // Find the top height of the vertical posts at corner positions
        float topY = posA.y;  // Fallback to ground
        for (auto& obj : m_sceneObjects) {
            if (!obj || !obj->isVisible()) continue;
            glm::vec3 objPos = obj->getTransform().getPosition();
            float distA = glm::length(glm::vec2(objPos.x - posA.x, objPos.z - posA.z));
            float distB = glm::length(glm::vec2(objPos.x - posB.x, objPos.z - posB.z));
            if (distA < 1.0f || distB < 1.0f) {
                AABB bounds = obj->getWorldBounds();
                topY = std::max(topY, bounds.max.y);
            }
        }

        // Beam midpoint at the top of the posts, lowered so beam top aligns with post top
        glm::vec3 midpoint = (posA + posB) * 0.5f;
        // The beam's vertical extent depends on which local axis ends up vertical after rotation
        // For longestAxis==0 or ==2 (X or Z aligned with beam direction), Y stays vertical
        // For longestAxis==1 (Y tilted to horizontal), X becomes vertical after roll
        float beamHalfHeight;
        if (longestAxis == 1) {
            beamHalfHeight = scaledSize.x * 0.5f;  // X becomes vertical after 90° Z roll
        } else {
            beamHalfHeight = scaledSize.y * 0.5f;  // Y stays vertical
        }
        midpoint.y = topY - beamHalfHeight;

        // Compute the beam direction (horizontal)
        glm::vec3 dir = glm::normalize(posB - posA);

        // Rotation: align longest axis with beam direction, keep horizontal
        glm::vec3 rotation(0.0f);
        if (longestAxis == 0) {
            // Longest is X — rotate around Y to aim X along beam direction
            rotation.y = glm::degrees(atan2(-dir.z, dir.x));
        } else if (longestAxis == 1) {
            // Longest is Y — tilt 90° via Z roll (Y→X), then aim X along beam direction
            rotation.z = 90.0f;
            rotation.y = glm::degrees(atan2(-dir.z, dir.x));
        } else {
            // Longest is Z — rotate around Y to aim Z along beam direction
            rotation.y = glm::degrees(atan2(dir.x, dir.z));
        }

        carried->setVisible(true);
        carried->setEulerRotation(rotation);
        carried->getTransform().setPosition(midpoint);

        std::string itemName = npc->getCarriedItemName();
        npc->clearCarriedItem();

        std::cout << "[AI Action] Placed '" << itemName << "' as horizontal beam between ("
                  << posA.x << "," << posA.z << ") and (" << posB.x << "," << posB.z
                  << ") at Y=" << topY << " (axis=" << longestAxis
                  << ", rotation=(" << rotation.x << "," << rotation.y << "," << rotation.z << "))" << std::endl;
    }

    /**
     * Update carried items — position them on the NPC's shoulder each frame
     */
    void updateCarriedItems() {
        for (auto& obj : m_sceneObjects) {
            if (!obj || !obj->isCarrying()) continue;

            SceneObject* carried = obj->getCarriedItemObject();
            if (!carried) continue;

            // Make sure carried object is visible (it was hidden on pickup)
            carried->setVisible(true);

            // Position directly on top of the NPC, centered
            glm::vec3 npcPos = obj->getTransform().getPosition();
            glm::vec3 carryPos = npcPos + glm::vec3(0.0f, 2.0f, 0.0f);
            carried->getTransform().setPosition(carryPos);

            // Match NPC's yaw so the item faces the same direction
            carried->setEulerRotation(obj->getEulerRotation());
        }
    }

    void tryInteractWithNearbyObject(const glm::vec3& playerPos) {
        // If already in conversation, don't start a new one
        if (m_inConversation) return;

        const float interactionRadius = 15.0f;
        float closestDist = interactionRadius;
        SceneObject* closestObject = nullptr;

        // Find the closest sentient scene object within interaction range
        for (auto& obj : m_sceneObjects) {
            if (!obj || !obj->isVisible()) continue;
            if (!obj->isSentient()) continue;  // Only chat with sentient beings
            if (obj.get() == m_playerAvatar) continue;  // Don't interact with self

            glm::vec3 objPos = obj->getTransform().getPosition();
            float dist = glm::length(objPos - playerPos);

            if (dist < closestDist) {
                closestDist = dist;
                closestObject = obj.get();
            }
        }

        // Start conversation with the object
        if (closestObject) {
            m_currentInteractObject = closestObject;
            m_inConversation = true;
            m_waitingForAIResponse = true;
            m_heartbeatTimer = 0.0f;
            m_heartbeatInFlight = false;

            // Free the mouse for conversation (cursor visible for chat UI)
            m_playModeCursorVisible = true;
            Input::setMouseCaptured(false);

            // Pause patrol during conversation
            if (closestObject->hasPatrolPath()) {
                closestObject->setPatrolPaused(true);
            }

            // Calculate target yaw to face player (will be smoothly applied in update)
            glm::vec3 objPos = closestObject->getTransform().getPosition();
            glm::vec3 toPlayer = playerPos - objPos;
            toPlayer.y = 0;  // Only rotate on Y axis
            if (glm::length(toPlayer) > 0.01f) {
                toPlayer = glm::normalize(toPlayer);
                m_conversationTargetYaw = glm::degrees(atan2(toPlayer.x, toPlayer.z));
                m_hasConversationTargetYaw = true;
            }

            // Clear previous conversation
            m_conversationHistory.clear();
            m_currentSessionId.clear();

            std::string npcName = closestObject->getName();
            int beingType = static_cast<int>(closestObject->getBeingType());
            std::cout << "Started conversation with: " << npcName
                      << " (type: " << getBeingTypeName(closestObject->getBeingType()) << ")" << std::endl;

            // Start a new session and get AI greeting
            if (m_httpClient && m_httpClient->isConnected()) {
                // For AI NPCs (Xenk, Eve, Robot), include perception data
                if (closestObject->getBeingType() == BeingType::AI_ARCHITECT ||
                    closestObject->getBeingType() == BeingType::EVE ||
                    closestObject->getBeingType() == BeingType::ROBOT) {
                    PerceptionData perception = performScanCone(closestObject, 120.0f, 50.0f);
                    std::cout << "  Scan cone: " << perception.visibleObjects.size() << " objects visible" << std::endl;

                    m_httpClient->sendChatMessageWithPerception("", "The player approaches you. Greet them in character.",
                        npcName, "", beingType, perception,
                        [this, npcName](const AsyncHttpClient::Response& resp) {
                            m_waitingForAIResponse = false;
                            if (resp.success) {
                                try {
                                    auto json = nlohmann::json::parse(resp.body);
                                    m_currentSessionId = json.value("session_id", "");
                                    std::string greeting = json.value("response", "Hello there.");
                                    m_conversationHistory.push_back({npcName, greeting, false});
                                } catch (...) {
                                    m_conversationHistory.push_back({npcName, "...", false});
                                }
                            } else {
                                m_conversationHistory.push_back({npcName, "(AI unavailable) Greetings, human!", false});
                            }
                            m_scrollToBottom = true;
                        });
                } else {
                    // Standard NPCs without perception
                    m_httpClient->sendChatMessage("", "The player approaches you. Greet them in character.",
                        npcName, "", beingType,
                        [this, npcName](const AsyncHttpClient::Response& resp) {
                            m_waitingForAIResponse = false;
                            if (resp.success) {
                                try {
                                    auto json = nlohmann::json::parse(resp.body);
                                    m_currentSessionId = json.value("session_id", "");
                                    std::string greeting = json.value("response", "Hello there.");
                                    m_conversationHistory.push_back({npcName, greeting, false});
                                } catch (...) {
                                    m_conversationHistory.push_back({npcName, "...", false});
                                }
                            } else {
                                m_conversationHistory.push_back({npcName, "(AI unavailable) Greetings, human!", false});
                            }
                            m_scrollToBottom = true;
                        });
                }
            } else {
                // Fallback when backend not available
                m_waitingForAIResponse = false;
                m_conversationHistory.push_back({npcName, "Greetings, human!", false});
                m_scrollToBottom = true;
            }
        }
    }

    glm::vec3 getNpcBubblePosition() {
        if (!m_currentInteractObject) return glm::vec3(0);
        glm::vec3 objPos = m_currentInteractObject->getTransform().getPosition();
        AABB localBounds = m_currentInteractObject->getLocalBounds();
        float modelHeight = localBounds.max.y - localBounds.min.y;
        glm::vec3 scale = m_currentInteractObject->getTransform().getScale();
        modelHeight *= scale.y;
        return objPos + glm::vec3(0, modelHeight + 1.0f, 0);
    }

    void endConversation() {
        // End the session with the backend
        if (m_httpClient && !m_currentSessionId.empty()) {
            m_httpClient->endSession(m_currentSessionId, [](const AsyncHttpClient::Response&) {});
        }

        // Resume patrol if the NPC was patrolling
        if (m_currentInteractObject && m_currentInteractObject->hasPatrolPath()) {
            m_currentInteractObject->setPatrolPaused(false);
            std::cout << "NPC resumes patrol" << std::endl;
        }

        m_inConversation = false;
        m_hasConversationTargetYaw = false;  // Clear target so patrol rotation takes over
        m_currentInteractObject = nullptr;
        m_responseBuffer[0] = '\0';
        m_conversationHistory.clear();
        m_currentSessionId.clear();
        m_waitingForAIResponse = false;

        // Restore mouse look after conversation (in play mode)
        if (m_isPlayMode) {
            m_playModeCursorVisible = false;
            Input::setMouseCaptured(true);
        }

        std::cout << "Conversation ended" << std::endl;
    }

    std::string generateUniqueName(const std::string& baseName) {
        std::string base = baseName;

        while (base.size() > 5 && base.substr(base.size() - 5) == "_copy") {
            base = base.substr(0, base.size() - 5);
        }

        size_t lastUnderscore = base.rfind('_');
        if (lastUnderscore != std::string::npos && lastUnderscore < base.size() - 1) {
            bool allDigits = true;
            for (size_t i = lastUnderscore + 1; i < base.size(); i++) {
                if (!std::isdigit(base[i])) {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits) {
                base = base.substr(0, lastUnderscore);
            }
        }

        int maxNum = 0;
        for (const auto& obj : m_sceneObjects) {
            const std::string& name = obj->getName();
            if (name.size() > base.size() && name.substr(0, base.size()) == base && name[base.size()] == '_') {
                std::string suffix = name.substr(base.size() + 1);
                bool allDigits = true;
                for (char c : suffix) {
                    if (!std::isdigit(c)) {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits && !suffix.empty()) {
                    int num = std::stoi(suffix);
                    if (num > maxNum) maxNum = num;
                }
            }
        }

        return base + "_" + std::to_string(maxNum + 1);
    }

    // Duplicate without selecting or printing — returns new object index, or -1 on failure
    int duplicateObjectSilent(int index) {
        if (index < 0 || index >= static_cast<int>(m_sceneObjects.size())) return -1;

        SceneObject* original = m_sceneObjects[index].get();
        if (index == m_spawnObjectIndex) return -1;

        if (!original->getModelPath().empty()) {
            std::unique_ptr<SceneObject> newObj;
            std::string modelPath = original->getModelPath();
            std::string ext = modelPath.substr(modelPath.find_last_of('.') + 1);

            if (ext == "lime") {
                auto result = LimeLoader::load(modelPath);
                if (!result.success) return -1;
                newObj = LimeLoader::createSceneObject(result.mesh, *m_modelRenderer);
            } else {
                auto result = GLBLoader::load(modelPath);
                if (!result.success || result.meshes.empty()) return -1;
                newObj = GLBLoader::createSceneObject(result.meshes[0], *m_modelRenderer);
            }
            if (!newObj) return -1;

            newObj->setModelPath(modelPath);
            newObj->setName(generateUniqueName(original->getName()));
            newObj->getTransform().setPosition(original->getTransform().getPosition());
            newObj->setEulerRotation(original->getEulerRotation());
            newObj->getTransform().setScale(original->getTransform().getScale());
            newObj->setHueShift(original->getHueShift());
            newObj->setSaturation(original->getSaturation());
            newObj->setBrightness(original->getBrightness());
            newObj->setBeingType(original->getBeingType());
            newObj->setDailySchedule(original->hasDailySchedule());
            newObj->setPatrolSpeed(original->getPatrolSpeed());
            for (const auto& behavior : original->getBehaviors()) {
                newObj->addBehavior(behavior);
            }
            m_sceneObjects.push_back(std::move(newObj));
        } else if (original->hasMeshData()) {
            const auto& verts = original->getVertices();
            const auto& inds = original->getIndices();
            uint32_t handle = m_modelRenderer->createModel(verts, inds);
            auto newObj = std::make_unique<SceneObject>(generateUniqueName(original->getName()));
            newObj->setBufferHandle(handle);
            newObj->setVertexCount(static_cast<uint32_t>(verts.size()));
            newObj->setIndexCount(static_cast<uint32_t>(inds.size()));
            newObj->setLocalBounds(original->getLocalBounds());
            newObj->setModelPath("");
            newObj->setMeshData(verts, inds);
            newObj->getTransform().setPosition(original->getTransform().getPosition());
            newObj->setEulerRotation(original->getEulerRotation());
            newObj->getTransform().setScale(original->getTransform().getScale());
            newObj->setHueShift(original->getHueShift());
            newObj->setSaturation(original->getSaturation());
            newObj->setBrightness(original->getBrightness());
            newObj->setBeingType(original->getBeingType());
            newObj->setDailySchedule(original->hasDailySchedule());
            newObj->setPatrolSpeed(original->getPatrolSpeed());
            for (const auto& behavior : original->getBehaviors()) {
                newObj->addBehavior(behavior);
            }
            m_sceneObjects.push_back(std::move(newObj));
        } else {
            return -1;
        }
        return static_cast<int>(m_sceneObjects.size()) - 1;
    }

    void duplicateObject(int index) {
        if (index < 0 || index >= static_cast<int>(m_sceneObjects.size())) return;

        SceneObject* original = m_sceneObjects[index].get();

        if (index == m_spawnObjectIndex) {
            std::cout << "Cannot duplicate spawn point" << std::endl;
            return;
        }

        if (!original->getModelPath().empty()) {
            std::unique_ptr<SceneObject> newObj;
            std::string modelPath = original->getModelPath();
            std::string ext = modelPath.substr(modelPath.find_last_of('.') + 1);

            if (ext == "lime") {
                auto result = LimeLoader::load(modelPath);
                if (!result.success) {
                    std::cerr << "Failed to reload .lime model for duplication: " << modelPath << std::endl;
                    return;
                }
                newObj = LimeLoader::createSceneObject(result.mesh, *m_modelRenderer);
            } else {
                auto result = GLBLoader::load(modelPath);
                if (!result.success || result.meshes.empty()) {
                    std::cerr << "Failed to reload model for duplication: " << modelPath << std::endl;
                    return;
                }
                newObj = GLBLoader::createSceneObject(result.meshes[0], *m_modelRenderer);
            }

            if (!newObj) {
                std::cerr << "Failed to create duplicate scene object" << std::endl;
                return;
            }

            newObj->setModelPath(modelPath);
            newObj->setName(generateUniqueName(original->getName()));

            glm::vec3 pos = original->getTransform().getPosition();
            newObj->getTransform().setPosition(pos);
            newObj->setEulerRotation(original->getEulerRotation());
            newObj->getTransform().setScale(original->getTransform().getScale());

            newObj->setHueShift(original->getHueShift());
            newObj->setSaturation(original->getSaturation());
            newObj->setBrightness(original->getBrightness());
            newObj->setBeingType(original->getBeingType());
            newObj->setDailySchedule(original->hasDailySchedule());
            newObj->setPatrolSpeed(original->getPatrolSpeed());

            for (const auto& behavior : original->getBehaviors()) {
                newObj->addBehavior(behavior);
            }

            m_sceneObjects.push_back(std::move(newObj));
        } else if (original->hasMeshData()) {
            const auto& verts = original->getVertices();
            const auto& inds = original->getIndices();

            uint32_t handle = m_modelRenderer->createModel(verts, inds);

            auto newObj = std::make_unique<SceneObject>(generateUniqueName(original->getName()));
            newObj->setBufferHandle(handle);
            newObj->setVertexCount(static_cast<uint32_t>(verts.size()));
            newObj->setIndexCount(static_cast<uint32_t>(inds.size()));
            newObj->setLocalBounds(original->getLocalBounds());
            newObj->setModelPath("");
            newObj->setMeshData(verts, inds);

            glm::vec3 pos = original->getTransform().getPosition();
            newObj->getTransform().setPosition(pos);
            newObj->setEulerRotation(original->getEulerRotation());
            newObj->getTransform().setScale(original->getTransform().getScale());

            newObj->setHueShift(original->getHueShift());
            newObj->setSaturation(original->getSaturation());
            newObj->setBrightness(original->getBrightness());
            newObj->setBeingType(original->getBeingType());
            newObj->setDailySchedule(original->hasDailySchedule());
            newObj->setPatrolSpeed(original->getPatrolSpeed());

            for (const auto& behavior : original->getBehaviors()) {
                newObj->addBehavior(behavior);
            }

            m_sceneObjects.push_back(std::move(newObj));
        } else {
            std::cout << "Cannot duplicate this object (no mesh data)" << std::endl;
            return;
        }

        selectObject(static_cast<int>(m_sceneObjects.size()) - 1);
        std::cout << "Object duplicated" << std::endl;
    }

    void pickFaceAtMouse() {
        float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();
        glm::vec2 mousePos = Input::getMousePosition();
        float normalizedX = (mousePos.x / getWindow().getWidth()) * 2.0f - 1.0f;
        float normalizedY = 1.0f - (mousePos.y / getWindow().getHeight()) * 2.0f;

        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 5000.0f);
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 invVP = glm::inverse(proj * view);

        glm::vec4 nearPoint = invVP * glm::vec4(normalizedX, normalizedY, -1.0f, 1.0f);
        glm::vec4 farPoint = invVP * glm::vec4(normalizedX, normalizedY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        glm::vec3 rayOrigin = glm::vec3(nearPoint);
        glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

        // Find closest cube-primitive hit with triangle-level raycast
        int closestIndex = -1;
        float closestDist = std::numeric_limits<float>::max();
        glm::vec3 hitNormal(0);

        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            auto& obj = m_sceneObjects[i];
            if (!obj || !obj->isVisible()) continue;
            if (obj->getPrimitiveType() != PrimitiveType::Cube) continue;

            // Quick AABB pre-check
            AABB worldBounds = obj->getWorldBounds();
            float aabbDist = worldBounds.intersect(rayOrigin, rayDir);
            if (aabbDist < 0 || aabbDist >= closestDist) continue;

            auto hit = obj->raycast(rayOrigin, rayDir);
            if (hit.hit && hit.distance < closestDist) {
                closestDist = hit.distance;
                closestIndex = static_cast<int>(i);
                hitNormal = hit.normal;
            }
        }

        if (closestIndex < 0) return;

        // Quantize normal to nearest axis
        glm::ivec3 qNormal(0);
        float ax = std::abs(hitNormal.x), ay = std::abs(hitNormal.y), az = std::abs(hitNormal.z);
        if (ax >= ay && ax >= az) {
            qNormal.x = (hitNormal.x > 0) ? 1 : -1;
        } else if (ay >= ax && ay >= az) {
            qNormal.y = (hitNormal.y > 0) ? 1 : -1;
        } else {
            qNormal.z = (hitNormal.z > 0) ? 1 : -1;
        }

        // Build spatial index of all cube-primitive objects: grid pos → object index
        // Use doubled coordinates (multiply by 2 and round) to avoid .5 ambiguity
        // Blocks at positions like 10.5, 11.5 become grid keys 21, 23 — stable and unique
        struct IVec3Hash {
            size_t operator()(const glm::ivec3& v) const {
                size_t h = std::hash<int>()(v.x);
                h ^= std::hash<int>()(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<int>()(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };
        auto toGrid = [](const glm::vec3& pos) -> glm::ivec3 {
            return glm::ivec3(
                static_cast<int>(std::floor(pos.x * 2.0f + 0.5f)),
                static_cast<int>(std::floor(pos.y * 2.0f + 0.5f)),
                static_cast<int>(std::floor(pos.z * 2.0f + 0.5f))
            );
        };
        std::unordered_map<glm::ivec3, int, IVec3Hash> grid;
        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            auto& obj = m_sceneObjects[i];
            if (!obj || !obj->isVisible()) continue;
            if (obj->getPrimitiveType() != PrimitiveType::Cube) continue;
            glm::vec3 pos = obj->getTransform().getPosition();
            grid[toGrid(pos)] = static_cast<int>(i);
        }

        // Get starting block grid position
        glm::vec3 startPos = m_sceneObjects[closestIndex]->getTransform().getPosition();
        glm::ivec3 startGP = toGrid(startPos);

        // Determine which 2 axes to flood-fill in (perpendicular to normal)
        int normalAxis = (qNormal.x != 0) ? 0 : (qNormal.y != 0) ? 1 : 2;

        // BFS flood fill
        m_selectedFaces.clear();
        std::unordered_set<glm::ivec3, IVec3Hash> visited;
        std::queue<glm::ivec3> bfsQueue;
        bfsQueue.push(startGP);
        visited.insert(startGP);

        // The two perpendicular axis directions for BFS neighbors
        // Step size is 2 in doubled-coordinate grid (1.0 world unit = 2 grid units)
        glm::ivec3 dirs[4];
        int dirCount = 0;
        if (normalAxis != 0) { dirs[dirCount++] = {2,0,0}; dirs[dirCount++] = {-2,0,0}; }
        if (normalAxis != 1) { dirs[dirCount++] = {0,2,0}; dirs[dirCount++] = {0,-2,0}; }
        if (normalAxis != 2) { dirs[dirCount++] = {0,0,2}; dirs[dirCount++] = {0,0,-2}; }

        while (!bfsQueue.empty()) {
            glm::ivec3 cur = bfsQueue.front();
            bfsQueue.pop();

            // Check that this block is on the same face plane as the start
            if (cur[normalAxis] != startGP[normalAxis]) continue;

            m_selectedFaces.push_back({grid[cur], qNormal});

            for (int d = 0; d < dirCount; d++) {
                glm::ivec3 neighbor = cur + dirs[d];
                if (visited.count(neighbor)) continue;
                visited.insert(neighbor);
                if (grid.count(neighbor) && neighbor[normalAxis] == startGP[normalAxis]) {
                    bfsQueue.push(neighbor);
                }
            }
        }
        syncFaceSelectionToUI();
    }

    void syncFaceSelectionToUI() {
        // Deduplicate object indices and pass to EditorUI
        std::set<int> uniqueIndices;
        for (const auto& sf : m_selectedFaces) {
            uniqueIndices.insert(sf.objectIndex);
        }
        m_editorUI.setFaceSelectedIndices(std::vector<int>(uniqueIndices.begin(), uniqueIndices.end()));
    }

    void pickObjectAtMouse() {
        m_selectedFaces.clear();
        syncFaceSelectionToUI();
        float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();
        glm::vec2 mousePos = Input::getMousePosition();
        float normalizedX = (mousePos.x / getWindow().getWidth()) * 2.0f - 1.0f;
        float normalizedY = 1.0f - (mousePos.y / getWindow().getHeight()) * 2.0f;

        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 5000.0f);
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 invVP = glm::inverse(proj * view);

        glm::vec4 nearPoint = invVP * glm::vec4(normalizedX, normalizedY, -1.0f, 1.0f);
        glm::vec4 farPoint = invVP * glm::vec4(normalizedX, normalizedY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        glm::vec3 rayOrigin = glm::vec3(nearPoint);
        glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

        int closestIndex = -1;
        float closestDist = std::numeric_limits<float>::max();

        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            auto& obj = m_sceneObjects[i];
            if (!obj || !obj->isVisible()) continue;

            AABB worldBounds = obj->getWorldBounds();
            float dist = worldBounds.intersect(rayOrigin, rayDir);

            if (dist >= 0 && dist < closestDist) {
                closestDist = dist;
                closestIndex = static_cast<int>(i);
            }
        }

        selectObject(closestIndex);
    }

    void updateSceneObjectsList() {
        std::vector<SceneObject*> objects;
        objects.reserve(m_sceneObjects.size());
        for (const auto& obj : m_sceneObjects) {
            objects.push_back(obj.get());
        }
        m_editorUI.setSceneObjects(objects);
    }

private:
    // Renderers
    std::unique_ptr<TerrainPipeline> m_pipeline;
    std::unique_ptr<TextureManager> m_textureManager;
    std::unique_ptr<ProceduralSkybox> m_skybox;
    std::unique_ptr<BrushRing> m_brushRing;
    std::unique_ptr<GizmoRenderer> m_gizmoRenderer;
    std::unique_ptr<ModelRenderer> m_modelRenderer;
    std::unique_ptr<SkinnedModelRenderer> m_skinnedModelRenderer;
    std::unique_ptr<WaterRenderer> m_waterRenderer;
    std::unique_ptr<SplineRenderer> m_splineRenderer;
    std::unique_ptr<AINodeRenderer> m_aiNodeRenderer;
    DialogueBubbleRenderer m_dialogueRenderer;
    SceneObject* m_currentInteractObject = nullptr;
    SceneObject* m_playerAvatar = nullptr;  // Visual representation of player for AI perception
    glm::vec3 m_thirdPersonPlayerPos{0};   // Player position for third-person camera
    float m_collisionHullHeight{1.7f};     // Height of collision hull (feet to eye)
    float m_collisionHullRadius{0.5f};     // Radius of collision hull
    glm::vec3 m_lastInteractBubblePos{0};
    bool m_inConversation = false;
    float m_conversationTargetYaw = 0.0f;
    bool m_hasConversationTargetYaw = false;
    char m_responseBuffer[256] = "";

    // Quick chat mode (press / to type, Enter to send - Minecraft style)
    bool m_quickChatMode = false;
    char m_quickChatBuffer[512] = "";

    // Chat log (Minecraft-style - shows recent messages)
    struct ChatLogEntry {
        std::string sender;
        std::string message;
        float timeRemaining;  // Fade out timer
    };
    std::vector<ChatLogEntry> m_chatLog;
    static constexpr int MAX_CHAT_LOG_ENTRIES = 8;
    static constexpr float CHAT_MESSAGE_DURATION = 10.0f;  // Seconds before fade

    // Persistent world chat history (all messages, never expires)
    struct WorldChatEntry {
        std::string sender;
        std::string message;
    };
    std::vector<WorldChatEntry> m_worldChatHistory;
    bool m_showWorldChatHistory = false;
    bool m_worldChatScrollToBottom = false;

    // Conversation history
    struct ChatMessage {
        std::string sender;     // "Player" or NPC name
        std::string text;
        bool isPlayer;
    };
    std::vector<ChatMessage> m_conversationHistory;
    bool m_scrollToBottom = false;

    // AI Backend
    std::unique_ptr<AsyncHttpClient> m_httpClient;
    std::string m_currentSessionId;
    std::unordered_map<std::string, std::string> m_quickChatSessionIds;  // npcName -> session_id
    bool m_waitingForAIResponse = false;

    // Heartbeat (passive perception for EDEN companions)
    float m_heartbeatTimer = 0.0f;
    float m_heartbeatInterval = 5.0f;   // seconds between heartbeat polls
    bool m_heartbeatEnabled = true;
    bool m_heartbeatInFlight = false;    // prevent stacking requests

    // TTS (text-to-speech)
    int m_ttsFileCounter = 0;
    std::string m_lastTTSFile;
    float m_ttsCooldown = 0.0f;        // seconds until next TTS can play (prevents overlap)
    bool m_ttsInFlight = false;        // a TTS request is waiting for audio from backend

    // Push-to-talk (speech-to-text)
    bool m_pttRecording = false;
    bool m_pttProcessing = false;

    // MCP Server (Claude Code integration)
    std::unique_ptr<MCPServer> m_mcpServer;
    
    // AI Motor Control Actions
    bool m_aiActionActive = false;
    std::string m_aiActionType;
    float m_aiActionDuration = 2.0f;
    float m_aiActionTimer = 0.0f;

    // AI Follow Mode — multi-NPC support (independent of conversation state)
    struct AIFollowState {
        SceneObject* npc = nullptr;
        float distance = 4.0f;
        float speed = 5.0f;
    };
    std::vector<AIFollowState> m_aiFollowers;
    float m_aiActionStartYaw = 0.0f;
    float m_aiActionTargetYaw = 0.0f;
    // Movement action state
    glm::vec3 m_aiActionStartPos{0.0f};
    glm::vec3 m_aiActionTargetPos{0.0f};
    float m_aiActionSpeed = 5.0f;  // units per second
    // Pickup action state
    SceneObject* m_aiPickupTarget = nullptr;
    std::string m_aiPickupTargetName;

    // Place action state
    SceneObject* m_aiPlaceTarget = nullptr;
    std::string m_aiPlaceTargetName;

    PerceptionData m_lastFullScanResult;
    bool m_hasFullScanResult = false;


    Gizmo m_gizmo;

    // ImGui
    ImGuiManager m_imguiManager;

    // Splash screen
    VkImage m_splashImage = VK_NULL_HANDLE;
    VkDeviceMemory m_splashMemory = VK_NULL_HANDLE;
    VkImageView m_splashView = VK_NULL_HANDLE;
    VkSampler m_splashSampler = VK_NULL_HANDLE;
    VkDescriptorSet m_splashDescriptor = VK_NULL_HANDLE;
    int m_splashWidth = 0;
    int m_splashHeight = 0;
    bool m_splashLoaded = false;

    // Grove scripting VM
    GroveVm* m_groveVm = nullptr;
    std::string m_groveOutputAccum;  // accumulates log() output during eval
    std::string m_groveScriptsDir;   // ~/eden/scripts/
    SceneObject* m_groveBotTarget = nullptr;  // Current AlgoBot target for behavior functions
    std::string m_groveCurrentScriptName = "grove_script";  // Name for behavior created by current script
    GroveContext m_groveContext;

    // Grove logo texture
    VkImage m_groveLogoImage = VK_NULL_HANDLE;
    VkDeviceMemory m_groveLogoMemory = VK_NULL_HANDLE;
    VkImageView m_groveLogoView = VK_NULL_HANDLE;
    VkSampler m_groveLogoSampler = VK_NULL_HANDLE;
    VkDescriptorSet m_groveLogoDescriptor = VK_NULL_HANDLE;
    bool m_groveLogoLoaded = false;

    // Game objects
    Camera m_camera{{0, 100, 0}};
    Terrain m_terrain{{
        .chunkResolution = 64,
        .tileSize = 2.0f,
        .viewDistance = 16,
        .heightScale = 200.0f,
        .noiseScale = 0.003f,
        .noiseOctaves = 5,
        .noisePersistence = 0.45f,
        .useFixedBounds = true,
        .minChunk = {-16, -16},
        .maxChunk = {15, 15},
        .wrapWorld = true
    }};

    // Filesystem browser (3D file/folder objects)
    eden::FilesystemBrowser m_filesystemBrowser;

    // Terminal emulator
    eden::EdenTerminal m_terminal;
    ImFont* m_monoFont = nullptr;
    bool m_sessionMode = false;
    bool m_terminalInitialized = false;
    // 3D terminal screen
    SceneObject* m_terminalScreenObject = nullptr;
    std::vector<unsigned char> m_terminalPixelBuffer;
    bool m_terminalPixelsDirty = false;
    bool m_terminalScreenBound = false;

    // Editor subsystems
    EditorUI m_editorUI;
    std::unique_ptr<TerrainBrushTool> m_brushTool;
    std::unique_ptr<PathTool> m_pathTool;
    std::unique_ptr<ChunkManager> m_chunkManager;
    bool m_wasLeftMouseDown = false;
    bool m_isLooking = false;
    // Orbit/pan camera state (editor mode - LIME-style navigation)
    glm::vec3 m_orbitTarget = glm::vec3(0.0f, 50.0f, 0.0f);  // Pivot point
    float m_orbitYaw = -90.0f;
    float m_orbitPitch = 20.0f;         // Slight downward angle to start
    glm::vec3 m_tumbleOrbitTarget = glm::vec3(0.0f);
    float m_tumbleOrbitDistance = 100.0f;  // Terrain scale — start further out
    bool m_isTumbling = false;
    bool m_wasTumbling = false;
    bool m_isPanning = false;
    bool m_wasGrabbing = false;
    float m_lastGrabMouseY = 0.0f;

    // Entity/Action system
    ActionSystem m_actionSystem;

    // Level save/load
    std::string m_currentLevelPath;
    std::string m_pendingDoorSpawn;  // Door ID to spawn at after level load
    std::unordered_map<std::string, LevelData> m_levelCache;  // Preloaded adjacent levels

    // Level transition fade
    enum class FadeState { NONE, FADING_OUT, LOADING, FADING_IN };
    FadeState m_fadeState = FadeState::NONE;
    float m_fadeAlpha = 0.0f;
    float m_fadeDuration = 0.3f;  // seconds for fade in/out
    std::string m_pendingLevelPath;
    std::string m_pendingTargetDoorId;

    // Loading state
    int m_chunksLoaded = 0;
    int m_totalChunks = 0;

    // FPS tracking
    float m_fps = 0.0f;
    float m_frameTimeAccum = 0.0f;
    int m_frameCount = 0;

    // Time tracking for water animation
    float m_totalTime = 0.0f;

    // Scene objects
    std::vector<std::unique_ptr<SceneObject>> m_sceneObjects;
    int m_selectedObjectIndex = -1;
    std::set<int> m_selectedObjectIndices;  // For multi-select

    // Alt+click face selection for block walls
    struct SelectedFace {
        int objectIndex;
        glm::ivec3 normal;  // quantized axis: e.g. (1,0,0), (0,-1,0)
    };
    std::vector<SelectedFace> m_selectedFaces;
    bool m_gizmoDragging = false;
    TransformMode m_transformMode = TransformMode::Select;
    glm::vec2 m_lastMousePos{0};
    BrushMode m_prevBrushMode = BrushMode::Raise;
    GizmoAxis m_gizmoHoveredAxis = GizmoAxis::None;
    GizmoAxis m_gizmoActiveAxis = GizmoAxis::None;
    glm::vec3 m_gizmoDragRawPos{0};    // Unsnapped accumulator for move snap
    glm::vec3 m_gizmoDragRawEuler{0};  // Unsnapped accumulator for rotate snap

    // Wall draw tool state
    bool m_wallDrawing = false;
    glm::vec3 m_wallCorner1{0};
    glm::vec3 m_wallCorner2{0};
    int m_buildingCounter = 0;  // For naming Building_N_Walls etc.

    // Building texture swatches (loaded from textures/building/)
    struct BuildingTexture {
        std::string name;
        std::vector<unsigned char> pixels;
        int width = 0, height = 0;
        // Vulkan resources for ImGui preview
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };
    std::vector<BuildingTexture> m_buildingTextures;

    // Object groups (for organization only) - uses EditorUI::ObjectGroup
    std::vector<EditorUI::ObjectGroup> m_objectGroups;

    // Pending spawns (processed after behavior updates to avoid iterator invalidation)
    struct SpawnRequest {
        std::string modelPath;
        glm::vec3 position;
        glm::vec3 rotation;
        glm::vec3 scale;
    };
    std::vector<SpawnRequest> m_pendingSpawns;

    // Pending grove-spawned objects (buffered to avoid invalidating m_sceneObjects during iteration)
    std::vector<std::unique_ptr<SceneObject>> m_pendingGroveSpawns;

    // Model cache: maps resolved file path → GPU buffer handle + mesh data
    // Duplicate spawns of the same .lime file share GPU resources instead of
    // creating separate Vulkan buffers/textures/descriptor sets per instance.
    struct CachedModel {
        uint32_t bufferHandle;
        uint32_t indexCount;
        uint32_t vertexCount;
        std::vector<ModelVertex> vertices;
        std::vector<uint32_t> indices;
        AABB bounds;
        glm::vec3 scale{1.0f};    // preserve LimeLoader's scale
        glm::vec3 rotation{0.0f}; // preserve LimeLoader's rotation
    };
    std::unordered_map<std::string, CachedModel> m_modelCache;

    // Objects marked for destruction (processed after behavior updates)
    std::vector<SceneObject*> m_objectsToDestroy;

    // Spawn point
    glm::vec3 m_spawnPosition{0.0f, 0.0f, 0.0f};
    bool m_hasSpawnPoint = false;
    int m_spawnObjectIndex = -1;

    // Underground/subfloor height for placing objects when editing below terrain
    static constexpr float SUBFLOOR_HEIGHT = -100.0f;

    // Play mode state
    bool m_isPlayMode = false;
    bool m_playModeCursorVisible = false;  // Toggle with right-click to interact with UI
    bool m_fsContextMenuOpen = false;      // Filesystem right-click context menu
    bool m_fsContextMenuWasOpen = false;   // Track popup close to restore mouse look
    std::vector<std::string> m_fsClipboard; // Paths of copied/cut files
    bool m_fsClipboardIsCut = false;       // True if clipboard is from Cut (move on paste)
    bool m_fsNewFolderPopup = false;       // Open "New Folder" naming dialog
    char m_fsNewFolderName[256] = "New Folder"; // Name buffer for new folder
    // Wall info saved when "New Folder" is triggered from a wall slot
    bool m_fsNewFolderOnWall = false;
    glm::vec3 m_fsNewFolderWallPos{0.0f};
    glm::vec3 m_fsNewFolderWallScale{0.0f};
    float m_fsNewFolderWallYaw = 0.0f;
    // Rename state
    bool m_fsRenamePopup = false;
    char m_fsRenameName[256] = {};
    std::string m_fsRenameOldPath;             // Full path of file being renamed
    // Drag-and-drop state
    SceneObject* m_fsDragObject = nullptr;     // Object being dragged
    SceneObject* m_fsDragHoverWall = nullptr;  // Wall currently hovered during drag
    float m_fsDragHoldTime = 0.0f;             // How long mouse has been held
    bool m_fsDragActive = false;               // True once hold threshold passed
    bool m_fsLeftWasDown = false;              // Track mouse release
    std::string m_fsHoverName;                 // Filename under crosshair
    bool m_playModeDebug = false;          // F3 toggles debug visuals (waypoints, etc.)

    // Game module (loaded for play mode UI)
    std::unique_ptr<eden::GameModule> m_gameModule;
    bool m_showModulePanel = false;
    glm::vec3 m_editorCameraPos{0.0f};

    // Test level state
    bool m_isTestLevel = false;
    bool m_isSpaceLevel = false;
    float m_testFloorSize = 100.0f;
    PhysicsBackend m_physicsBackend = PhysicsBackend::Jolt;  // Physics backend for this level
    float m_editorCameraYaw = 0.0f;
    float m_editorCameraPitch = 0.0f;

    // Game time system (24 game hours = 5 real minutes for testing)
    float m_gameTimeMinutes = 360.0f;  // Current time in game minutes (0-1440), starts at 0600
    float m_gameTimeScale = 4.8f;      // Game minutes per real second (1440 / 300 = 4.8)

    // Player economy
    float m_playerCredits = 1000.0f;   // Starting credits
    float m_cityCredits = 5000.0f;     // City treasury (separate from player)

    // Camera speed (tracked separately since Camera doesn't expose getter)
    float m_cameraSpeed = 15.0f;

    // AI Nodes
    std::vector<std::unique_ptr<AINode>> m_aiNodes;
    int m_selectedAINodeIndex = -1;
    uint32_t m_nextAINodeId = 1;
    bool m_aiPlacementMode = false;
    int m_aiPlacementType = 0;
    glm::vec3 m_aiPlacementPreview{0};
    bool m_hasAIPlacementPreview = false;

    // AI Paths
    std::vector<std::unique_ptr<AIPath>> m_aiPaths;
    int m_selectedPathIndex = -1;
    uint32_t m_nextPathId = 1;

    // Economy and Trading Systems
    std::unique_ptr<EconomySystem> m_economySystem;
    std::unique_ptr<CityGovernor> m_cityGovernor;
    std::unique_ptr<AStarPathfinder> m_pathfinder;

    // Physics/Collision
    // Zone system
    std::unique_ptr<ZoneSystem> m_zoneSystem;
    bool m_showZoneMap = false;

    // World generation
    bool m_worldGenerated = false;
    bool m_showPlanetInfo = false;
    nlohmann::json m_planetData;
    float m_zoneMapZoom = 1.0f;
    glm::vec2 m_zoneMapPan{0.0f, 0.0f};
    bool m_zoneMapDragging = false;
    glm::vec2 m_zoneMapDragStart{0.0f};

    std::unique_ptr<PhysicsWorld> m_physicsWorld;
    std::unique_ptr<ICharacterController> m_characterController;  // Physics character controller (Jolt or Homebrew)

    // Traders (player + AI)
    std::vector<std::unique_ptr<TraderAI>> m_modelTraders;  // Traders created from model scripts
    uint32_t m_nextTraderId = 1;

    // Economy UI state
    bool m_showEconomyPanel = false;
    bool m_showTraderPanel = false;

    // Projectiles
    struct Projectile {
        glm::vec3 position;
        glm::vec3 startPosition;
        glm::vec3 velocity;
        float size = 0.3f;
        float lifetime = 0.0f;
        int sceneObjectIndex = -1;  // Index in m_sceneObjects for rendering
        bool isEnemy = false;       // True if fired by enemy AI
    };
    std::vector<Projectile> m_projectiles;
    float m_shootCooldown = 0.0f;

    // Player health
    float m_playerHealth = 100.0f;
    float m_playerMaxHealth = 100.0f;
    float m_playerHitboxRadius = 1.0f;  // Invisible sphere hitbox around camera
    int m_engineHumLoopId = -1;
    MovementMode m_lastMovementMode = MovementMode::Fly;

    // Dogfight AI
    std::vector<std::unique_ptr<DogfightAI>> m_dogfighters;
    uint32_t m_nextDogfighterId = 1;

    // Jettisoned cargo (floating objects that can be picked up)
    struct JettisonedCargo {
        glm::vec3 position;
        glm::vec3 velocity;
        float value;
        float lifetime = 60.0f;
        int sceneObjectIndex = -1;
    };
    std::vector<JettisonedCargo> m_jettisonedCargo;

    // Ejected pilots (falling/parachuting objects)
    struct EjectedPilot {
        glm::vec3 position;
        glm::vec3 velocity;
        float lifetime = 120.0f;
        int sceneObjectIndex = -1;
        bool hasParachute = false;
    };
    std::vector<EjectedPilot> m_ejectedPilots;

    // Pirate system - scans for and attacks traders with cargo
    struct Pirate {
        uint32_t dogfighterId;      // Associated DogfightAI
        SceneObject* sceneObject;   // Visual representation
        float scanTimer = 0.0f;     // Time until next scan
        float scanInterval = 2.0f;  // Scan every 2 seconds
        float scanRange = 800.0f;   // Detection range for traders
        SceneObject* targetTrader = nullptr;  // Current target
        bool waitingForCargoJettison = false; // True when target is low health
    };
    std::vector<Pirate> m_pirates;
};

static void crashHandler(int sig) {
    std::cerr << "\n=== CRASH: signal " << sig << " ===" << std::endl;
    void* frames[32];
    int n = backtrace(frames, 32);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    std::cerr << "=== END CRASH ===" << std::endl;
    _exit(1);
}

int main(int argc, char* argv[]) {
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE, crashHandler);

    bool sessionMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--session-mode") {
            sessionMode = true;
        }
    }

    try {
        TerrainEditor editor;
        if (sessionMode) {
            editor.setSessionMode(true);
        }
        editor.run();
    } catch (const std::exception& e) {
        std::cerr << "\n=== EXCEPTION: " << e.what() << " ===" << std::endl;
        std::cerr.flush();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
