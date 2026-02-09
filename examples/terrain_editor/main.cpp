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

#include <iostream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <set>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>

using namespace eden;

struct TerrainPushConstants {
    glm::mat4 mvp;
    glm::vec4 fogColor;
    float fogStart;
    float fogEnd;
};

class TerrainEditor : public VulkanApplicationBase {
public:
    TerrainEditor() : VulkanApplicationBase(1280, 720, "EDEN - Terrain Editor") {}

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
        loadSplashTexture();
        loadGroveLogoTexture();

        // Create scripts directory
        {
            const char* home = getenv("HOME");
            m_groveScriptsDir = home ? std::string(home) + "/eden/scripts" : "scripts";
            std::filesystem::create_directories(m_groveScriptsDir);
            std::cout << "Grove scripts directory: " << m_groveScriptsDir << std::endl;
        }

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

        // Initialize Economy and Trading Systems
        initializeEconomySystems();

        // Initialize Zone System
        m_zoneSystem = std::make_unique<ZoneSystem>(-2016.0f, -2016.0f, 2016.0f, 2016.0f, 32.0f);
        m_zoneSystem->generateDefaultLayout();
        m_editorUI.setZoneSystem(m_zoneSystem.get());
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
        getContext().waitIdle();

        saveEditorConfig();
        Audio::getInstance().shutdown();
        NFD_Quit();
        cleanupSplashTexture();
        cleanupGroveLogoTexture();
        if (m_groveVm) { grove_destroy(m_groveVm); m_groveVm = nullptr; }

        m_imguiManager.cleanup();

        m_waterRenderer.reset();
        m_skinnedModelRenderer.reset();
        m_modelRenderer.reset();
        m_skybox.reset();
        m_pipeline.reset();
    }

    void update(float deltaTime) override {
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

        for (size_t i = 0; i < m_sceneObjects.size(); i++) {
            const auto& objPtr = m_sceneObjects[i];
            if (!objPtr || !objPtr->isVisible()) continue;

            // Hide door trigger zones in play mode (they're invisible interaction areas)
            if (m_isPlayMode && objPtr->isDoor()) continue;

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

        if (m_waterRenderer && m_waterRenderer->isVisible()) {
            m_waterRenderer->render(cmd, vp, m_camera.getPosition(), m_totalTime);
        }

        if (m_brushRing) {
            m_brushRing->render(cmd, vp);
        }

        if (m_gizmoRenderer && m_gizmo.isVisible()) {
            m_gizmoRenderer->render(cmd, vp, m_gizmo);
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
                m_editorUI.getSelectedTexBrightness()
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

    // ── Grove scripting VM initialization ──────────────
    static int32_t groveLogFn(const GroveValue* args, uint32_t argc, GroveValue* /*result*/, void* ud) {
        auto* accum = static_cast<std::string*>(ud);
        for (uint32_t i = 0; i < argc; i++) {
            if (i > 0) accum->append("\t");
            switch (args[i].tag) {
                case GROVE_NIL:    accum->append("nil"); break;
                case GROVE_BOOL:   accum->append(args[i].data.bool_val ? "true" : "false"); break;
                case GROVE_NUMBER: {
                    double n = args[i].data.number_val;
                    if (n == static_cast<double>(static_cast<int64_t>(n)) && std::isfinite(n))
                        accum->append(std::to_string(static_cast<int64_t>(n)));
                    else
                        accum->append(std::to_string(n));
                    break;
                }
                case GROVE_STRING: {
                    auto& sv = args[i].data.string_val;
                    if (sv.ptr && sv.len > 0) accum->append(sv.ptr, sv.len);
                    break;
                }
                case GROVE_VEC3: {
                    auto& v = args[i].data.vec3_val;
                    accum->append("vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")");
                    break;
                }
                case GROVE_OBJECT:
                    accum->append("<object:" + std::to_string(args[i].data.object_handle) + ">");
                    break;
            }
        }
        accum->append("\n");
        return 0;
    }

    static int32_t groveTerrainHeightFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        if (argc < 1 || args[0].tag != GROVE_VEC3) {
            result->tag = GROVE_NUMBER;
            result->data.number_val = 0.0;
            return 0;
        }
        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);
        float h = self->m_terrain.getHeightAt(x, z);
        result->tag = GROVE_NUMBER;
        result->data.number_val = static_cast<double>(h);
        return 0;
    }

    static int32_t groveSpawnFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        // spawn(name_string, position_vec3)
        std::string name = "grove_object";
        glm::vec3 pos(0.0f);

        if (argc >= 1 && args[0].tag == GROVE_STRING) {
            auto& sv = args[0].data.string_val;
            if (sv.ptr && sv.len > 0) name = std::string(sv.ptr, sv.len);
        }
        if (argc >= 2 && args[1].tag == GROVE_VEC3) {
            pos.x = static_cast<float>(args[1].data.vec3_val.x);
            pos.y = static_cast<float>(args[1].data.vec3_val.y);
            pos.z = static_cast<float>(args[1].data.vec3_val.z);
        }

        SceneObject* obj = self->spawnPostholeAt(pos, name);
        result->tag = GROVE_OBJECT;
        result->data.object_handle = obj ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(obj)) : 0;
        return 0;
    }

    // ─── Construction primitives for Grove scripts ───

    // get_player_pos() → vec3
    static int32_t groveGetPlayerPos(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        glm::vec3 pos = self->m_camera.getPosition();
        result->tag = GROVE_VEC3;
        result->data.vec3_val.x = static_cast<double>(pos.x);
        result->data.vec3_val.y = static_cast<double>(pos.y);
        result->data.vec3_val.z = static_cast<double>(pos.z);
        return 0;
    }

    // spawn_cube(name, pos, size, r, g, b) → bool
    static int32_t groveSpawnCubeFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;

        if (argc < 6 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
            args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER ||
            args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "grove_cube";
        float size = static_cast<float>(args[2].data.number_val);
        float r = static_cast<float>(args[3].data.number_val);
        float g = static_cast<float>(args[4].data.number_val);
        float b = static_cast<float>(args[5].data.number_val);
        glm::vec4 color(r, g, b, 1.0f);

        auto meshData = PrimitiveMeshBuilder::createCube(size, color);
        auto obj = std::make_unique<SceneObject>(name);
        uint32_t handle = self->m_modelRenderer->createModel(meshData.vertices, meshData.indices);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
        obj->setLocalBounds(meshData.bounds);
        obj->setModelPath("");
        obj->setMeshData(meshData.vertices, meshData.indices);
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setPrimitiveSize(size);
        obj->setPrimitiveColor(color);

        // Position bottom on terrain
        float posX = static_cast<float>(args[1].data.vec3_val.x);
        float posZ = static_cast<float>(args[1].data.vec3_val.z);
        float terrainY = self->m_terrain.getHeightAt(posX, posZ);
        float halfSize = size * 0.5f;
        obj->getTransform().setPosition(glm::vec3(posX, terrainY + halfSize, posZ));

        self->m_sceneObjects.push_back(std::move(obj));
        std::cout << "[Grove] Spawned cube '" << name << "' at (" << posX << ", " << terrainY + halfSize << ", " << posZ << ")" << std::endl;
        result->data.bool_val = 1;
        return 0;
    }

    // spawn_cylinder(name, pos, radius, height, r, g, b) → bool
    static int32_t groveSpawnCylinderFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;

        if (argc < 7 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
            args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER ||
            args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER ||
            args[6].tag != GROVE_NUMBER) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "grove_cylinder";
        float radius = static_cast<float>(args[2].data.number_val);
        float height = static_cast<float>(args[3].data.number_val);
        float r = static_cast<float>(args[4].data.number_val);
        float g = static_cast<float>(args[5].data.number_val);
        float b = static_cast<float>(args[6].data.number_val);
        glm::vec4 color(r, g, b, 1.0f);

        auto meshData = PrimitiveMeshBuilder::createCylinder(radius, height, 12, color);
        auto obj = std::make_unique<SceneObject>(name);
        uint32_t handle = self->m_modelRenderer->createModel(meshData.vertices, meshData.indices);
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

        // Position bottom on terrain
        float posX = static_cast<float>(args[1].data.vec3_val.x);
        float posZ = static_cast<float>(args[1].data.vec3_val.z);
        float terrainY = self->m_terrain.getHeightAt(posX, posZ);
        // Cylinder mesh is centered at origin, so offset by half height
        float halfHeight = height * 0.5f;
        obj->getTransform().setPosition(glm::vec3(posX, terrainY + halfHeight, posZ));

        self->m_sceneObjects.push_back(std::move(obj));
        std::cout << "[Grove] Spawned cylinder '" << name << "' at (" << posX << ", " << terrainY + halfHeight << ", " << posZ << ")" << std::endl;
        result->data.bool_val = 1;
        return 0;
    }

    // spawn_model(name, path, pos) → bool
    static int32_t groveSpawnModelFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;

        if (argc < 3 || args[0].tag != GROVE_STRING || args[1].tag != GROVE_STRING ||
            args[2].tag != GROVE_VEC3) return 0;

        auto& nameSv = args[0].data.string_val;
        std::string name = (nameSv.ptr && nameSv.len > 0) ? std::string(nameSv.ptr, nameSv.len) : "grove_model";
        auto& pathSv = args[1].data.string_val;
        std::string modelPath = (pathSv.ptr && pathSv.len > 0) ? std::string(pathSv.ptr, pathSv.len) : "";

        if (modelPath.empty()) return 0;

        // Resolve relative paths from current level directory
        if (modelPath[0] != '/' && !self->m_currentLevelPath.empty()) {
            size_t lastSlash = self->m_currentLevelPath.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                modelPath = self->m_currentLevelPath.substr(0, lastSlash + 1) + modelPath;
            }
        }

        bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";
        std::unique_ptr<SceneObject> obj;

        if (isLime) {
            auto loadResult = LimeLoader::load(modelPath);
            if (loadResult.success) {
                obj = LimeLoader::createSceneObject(loadResult.mesh, *self->m_modelRenderer);
            }
        } else {
            auto loadResult = GLBLoader::load(modelPath);
            if (loadResult.success && !loadResult.meshes.empty()) {
                obj = GLBLoader::createSceneObject(loadResult.meshes[0], *self->m_modelRenderer);
            }
        }

        if (!obj) {
            std::cout << "[Grove] Failed to load model: " << modelPath << std::endl;
            return 0;
        }

        obj->setName(name);
        obj->setModelPath(modelPath);

        // Position bottom on terrain using min-vertex-Y offset
        float posX = static_cast<float>(args[2].data.vec3_val.x);
        float posZ = static_cast<float>(args[2].data.vec3_val.z);
        float terrainY = self->m_terrain.getHeightAt(posX, posZ);

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
        obj->getTransform().setPosition(glm::vec3(posX, terrainY + bottomOffset, posZ));

        self->m_sceneObjects.push_back(std::move(obj));
        std::cout << "[Grove] Spawned model '" << name << "' at (" << posX << ", " << terrainY + bottomOffset << ", " << posZ << ")" << std::endl;
        result->data.bool_val = 1;
        return 0;
    }

    // set_object_rotation(name, rx, ry, rz) → bool
    static int32_t groveSetObjectRotation(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;

        if (argc < 4 || args[0].tag != GROVE_STRING ||
            args[1].tag != GROVE_NUMBER || args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";
        float rx = static_cast<float>(args[1].data.number_val);
        float ry = static_cast<float>(args[2].data.number_val);
        float rz = static_cast<float>(args[3].data.number_val);

        for (auto& obj : self->m_sceneObjects) {
            if (obj && obj->getName() == name) {
                obj->setEulerRotation(glm::vec3(rx, ry, rz));
                result->data.bool_val = 1;
                return 0;
            }
        }
        return 0;
    }

    // set_object_scale(name, sx, sy, sz) → bool
    static int32_t groveSetObjectScale(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;

        if (argc < 4 || args[0].tag != GROVE_STRING ||
            args[1].tag != GROVE_NUMBER || args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";
        float sx = static_cast<float>(args[1].data.number_val);
        float sy = static_cast<float>(args[2].data.number_val);
        float sz = static_cast<float>(args[3].data.number_val);

        for (auto& obj : self->m_sceneObjects) {
            if (obj && obj->getName() == name) {
                obj->getTransform().setScale(glm::vec3(sx, sy, sz));
                result->data.bool_val = 1;
                return 0;
            }
        }
        return 0;
    }

    // delete_object(name) → bool
    static int32_t groveDeleteObject(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;

        if (argc < 1 || args[0].tag != GROVE_STRING) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

        for (auto it = self->m_sceneObjects.begin(); it != self->m_sceneObjects.end(); ++it) {
            if (*it && (*it)->getName() == name) {
                self->m_sceneObjects.erase(it);
                std::cout << "[Grove] Deleted object '" << name << "'" << std::endl;
                result->data.bool_val = 1;
                return 0;
            }
        }
        return 0;
    }

    // ─── Queued construction commands (execute during behavior sequence) ───
    // These queue GROVE_COMMAND actions on the bot target's behavior.
    // The command is stored as a pipe-delimited string in stringParam,
    // position in vec3Param. Parsed and executed by updateActiveBehavior.

    // queue_spawn_cube(name, pos, size, r, g, b) — queue a cube spawn
    static int32_t groveQueueSpawnCube(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (!self->m_groveBotTarget || argc < 6) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
            args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER ||
            args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "cube";

        glm::vec3 pos(static_cast<float>(args[1].data.vec3_val.x),
                      static_cast<float>(args[1].data.vec3_val.y),
                      static_cast<float>(args[1].data.vec3_val.z));

        // Encode: "cube|name|size|r|g|b"
        std::string cmd = "cube|" + name + "|" +
            std::to_string(args[2].data.number_val) + "|" +
            std::to_string(args[3].data.number_val) + "|" +
            std::to_string(args[4].data.number_val) + "|" +
            std::to_string(args[5].data.number_val);

        Action a;
        a.type = ActionType::GROVE_COMMAND;
        a.stringParam = cmd;
        a.vec3Param = pos;
        a.duration = 0.0f;
        b->actions.push_back(a);

        result->data.bool_val = 1;
        return 0;
    }

    // queue_spawn_cylinder(name, pos, radius, height, r, g, b) — queue a cylinder spawn
    static int32_t groveQueueSpawnCylinder(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (!self->m_groveBotTarget || argc < 7) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_VEC3 ||
            args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER ||
            args[4].tag != GROVE_NUMBER || args[5].tag != GROVE_NUMBER ||
            args[6].tag != GROVE_NUMBER) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "cylinder";

        glm::vec3 pos(static_cast<float>(args[1].data.vec3_val.x),
                      static_cast<float>(args[1].data.vec3_val.y),
                      static_cast<float>(args[1].data.vec3_val.z));

        // Encode: "cylinder|name|radius|height|r|g|b"
        std::string cmd = "cylinder|" + name + "|" +
            std::to_string(args[2].data.number_val) + "|" +
            std::to_string(args[3].data.number_val) + "|" +
            std::to_string(args[4].data.number_val) + "|" +
            std::to_string(args[5].data.number_val) + "|" +
            std::to_string(args[6].data.number_val);

        Action a;
        a.type = ActionType::GROVE_COMMAND;
        a.stringParam = cmd;
        a.vec3Param = pos;
        a.duration = 0.0f;
        b->actions.push_back(a);

        result->data.bool_val = 1;
        return 0;
    }

    // queue_set_rotation(name, rx, ry, rz) — queue a rotation change
    static int32_t groveQueueSetRotation(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (!self->m_groveBotTarget || argc < 4) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_NUMBER ||
            args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

        // Encode: "set_rotation|name|rx|ry|rz"
        std::string cmd = "set_rotation|" + name + "|" +
            std::to_string(args[1].data.number_val) + "|" +
            std::to_string(args[2].data.number_val) + "|" +
            std::to_string(args[3].data.number_val);

        Action a;
        a.type = ActionType::GROVE_COMMAND;
        a.stringParam = cmd;
        a.duration = 0.0f;
        b->actions.push_back(a);

        result->data.bool_val = 1;
        return 0;
    }

    // queue_set_scale(name, sx, sy, sz) — queue a scale change
    static int32_t groveQueueSetScale(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (!self->m_groveBotTarget || argc < 4) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        if (args[0].tag != GROVE_STRING || args[1].tag != GROVE_NUMBER ||
            args[2].tag != GROVE_NUMBER || args[3].tag != GROVE_NUMBER) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

        // Encode: "set_scale|name|sx|sy|sz"
        std::string cmd = "set_scale|" + name + "|" +
            std::to_string(args[1].data.number_val) + "|" +
            std::to_string(args[2].data.number_val) + "|" +
            std::to_string(args[3].data.number_val);

        Action a;
        a.type = ActionType::GROVE_COMMAND;
        a.stringParam = cmd;
        a.duration = 0.0f;
        b->actions.push_back(a);

        result->data.bool_val = 1;
        return 0;
    }

    // queue_delete(name) — queue an object deletion
    static int32_t groveQueueDelete(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        auto& sv = args[0].data.string_val;
        std::string name = (sv.ptr && sv.len > 0) ? std::string(sv.ptr, sv.len) : "";

        std::string cmd = "delete|" + name;

        Action a;
        a.type = ActionType::GROVE_COMMAND;
        a.stringParam = cmd;
        a.duration = 0.0f;
        b->actions.push_back(a);

        result->data.bool_val = 1;
        return 0;
    }

    // Zone system Grove bindings — return static strings (valid for VM lifetime)
    static int32_t groveZoneTypeFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        if (!self->m_zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
            result->tag = GROVE_STRING;
            result->data.string_val.ptr = "unknown";
            result->data.string_val.len = 7;
            return 0;
        }
        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);
        const char* name = ZoneSystem::zoneTypeName(self->m_zoneSystem->getZoneType(x, z));
        result->tag = GROVE_STRING;
        result->data.string_val.ptr = name;
        result->data.string_val.len = static_cast<uint32_t>(strlen(name));
        return 0;
    }

    static int32_t groveZoneResourceFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        if (!self->m_zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
            result->tag = GROVE_STRING;
            result->data.string_val.ptr = "none";
            result->data.string_val.len = 4;
            return 0;
        }
        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);
        const char* name = ZoneSystem::resourceTypeName(self->m_zoneSystem->getResource(x, z));
        result->tag = GROVE_STRING;
        result->data.string_val.ptr = name;
        result->data.string_val.len = static_cast<uint32_t>(strlen(name));
        return 0;
    }

    static int32_t groveZoneOwnerFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        if (!self->m_zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
            result->tag = GROVE_NUMBER;
            result->data.number_val = 0.0;
            return 0;
        }
        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);
        result->tag = GROVE_NUMBER;
        result->data.number_val = static_cast<double>(self->m_zoneSystem->getOwner(x, z));
        return 0;
    }

    static int32_t groveCanBuildFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        if (!self->m_zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
            result->tag = GROVE_BOOL;
            result->data.bool_val = 0;
            return 0;
        }
        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);
        // Use player ID 1 as default for script queries
        result->tag = GROVE_BOOL;
        result->data.bool_val = self->m_zoneSystem->canBuild(x, z, 1) ? 1 : 0;
        return 0;
    }

    static int32_t grovePlotPriceFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        if (!self->m_zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
            result->tag = GROVE_NUMBER;
            result->data.number_val = 0.0;
            return 0;
        }
        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);
        auto g = self->m_zoneSystem->worldToGrid(x, z);
        result->tag = GROVE_NUMBER;
        result->data.number_val = static_cast<double>(self->m_zoneSystem->getPlotPrice(g.x, g.y));
        return 0;
    }

    // ─── AlgoBot behavior host functions ───
    // These queue actions onto a target SceneObject's behavior list.
    // Usage from Grove:
    //   bot_target("Worker1")    -- select which object to program
    //   move_to(vec3(10, 0, 20)) -- queue a move action
    //   wait(2.0)                -- queue a wait
    //   rotate_to(vec3(0, 90, 0))
    //   bot_run()                -- trigger the behavior

    // bot_target(name_string) — select a scene object by name
    static int32_t groveBotTargetFn(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (argc < 1 || args[0].tag != GROVE_STRING) return 0;

        auto& sv = args[0].data.string_val;
        std::string name(sv.ptr, sv.len);
        self->m_groveBotTarget = nullptr;
        for (auto& obj : self->m_sceneObjects) {
            if (obj->getName() == name) {
                self->m_groveBotTarget = obj.get();
                // Ensure it has a "grove_script" behavior we can append to
                bool found = false;
                for (auto& b : obj->getBehaviors()) {
                    if (b.name == "grove_script") { found = true; break; }
                }
                if (!found) {
                    Behavior b;
                    b.name = "grove_script";
                    b.trigger = TriggerType::MANUAL;
                    b.loop = false;
                    b.enabled = true;
                    obj->addBehavior(b);
                }
                result->data.bool_val = 1;
                return 0;
            }
        }
        return 0;
    }

    // Helper: get the "grove_script" behavior on the current bot target
    Behavior* getBotScriptBehavior() {
        if (!m_groveBotTarget) return nullptr;
        for (auto& b : m_groveBotTarget->getBehaviors()) {
            if (b.name == "grove_script") return &b;
        }
        return nullptr;
    }

    // move_to(vec3) — queue MOVE_TO action
    static int32_t groveMoveTo(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        glm::vec3 target(
            static_cast<float>(args[0].data.vec3_val.x),
            static_cast<float>(args[0].data.vec3_val.y),
            static_cast<float>(args[0].data.vec3_val.z));

        float duration = (argc >= 2 && args[1].tag == GROVE_NUMBER) ? static_cast<float>(args[1].data.number_val) : 2.0f;

        Action a = Action::MoveTo(target, duration);
        // Optional animation param
        if (argc >= 3 && args[2].tag == GROVE_STRING) {
            auto& asv = args[2].data.string_val;
            a.animationParam = std::string(asv.ptr, asv.len);
        }
        b->actions.push_back(a);
        return 0;
    }

    // rotate_to(vec3) — queue ROTATE_TO action (euler degrees)
    static int32_t groveRotateTo(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        glm::vec3 target(
            static_cast<float>(args[0].data.vec3_val.x),
            static_cast<float>(args[0].data.vec3_val.y),
            static_cast<float>(args[0].data.vec3_val.z));

        float duration = (argc >= 2 && args[1].tag == GROVE_NUMBER) ? static_cast<float>(args[1].data.number_val) : 1.0f;

        b->actions.push_back(Action::RotateTo(target, duration));
        return 0;
    }

    // turn_to(vec3) — queue TURN_TO action (face a world position, yaw only)
    static int32_t groveTurnTo(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        glm::vec3 target(
            static_cast<float>(args[0].data.vec3_val.x),
            static_cast<float>(args[0].data.vec3_val.y),
            static_cast<float>(args[0].data.vec3_val.z));

        float duration = (argc >= 2 && args[1].tag == GROVE_NUMBER) ? static_cast<float>(args[1].data.number_val) : 0.5f;

        Action a;
        a.type = ActionType::TURN_TO;
        a.vec3Param = target;
        a.duration = duration;
        b->actions.push_back(a);
        return 0;
    }

    // wait(seconds) — queue WAIT action
    static int32_t groveWait(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_NUMBER) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        float seconds = static_cast<float>(args[0].data.number_val);
        b->actions.push_back(Action::Wait(seconds));
        return 0;
    }

    // set_visible(bool) — queue SET_VISIBLE action
    static int32_t groveSetVisible(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_BOOL) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        b->actions.push_back(Action::SetVisible(args[0].data.bool_val != 0));
        return 0;
    }

    // play_anim(name_string, duration?) — queue WAIT with animation param (plays anim for duration)
    static int32_t grovePlayAnim(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        float duration = (argc >= 2 && args[1].tag == GROVE_NUMBER) ? static_cast<float>(args[1].data.number_val) : 0.0f;

        Action a = Action::Wait(duration);
        auto& asv = args[0].data.string_val;
        a.animationParam = std::string(asv.ptr, asv.len);
        b->actions.push_back(a);
        return 0;
    }

    // send_signal(signal_name, target_entity?) — queue SEND_SIGNAL action
    static int32_t groveSendSignal(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        auto& ssv = args[0].data.string_val;
        std::string signalName(ssv.ptr, ssv.len);
        std::string targetEntity;
        if (argc >= 2 && args[1].tag == GROVE_STRING) {
            auto& tsv = args[1].data.string_val;
            targetEntity = std::string(tsv.ptr, tsv.len);
        }

        b->actions.push_back(Action::SendSignal(signalName, targetEntity));
        return 0;
    }

    // follow_path(path_name) — queue FOLLOW_PATH action
    static int32_t groveFollowPath(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget || argc < 1 || args[0].tag != GROVE_STRING) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        auto& fsv = args[0].data.string_val;
        b->actions.push_back(Action::FollowPath(std::string(fsv.ptr, fsv.len)));
        return 0;
    }

    // bot_loop(bool) — set whether the grove_script behavior loops
    static int32_t groveBotLoop(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        b->loop = (argc >= 1 && args[0].tag == GROVE_BOOL && args[0].data.bool_val != 0);
        return 0;
    }

    // bot_clear() — clear all queued actions on current target
    static int32_t groveBotClear(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NIL;
        if (!self->m_groveBotTarget) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (b) b->actions.clear();
        return 0;
    }

    // bot_run() — mark the grove_script behavior as ready and start it if in play mode
    static int32_t groveBotRun(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (!self->m_groveBotTarget) return 0;

        Behavior* b = self->getBotScriptBehavior();
        if (!b) return 0;

        // Set trigger to ON_GAMESTART so it runs when play mode starts
        b->trigger = TriggerType::ON_GAMESTART;
        b->enabled = true;
        result->data.bool_val = 1;

        // If already in play mode, start the behavior immediately
        if (self->m_isPlayMode && !b->actions.empty()) {
            auto& behaviors = self->m_groveBotTarget->getBehaviors();
            for (size_t i = 0; i < behaviors.size(); i++) {
                if (&behaviors[i] == b) {
                    self->m_groveBotTarget->setActiveBehaviorIndex(static_cast<int>(i));
                    self->m_groveBotTarget->setActiveActionIndex(0);
                    self->m_groveBotTarget->resetPathComplete();
                    self->m_groveBotTarget->clearPathWaypoints();

                    if (b->actions[0].type == ActionType::FOLLOW_PATH) {
                        self->loadPathForAction(self->m_groveBotTarget, b->actions[0]);
                    }
                    break;
                }
            }
        }
        return 0;
    }

    // ─── Player economy host functions ───

    // get_credits() → number
    static int32_t groveGetCredits(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NUMBER;
        result->data.number_val = static_cast<double>(self->m_playerCredits);
        return 0;
    }

    // add_credits(amount) → number (new balance)
    static int32_t groveAddCredits(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_NUMBER;
        if (argc >= 1 && args[0].tag == GROVE_NUMBER) {
            float amount = static_cast<float>(args[0].data.number_val);
            if (amount > 0) self->m_playerCredits += amount;
        }
        result->data.number_val = static_cast<double>(self->m_playerCredits);
        return 0;
    }

    // deduct_credits(amount) → bool (true if sufficient funds, false if not)
    static int32_t groveDeductCredits(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (argc >= 1 && args[0].tag == GROVE_NUMBER) {
            float amount = static_cast<float>(args[0].data.number_val);
            if (amount > 0 && self->m_playerCredits >= amount) {
                self->m_playerCredits -= amount;
                result->data.bool_val = 1;
            }
        }
        return 0;
    }

    // buy_plot(vec3) → bool (true if purchased, false if can't afford or already owned)
    static int32_t groveBuyPlot(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (!self->m_zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);

        // Check if plot is already owned
        uint32_t owner = self->m_zoneSystem->getOwner(x, z);
        if (owner != 0) return 0;  // Already owned

        // Check if zone allows building/purchasing
        ZoneType zt = self->m_zoneSystem->getZoneType(x, z);
        if (zt == ZoneType::Battlefield || zt == ZoneType::SpawnSafe) return 0;  // Can't buy these

        // Get price
        auto g = self->m_zoneSystem->worldToGrid(x, z);
        float price = self->m_zoneSystem->getPlotPrice(g.x, g.y);

        // Check funds
        if (self->m_playerCredits < price) return 0;  // Can't afford

        // Purchase!
        self->m_playerCredits -= price;
        self->m_zoneSystem->setOwner(g.x, g.y, 1);  // Player ID 1

        std::cout << "[Economy] Purchased plot (" << g.x << ", " << g.y << ") for "
                  << static_cast<int>(price) << " CR. Balance: "
                  << static_cast<int>(self->m_playerCredits) << " CR" << std::endl;

        // Spawn corner boundary posts
        self->spawnPlotPosts(g.x, g.y);

        result->data.bool_val = 1;
        return 0;
    }

    // sell_plot(vec3) → bool (true if sold, refunds 50% of current price)
    static int32_t groveSellPlot(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_BOOL;
        result->data.bool_val = 0;
        if (!self->m_zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) return 0;

        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);

        // Must own this plot
        uint32_t owner = self->m_zoneSystem->getOwner(x, z);
        if (owner != 1) return 0;  // Not our plot

        auto g = self->m_zoneSystem->worldToGrid(x, z);
        float price = self->m_zoneSystem->getPlotPrice(g.x, g.y);
        float refund = price * 0.5f;

        self->m_playerCredits += refund;
        self->m_zoneSystem->setOwner(g.x, g.y, 0);  // Unown

        std::cout << "[Economy] Sold plot (" << g.x << ", " << g.y << ") for "
                  << static_cast<int>(refund) << " CR. Balance: "
                  << static_cast<int>(self->m_playerCredits) << " CR" << std::endl;

        // Remove corner boundary posts
        self->removePlotPosts(g.x, g.y);

        result->data.bool_val = 1;
        return 0;
    }

    // plot_status(vec3) → string ("available", "owned", "spawn_zone", "battlefield", "too_expensive")
    static int32_t grovePlotStatus(const GroveValue* args, uint32_t argc, GroveValue* result, void* ud) {
        auto* self = static_cast<TerrainEditor*>(ud);
        result->tag = GROVE_STRING;

        static const char* s_available = "available";
        static const char* s_owned = "owned";
        static const char* s_spawn = "spawn_zone";
        static const char* s_battlefield = "battlefield";
        static const char* s_expensive = "too_expensive";
        static const char* s_unknown = "unknown";

        if (!self->m_zoneSystem || argc < 1 || args[0].tag != GROVE_VEC3) {
            result->data.string_val.ptr = s_unknown;
            result->data.string_val.len = 7;
            return 0;
        }

        float x = static_cast<float>(args[0].data.vec3_val.x);
        float z = static_cast<float>(args[0].data.vec3_val.z);

        ZoneType zt = self->m_zoneSystem->getZoneType(x, z);
        if (zt == ZoneType::SpawnSafe) {
            result->data.string_val.ptr = s_spawn;
            result->data.string_val.len = static_cast<uint32_t>(strlen(s_spawn));
            return 0;
        }
        if (zt == ZoneType::Battlefield) {
            result->data.string_val.ptr = s_battlefield;
            result->data.string_val.len = static_cast<uint32_t>(strlen(s_battlefield));
            return 0;
        }

        uint32_t owner = self->m_zoneSystem->getOwner(x, z);
        if (owner != 0) {
            result->data.string_val.ptr = s_owned;
            result->data.string_val.len = static_cast<uint32_t>(strlen(s_owned));
            return 0;
        }

        auto g = self->m_zoneSystem->worldToGrid(x, z);
        float price = self->m_zoneSystem->getPlotPrice(g.x, g.y);
        if (self->m_playerCredits < price) {
            result->data.string_val.ptr = s_expensive;
            result->data.string_val.len = static_cast<uint32_t>(strlen(s_expensive));
            return 0;
        }

        result->data.string_val.ptr = s_available;
        result->data.string_val.len = static_cast<uint32_t>(strlen(s_available));
        return 0;
    }

    void initGroveVM() {
        m_groveVm = grove_new();
        if (!m_groveVm) {
            std::cout << "Failed to create Grove VM\n";
            return;
        }
        grove_set_instruction_limit(m_groveVm, 1000000);

        // Register host functions
        grove_register_fn(m_groveVm, "log", groveLogFn, &m_groveOutputAccum);
        grove_register_fn(m_groveVm, "terrain_height", groveTerrainHeightFn, this);
        grove_register_fn(m_groveVm, "spawn", groveSpawnFn, this);

        // Construction primitives
        grove_register_fn(m_groveVm, "get_player_pos", groveGetPlayerPos, this);
        grove_register_fn(m_groveVm, "spawn_cube", groveSpawnCubeFn, this);
        grove_register_fn(m_groveVm, "spawn_cylinder", groveSpawnCylinderFn, this);
        grove_register_fn(m_groveVm, "spawn_model", groveSpawnModelFn, this);
        grove_register_fn(m_groveVm, "set_object_rotation", groveSetObjectRotation, this);
        grove_register_fn(m_groveVm, "set_object_scale", groveSetObjectScale, this);
        grove_register_fn(m_groveVm, "delete_object", groveDeleteObject, this);

        // Queued construction commands (for behavior sequences)
        grove_register_fn(m_groveVm, "queue_spawn_cube", groveQueueSpawnCube, this);
        grove_register_fn(m_groveVm, "queue_spawn_cylinder", groveQueueSpawnCylinder, this);
        grove_register_fn(m_groveVm, "queue_set_rotation", groveQueueSetRotation, this);
        grove_register_fn(m_groveVm, "queue_set_scale", groveQueueSetScale, this);
        grove_register_fn(m_groveVm, "queue_delete", groveQueueDelete, this);

        grove_register_fn(m_groveVm, "zone_type", groveZoneTypeFn, this);
        grove_register_fn(m_groveVm, "zone_resource", groveZoneResourceFn, this);
        grove_register_fn(m_groveVm, "zone_owner", groveZoneOwnerFn, this);
        grove_register_fn(m_groveVm, "can_build", groveCanBuildFn, this);
        grove_register_fn(m_groveVm, "plot_price", grovePlotPriceFn, this);

        // AlgoBot behavior functions
        grove_register_fn(m_groveVm, "bot_target", groveBotTargetFn, this);
        grove_register_fn(m_groveVm, "move_to", groveMoveTo, this);
        grove_register_fn(m_groveVm, "rotate_to", groveRotateTo, this);
        grove_register_fn(m_groveVm, "turn_to", groveTurnTo, this);
        grove_register_fn(m_groveVm, "wait", groveWait, this);
        grove_register_fn(m_groveVm, "set_visible", groveSetVisible, this);
        grove_register_fn(m_groveVm, "play_anim", grovePlayAnim, this);
        grove_register_fn(m_groveVm, "send_signal", groveSendSignal, this);
        grove_register_fn(m_groveVm, "follow_path", groveFollowPath, this);
        grove_register_fn(m_groveVm, "bot_loop", groveBotLoop, this);
        grove_register_fn(m_groveVm, "bot_clear", groveBotClear, this);
        grove_register_fn(m_groveVm, "bot_run", groveBotRun, this);

        // Player economy functions
        grove_register_fn(m_groveVm, "get_credits", groveGetCredits, this);
        grove_register_fn(m_groveVm, "add_credits", groveAddCredits, this);
        grove_register_fn(m_groveVm, "deduct_credits", groveDeductCredits, this);
        grove_register_fn(m_groveVm, "buy_plot", groveBuyPlot, this);
        grove_register_fn(m_groveVm, "sell_plot", groveSellPlot, this);
        grove_register_fn(m_groveVm, "plot_status", grovePlotStatus, this);

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
            // Check for right-click to toggle cursor
            static bool wasRightClickDown = false;
            bool rightClickDown = Input::isMouseButtonDown(Input::MOUSE_RIGHT);
            if (rightClickDown && !wasRightClickDown) {
                m_playModeCursorVisible = !m_playModeCursorVisible;
                Input::setMouseCaptured(!m_playModeCursorVisible);
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
            bool wantLook = Input::isMouseButtonDown(Input::MOUSE_RIGHT) && !ImGui::GetIO().WantCaptureMouse;

            if (wantLook) {
                glm::vec2 mouseDelta = Input::getMouseDelta();
                m_camera.processMouse(mouseDelta.x, -mouseDelta.y);

                if (!m_isLooking) {
                    m_isLooking = true;
                    Input::setMouseCaptured(true);
                }
            } else if (m_isLooking) {
                m_isLooking = false;
                Input::setMouseCaptured(false);
            }
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
            const float playerRadius = 0.3f;
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

        // Handle space key for jump/fly toggle (editor mode only - Jolt handles jumping in play mode)
        if (!m_isPlayMode && !imguiWantsKeyboard && Input::isKeyPressed(Input::KEY_SPACE)) {
            m_camera.onSpacePressed(groundHeight);
        }

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
            const float playerRadius = 0.3f;
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

        // Use character controller for play mode walk
        bool useCharacterController = m_isPlayMode && m_characterController &&
                                m_camera.getMovementMode() == MovementMode::Walk;

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
            const float characterHeight = 1.8f;  // Full character height
            const float halfHeight = characterHeight * 0.5f;  // Center is 0.9m above feet
            const float eyeHeight = 1.7f;        // Eye level from ground
            const float centerToEye = eyeHeight - halfHeight;  // 0.8m from center to eyes

            float feetY = charPos.y - halfHeight;
            if (feetY < terrainHeight) {
                charPos.y = terrainHeight + halfHeight;
                m_characterController->setPosition(charPos);
            }

            // Camera positioning based on camera mode
            auto cameraMode = m_editorUI.getCameraMode();
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
            // Camera handles movement (fly mode or editor mode)
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

        // Post-movement AABB collision for play mode walk (fallback for non-Jolt objects)
        if (m_isPlayMode && m_camera.getMovementMode() == MovementMode::Walk && !useCharacterController) {
            glm::vec3 newPos = m_camera.getPosition();
            const float playerRadius = 0.5f;
            const float playerHeight = 1.7f;

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

        // Engine hum when flying (play mode only)
        MovementMode currentMode = m_camera.getMovementMode();
        if (currentMode != m_lastMovementMode) {
            if (currentMode == MovementMode::Fly) {
                // Only play engine hum in play mode
                if (m_isPlayMode && m_engineHumLoopId < 0) {
                    m_engineHumLoopId = Audio::getInstance().startLoop("sounds/enginehum.wav", 0.5f);
                }
            } else if (currentMode == MovementMode::Walk) {
                if (m_engineHumLoopId >= 0) {
                    Audio::getInstance().stopLoop(m_engineHumLoopId);
                    m_engineHumLoopId = -1;
                }
            }
            m_lastMovementMode = currentMode;
        }

        if (m_terrain.getConfig().wrapWorld) {
            glm::vec3 wrapped = m_terrain.wrapWorldPosition(m_camera.getPosition());
            if (wrapped != m_camera.getPosition()) {
                m_camera.setPosition(wrapped);
            }
        }
    }

    void handleKeyboardShortcuts(float deltaTime) {
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

        // Ctrl+D - duplicate selected object
        static bool wasDKeyDown = false;
        bool dKeyDown = Input::isKeyDown(Input::KEY_D);
        if (ctrlDown && dKeyDown && !wasDKeyDown && !ImGui::GetIO().WantTextInput && !m_isPlayMode) {
            if (m_selectedObjectIndex >= 0) {
                duplicateObject(m_selectedObjectIndex);
            }
        }
        wasDKeyDown = dKeyDown;

        static bool wasFKeyDown = false;
        bool fKeyDown = Input::isKeyDown(Input::KEY_F);
        if (!m_isPlayMode && fKeyDown && !wasFKeyDown && !ImGui::GetIO().WantTextInput) {
            focusOnSelectedObject();
        }
        wasFKeyDown = fKeyDown;

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
        
        // Ensure m_currentInteractObject stays set for active compound/blueprint actions
        if (m_aiActionActive) {
            if (m_compoundActionActive && m_compoundNPC) {
                m_currentInteractObject = m_compoundNPC;
            } else if (m_blueprintActive && m_blueprintNPC) {
                m_currentInteractObject = m_blueprintNPC;
            }
        }
        // Update AI motor control actions (look_around, turn_to, etc.)
        updateAIAction(deltaTime);
        updateCompoundAction();   // Advance compound action state machine (build_post etc.)
        updateBlueprintAction();  // Advance blueprint state machine (build_frame etc.)

        // Update player avatar position to track camera
        updatePlayerAvatar();

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

        // Shooting - left mouse click to shoot (weapon type selected by 1-4 keys)
        m_shootCooldown -= deltaTime;
        bool leftClickDown = Input::isMouseButtonDown(Input::MOUSE_LEFT) && !ImGui::GetIO().WantCaptureMouse;
        if (leftClickDown && m_shootCooldown <= 0.0f && !m_inConversation) {
            shootProjectile();
            Audio::getInstance().playSound("sounds/tir.mp3", 0.15f);
            m_shootCooldown = 0.2f;  // Fire rate limit
        }

        // Update projectiles
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
            m_sceneObjects.push_back(std::move(obj));
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
            obj->getTransform().setPosition(glm::vec3(pos.x, terrainY + height * 0.5f, pos.z));
            m_sceneObjects.push_back(std::move(obj));
            std::cout << "[Grove CMD] Spawned cylinder '" << name << "'" << std::endl;
        }
        else if (type == "set_rotation" && parts.size() >= 5) {
            // set_rotation|name|rx|ry|rz
            std::string name = parts[1];
            float rx = std::stof(parts[2]);
            float ry = std::stof(parts[3]);
            float rz = std::stof(parts[4]);
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == name) {
                    o->setEulerRotation(glm::vec3(rx, ry, rz));
                    std::cout << "[Grove CMD] Set rotation on '" << name << "'" << std::endl;
                    break;
                }
            }
        }
        else if (type == "set_scale" && parts.size() >= 5) {
            // set_scale|name|sx|sy|sz
            std::string name = parts[1];
            float sx = std::stof(parts[2]);
            float sy = std::stof(parts[3]);
            float sz = std::stof(parts[4]);
            for (auto& o : m_sceneObjects) {
                if (o && o->getName() == name) {
                    o->getTransform().setScale(glm::vec3(sx, sy, sz));
                    std::cout << "[Grove CMD] Set scale on '" << name << "'" << std::endl;
                    break;
                }
            }
        }
        else if (type == "delete" && parts.size() >= 2) {
            // delete|name
            std::string name = parts[1];
            for (auto it = m_sceneObjects.begin(); it != m_sceneObjects.end(); ++it) {
                if (*it && (*it)->getName() == name) {
                    m_sceneObjects.erase(it);
                    std::cout << "[Grove CMD] Deleted '" << name << "'" << std::endl;
                    break;
                }
            }
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
            if (behavior.loop) {
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

            glm::vec2 mousePos = Input::getMousePosition();
            float normalizedX = mousePos.x / getWindow().getWidth();
            float normalizedY = mousePos.y / getWindow().getHeight();
            float aspect = static_cast<float>(getWindow().getWidth()) / getWindow().getHeight();

            m_brushTool->updatePreview(normalizedX, normalizedY, aspect);

            // Update brush shape from UI
            m_brushTool->setShape(m_editorUI.getBrushShape());
            m_brushTool->setShapeAspectRatio(m_editorUI.getBrushShapeAspectRatio());
            m_brushTool->setShapeRotation(m_editorUI.getBrushShapeRotation());

            // Update brush ring visualization (hidden in space levels - no terrain)
            if (m_brushTool->hasValidPosition() && !m_isSpaceLevel) {
                m_brushRing->update(m_brushTool->getPosition(), m_editorUI.getBrushRadius(),
                                   m_terrain, m_brushTool->getShapeParams());
                m_brushRing->setVisible(true);
            } else {
                m_brushRing->setVisible(false);
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

        bool inMoveObjectMode = m_editorUI.getBrushMode() == BrushMode::MoveObject;

        if (inMoveObjectMode && m_selectedObjectIndex >= 0 && m_selectedObjectIndex < static_cast<int>(m_sceneObjects.size())) {
            SceneObject* selected = m_sceneObjects[m_selectedObjectIndex].get();
            if (selected) {
                m_gizmo.setPosition(selected->getTransform().getPosition());
                m_gizmo.setVisible(true);

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

                bool leftMousePressed = Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !ImGui::GetIO().WantCaptureMouse;

                if (leftMousePressed && !m_gizmoDragging) {
                    m_gizmo.updateHover(rayOrigin, rayDir);
                    if (m_gizmo.beginDrag(rayOrigin, rayDir)) {
                        m_gizmoDragging = true;
                    } else {
                        pickObjectAtMouse();
                    }
                } else if (m_gizmoDragging && Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
                    glm::vec3 delta = m_gizmo.updateDrag(rayOrigin, rayDir);
                    selected->getTransform().setPosition(selected->getTransform().getPosition() + delta);
                    m_gizmo.setPosition(selected->getTransform().getPosition());
                } else if (m_gizmoDragging && !Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
                    m_gizmoDragging = false;
                    m_gizmo.endDrag();
                }

                if (!m_gizmoDragging && leftMousePressed) {
                    m_gizmo.updateHover(rayOrigin, rayDir);
                    if (m_gizmo.getActiveAxis() == GizmoAxis::None) {
                        pickObjectAtMouse();
                    }
                }
            }
        } else if (inMoveObjectMode) {
            m_gizmo.setVisible(false);

            bool leftMousePressed = Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !ImGui::GetIO().WantCaptureMouse;
            if (leftMousePressed) {
                pickObjectAtMouse();
            }
        } else {
            m_gizmo.setVisible(false);
            m_gizmoDragging = false;
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

        // Send to AI backend
        if (m_httpClient && m_httpClient->isConnected()) {
            m_waitingForAIResponse = true;
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

                                // Check for and execute AI action
                                if (json.contains("action") && !json["action"].is_null()) {
                                    executeAIAction(json["action"]);
                                }
                            } catch (...) {
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
        } else if (sender == "System") {
            return ImVec4(1.0f, 1.0f, 0.4f, alpha);
        } else {
            return ImVec4(0.4f, 0.9f, 1.0f, alpha);
        }
    }

    // Render the chat log overlay (bottom-left, Minecraft style)
    void renderChatLog() {
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
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "/eve | /xenk | /robot <msg> — Enter to send, Esc to cancel");
        }
        ImGui::End();
    }

    void sendQuickChatMessage() {
        std::string message = m_quickChatBuffer;
        glm::vec3 playerPos = m_camera.getPosition();

        // Parse command prefix: /eve or /xenk to target a specific NPC
        BeingType targetType = BeingType::STATIC;  // STATIC = no specific target
        bool hasTargetPrefix = false;

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
            // Proximity-based search (existing behavior)
            const float quickChatRadius = 100.0f;
            float closestDist = quickChatRadius;

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
                closestSentient->getBeingType() == BeingType::ROBOT) {
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

    void renderPlayModeUI() {
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

        // Credits + Game time display (upper right corner)
        std::string timeStr = formatGameTimeDisplay(m_gameTimeMinutes);
        char creditsStr[64];
        snprintf(creditsStr, sizeof(creditsStr), "%d CR", static_cast<int>(m_playerCredits));
        ImVec2 timeSize = ImGui::CalcTextSize(timeStr.c_str());
        ImVec2 creditsSize = ImGui::CalcTextSize(creditsStr);
        float hudWindowWidth = creditsSize.x + 20.0f + timeSize.x + 20.0f;
        ImGui::SetNextWindowPos(ImVec2(getWindow().getWidth() - hudWindowWidth - 10, 10));
        ImGui::SetNextWindowBgAlpha(0.5f);
        if (ImGui::Begin("##GameHUD", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", creditsStr);
            ImGui::SameLine(0, 20.0f);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "%s", timeStr.c_str());
        }
        ImGui::End();

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        float cx = getWindow().getWidth() * 0.5f;
        float cy = getWindow().getHeight() * 0.5f;
        float size = 10.0f;
        float thickness = 2.0f;
        ImU32 color = IM_COL32(255, 255, 255, 200);

        drawList->AddLine(ImVec2(cx - size, cy), ImVec2(cx + size, cy), color, thickness);
        drawList->AddLine(ImVec2(cx, cy - size), ImVec2(cx, cy + size), color, thickness);

        // Player health bar - HIDDEN for now (was overlapping chat text)
        // TODO: re-enable when UI layout is finalized

        // Render trading UI panels
        renderTradingUI();

        // Render game module UI
        if (m_gameModule) {
            float width = static_cast<float>(getWindow().getWidth());
            float height = static_cast<float>(getWindow().getHeight());
            m_gameModule->renderUI(width, height);
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
                        // Tint by resource type
                        switch (cell->resource) {
                            case ResourceType::Wood:      color = IM_COL32(60, 140, 40, 255); break;
                            case ResourceType::Limestone: color = IM_COL32(180, 170, 140, 255); break;
                            case ResourceType::Iron:      color = IM_COL32(140, 100, 80, 255); break;
                            case ResourceType::Oil:       color = IM_COL32(40, 40, 40, 255); break;
                            default:                      color = IM_COL32(150, 40, 180, 255); break;
                        }
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
                    const char* label = "";
                    switch (cell->resource) {
                        case ResourceType::Wood:      label = "W"; break;
                        case ResourceType::Limestone: label = "L"; break;
                        case ResourceType::Iron:      label = "Fe"; break;
                        case ResourceType::Oil:       label = "O"; break;
                        default: break;
                    }
                    ImVec2 textSize = ImGui::CalcTextSize(label);
                    drawList->AddText(ImVec2(x0 + (cellPx - textSize.x) * 0.5f,
                                             y0 + (cellPx - textSize.y) * 0.5f),
                                      IM_COL32(255, 255, 255, 200), label);
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
                    if (cell->resource != ResourceType::None)
                        ImGui::Text("Resource: %s (%.0f%%)",
                                    ZoneSystem::resourceTypeName(cell->resource),
                                    cell->resourceDensity * 100.0f);
                    if (cell->ownerPlayerId != 0)
                        ImGui::Text("Owner: Player %u", cell->ownerPlayerId);
                    ImGui::Text("Price: $%.0f", cell->purchasePrice);
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
            // Append zone data to the saved JSON file
            if (m_zoneSystem) {
                try {
                    std::ifstream inFile(filepath);
                    nlohmann::json root = nlohmann::json::parse(inFile);
                    inFile.close();
                    m_zoneSystem->save(root);
                    std::ofstream outFile(filepath);
                    outFile << root.dump(2);
                    outFile.close();
                } catch (const std::exception& e) {
                    std::cerr << "Failed to save zone data: " << e.what() << std::endl;
                }
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

        // Load zone data
        if (m_zoneSystem && levelData.zoneData.hasData) {
            // Re-read the raw JSON to pass to ZoneSystem::load
            try {
                std::ifstream zfile(filepath);
                nlohmann::json zroot = nlohmann::json::parse(zfile);
                zfile.close();
                m_zoneSystem->load(zroot);
            } catch (const std::exception& e) {
                std::cerr << "Failed to load zone data: " << e.what() << std::endl;
            }
        }

        m_currentLevelPath = filepath;
        std::cout << "Level loaded from: " << filepath << std::endl;

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

    void enterPlayMode() {
        m_isPlayMode = true;
        m_playModeCursorVisible = false;  // Start with cursor hidden (mouse look active)
        m_playModeDebug = false;          // Debug visuals off by default
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
                    // Use 513 samples for good balance of accuracy and speed
                    const int sampleCount = 513;
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

                if (!targetLevel.empty()) {
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

        glm::vec3 camPos = objPos - m_camera.getFront() * distance;
        m_camera.setPosition(camPos);

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

        std::string baseName = "PlotPost_" + std::to_string(gridX) + "_" + std::to_string(gridZ);

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
        else if (actionType == "build_post") {
            // Compound action: scan → walk to item → pickup → scan → walk to target → place
            std::cout << "[AI Action] ENTERED build_post handler" << std::endl;
            std::string itemName = action.value("item", "");
            std::string targetName = action.value("target", "");
            if (itemName.empty() || targetName.empty()) {
                std::cout << "[AI Action] build_post: need both 'item' and 'target'" << std::endl;
            } else if (m_currentInteractObject->isCarrying()) {
                std::cout << "[AI Action] build_post: already carrying " << m_currentInteractObject->getCarriedItemName() << std::endl;
            } else if (m_compoundActionActive) {
                std::cout << "[AI Action] build_post: compound action already in progress" << std::endl;
            } else {
                m_compoundActionActive = true;
                m_compoundStep = 0;
                m_compoundItemName = itemName;
                m_compoundTargetName = targetName;
                m_compoundItemObj = nullptr;
                m_compoundTargetObj = nullptr;
                m_compoundNPC = m_currentInteractObject;  // Persist NPC ref beyond chat lifetime

                // Kick off step 0: start 360° scan to find the item
                m_aiActionActive = true;
                m_aiActionType = "look_around";
                m_aiActionDuration = 2.0f;
                m_aiActionTimer = 0.0f;
                m_aiActionStartYaw = m_currentInteractObject->getEulerRotation().y;
                std::cout << "[AI Compound] build_post started: item='" << itemName << "' target='" << targetName << "'" << std::endl;
            }
        }
        else if (actionType == "build_frame") {
            // Blueprint action: autonomously build a 4-post frame building
            if (m_blueprintActive) {
                std::cout << "[AI Action] build_frame: blueprint already in progress" << std::endl;
            } else if (m_compoundActionActive) {
                std::cout << "[AI Action] build_frame: compound action already in progress" << std::endl;
            } else {
                glm::vec3 npcPos = m_currentInteractObject->getTransform().getPosition();
                m_blueprintNPC = m_currentInteractObject;

                // 4m x 4m frame centered 6m in front of the NPC, axis-aligned to world grid
                float yawRad = glm::radians(m_currentInteractObject->getEulerRotation().y);
                glm::vec3 forward(sin(yawRad), 0.0f, cos(yawRad));
                glm::vec3 center = npcPos + forward * 6.0f;
                float halfSize = 2.0f;

                // World-axis-aligned corners — sample terrain height at each corner
                // so postholes sit on top of the terrain surface
                glm::vec3 corners[4] = {
                    {center.x - halfSize, m_terrain.getHeightAt(center.x - halfSize, center.z - halfSize), center.z - halfSize},  // SW
                    {center.x + halfSize, m_terrain.getHeightAt(center.x + halfSize, center.z - halfSize), center.z - halfSize},  // SE
                    {center.x + halfSize, m_terrain.getHeightAt(center.x + halfSize, center.z + halfSize), center.z + halfSize},  // NE
                    {center.x - halfSize, m_terrain.getHeightAt(center.x - halfSize, center.z + halfSize), center.z + halfSize},  // NW
                };
                m_blueprintGroundY = m_terrain.getHeightAt(center.x, center.z);

                // Pre-generate unique posthole names
                int maxNum = 0;
                for (const auto& obj : m_sceneObjects) {
                    const std::string& name = obj->getName();
                    if (name.size() > 9 && name.substr(0, 9) == "posthole_") {
                        std::string suffix = name.substr(9);
                        bool allDigits = true;
                        for (char c : suffix) { if (!std::isdigit(c)) { allDigits = false; break; } }
                        if (allDigits && !suffix.empty()) {
                            int n = std::stoi(suffix);
                            if (n > maxNum) maxNum = n;
                        }
                    }
                }

                m_blueprintSteps.clear();
                m_blueprintCurrentStep = 0;
                m_blueprintSubStep = 0;
                m_blueprintUsedTimbers.clear();

                // Find an existing posthole to use as template (clone its model/texture/scale)
                m_blueprintPostholeTemplate = nullptr;
                for (auto& obj : m_sceneObjects) {
                    if (!obj) continue;
                    std::string lowerName = obj->getName();
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                    if (lowerName.find("posthole") != std::string::npos) {
                        m_blueprintPostholeTemplate = obj.get();
                        std::cout << "[AI Blueprint] Using '" << obj->getName()
                                  << "' as posthole template (model=" << obj->getModelPath() << ")" << std::endl;
                        break;
                    }
                }

                // Phase 1: Spawn 4 postholes at corners (Y already set to terrain height)
                for (int i = 0; i < 4; i++) {
                    BlueprintStep step;
                    step.type = BlueprintStep::SPAWN_POSTHOLE;
                    step.position = corners[i];
                    step.targetName = "posthole_" + std::to_string(maxNum + 1 + i);
                    m_blueprintSteps.push_back(step);
                }

                // Phase 2: Build post in each posthole (find nearest timber automatically)
                for (int i = 0; i < 4; i++) {
                    BlueprintStep step;
                    step.type = BlueprintStep::BUILD_POST;
                    step.targetName = "posthole_" + std::to_string(maxNum + 1 + i);
                    m_blueprintSteps.push_back(step);
                }

                // Phase 3: Place 4 horizontal top rails connecting adjacent corners
                for (int i = 0; i < 4; i++) {
                    BlueprintStep step;
                    step.type = BlueprintStep::BUILD_BEAM;
                    step.beamStart = corners[i];
                    step.beamEnd = corners[(i + 1) % 4];  // SW→SE, SE→NE, NE→NW, NW→SW
                    m_blueprintSteps.push_back(step);
                }

                // Find sidewall template
                m_blueprintSidewallTemplate = nullptr;
                for (auto& obj : m_sceneObjects) {
                    if (!obj) continue;
                    std::string lowerName = obj->getName();
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                    if (lowerName.find("sidewall") != std::string::npos) {
                        m_blueprintSidewallTemplate = obj.get();
                        std::cout << "[AI Blueprint] Using '" << obj->getName()
                                  << "' as sidewall template (model=" << obj->getModelPath() << ")" << std::endl;
                        break;
                    }
                }

                // Phase 4: Place sidewalls on left and right sides of the frame
                if (m_blueprintSidewallTemplate) {
                    float npcYawRad = glm::radians(m_currentInteractObject->getEulerRotation().y);
                    glm::vec3 frontDir(sin(npcYawRad), 0.0f, cos(npcYawRad));
                    glm::vec3 rightDir(cos(npcYawRad), 0.0f, -sin(npcYawRad));

                    // Classify each edge — side walls have outward normals aligned with left/right
                    for (int i = 0; i < 4; i++) {
                        glm::vec3 edgeMid = (corners[i] + corners[(i + 1) % 4]) * 0.5f;
                        glm::vec3 outward = glm::normalize(glm::vec3(edgeMid.x - center.x, 0.0f, edgeMid.z - center.z));
                        float dotRight = glm::dot(outward, rightDir);
                        float dotFront = glm::dot(outward, frontDir);

                        if (std::abs(dotRight) > std::abs(dotFront)) {
                            BlueprintStep step;
                            step.type = BlueprintStep::SPAWN_SIDEWALL;
                            step.wallStart = corners[i];
                            step.wallEnd = corners[(i + 1) % 4];
                            step.wallYaw = glm::degrees(std::atan2(outward.x, outward.z));
                            m_blueprintSteps.push_back(step);
                        }
                    }
                }

                // Find roof template
                m_blueprintRoofTemplate = nullptr;
                for (auto& obj : m_sceneObjects) {
                    if (!obj) continue;
                    std::string lowerName = obj->getName();
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                    if (lowerName.find("roof") != std::string::npos) {
                        m_blueprintRoofTemplate = obj.get();
                        std::cout << "[AI Blueprint] Using '" << obj->getName()
                                  << "' as roof template (model=" << obj->getModelPath() << ")" << std::endl;
                        break;
                    }
                }

                // Phase 5: Place roof on top of the frame
                if (m_blueprintRoofTemplate) {
                    BlueprintStep step;
                    step.type = BlueprintStep::SPAWN_ROOF;
                    step.roofCenter = center;
                    for (int i = 0; i < 4; i++) step.roofCorners[i] = corners[i];
                    // Front is the direction the NPC was facing when building
                    step.roofFrontYaw = m_currentInteractObject->getEulerRotation().y;
                    m_blueprintSteps.push_back(step);
                }

                m_blueprintActive = true;
                std::cout << "[AI Blueprint] build_frame started: 4x4 frame at ("
                          << center.x << ", " << center.z << ") with "
                          << m_blueprintSteps.size() << " steps" << std::endl;
            }
        }
        else if (actionType == "run_script") {
            // Execute a Grove script directly (for economy, zone queries, etc.)
            std::string script = action.value("script", "");
            if (script.empty()) {
                std::cout << "[AI Action] run_script: no script provided" << std::endl;
            } else {
                std::cout << "[AI Action] run_script: executing " << script.size() << " bytes" << std::endl;
                m_groveOutputAccum.clear();
                int32_t ret = grove_eval(m_groveVm, script.c_str());
                if (ret != 0) {
                    const char* err = grove_last_error(m_groveVm);
                    int line = static_cast<int>(grove_last_error_line(m_groveVm));
                    std::cout << "[AI Action] Grove error (line " << line << "): "
                              << (err ? err : "unknown") << std::endl;
                } else if (!m_groveOutputAccum.empty()) {
                    // Show Grove output in chat as a system message
                    std::cout << "[Grove] " << m_groveOutputAccum;
                    // Add to conversation so player can see the result
                    std::string output = m_groveOutputAccum;
                    // Trim trailing newline
                    while (!output.empty() && output.back() == '\n') output.pop_back();
                    addChatMessage(m_currentInteractObject ? m_currentInteractObject->getName() : "System", output);
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
                        std::cout << "[AI Action] Grove script error (line " << line << "): "
                                  << (err ? err : "unknown") << std::endl;
                    } else {
                        if (!m_groveOutputAccum.empty()) {
                            std::cout << "[AI Action] Grove output: " << m_groveOutputAccum;
                        }

                        // If we're in play mode, immediately start the behavior
                        if (m_isPlayMode && targetBot->hasBehaviors()) {
                            auto& behaviors = targetBot->getBehaviors();
                            for (size_t i = 0; i < behaviors.size(); i++) {
                                if (behaviors[i].name == "grove_script" && !behaviors[i].actions.empty()) {
                                    targetBot->setActiveBehaviorIndex(static_cast<int>(i));
                                    targetBot->setActiveActionIndex(0);
                                    targetBot->resetPathComplete();
                                    targetBot->clearPathWaypoints();

                                    // If first action is FOLLOW_PATH, load it
                                    if (behaviors[i].actions[0].type == ActionType::FOLLOW_PATH) {
                                        loadPathForAction(targetBot, behaviors[i].actions[0]);
                                    }

                                    std::cout << "[AI Action] AlgoBot '" << targetName
                                              << "' program started (" << behaviors[i].actions.size()
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
            // Cancel any active compound action for this NPC
            if (m_compoundActionActive && m_compoundNPC == m_currentInteractObject) {
                std::cout << "[AI Compound] Cancelled by stop action" << std::endl;
                m_compoundActionActive = false;
                m_compoundNPC = nullptr;
            }
            // Cancel any active blueprint for this NPC
            if (m_blueprintActive && m_blueprintNPC == m_currentInteractObject) {
                std::cout << "[AI Blueprint] Cancelled by stop action" << std::endl;
                m_blueprintActive = false;
                m_blueprintNPC = nullptr;
                m_blueprintSteps.clear();
                m_blueprintUsedTimbers.clear();
            }
            std::cout << "[AI Action] Stopped for " << m_currentInteractObject->getName()
                      << " (remaining followers: " << m_aiFollowers.size() << ")" << std::endl;
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
     * Update compound action state machine (build_post etc.)
     * Called each frame after updateAIAction(). Waits for each sub-action to complete,
     * then advances to the next step.
     */
    void updateCompoundAction() {
        if (!m_compoundActionActive || !m_compoundNPC) return;

        // Wait for current sub-action to finish
        if (m_aiActionActive) return;

        std::cout << "[AI Compound] Advancing step " << m_compoundStep
                  << " (interactObj=" << (m_currentInteractObject ? m_currentInteractObject->getName() : "NULL")
                  << ", compoundNPC=" << m_compoundNPC->getName() << ")" << std::endl;

        switch (m_compoundStep) {
            case 0: {
                // SCAN_ITEM just completed — find the item
                SceneObject* foundItem = m_compoundItemObj;  // May be pre-set by blueprint
                if (!foundItem) {
                    // Normal path: search scan results for item by name
                    std::cout << "[AI Compound] Scan found " << m_lastFullScanResult.visibleObjects.size() << " objects, looking for '" << m_compoundItemName << "'" << std::endl;
                    for (const auto& vis : m_lastFullScanResult.visibleObjects) {
                        if (vis.name == m_compoundItemName) {
                            for (auto& obj : m_sceneObjects) {
                                if (obj && obj->getName() == m_compoundItemName && obj->isVisible()) {
                                    foundItem = obj.get();
                                    break;
                                }
                            }
                            break;
                        }
                    }
                } else {
                    std::cout << "[AI Compound] Using pre-set item '" << m_compoundItemName << "'" << std::endl;
                }
                if (!foundItem) {
                    std::cout << "[AI Compound] ABORT: item '" << m_compoundItemName << "' not found in scan" << std::endl;
                    m_compoundActionActive = false;
                    return;
                }
                m_compoundItemObj = foundItem;

                // Start move_to toward item
                glm::vec3 itemPos = foundItem->getTransform().getPosition();
                m_aiActionStartPos = m_compoundNPC->getTransform().getPosition();
                m_aiActionTargetPos = itemPos;
                m_aiActionTargetPos.y = m_aiActionStartPos.y;
                m_aiActionSpeed = 5.0f;
                float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                m_aiActionDuration = distance / m_aiActionSpeed;

                if (m_aiActionDuration > 0.01f) {
                    m_currentInteractObject = m_compoundNPC;  // Ensure set for updateAIAction
                    m_aiActionActive = true;
                    m_aiActionType = "move_to";
                    m_aiActionTimer = 0.0f;
                    glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                    m_aiActionStartYaw = m_compoundNPC->getEulerRotation().y;
                    std::cout << "[AI Compound] Step 1: Moving to item '" << m_compoundItemName << "'" << std::endl;
                }
                m_compoundStep = 1;
                break;
            }
            case 1: {
                // GOTO_ITEM complete — pick up the item
                if (m_compoundItemObj && !m_compoundNPC->isCarrying()) {
                    m_compoundItemObj->setVisible(false);
                    m_compoundNPC->setCarriedItem(m_compoundItemName, m_compoundItemObj);
                    std::cout << "[AI Compound] Step 2: Picked up '" << m_compoundItemName << "'" << std::endl;
                } else {
                    std::cout << "[AI Compound] ABORT: couldn't pick up item" << std::endl;
                    m_compoundActionActive = false;
                    return;
                }
                m_compoundStep = 2;
                break;
            }
            case 2: {
                // PICKUP done — start 360° scan to find target
                m_currentInteractObject = m_compoundNPC;
                m_aiActionActive = true;
                m_aiActionType = "look_around";
                m_aiActionDuration = 2.0f;
                m_aiActionTimer = 0.0f;
                m_aiActionStartYaw = m_compoundNPC->getEulerRotation().y;
                std::cout << "[AI Compound] Step 3: Scanning for target '" << m_compoundTargetName << "'" << std::endl;
                m_compoundStep = 3;
                break;
            }
            case 3: {
                // SCAN_TARGET complete — search scan results for target
                SceneObject* foundTarget = nullptr;
                for (const auto& vis : m_lastFullScanResult.visibleObjects) {
                    if (vis.name == m_compoundTargetName) {
                        for (auto& obj : m_sceneObjects) {
                            if (obj && obj->getName() == m_compoundTargetName && obj->isVisible()) {
                                foundTarget = obj.get();
                                break;
                            }
                        }
                        break;
                    }
                }
                if (!foundTarget) {
                    std::cout << "[AI Compound] ABORT: target '" << m_compoundTargetName << "' not found in scan — dropping item" << std::endl;
                    // Drop the carried item so it's not lost
                    SceneObject* carried = m_compoundNPC->getCarriedItemObject();
                    if (carried) {
                        glm::vec3 dropPos = m_compoundNPC->getTransform().getPosition();
                        float yawRad = glm::radians(m_compoundNPC->getEulerRotation().y);
                        glm::vec3 forward(sin(yawRad), 0.0f, cos(yawRad));
                        dropPos += forward * 1.5f;
                        dropPos.y = carried->getTransform().getPosition().y;
                        carried->getTransform().setPosition(dropPos);
                        carried->setVisible(true);
                    }
                    m_compoundNPC->clearCarriedItem();
                    m_compoundActionActive = false;
                    return;
                }
                m_compoundTargetObj = foundTarget;

                // Start move_to toward target — stop 2m short so NPC doesn't clip into it
                glm::vec3 targetPos = foundTarget->getTransform().getPosition();
                m_aiActionStartPos = m_compoundNPC->getTransform().getPosition();
                glm::vec3 toTarget = targetPos - m_aiActionStartPos;
                toTarget.y = 0.0f;
                float distToTarget = glm::length(toTarget);
                float standoff = 2.0f;
                if (distToTarget > standoff) {
                    m_aiActionTargetPos = m_aiActionStartPos + glm::normalize(toTarget) * (distToTarget - standoff);
                } else {
                    m_aiActionTargetPos = m_aiActionStartPos;  // Already close enough
                }
                m_aiActionTargetPos.y = m_aiActionStartPos.y;
                m_aiActionSpeed = 5.0f;
                float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                m_aiActionDuration = distance / m_aiActionSpeed;

                if (m_aiActionDuration > 0.01f) {
                    m_currentInteractObject = m_compoundNPC;
                    m_aiActionActive = true;
                    m_aiActionType = "move_to";
                    m_aiActionTimer = 0.0f;
                    glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                    m_aiActionStartYaw = m_compoundNPC->getEulerRotation().y;
                    std::cout << "[AI Compound] Step 4: Moving to target '" << m_compoundTargetName << "'" << std::endl;
                }
                m_compoundStep = 4;
                break;
            }
            case 4: {
                // GOTO_TARGET complete — place the item
                if (m_compoundTargetObj && m_compoundNPC->isCarrying()) {
                    placeCarriedItemAt(m_compoundNPC, m_compoundTargetObj);
                    std::cout << "[AI Compound] Step 5: Placed '" << m_compoundItemName << "' in '" << m_compoundTargetName << "'" << std::endl;
                } else {
                    std::cout << "[AI Compound] ABORT: couldn't place item" << std::endl;
                }
                m_compoundStep = 5;
                break;
            }
            case 5: {
                // All done — clear compound state
                std::cout << "[AI Compound] build_post complete!" << std::endl;
                m_compoundActionActive = false;
                m_compoundStep = 0;
                m_compoundItemObj = nullptr;
                m_compoundTargetObj = nullptr;
                m_compoundNPC = nullptr;
                break;
            }
        }
    }

    /**
     * Spawn a posthole at a given position with a given name.
     * Clones m_blueprintPostholeTemplate if available; falls back to a primitive cube.
     * Returns a pointer to the new object (owned by m_sceneObjects).
     */
    SceneObject* spawnPostholeAt(const glm::vec3& position, const std::string& name) {
        std::unique_ptr<SceneObject> obj;

        if (m_blueprintPostholeTemplate && !m_blueprintPostholeTemplate->getModelPath().empty()) {
            // Clone from existing posthole's model file
            std::string modelPath = m_blueprintPostholeTemplate->getModelPath();
            bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";

            if (isLime) {
                auto result = LimeLoader::load(modelPath);
                if (result.success) {
                    obj = LimeLoader::createSceneObject(result.mesh, *m_modelRenderer);
                }
            } else {
                auto result = GLBLoader::load(modelPath);
                if (result.success && !result.meshes.empty()) {
                    obj = GLBLoader::createSceneObject(result.meshes[0], *m_modelRenderer);
                }
            }

            if (obj) {
                obj->setModelPath(modelPath);
                obj->setEulerRotation(m_blueprintPostholeTemplate->getEulerRotation());
                obj->getTransform().setScale(m_blueprintPostholeTemplate->getTransform().getScale());
                obj->setHueShift(m_blueprintPostholeTemplate->getHueShift());
                obj->setSaturation(m_blueprintPostholeTemplate->getSaturation());
                obj->setBrightness(m_blueprintPostholeTemplate->getBrightness());
            }
        } else if (m_blueprintPostholeTemplate && m_blueprintPostholeTemplate->hasMeshData()) {
            // Clone from existing posthole's mesh data (primitive)
            const auto& verts = m_blueprintPostholeTemplate->getVertices();
            const auto& inds = m_blueprintPostholeTemplate->getIndices();
            uint32_t handle = m_modelRenderer->createModel(verts, inds);
            obj = std::make_unique<SceneObject>(name);
            obj->setBufferHandle(handle);
            obj->setVertexCount(static_cast<uint32_t>(verts.size()));
            obj->setIndexCount(static_cast<uint32_t>(inds.size()));
            obj->setLocalBounds(m_blueprintPostholeTemplate->getLocalBounds());
            obj->setModelPath("");
            obj->setMeshData(verts, inds);
            obj->setEulerRotation(m_blueprintPostholeTemplate->getEulerRotation());
            obj->getTransform().setScale(m_blueprintPostholeTemplate->getTransform().getScale());
        }

        if (!obj) {
            // Fallback: create a primitive cube
            float size = 0.5f;
            glm::vec4 color(0.65f, 0.63f, 0.58f, 1.0f);
            auto meshData = PrimitiveMeshBuilder::createCube(size, color);
            obj = std::make_unique<SceneObject>(name);
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
        }

        obj->setName(name);
        obj->setDescription("Concrete base for a fence post");

        // Offset Y so the bottom of the object sits on the terrain surface
        // Compute min vertex Y directly from mesh data (bypasses potentially unset bounds)
        glm::vec3 placePos = position;
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
        placePos.y = position.y + bottomOffset;

        std::cout << "[AI Blueprint] minVertexY=" << minVertexY
                  << " scale.y=" << scale.y
                  << " bottomOffset=" << bottomOffset
                  << " terrainY=" << position.y
                  << " finalY=" << placePos.y << std::endl;

        obj->getTransform().setPosition(placePos);

        SceneObject* ptr = obj.get();
        m_sceneObjects.push_back(std::move(obj));
        std::cout << "[AI Blueprint] Spawned '" << name << "' at ("
                  << placePos.x << ", " << placePos.y << ", " << placePos.z << ")" << std::endl;
        return ptr;
    }

    /**
     * Spawn a sidewall panel between two corner posts.
     * Clones m_blueprintSidewallTemplate. Positions at midpoint of the edge, on the terrain.
     */
    SceneObject* spawnSidewallAt(const glm::vec3& start, const glm::vec3& end, float wallYaw) {
        if (!m_blueprintSidewallTemplate) return nullptr;

        std::unique_ptr<SceneObject> obj;
        std::string modelPath = m_blueprintSidewallTemplate->getModelPath();

        if (!modelPath.empty()) {
            bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";
            if (isLime) {
                auto result = LimeLoader::load(modelPath);
                if (result.success) {
                    obj = LimeLoader::createSceneObject(result.mesh, *m_modelRenderer);
                }
            } else {
                auto result = GLBLoader::load(modelPath);
                if (result.success && !result.meshes.empty()) {
                    obj = GLBLoader::createSceneObject(result.meshes[0], *m_modelRenderer);
                }
            }

            if (obj) {
                obj->setModelPath(modelPath);
                obj->getTransform().setScale(m_blueprintSidewallTemplate->getTransform().getScale());
                obj->setHueShift(m_blueprintSidewallTemplate->getHueShift());
                obj->setSaturation(m_blueprintSidewallTemplate->getSaturation());
                obj->setBrightness(m_blueprintSidewallTemplate->getBrightness());
            }
        }

        if (!obj) {
            std::cout << "[AI Blueprint] Failed to clone sidewall template" << std::endl;
            return nullptr;
        }

        obj->setName("sidewall_building");
        obj->setDescription("Side wall panel for frame building");

        // Position at midpoint of the edge between two corners
        glm::vec3 placePos = (start + end) * 0.5f;

        // Sample actual terrain height at the wall's midpoint
        float terrainY = m_terrain.getHeightAt(placePos.x, placePos.z);
        glm::vec3 scale = obj->getTransform().getScale();

        // Compute bottom offset from mesh vertices
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
        placePos.y = terrainY + bottomOffset;

        obj->setEulerRotation(glm::vec3(0.0f, wallYaw, 0.0f));
        obj->getTransform().setPosition(placePos);

        SceneObject* ptr = obj.get();
        m_sceneObjects.push_back(std::move(obj));
        std::cout << "[AI Blueprint] Spawned sidewall at ("
                  << placePos.x << ", " << placePos.y << ", " << placePos.z
                  << ") yaw=" << wallYaw << std::endl;
        return ptr;
    }

    /**
     * Spawn a roof at the top of a frame structure.
     * Clones m_blueprintRoofTemplate. Positions it centered over the frame with a front overhang offset.
     * corners[4] are the frame corner positions used to find the top of the structure.
     * frontYaw is the NPC's facing direction — the front overhang goes that way.
     */
    SceneObject* spawnRoofAt(const glm::vec3& center, const glm::vec3 corners[4], float frontYaw) {
        if (!m_blueprintRoofTemplate) return nullptr;

        std::unique_ptr<SceneObject> obj;
        std::string modelPath = m_blueprintRoofTemplate->getModelPath();

        if (!modelPath.empty()) {
            bool isLime = modelPath.size() >= 5 && modelPath.substr(modelPath.size() - 5) == ".lime";
            if (isLime) {
                auto result = LimeLoader::load(modelPath);
                if (result.success) {
                    obj = LimeLoader::createSceneObject(result.mesh, *m_modelRenderer);
                }
            } else {
                auto result = GLBLoader::load(modelPath);
                if (result.success && !result.meshes.empty()) {
                    obj = GLBLoader::createSceneObject(result.meshes[0], *m_modelRenderer);
                }
            }

            if (obj) {
                obj->setModelPath(modelPath);
                obj->getTransform().setScale(m_blueprintRoofTemplate->getTransform().getScale());
                obj->setHueShift(m_blueprintRoofTemplate->getHueShift());
                obj->setSaturation(m_blueprintRoofTemplate->getSaturation());
                obj->setBrightness(m_blueprintRoofTemplate->getBrightness());
            }
        }

        if (!obj) {
            std::cout << "[AI Blueprint] Failed to clone roof template" << std::endl;
            return nullptr;
        }

        obj->setName("roof_building");
        obj->setDescription("Roof panel for frame building");

        // Find the top of the structure by scanning objects near the corners
        float topY = corners[0].y;
        for (auto& sceneObj : m_sceneObjects) {
            if (!sceneObj || !sceneObj->isVisible()) continue;
            glm::vec3 objPos = sceneObj->getTransform().getPosition();
            for (int i = 0; i < 4; i++) {
                float dist = glm::length(glm::vec2(objPos.x - corners[i].x, objPos.z - corners[i].z));
                if (dist < 1.5f) {
                    AABB bounds = sceneObj->getWorldBounds();
                    topY = std::max(topY, bounds.max.y);
                }
            }
        }

        // Position roof at center of frame, on top of the structure
        glm::vec3 placePos = center;
        placePos.y = topY;

        // Offset toward front for asymmetric overhang (front gets more overhang)
        float yawRad = glm::radians(frontYaw);
        glm::vec3 frontDir(sin(yawRad), 0.0f, cos(yawRad));
        float frontOffset = 0.5f;  // Shift roof forward by 0.5m for front overhang
        placePos += frontDir * frontOffset;

        // Rotate 90° so the longer dimension runs front-to-back
        obj->setEulerRotation(glm::vec3(0.0f, frontYaw + 90.0f, 0.0f));
        obj->getTransform().setPosition(placePos);

        SceneObject* ptr = obj.get();
        m_sceneObjects.push_back(std::move(obj));
        std::cout << "[AI Blueprint] Spawned roof at ("
                  << placePos.x << ", " << placePos.y << ", " << placePos.z
                  << ") frontYaw=" << frontYaw << std::endl;
        return ptr;
    }

    /**
     * Update blueprint action state machine (build_frame etc.)
     * Called each frame after updateCompoundAction(). Processes blueprint steps sequentially.
     * Each step either walks + spawns a posthole, or triggers a build_post compound action.
     */
    void updateBlueprintAction() {
        if (!m_blueprintActive || !m_blueprintNPC) return;

        // Wait for any compound action to finish (BUILD_POST steps)
        if (m_compoundActionActive) return;

        // Wait for any AI action to finish (walking steps)
        if (m_aiActionActive) return;

        // Check if all steps are done
        if (m_blueprintCurrentStep >= static_cast<int>(m_blueprintSteps.size())) {
            std::cout << "[AI Blueprint] All " << m_blueprintSteps.size() << " steps complete!" << std::endl;
            m_blueprintActive = false;
            m_blueprintNPC = nullptr;
            m_blueprintSteps.clear();
            m_blueprintUsedTimbers.clear();
            return;
        }

        auto& step = m_blueprintSteps[m_blueprintCurrentStep];

        if (step.type == BlueprintStep::SPAWN_POSTHOLE) {
            switch (m_blueprintSubStep) {
                case 0: {
                    // Start walking to the posthole position
                    glm::vec3 targetPos = step.position;
                    targetPos.y = m_blueprintNPC->getTransform().getPosition().y;
                    m_aiActionStartPos = m_blueprintNPC->getTransform().getPosition();

                    // Stop 1.5m short of the exact position
                    glm::vec3 toTarget = targetPos - m_aiActionStartPos;
                    toTarget.y = 0.0f;
                    float dist = glm::length(toTarget);
                    float standoff = 1.5f;
                    if (dist > standoff) {
                        m_aiActionTargetPos = m_aiActionStartPos + glm::normalize(toTarget) * (dist - standoff);
                    } else {
                        m_aiActionTargetPos = m_aiActionStartPos;
                    }
                    m_aiActionTargetPos.y = m_aiActionStartPos.y;
                    m_aiActionSpeed = 5.0f;
                    float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionDuration = distance / m_aiActionSpeed;

                    if (m_aiActionDuration > 0.01f) {
                        m_currentInteractObject = m_blueprintNPC;
                        m_aiActionActive = true;
                        m_aiActionType = "move_to";
                        m_aiActionTimer = 0.0f;
                        glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                        m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                        m_aiActionStartYaw = m_blueprintNPC->getEulerRotation().y;
                    }

                    std::cout << "[AI Blueprint] Step " << m_blueprintCurrentStep
                              << ": Walking to spawn '" << step.targetName << "'" << std::endl;
                    m_blueprintSubStep = 1;
                    break;
                }
                case 1: {
                    // Arrived — spawn the posthole
                    spawnPostholeAt(step.position, step.targetName);

                    // Advance to next blueprint step
                    m_blueprintCurrentStep++;
                    m_blueprintSubStep = 0;
                    std::cout << "[AI Blueprint] Progress: " << m_blueprintCurrentStep
                              << "/" << m_blueprintSteps.size() << " steps done" << std::endl;
                    break;
                }
            }
        }
        else if (step.type == BlueprintStep::BUILD_POST) {
            switch (m_blueprintSubStep) {
                case 0: {
                    // Find nearest available timber (visible, name contains "timber")
                    SceneObject* nearestTimber = nullptr;
                    float nearestDist = std::numeric_limits<float>::max();
                    glm::vec3 npcPos = m_blueprintNPC->getTransform().getPosition();

                    for (auto& obj : m_sceneObjects) {
                        if (!obj || !obj->isVisible()) continue;
                        std::string lowerName = obj->getName();
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        if (lowerName.find("timber") == std::string::npos) continue;
                        // Skip timbers already placed by this blueprint (tracked by pointer)
                        if (m_blueprintUsedTimbers.count(obj.get())) continue;
                        float dist = glm::length(obj->getTransform().getPosition() - npcPos);
                        if (dist < nearestDist) {
                            nearestDist = dist;
                            nearestTimber = obj.get();
                        }
                    }

                    if (!nearestTimber) {
                        std::cout << "[AI Blueprint] ABORT at step " << m_blueprintCurrentStep
                                  << ": no timber available for '" << step.targetName << "'" << std::endl;
                        m_blueprintActive = false;
                        m_blueprintNPC = nullptr;
                        m_blueprintSteps.clear();
                        m_blueprintUsedTimbers.clear();
                        return;
                    }

                    std::cout << "[AI Blueprint] Step " << m_blueprintCurrentStep
                              << ": build_post with '" << nearestTimber->getName()
                              << "' → '" << step.targetName << "'" << std::endl;

                    // Programmatically kick off the build_post compound action
                    // Pre-set m_compoundItemObj so compound step 0 uses this exact pointer
                    // (avoids wrong match when multiple timbers share the same name)
                    m_compoundActionActive = true;
                    m_compoundStep = 0;
                    m_compoundItemName = nearestTimber->getName();
                    m_compoundTargetName = step.targetName;
                    m_compoundItemObj = nearestTimber;  // Pre-set — compound step 0 will use this
                    m_compoundTargetObj = nullptr;
                    m_compoundNPC = m_blueprintNPC;

                    // Start step 0 of compound: 360° scan
                    m_currentInteractObject = m_blueprintNPC;
                    m_aiActionActive = true;
                    m_aiActionType = "look_around";
                    m_aiActionDuration = 2.0f;
                    m_aiActionTimer = 0.0f;
                    m_aiActionStartYaw = m_blueprintNPC->getEulerRotation().y;

                    // Save the timber pointer for tracking (compound step 5 clears m_compoundItemObj)
                    m_blueprintUsedTimbers.insert(nearestTimber);
                    m_blueprintSubStep = 1;
                    break;
                }
                case 1: {
                    // Compound action finished — advance to next step
                    std::cout << "[AI Blueprint] build_post for '" << step.targetName
                              << "' complete (used timber: '" << m_compoundItemName << "')" << std::endl;
                    m_blueprintCurrentStep++;
                    m_blueprintSubStep = 0;
                    std::cout << "[AI Blueprint] Progress: " << m_blueprintCurrentStep
                              << "/" << m_blueprintSteps.size() << " steps done" << std::endl;
                    break;
                }
            }
        }
        else if (step.type == BlueprintStep::BUILD_BEAM) {
            switch (m_blueprintSubStep) {
                case 0: {
                    // Find nearest available timber
                    SceneObject* nearestTimber = nullptr;
                    float nearestDist = std::numeric_limits<float>::max();
                    glm::vec3 npcPos = m_blueprintNPC->getTransform().getPosition();

                    for (auto& obj : m_sceneObjects) {
                        if (!obj || !obj->isVisible()) continue;
                        std::string lowerName = obj->getName();
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        if (lowerName.find("timber") == std::string::npos) continue;
                        if (m_blueprintUsedTimbers.count(obj.get())) continue;
                        float dist = glm::length(obj->getTransform().getPosition() - npcPos);
                        if (dist < nearestDist) {
                            nearestDist = dist;
                            nearestTimber = obj.get();
                        }
                    }

                    if (!nearestTimber) {
                        std::cout << "[AI Blueprint] ABORT at step " << m_blueprintCurrentStep
                                  << ": no timber available for beam" << std::endl;
                        m_blueprintActive = false;
                        m_blueprintNPC = nullptr;
                        m_blueprintSteps.clear();
                        m_blueprintUsedTimbers.clear();
                        return;
                    }

                    m_blueprintBeamTimber = nearestTimber;
                    m_blueprintUsedTimbers.insert(nearestTimber);

                    // Start walk to the timber
                    glm::vec3 timberPos = nearestTimber->getTransform().getPosition();
                    m_aiActionStartPos = npcPos;
                    m_aiActionTargetPos = timberPos;
                    m_aiActionTargetPos.y = m_aiActionStartPos.y;
                    m_aiActionSpeed = 5.0f;
                    float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionDuration = distance / m_aiActionSpeed;

                    if (m_aiActionDuration > 0.01f) {
                        m_currentInteractObject = m_blueprintNPC;
                        m_aiActionActive = true;
                        m_aiActionType = "move_to";
                        m_aiActionTimer = 0.0f;
                        glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                        m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                        m_aiActionStartYaw = m_blueprintNPC->getEulerRotation().y;
                    }

                    std::cout << "[AI Blueprint] Step " << m_blueprintCurrentStep
                              << ": Walking to timber '" << nearestTimber->getName() << "' for beam" << std::endl;
                    m_blueprintSubStep = 1;
                    break;
                }
                case 1: {
                    // Arrived at timber — pick it up
                    if (m_blueprintBeamTimber && !m_blueprintNPC->isCarrying()) {
                        m_blueprintBeamTimber->setVisible(false);
                        m_blueprintNPC->setCarriedItem(m_blueprintBeamTimber->getName(), m_blueprintBeamTimber);
                        std::cout << "[AI Blueprint] Picked up '" << m_blueprintBeamTimber->getName() << "' for beam" << std::endl;
                    }

                    // Start walk to beam midpoint (with standoff)
                    glm::vec3 midpoint = (step.beamStart + step.beamEnd) * 0.5f;
                    midpoint.y = m_blueprintNPC->getTransform().getPosition().y;
                    m_aiActionStartPos = m_blueprintNPC->getTransform().getPosition();
                    glm::vec3 toMid = midpoint - m_aiActionStartPos;
                    toMid.y = 0.0f;
                    float dist = glm::length(toMid);
                    float standoff = 2.0f;
                    if (dist > standoff) {
                        m_aiActionTargetPos = m_aiActionStartPos + glm::normalize(toMid) * (dist - standoff);
                    } else {
                        m_aiActionTargetPos = m_aiActionStartPos;
                    }
                    m_aiActionTargetPos.y = m_aiActionStartPos.y;
                    m_aiActionSpeed = 5.0f;
                    float distance = glm::length(m_aiActionTargetPos - m_aiActionStartPos);
                    m_aiActionDuration = distance / m_aiActionSpeed;

                    if (m_aiActionDuration > 0.01f) {
                        m_currentInteractObject = m_blueprintNPC;
                        m_aiActionActive = true;
                        m_aiActionType = "move_to";
                        m_aiActionTimer = 0.0f;
                        glm::vec3 direction = glm::normalize(m_aiActionTargetPos - m_aiActionStartPos);
                        m_aiActionTargetYaw = glm::degrees(atan2(direction.x, direction.z));
                        m_aiActionStartYaw = m_blueprintNPC->getEulerRotation().y;
                    }

                    std::cout << "[AI Blueprint] Walking to beam position" << std::endl;
                    m_blueprintSubStep = 2;
                    break;
                }
                case 2: {
                    // Arrived at beam position — place timber horizontally
                    if (m_blueprintNPC->isCarrying()) {
                        placeCarriedItemHorizontal(m_blueprintNPC, step.beamStart, step.beamEnd);
                    }
                    m_blueprintBeamTimber = nullptr;

                    m_blueprintCurrentStep++;
                    m_blueprintSubStep = 0;
                    std::cout << "[AI Blueprint] Progress: " << m_blueprintCurrentStep
                              << "/" << m_blueprintSteps.size() << " steps done" << std::endl;
                    break;
                }
            }
        }
        else if (step.type == BlueprintStep::SPAWN_SIDEWALL) {
            // Spawn sidewall immediately
            spawnSidewallAt(step.wallStart, step.wallEnd, step.wallYaw);

            m_blueprintCurrentStep++;
            m_blueprintSubStep = 0;
            std::cout << "[AI Blueprint] Progress: " << m_blueprintCurrentStep
                      << "/" << m_blueprintSteps.size() << " steps done" << std::endl;
        }
        else if (step.type == BlueprintStep::SPAWN_ROOF) {
            // Spawn the roof immediately (no walking needed — NPC is already near the frame)
            spawnRoofAt(step.roofCenter, step.roofCorners, step.roofFrontYaw);

            m_blueprintCurrentStep++;
            m_blueprintSubStep = 0;
            std::cout << "[AI Blueprint] Progress: " << m_blueprintCurrentStep
                      << "/" << m_blueprintSteps.size() << " steps done" << std::endl;
        }
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

    void duplicateObject(int index) {
        if (index < 0 || index >= static_cast<int>(m_sceneObjects.size())) return;

        SceneObject* original = m_sceneObjects[index].get();

        if (index == m_spawnObjectIndex) {
            std::cout << "Cannot duplicate spawn point" << std::endl;
            return;
        }

        if (!original->getModelPath().empty()) {
            auto result = GLBLoader::load(original->getModelPath());
            if (!result.success || result.meshes.empty()) {
                std::cerr << "Failed to reload model for duplication: " << original->getModelPath() << std::endl;
                return;
            }

            const auto& mesh = result.meshes[0];
            auto newObj = GLBLoader::createSceneObject(mesh, *m_modelRenderer);
            if (!newObj) {
                std::cerr << "Failed to create duplicate scene object" << std::endl;
                return;
            }

            newObj->setModelPath(original->getModelPath());
            newObj->setName(generateUniqueName(original->getName()));

            glm::vec3 pos = original->getTransform().getPosition();
            newObj->getTransform().setPosition(pos + glm::vec3(2.0f, 0.0f, 2.0f));
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
            newObj->getTransform().setPosition(pos + glm::vec3(2.0f, 0.0f, 2.0f));
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

    void pickObjectAtMouse() {
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

    // Compound action state machine (build_post etc.)
    bool m_compoundActionActive = false;
    int m_compoundStep = 0;               // 0-5 for the 6 steps
    std::string m_compoundItemName;       // e.g. "timber6612"
    std::string m_compoundTargetName;     // e.g. "posthole_01"
    SceneObject* m_compoundItemObj = nullptr;
    SceneObject* m_compoundTargetObj = nullptr;
    SceneObject* m_compoundNPC = nullptr; // NPC performing the compound action (persists after chat ends)

    // Blueprint system (build_frame etc.)
    struct BlueprintStep {
        enum Type { SPAWN_POSTHOLE, BUILD_POST, BUILD_BEAM, SPAWN_SIDEWALL, SPAWN_ROOF } type;
        glm::vec3 position{0.0f};     // SPAWN_POSTHOLE/SPAWN_ROOF: where to place it
        std::string targetName;        // Posthole name (used by SPAWN_POSTHOLE and BUILD_POST)
        glm::vec3 beamStart{0.0f};    // BUILD_BEAM: first corner position
        glm::vec3 beamEnd{0.0f};      // BUILD_BEAM: second corner position
        glm::vec3 wallStart{0.0f};    // SPAWN_SIDEWALL: one corner of the wall
        glm::vec3 wallEnd{0.0f};      // SPAWN_SIDEWALL: other corner of the wall
        float wallYaw = 0.0f;         // SPAWN_SIDEWALL: rotation of the wall panel
        glm::vec3 roofCenter{0.0f};   // SPAWN_ROOF: center of the frame
        glm::vec3 roofCorners[4];     // SPAWN_ROOF: corner positions for height reference
        float roofFrontYaw = 0.0f;    // SPAWN_ROOF: NPC yaw at build time (front direction)
    };
    bool m_blueprintActive = false;
    std::vector<BlueprintStep> m_blueprintSteps;
    int m_blueprintCurrentStep = 0;
    int m_blueprintSubStep = 0;        // Sub-state within current step
    SceneObject* m_blueprintNPC = nullptr;
    float m_blueprintGroundY = 0.0f;   // Ground level for the building
    std::set<SceneObject*> m_blueprintUsedTimbers;  // Timber pointers already placed by this blueprint
    SceneObject* m_blueprintPostholeTemplate = nullptr;  // Existing posthole to clone
    SceneObject* m_blueprintRoofTemplate = nullptr;      // Existing roof to clone
    SceneObject* m_blueprintSidewallTemplate = nullptr;  // Existing sidewall to clone
    SceneObject* m_blueprintBeamTimber = nullptr;  // Current timber being placed as beam

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

    // Editor subsystems
    EditorUI m_editorUI;
    std::unique_ptr<TerrainBrushTool> m_brushTool;
    std::unique_ptr<PathTool> m_pathTool;
    std::unique_ptr<ChunkManager> m_chunkManager;
    bool m_wasLeftMouseDown = false;
    bool m_isLooking = false;
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
    bool m_gizmoDragging = false;

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

int main() {
    try {
        TerrainEditor editor;
        editor.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
