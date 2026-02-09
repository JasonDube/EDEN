/**
 * @file main.cpp
 * @brief EVE - AI Companion Application
 * 
 * An intelligent android companion living aboard your trading vessel.
 * Features:
 * - Real-time conversation with Eve via LLM backend
 * - Parameter laboratory for personality tuning
 * - Ship environment awareness
 * - 3D spaceship interior (TODO: model integration)
 */

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ImGuiManager.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/SkinnedModelRenderer.hpp"

#include "Eve.hpp"
#include "Spaceship.hpp"
#include "ChatInterface.hpp"
#include "ParameterLab.hpp"
#include "XenkTerminal.hpp"
#include "ShipInterior.hpp"

#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Window.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <memory>

using namespace eden;

class EveApplication : public VulkanApplicationBase {
public:
    EveApplication() : VulkanApplicationBase(1600, 900, "EVE - AI Companion") {}

protected:
    void onInit() override {
        // Initialize renderers
        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(), 
            getSwapchain().getRenderPass(), 
            getSwapchain().getExtent()
        );
        
        m_skinnedModelRenderer = std::make_unique<SkinnedModelRenderer>(
            getContext(), 
            getSwapchain().getRenderPass(), 
            getSwapchain().getExtent()
        );

        // Initialize ImGui
        m_imguiManager.init(getContext(), getSwapchain(), getWindow().getHandle(), "eve_imgui.ini");

        // Build ship interior
        m_shipInterior.build(*m_modelRenderer);
        
        // Setup camera for ship interior view (inside the ship)
        m_camera.setPosition(glm::vec3(0, 4.0f, 0)); // Center of ship, eye height
        m_camera.setYaw(90.0f);  // Looking toward front (window)
        m_camera.setPitch(0.0f);
        m_camera.setNoClip(true); // Free camera for now
        
        // Initialize Eve
        m_eve = std::make_unique<eve::Eve>();
        m_eve->loadPersonality("eve_personality.json");
        
        if (!m_eve->initialize("http://localhost:8080")) {
            std::cerr << "Warning: Could not connect to AI backend. "
                      << "Make sure the EDEN backend server is running." << std::endl;
        }
        
        // Initialize spaceship
        m_ship = std::make_unique<eve::Spaceship>();
        m_ship->setName("The Wanderer"); // Default name, Captain can change
        
        // Set Eve's initial location
        m_eve->setLocation("Bridge");
        
        // Initialize UI
        m_chatInterface = std::make_unique<eve::ChatInterface>();
        m_chatInterface->setEve(m_eve.get());
        
        m_parameterLab = std::make_unique<eve::ParameterLab>();
        m_parameterLab->setEve(m_eve.get());
        m_parameterLab->setShip(m_ship.get());
        
        // Initialize Xenk terminal
        m_xenkTerminal = std::make_unique<eve::XenkTerminal>();
        m_xenkTerminal->initialize();
        
        std::cout << "EVE initialized. Press F1 for help, F3 for Xenk terminal." << std::endl;
    }

    void onCleanup() override {
        vkDeviceWaitIdle(getContext().getDevice());
        m_skinnedModelRenderer.reset();
        m_modelRenderer.reset();
        m_imguiManager.cleanup();
    }

    void onSwapchainRecreated() override {
        // Renderers will be recreated if needed
    }

    void update(float deltaTime) override {
        // Start ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        
        // Update Eve (poll for AI responses)
        if (m_eve) {
            m_eve->update();
        }
        
        // Update Xenk terminal (poll for responses)
        if (m_xenkTerminal) {
            m_xenkTerminal->update();
        }
        
        // Update input
        Input::update();
        
        // Camera movement (when not typing in chat or terminal)
        if (!m_chatInterface->hasFocus() && !m_xenkTerminal->hasFocus()) {
            handleCameraInput(deltaTime);
        }
        
        // Handle keyboard shortcuts
        handleKeyboardShortcuts();
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        // Begin command buffer
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
        clearValues[0].color = {{0.02f, 0.02f, 0.05f, 1.0f}}; // Dark space background
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();
        
        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        // Get camera matrices
        auto extent = swapchain.getExtent();
        float aspect = static_cast<float>(extent.width) / extent.height;
        
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 1000.0f);
        
        // Render spaceship interior
        m_shipInterior.render(*m_modelRenderer, cmd, view, proj);
        
        // TODO: Render Eve's avatar
        // TODO: Render Xenk's avatar
        
        // Render ImGui
        renderUI();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

private:
    void handleCameraInput(float deltaTime) {
        float moveSpeed = 10.0f;
        float lookSpeed = 0.5f;  // Increased from 0.1f
        
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
        
        // F2 - Toggle parameter lab
        if (Input::isKeyPressed(Input::KEY_F2)) {
            m_parameterLab->toggleVisible();
        }
        
        // F3 - Toggle Xenk terminal
        if (Input::isKeyPressed(Input::KEY_F3)) {
            m_xenkTerminal->toggleVisible();
        }
        
        // Enter - Focus chat input (only if not in chat)
        // Note: This needs to be handled carefully with ImGui focus
        
        // Escape - Clear chat focus
        if (Input::isKeyPressed(Input::KEY_ESCAPE)) {
            ImGui::SetWindowFocus(nullptr);
        }
    }

    void renderUI() {
        ImGui::NewFrame();
        
        auto extent = getSwapchain().getExtent();
        float width = static_cast<float>(extent.width);
        float height = static_cast<float>(extent.height);
        
        // Create fullscreen dockspace for edge docking (offset below status bar)
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float statusBarHeight = 30.0f;
        ImVec2 dockPos = viewport->WorkPos;
        dockPos.y += statusBarHeight;
        ImVec2 dockSize = viewport->WorkSize;
        dockSize.y -= statusBarHeight;
        ImGui::SetNextWindowPos(dockPos);
        ImGui::SetNextWindowSize(dockSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        
        ImGuiWindowFlags dockspaceFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpaceWindow", nullptr, dockspaceFlags);
        ImGui::PopStyleVar(3);
        
        ImGuiID dockspaceId = ImGui::GetID("EveDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();
        
        // Render chat interface
        m_chatInterface->render(width, height);
        
        // Render parameter lab
        m_parameterLab->render();
        
        // Render Xenk terminal
        m_xenkTerminal->render();
        
        // Render help window
        if (m_showHelp) {
            renderHelpWindow();
        }
        
        // Render ship info bar
        renderShipInfoBar(width);
        
        ImGui::Render();
    }

    void renderHelpWindow() {
        ImGui::SetNextWindowPos(ImVec2(400, 200), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Help - EVE Companion", &m_showHelp)) {
            ImGui::TextWrapped("Welcome aboard, Captain.");
            ImGui::Separator();
            
            ImGui::Text("CONTROLS:");
            ImGui::BulletText("WASD - Move camera");
            ImGui::BulletText("Right Mouse + Move - Look around");
            ImGui::BulletText("Space/Ctrl - Move up/down");
            ImGui::BulletText("Escape - Unfocus windows");
            ImGui::BulletText("F1 - Toggle this help");
            ImGui::BulletText("F2 - Toggle Parameter Lab");
            ImGui::BulletText("F3 - Toggle Xenk Terminal");
            
            ImGui::Separator();
            ImGui::Text("ABOUT EVE:");
            ImGui::TextWrapped(
                "Eve is your AI companion aboard this vessel. "
                "She's recently been activated and is eager to assist you. "
                "Use the Parameter Lab to adjust her personality traits."
            );
            
            ImGui::Separator();
            ImGui::Text("BACKEND STATUS:");
            if (m_eve && m_eve->isConnected()) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), 
                    "Connected to AI backend");
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), 
                    "Backend disconnected!");
                ImGui::TextWrapped(
                    "Run the backend server:\n"
                    "cd backend && python server.py"
                );
            }
        }
        ImGui::End();
    }

    void renderShipInfoBar(float width) {
        ImGuiWindowFlags barFlags = 
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;
        
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width, 30));
        ImGui::SetNextWindowBgAlpha(0.7f);
        
        if (ImGui::Begin("##ShipBar", nullptr, barFlags)) {
            if (m_ship) {
                ImGui::Text("%s", m_ship->getName().c_str());
                ImGui::SameLine(200);
                
                // Hull
                float hull = m_ship->getHullIntegrity();
                ImVec4 hullColor = hull > 0.5f ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : 
                                   hull > 0.25f ? ImVec4(0.9f, 0.9f, 0.3f, 1.0f) : 
                                   ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                ImGui::TextColored(hullColor, "HULL: %.0f%%", hull * 100);
                
                ImGui::SameLine(350);
                ImGui::Text("FUEL: %.0f%%", m_ship->getFuelLevel() * 100);
                
                ImGui::SameLine(480);
                ImGui::Text("PWR: %.0f%%", m_ship->getPowerLevel() * 100);
                
                if (m_eve) {
                    ImGui::SameLine(width - 200);
                    ImGui::Text("EVE: %s", m_eve->getState().current_location.c_str());
                }
            }
        }
        ImGui::End();
    }

    // Rendering
    std::unique_ptr<ModelRenderer> m_modelRenderer;
    std::unique_ptr<SkinnedModelRenderer> m_skinnedModelRenderer;
    ImGuiManager m_imguiManager;
    Camera m_camera;
    
    // Eve system
    std::unique_ptr<eve::Eve> m_eve;
    std::unique_ptr<eve::Spaceship> m_ship;
    std::unique_ptr<eve::ChatInterface> m_chatInterface;
    std::unique_ptr<eve::ParameterLab> m_parameterLab;
    std::unique_ptr<eve::XenkTerminal> m_xenkTerminal;
    
    // 3D Environment
    eve::ShipInterior m_shipInterior;
    
    // UI state
    bool m_showHelp = true;
};


int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  EVE - AI Companion System" << std::endl;
    std::cout << "  EDEN Engine" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        EveApplication app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
