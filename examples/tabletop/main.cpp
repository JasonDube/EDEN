// EDEN Tabletop — the game's first vertical slice: a 3D table viewed straight
// down, a battle grid, and a mini figure you can click and drag across the
// board. Top-down orthographic camera with scroll-zoom and right-drag pan.
//
// This is the "game" target (as opposed to the terrain_editor authoring tool).
// It renders with the full engine (Camera + ModelRenderer) and will grow to load
// levels authored in the editor, spawn the party, and host the ImGui rules panels.

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/ImGuiManager.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"

#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Window.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image_write.h>   // implementation already lives in libeden (GLBLoader.cpp)

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

using namespace eden;

namespace {
constexpr float kBoardHalf = 16.0f;   // table extends [-16,16] in X and Z
constexpr float kGridStep  = 2.0f;    // one grid cell = 2 world units (~a 5ft square)
constexpr float kMiniR     = 0.7f;    // mini base radius (also the click radius)
}  // namespace

class TabletopApp : public VulkanApplicationBase {
public:
    TabletopApp() : VulkanApplicationBase(1280, 720, "EDEN Tabletop") {
        if (const char* p = std::getenv("TABLETOP_SHOT"); p && *p) {
            m_shotPath = p;
            m_shotCountdown = 8;
            m_shotExit = std::getenv("TABLETOP_SHOT_EXIT") != nullptr;
        }
    }

protected:
    void onInit() override {
        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());
        m_imgui.init(getContext(), getSwapchain(), getWindow().getHandle(), "imgui_tabletop.ini");

        // Top-down orthographic camera. Set the zoom (ortho half-height) first;
        // the Top preset positions the camera above the target and switches to
        // orthographic for us.
        m_camera.setOrthoSize(m_orthoSize);
        m_camera.setViewPreset(ViewPreset::Top, glm::vec3(0.0f));
        m_camera.setNoClip(true);

        // Table slab (top surface at y=0) and one draggable mini.
        auto table = PrimitiveMeshBuilder::createFoundation(
            {-kBoardHalf, -kBoardHalf}, {kBoardHalf, kBoardHalf}, -0.3f, 0.3f,
            glm::vec4(0.30f, 0.23f, 0.16f, 1.0f));
        m_tableHandle = m_modelRenderer->createModel(table.vertices, table.indices);

        auto mini = PrimitiveMeshBuilder::createCylinder(
            kMiniR, 1.8f, 28, glm::vec4(0.85f, 0.18f, 0.18f, 1.0f));
        m_miniHandle = m_modelRenderer->createModel(mini.vertices, mini.indices);

        m_grid = buildGrid();
    }

    void onCleanup() override {
        vkDeviceWaitIdle(getContext().getDevice());
        m_modelRenderer.reset();
        m_imgui.cleanup();
    }

    void onSwapchainRecreated() override {
        // recreatePipeline preserves the uploaded model data (no mesh rebuild).
        if (m_modelRenderer)
            m_modelRenderer->recreatePipeline(getSwapchain().getRenderPass(),
                                              getSwapchain().getExtent());
    }

    void update(float /*dt*/) override {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        handleCameraAndPieces();

        // Fire the dev screenshot once the countdown elapses.
        if (m_shotCountdown >= 0 && --m_shotCountdown < 0) {
            captureScreenshot(m_shotPath);
            if (m_shotExit) getWindow().close();
        }

        Input::update();
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &bi);

        auto& sc = getSwapchain();
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = sc.getRenderPass();
        rp.framebuffer = sc.getFramebuffers()[imageIndex];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = sc.getExtent();
        std::array<VkClearValue, 2> clear{};
        clear[0].color = {{0.06f, 0.06f, 0.09f, 1.0f}};
        clear[1].depthStencil = {1.0f, 0};
        rp.clearValueCount = static_cast<uint32_t>(clear.size());
        rp.pClearValues = clear.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        glm::mat4 viewProj = computeViewProj();
        m_modelRenderer->render(cmd, viewProj, m_tableHandle, glm::mat4(1.0f));
        m_modelRenderer->renderLines(cmd, viewProj, m_grid, glm::vec3(0.45f, 0.42f, 0.34f));
        glm::mat4 miniM = glm::translate(glm::mat4(1.0f),
                                         glm::vec3(m_miniPos.x, 0.0f, m_miniPos.y));
        m_modelRenderer->render(cmd, viewProj, m_miniHandle, miniM);

        renderUI();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
        m_lastImageIndex = imageIndex;
    }

private:
    // ----- matrices -----
    float aspect() const {
        auto e = getSwapchain().getExtent();
        return static_cast<float>(e.width) / static_cast<float>(e.height);
    }
    glm::mat4 computeViewProj() const {
        // Un-flipped proj: ModelRenderer's pipeline handles Y itself.
        return m_camera.getProjectionMatrix(aspect(), 0.1f, 5000.0f) * m_camera.getViewMatrix();
    }

    // Unproject the mouse onto the y=0 board plane. Correct for orthographic
    // (Camera::screenToWorldRay is perspective-only, so we invert VP ourselves).
    bool mouseOnBoard(glm::vec2& outXZ) const {
        float w = static_cast<float>(getWindow().getWidth());
        float h = static_cast<float>(getWindow().getHeight());
        glm::vec2 m = Input::getMousePosition();
        glm::mat4 invVP = glm::inverse(computeViewProj());
        float ndcX = 2.0f * m.x / w - 1.0f;
        float ndcY = 1.0f - 2.0f * m.y / h;
        glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f); nearP /= nearP.w;
        glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f); farP  /= farP.w;
        glm::vec3 o = glm::vec3(nearP);
        glm::vec3 d = glm::normalize(glm::vec3(farP - nearP));
        if (std::abs(d.y) < 1e-6f) return false;
        float t = -o.y / d.y;
        if (t < 0.0f) return false;
        glm::vec3 hit = o + d * t;
        outXZ = glm::vec2(hit.x, hit.z);
        return true;
    }

    void handleCameraAndPieces() {
        ImGuiIO& io = ImGui::GetIO();
        bool overUI = io.WantCaptureMouse;

        // Zoom (scroll): smaller ortho size = closer.
        float scroll = Input::getScrollDelta();
        if (scroll != 0.0f && !overUI) {
            m_orthoSize = std::clamp(m_orthoSize - scroll * 2.0f, 4.0f, 60.0f);
            m_camera.setOrthoSize(m_orthoSize);
        }

        // Pan (right-drag): move the camera in the board plane. World units per
        // pixel scale with the current zoom so panning feels 1:1 with the table.
        if (Input::isMouseButtonDown(Input::MOUSE_RIGHT)) {
            glm::vec2 delta = Input::getMouseDelta();
            float wpp = (2.0f * m_orthoSize) / static_cast<float>(getWindow().getHeight());
            glm::vec3 p = m_camera.getPosition();
            p.x -= delta.x * wpp;
            p.z -= delta.y * wpp;
            m_camera.setPosition(p);
        }

        // Pick / drag the mini (left button).
        glm::vec2 board;
        if (Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !overUI) {
            if (mouseOnBoard(board) && glm::distance(board, m_miniPos) <= kMiniR * 1.5f)
                m_dragging = true;
        }
        if (m_dragging && Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
            if (mouseOnBoard(board)) {
                m_miniPos.x = std::clamp(board.x, -kBoardHalf, kBoardHalf);
                m_miniPos.y = std::clamp(board.y, -kBoardHalf, kBoardHalf);
            }
        } else {
            m_dragging = false;
        }
    }

    std::vector<glm::vec3> buildGrid() const {
        std::vector<glm::vec3> lines;
        const float y = 0.02f;  // just above the table surface
        for (float c = -kBoardHalf; c <= kBoardHalf + 0.001f; c += kGridStep) {
            lines.push_back({c, y, -kBoardHalf}); lines.push_back({c, y, kBoardHalf});
            lines.push_back({-kBoardHalf, y, c}); lines.push_back({kBoardHalf, y, c});
        }
        return lines;
    }

    void renderUI() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Tabletop");
        ImGui::TextUnformatted("EDEN Tabletop - prototype");
        ImGui::Separator();
        ImGui::BulletText("Left-drag the red mini to move it");
        ImGui::BulletText("Right-drag to pan");
        ImGui::BulletText("Scroll to zoom");
        ImGui::Spacing();
        ImGui::Text("Mini:  (%.1f, %.1f)", m_miniPos.x, m_miniPos.y);
        ImGui::Text("Zoom:  %.1f", m_orthoSize);
        if (ImGui::Button("Recenter mini")) m_miniPos = glm::vec2(0.0f);
        ImGui::End();
        ImGui::Render();
    }

    // ----- dev screenshot (copy last swapchain image -> PNG) -----
    void captureScreenshot(const std::string& path) {
        VkDevice device = getContext().getDevice();
        VkExtent2D ext = getSwapchain().getExtent();
        VkFormat fmt = getSwapchain().getImageFormat();
        const auto& images = getSwapchain().getImages();
        if (m_lastImageIndex >= images.size()) return;
        VkImage src = images[m_lastImageIndex];

        uint32_t w = ext.width, h = ext.height;
        VkDeviceSize sz = static_cast<VkDeviceSize>(w) * h * 4;

        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        getContext().createBuffer(sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buf, mem);

        vkDeviceWaitIdle(device);
        VkCommandBuffer cmd = getContext().beginSingleTimeCommands();
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image = src;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);

        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        getContext().endSingleTimeCommands(cmd);

        void* data = nullptr;
        vkMapMemory(device, mem, 0, sz, 0, &data);
        const unsigned char* px = static_cast<const unsigned char*>(data);
        bool bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
        std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            unsigned char r = px[i * 4 + 0], g = px[i * 4 + 1], bl = px[i * 4 + 2];
            if (bgra) std::swap(r, bl);
            rgba[i * 4 + 0] = r; rgba[i * 4 + 1] = g; rgba[i * 4 + 2] = bl; rgba[i * 4 + 3] = 255;
        }
        vkUnmapMemory(device, mem);
        int ok = stbi_write_png(path.c_str(), (int)w, (int)h, 4, rgba.data(), (int)w * 4);
        std::cerr << (ok ? "screenshot written: " : "screenshot FAILED: ") << path << "\n";

        vkDestroyBuffer(device, buf, nullptr);
        vkFreeMemory(device, mem, nullptr);
    }

    std::unique_ptr<ModelRenderer> m_modelRenderer;
    ImGuiManager m_imgui;
    Camera m_camera;

    uint32_t m_tableHandle = 0;
    uint32_t m_miniHandle  = 0;
    std::vector<glm::vec3> m_grid;

    glm::vec2 m_miniPos{0.0f, 0.0f};   // mini position on the board (world XZ)
    bool      m_dragging = false;
    float     m_orthoSize = 18.0f;

    // dev screenshot
    std::string m_shotPath;
    int         m_shotCountdown = -1;
    bool        m_shotExit = false;
    uint32_t    m_lastImageIndex = 0;
};

int main() {
    try {
        TabletopApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
