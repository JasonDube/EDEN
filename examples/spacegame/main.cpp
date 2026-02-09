/**
 * @file main.cpp
 * @brief Space Game Application
 *
 * A space game that loads levels created by the terrain editor.
 * Game-specific UI and logic are separate from the editor tools.
 *
 * To rename this game, edit GameConfig.hpp
 */

#include "GameConfig.hpp"
#include "GameUI.hpp"

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ImGuiManager.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/ProceduralSkybox.hpp"
#include "Editor/GLBLoader.hpp"

#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Window.hpp>
#include <eden/LevelSerializer.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <nfd.h>

#include <iostream>
#include <memory>
#include <filesystem>

using namespace eden;

class SpaceGameApplication : public VulkanApplicationBase {
public:
    SpaceGameApplication()
        : VulkanApplicationBase(
            spacegame::DEFAULT_WINDOW_WIDTH,
            spacegame::DEFAULT_WINDOW_HEIGHT,
            spacegame::GAME_WINDOW_TITLE)
    {}

protected:
    void onInit() override {
        // Initialize renderers
        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(),
            getSwapchain().getRenderPass(),
            getSwapchain().getExtent()
        );

        m_skybox = std::make_unique<ProceduralSkybox>(
            getContext(),
            getSwapchain().getRenderPass(),
            getSwapchain().getExtent()
        );

        // Initialize ImGui
        m_imguiManager.init(getContext(), getSwapchain(), getWindow().getHandle(),
                           spacegame::GAME_CONFIG_FILE);

        // Initialize native file dialog
        NFD_Init();

        // Initialize game UI
        m_gameUI = std::make_unique<spacegame::GameUI>();
        m_gameUI->initialize();

        // Setup camera
        m_camera.setPosition(glm::vec3(0, 5.0f, 10.0f));
        m_camera.setYaw(-90.0f);
        m_camera.setPitch(0.0f);
        m_camera.setNoClip(true);

        // Check for command line level to load
        // TODO: Parse command line args

        std::cout << spacegame::GAME_NAME << " initialized." << std::endl;
        std::cout << "Press F1 for help, L to load a level." << std::endl;
    }

    void onCleanup() override {
        vkDeviceWaitIdle(getContext().getDevice());
        NFD_Quit();
        m_skybox.reset();
        m_modelRenderer.reset();
        m_imguiManager.cleanup();
    }

    void onSwapchainRecreated() override {
        // Recreate renderers with new swapchain extent
        SkyParameters savedSkyParams;
        if (m_skybox) {
            savedSkyParams = m_skybox->getParameters();
        }

        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(),
            getSwapchain().getRenderPass(),
            getSwapchain().getExtent()
        );

        m_skybox = std::make_unique<ProceduralSkybox>(
            getContext(),
            getSwapchain().getRenderPass(),
            getSwapchain().getExtent()
        );
        m_skybox->updateParameters(savedSkyParams);

        // Re-upload any loaded models to GPU
        // (model handles are invalidated when renderer is recreated)
        // For now, just clear loaded models - user would need to reload level
        m_loadedModels.clear();
        if (m_levelLoaded) {
            std::cout << "Window resized - please reload level (press L)" << std::endl;
            m_levelLoaded = false;
        }
    }

    void update(float deltaTime) override {
        // Start ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        // Handle global keyboard shortcuts BEFORE Input::update() clears pressed state
        handleKeyboardShortcuts();

        // Update input state (calculates mouse delta, then clears per-frame flags)
        Input::update();

        // Update game UI
        if (m_gameUI) {
            m_gameUI->update(deltaTime);
        }

        // Camera movement AFTER Input::update() so mouse delta is available
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            handleCameraInput(deltaTime);
        }
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Begin render pass
        auto& swapchain = getSwapchain();
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = swapchain.getRenderPass();
        renderPassInfo.framebuffer = swapchain.getFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchain.getExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // Black for space
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Get camera matrices
        auto extent = swapchain.getExtent();
        float aspect = static_cast<float>(extent.width) / extent.height;

        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 10000.0f);
        glm::mat4 viewProj = proj * view;

        // Render skybox
        if (m_skybox) {
            m_skybox->render(cmd, view, proj);
        }

        // Render loaded models
        for (auto& model : m_loadedModels) {
            m_modelRenderer->render(cmd, viewProj, model.handle, model.transform);
        }

        // Render ImGui
        renderUI();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

private:
    void handleCameraInput(float deltaTime) {
        float moveSpeed = 20.0f;
        float lookSpeed = 0.5f;

        // WASD movement
        bool forward = Input::isKeyDown(Input::KEY_W);
        bool backward = Input::isKeyDown(Input::KEY_S);
        bool left = Input::isKeyDown(Input::KEY_A);
        bool right = Input::isKeyDown(Input::KEY_D);
        bool up = Input::isKeyDown(Input::KEY_SPACE);
        bool down = Input::isKeyDown(Input::KEY_LEFT_CONTROL);

        m_camera.setSpeed(moveSpeed);
        m_camera.processKeyboard(deltaTime, forward, backward, left, right, up, down);

        // Mouse look (when right button held)
        if (Input::isMouseButtonDown(Input::MOUSE_RIGHT)) {
            auto delta = Input::getMouseDelta();
            m_camera.processMouse(delta.x * lookSpeed, delta.y * lookSpeed);
            Input::setMouseCaptured(true);
        } else {
            Input::setMouseCaptured(false);
        }
    }

    void handleKeyboardShortcuts() {
        // F1 - Toggle help
        if (Input::isKeyPressed(Input::KEY_F1)) {
            m_showHelp = !m_showHelp;
        }

        // L - Open native file dialog to load level
        if (Input::isKeyPressed(Input::KEY_L)) {
            openLoadLevelDialog();
        }

        // Escape - Close help
        if (Input::isKeyPressed(Input::KEY_ESCAPE)) {
            m_showHelp = false;
            ImGui::SetWindowFocus(nullptr);
        }
    }

    void openLoadLevelDialog() {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filters[1] = {{"EDEN Level", "eden"}};

        nfdresult_t result = NFD_OpenDialog(&outPath, filters, 1, nullptr);

        if (result == NFD_OKAY) {
            loadLevel(outPath);
            NFD_FreePath(outPath);
        }
    }

    void renderUI() {
        ImGui::NewFrame();

        auto extent = getSwapchain().getExtent();
        float width = static_cast<float>(extent.width);
        float height = static_cast<float>(extent.height);

        // Render game-specific UI
        if (m_gameUI) {
            m_gameUI->render(width, height);
        }

        // Render help window
        if (m_showHelp) {
            renderHelpWindow();
        }

        // Render load dialog
        if (m_showLoadDialog) {
            renderLoadDialog();
        }

        // Render status bar
        renderStatusBar(width);

        ImGui::Render();
    }

    void renderHelpWindow() {
        ImGui::SetNextWindowPos(ImVec2(400, 200), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Help", &m_showHelp)) {
            ImGui::Text("%s v%s", spacegame::GAME_NAME, spacegame::GAME_VERSION);
            ImGui::Separator();

            ImGui::Text("CONTROLS:");
            ImGui::BulletText("WASD - Move camera");
            ImGui::BulletText("Right Mouse + Move - Look around");
            ImGui::BulletText("Space/Ctrl - Move up/down");
            ImGui::BulletText("L - Load level");
            ImGui::BulletText("F1 - Toggle this help");
            ImGui::BulletText("Escape - Close dialogs");

            ImGui::Separator();
            ImGui::Text("LEVEL STATUS:");
            if (m_levelLoaded) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                    "Loaded: %s", m_currentLevelName.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f),
                    "No level loaded");
                ImGui::TextWrapped("Press L to load a .eden level file created with the terrain editor.");
            }
        }
        ImGui::End();
    }

    void renderLoadDialog() {
        ImGui::SetNextWindowPos(ImVec2(300, 150), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Load Level", &m_showLoadDialog)) {
            ImGui::Text("Enter path to .eden level file:");

            static char pathBuffer[512] = "";
            ImGui::InputText("##path", pathBuffer, sizeof(pathBuffer));

            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                loadLevel(pathBuffer);
                m_showLoadDialog = false;
            }

            ImGui::Separator();
            ImGui::Text("Recent levels would appear here...");
            // TODO: Implement recent levels list
        }
        ImGui::End();
    }

    void renderStatusBar(float width) {
        ImGuiWindowFlags barFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width, 30));
        ImGui::SetNextWindowBgAlpha(0.7f);

        if (ImGui::Begin("##StatusBar", nullptr, barFlags)) {
            ImGui::Text("%s", spacegame::GAME_NAME);

            ImGui::SameLine(200);
            if (m_levelLoaded) {
                ImGui::Text("Level: %s", m_currentLevelName.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No level loaded");
            }

            ImGui::SameLine(width - 150);
            auto pos = m_camera.getPosition();
            ImGui::Text("%.0f, %.0f, %.0f", pos.x, pos.y, pos.z);
        }
        ImGui::End();
    }

    void loadLevel(const std::string& filepath) {
        std::cout << "Loading level: " << filepath << std::endl;

        LevelData levelData;
        if (!LevelSerializer::load(filepath, levelData)) {
            std::cerr << "Failed to load level: " << LevelSerializer::getLastError() << std::endl;
            return;
        }

        // Clear existing models
        for (auto& model : m_loadedModels) {
            m_modelRenderer->destroyModel(model.handle);
        }
        m_loadedModels.clear();

        // Apply sky parameters
        if (m_skybox) {
            m_skybox->updateParameters(levelData.skyParams);
        }

        // Load scene objects
        for (const auto& objData : levelData.objects) {
            if (!objData.modelPath.empty()) {
                // Load GLB model
                std::string modelPath = objData.modelPath;

                // Try to find the model file
                if (!std::filesystem::exists(modelPath)) {
                    // Try relative to level file
                    auto levelDir = std::filesystem::path(filepath).parent_path();
                    auto relPath = levelDir / objData.modelPath;
                    if (std::filesystem::exists(relPath)) {
                        modelPath = relPath.string();
                    } else {
                        std::cerr << "Model not found: " << objData.modelPath << std::endl;
                        continue;
                    }
                }

                // Load GLB file
                auto result = GLBLoader::load(modelPath);
                if (!result.success || result.meshes.empty()) {
                    std::cerr << "Failed to load model: " << result.error << std::endl;
                    continue;
                }

                // Create GPU buffers for the mesh
                const auto& mesh = result.meshes[0];
                uint32_t handle = m_modelRenderer->createModel(mesh.vertices, mesh.indices);

                LoadedModel model;
                model.handle = handle;
                model.name = objData.name;

                // Build transform matrix
                glm::mat4 transform = glm::mat4(1.0f);
                transform = glm::translate(transform, objData.position);
                transform = glm::rotate(transform, glm::radians(objData.rotation.y), glm::vec3(0, 1, 0));
                transform = glm::rotate(transform, glm::radians(objData.rotation.x), glm::vec3(1, 0, 0));
                transform = glm::rotate(transform, glm::radians(objData.rotation.z), glm::vec3(0, 0, 1));
                transform = glm::scale(transform, objData.scale);
                model.transform = transform;

                m_loadedModels.push_back(model);
                std::cout << "Loaded model: " << objData.name << std::endl;
            }
        }

        // Set spawn position
        m_camera.setPosition(levelData.spawnPosition + glm::vec3(0, 1.8f, 0)); // Eye height
        m_camera.setYaw(levelData.spawnYaw);
        m_camera.setPitch(0.0f);

        m_levelLoaded = true;
        m_currentLevelName = levelData.name.empty() ?
            std::filesystem::path(filepath).stem().string() : levelData.name;

        std::cout << "Level loaded successfully: " << m_currentLevelName << std::endl;
    }

    // Rendering
    std::unique_ptr<ModelRenderer> m_modelRenderer;
    std::unique_ptr<ProceduralSkybox> m_skybox;
    ImGuiManager m_imguiManager;
    Camera m_camera;

    // Game UI
    std::unique_ptr<spacegame::GameUI> m_gameUI;

    // Loaded level data
    struct LoadedModel {
        uint32_t handle;
        std::string name;
        glm::mat4 transform;
    };
    std::vector<LoadedModel> m_loadedModels;

    // Level state
    bool m_levelLoaded = false;
    std::string m_currentLevelName;

    // UI state
    bool m_showHelp = true;
    bool m_showLoadDialog = false;
};


int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  " << spacegame::GAME_NAME << std::endl;
    std::cout << "  Version " << spacegame::GAME_VERSION << std::endl;
    std::cout << "  EDEN Engine" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        SpaceGameApplication app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
