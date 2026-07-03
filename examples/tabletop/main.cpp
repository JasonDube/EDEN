// EDEN Tabletop — the game's first vertical slice: a 3D table viewed straight
// down, a battle grid, and a party of minis played out in initiative order.
// Top-down orthographic camera with scroll-zoom and middle-drag pan.
//
// This is the "game" target (as opposed to the terrain_editor authoring tool).
// The turn/round/movement rules live in the UI-free encounter.hpp (unit-tested);
// this file owns the camera, rendering, and the ImGui encounter panel. On each
// combatant's turn you drag their mini within its movement range (reachable
// squares highlighted); "End Turn" advances initiative and rolls the round over.
// It will grow to load editor-authored levels and host the full SRD rules.

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/ImGuiManager.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Editor/BinaryLevelReader.hpp"
#include "Editor/GLBLoader.hpp"

#include <eden/Audio.hpp>

#include "encounter.hpp"
#include "character.hpp"
#include "houses.hpp"
#include "origins.hpp"
#include "elfhouses.hpp"
#include "dwarfhouses.hpp"
#include "dragonhouses.hpp"
#include "feyhouses.hpp"
#include "infernalhouses.hpp"
#include "shop.hpp"
#include "family.hpp"
#include "relations.hpp"
#include "classfit.hpp"

#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Window.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image_write.h>   // implementation already lives in libeden (GLBLoader.cpp)

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace eden;

namespace {
constexpr float kBoardHalf = 16.0f;   // table extends [-16,16] in X and Z
constexpr float kGridStep  = 2.0f;    // one grid cell = 2 world units (~a 5ft square)
constexpr float kMiniR     = 0.7f;    // mini base radius
constexpr int   kGridN     = static_cast<int>((2 * kBoardHalf) / kGridStep);  // cells per axis (16)

// Beats between a foe's AI phases, so its turn is watchable rather than instant.
constexpr float kAIMoveDelay   = 0.45f;
constexpr float kAIStrikeDelay = 0.55f;
constexpr float kAIEndDelay    = 0.55f;

// World<->cell mapping. Cells are indexed [0, kGridN); cell centers sit on the
// grid squares, cell edges on the grid lines.
inline float cellCenter(int c) { return -kBoardHalf + (c + 0.5f) * kGridStep; }
inline float cellEdge(int i)   { return -kBoardHalf + i * kGridStep; }
inline int   worldToCell(float w) {
    return std::clamp(static_cast<int>(std::floor((w + kBoardHalf) / kGridStep)), 0, kGridN - 1);
}
}  // namespace

class TabletopApp : public VulkanApplicationBase {
    // Portrait image + its ImGui texture handle (defined early so method
    // signatures below can reference it).
    struct Portrait {
        std::string path, race, gender;    // race = subfolder; gender from filename
        int w = 0, h = 0;                  // source pixel size (for aspect-correct display)
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };

public:
    TabletopApp() : VulkanApplicationBase(1280, 720, "EDEN Tabletop") {
        if (const char* p = std::getenv("TABLETOP_SHOT"); p && *p) {
            m_shotPath = p;
            m_shotCountdown = 8;
            m_shotExit = std::getenv("TABLETOP_SHOT_EXIT") != nullptr;
        }
        // TABLETOP_LEVEL=<path.edenbin> loads a terrain_editor level for a
        // top-down preview (scale/pipeline check) instead of the combat sandbox.
        if (const char* lp = std::getenv("TABLETOP_LEVEL"); lp && *lp) m_levelPath = lp;
    }

protected:
    void onInit() override {
        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());
        m_imgui.init(getContext(), getSwapchain(), getWindow().getHandle(), "imgui_tabletop.ini");

        // Light the scene — without this the model shader gets ~zero ambient/sun
        // and everything renders dark. Bright, mostly-flat top-down lighting.
        m_modelRenderer->setLights({});
        m_modelRenderer->setDayNight(/*sunY*/0.8f, /*ambientLevel*/0.55f);

        // Top-down orthographic camera. Set the zoom (ortho half-height) first;
        // the Top preset positions the camera above the target and switches to
        // orthographic for us.
        m_camera.setOrthoSize(m_orthoSize);
        m_camera.setViewPreset(ViewPreset::Top, glm::vec3(0.0f));
        m_camera.setNoClip(true);

        // Level-preview mode: load a real level, frame it, and skip the combat
        // sandbox entirely.
        if (!m_levelPath.empty()) loadLevel(m_levelPath);
        if (m_hasLevel) {
            m_grid = buildLevelGrid();
            frameCameraOnLevel();
            loadCharacters();
            // Open on the Savage Lands title screen with looping theme music.
            m_screen = Screen::Title;
            eden::Audio::getInstance().init();
            startTitleMusic();
            return;
        }

        // Table slab (top surface at y=0).
        auto table = PrimitiveMeshBuilder::createFoundation(
            {-kBoardHalf, -kBoardHalf}, {kBoardHalf, kBoardHalf}, -0.3f, 0.3f,
            glm::vec4(0.30f, 0.23f, 0.16f, 1.0f));
        m_tableHandle = m_modelRenderer->createModel(table.vertices, table.indices);

        m_grid = buildGrid();

        // A small sample party + two foes, each a colored mini on the grid. Every
        // combatant gets its own cylinder model so it can carry its own color.
        auto spawn = [&](const char* name, int init, int spd, int cx, int cy,
                         glm::vec3 col, bool foe, int hp, int ac,
                         int atk, int dDice, int dSides, int dBonus) {
            rpgtt::Combatant c;
            c.name = name; c.initiative = init; c.speedFeet = spd;
            c.cx = cx; c.cy = cy; c.cr = col.r; c.cg = col.g; c.cb = col.b; c.foe = foe;
            c.maxHp = c.hp = hp; c.ac = ac; c.attackBonus = atk;
            c.dmgDice = dDice; c.dmgSides = dSides; c.dmgBonus = dBonus;
            m_enc.add(c);
            auto mesh = PrimitiveMeshBuilder::createCylinder(kMiniR, 1.8f, 28, glm::vec4(col, 1.0f));
            m_miniHandles.push_back(m_modelRenderer->createModel(mesh.vertices, mesh.indices));
        };
        //     name             init spd  cx  cy  color                    foe    hp  ac atk dice sides bonus
        spawn("Mera the Swift",   20, 35,  5,  8, {0.30f,0.72f,0.38f}, false, 24, 14,  5,  1,  6,  3);
        spawn("Sir Aldric",       17, 30,  6,  8, {0.28f,0.48f,0.86f}, false, 30, 18,  5,  1,  8,  3);
        spawn("Bandit",           14, 30,  9,  8, {0.82f,0.22f,0.20f}, true,  28, 13,  4,  1,  6,  2);
        spawn("Doran Stone",      12, 25,  6,  9, {0.72f,0.56f,0.28f}, false, 32, 16,  4,  1, 10,  2);
        spawn("Cutthroat",        10, 30, 10,  9, {0.90f,0.42f,0.20f}, true,  22, 14,  5,  1,  6,  3);

        m_spawn = m_enc.combatants();   // remember starting layout for Reset
        m_enc.start();
    }

    void onCleanup() override {
        stopTitleMusic();
        eden::Audio::getInstance().shutdown();
        vkDeviceWaitIdle(getContext().getDevice());
        destroyPortraits();     // RemoveTexture needs the ImGui Vulkan backend still alive
        m_modelRenderer.reset();
        m_imgui.cleanup();
    }

    void onSwapchainRecreated() override {
        // recreatePipeline preserves the uploaded model data (no mesh rebuild).
        if (m_modelRenderer)
            m_modelRenderer->recreatePipeline(getSwapchain().getRenderPass(),
                                              getSwapchain().getExtent());
    }

    void update(float dt) override {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        handleCameraAndPieces();
        stepAI(dt);
        if (m_screen == Screen::Title) m_titlePulse += dt;
        if (m_hasLevel && m_screen == Screen::Game) updateFacing();
        if (m_hintTimer > 0.0f) m_hintTimer -= dt;

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

        if (m_hasLevel && m_screen != Screen::Game) {
            // Title / character-creation screens: dark clear + UI, no level yet.
            renderUI();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            vkCmdEndRenderPass(cmd);
            vkEndCommandBuffer(cmd);
            m_lastImageIndex = imageIndex;
            return;
        }

        if (m_hasLevel) {
            // Level preview: render the loaded meshes (textured) + the 5-ft grid.
            // Force opaque — floor/wall textures can carry an alpha channel that
            // the editor treats as "frosted glass", which would let the dark
            // background bleed through and make the floor look cloudy.
            for (const auto& d : m_levelDraws)
                m_modelRenderer->render(cmd, viewProj, m_levelMeshHandles[d.meshIdx], d.model,
                                        0.0f, 1.0f, 1.0f, /*twoSided*/false, /*indoor*/false,
                                        /*transparent*/false);
            m_modelRenderer->renderLines(cmd, viewProj, m_grid, glm::vec3(0.28f, 0.46f, 0.34f));
            // Character tokens (Percy, Orlen) standing on the grid.
            for (const auto& t : m_tokens) {
                glm::mat4 M = tokenMatrix(t);
                for (uint32_t h : t.meshHandles)
                    m_modelRenderer->render(cmd, viewProj, h, M, 0.0f, 1.0f, 1.0f,
                                            /*twoSided*/false, /*indoor*/false, /*transparent*/false);
            }
            // Ring under the token being dragged.
            if (m_dragToken >= 0 && m_dragToken < static_cast<int>(m_tokens.size())) {
                const Token& t = m_tokens[m_dragToken];
                auto ring = buildRing(t.cx * 5.0f + 2.5f, t.cy * 5.0f + 2.5f, 2.4f, 0.4f);
                m_modelRenderer->renderLines(cmd, viewProj, ring, glm::vec3(0.96f, 0.86f, 0.22f));
            }
            // Interaction affordance: ring under NPCs/foes adjacent to your piece
            // (green = talk, red = hostile), so you can see who you can act on.
            if (int p = playerTokenIndex(); p >= 0) {
                for (int i = 0; i < static_cast<int>(m_tokens.size()); ++i) {
                    const Token& t = m_tokens[i];
                    if (t.attitude == Attitude::Player || !tokensAdjacent(m_tokens[p], t)) continue;
                    glm::vec3 col = (t.attitude == Attitude::Hostile)
                                        ? glm::vec3(0.92f, 0.25f, 0.20f) : glm::vec3(0.30f, 0.85f, 0.42f);
                    auto ring = buildRing(t.cx * 5.0f + 2.5f, t.cy * 5.0f + 2.5f, 2.4f, 0.42f);
                    m_modelRenderer->renderLines(cmd, viewProj, ring, col);
                }
            }
            renderUI();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            vkCmdEndRenderPass(cmd);
            vkEndCommandBuffer(cmd);
            m_lastImageIndex = imageIndex;
            return;
        }

        m_modelRenderer->render(cmd, viewProj, m_tableHandle, glm::mat4(1.0f));
        m_modelRenderer->renderLines(cmd, viewProj, m_grid, glm::vec3(0.42f, 0.40f, 0.34f));

        // Reachable squares for the active mover, drawn as a green sub-grid.
        if (m_enc.hasActive()) {
            const auto& a = m_enc.active();
            auto reach = buildReach(a.cx, a.cy, m_enc.cellsLeft());
            m_modelRenderer->renderLines(cmd, viewProj, reach, glm::vec3(0.30f, 0.80f, 0.42f));
        }

        // Minis. The active mini follows the cursor (snapped to a cell) while
        // being dragged; a ring marks whose turn it is — yellow if the hovered
        // cell is a legal move, red if it's out of range.
        for (int i = 0; i < static_cast<int>(m_enc.combatants().size()); ++i) {
            const auto& c = m_enc.combatants()[i];
            bool isActive = m_enc.hasActive() && i == m_enc.activeId();
            int px = c.cx, py = c.cy;
            if (isActive && m_dragging) { px = m_hoverCx; py = m_hoverCy; }
            float wx = cellCenter(px), wz = cellCenter(py);
            glm::mat4 mm = glm::translate(glm::mat4(1.0f), glm::vec3(wx, 0.0f, wz));
            if (c.isDown())  // fallen: flatten the token to the tabletop
                mm = glm::scale(mm, glm::vec3(1.25f, 0.10f, 1.25f));
            m_modelRenderer->render(cmd, viewProj, m_miniHandles[i], mm);

            if (isActive && !c.isDown()) {
                bool ok = !m_dragging || m_enc.canActiveReach(m_hoverCx, m_hoverCy, kGridN);
                glm::vec3 ringCol = ok ? glm::vec3(0.96f, 0.86f, 0.22f)
                                       : glm::vec3(0.90f, 0.26f, 0.20f);
                auto ring = buildRing(wx, wz, kMiniR * 1.35f, 0.06f);
                m_modelRenderer->renderLines(cmd, viewProj, ring, ringCol);
            } else if (!c.isDown() && m_hoverCx == c.cx && m_hoverCy == c.cy &&
                       m_enc.canAttack(i)) {
                // Hovering a foe the active mover can strike: red target ring,
                // gold if the strike would be flanked (advantage).
                glm::vec3 col = m_enc.isFlanking(m_enc.activeId(), i)
                                    ? glm::vec3(0.98f, 0.80f, 0.20f)
                                    : glm::vec3(0.92f, 0.22f, 0.18f);
                auto ring = buildRing(wx, wz, kMiniR * 1.35f, 0.06f);
                m_modelRenderer->renderLines(cmd, viewProj, ring, col);
            }
        }

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
        // Vulkan clip space is Y-down; glm's projection is Y-up. Flip clip Y so
        // the frame isn't rendered vertically mirrored (which flips winding and
        // turns tilted views upside down). This is the standard Vulkan fix.
        // Tight near/far for good depth precision at room scale — a huge range
        // (e.g. 0.1..5000) makes the floor z-fight and hide the grid.
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect(), 0.5f, 2000.0f);
        proj[1][1] *= -1.0f;
        return proj * m_camera.getViewMatrix();
    }

    // Unproject the mouse onto the y=0 board plane. Correct for orthographic
    // (Camera::screenToWorldRay is perspective-only, so we invert VP ourselves).
    // Ray from the cursor into the world. computeViewProj flips clip Y to Vulkan
    // convention (NDC y = -1 at screen top), so ndcY uses 2*y/h - 1 to match.
    void mouseRay(glm::vec3& outO, glm::vec3& outD) const {
        float w = static_cast<float>(getWindow().getWidth());
        float h = static_cast<float>(getWindow().getHeight());
        glm::vec2 m = Input::getMousePosition();
        glm::mat4 invVP = glm::inverse(computeViewProj());
        float ndcX = 2.0f * m.x / w - 1.0f;
        float ndcY = 2.0f * m.y / h - 1.0f;
        glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f); nearP /= nearP.w;
        glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f); farP  /= farP.w;
        outO = glm::vec3(nearP);
        outD = glm::normalize(glm::vec3(farP - nearP));
    }

    bool mouseOnBoard(glm::vec2& outXZ) const {
        glm::vec3 o, d;
        mouseRay(o, d);
        if (std::abs(d.y) < 1e-6f) return false;
        float t = -o.y / d.y;
        if (t < 0.0f) return false;
        glm::vec3 hit = o + d * t;
        outXZ = glm::vec2(hit.x, hit.z);
        return true;
    }

    // Ray vs axis-aligned box (slab method); returns entry distance in tHit.
    static bool rayAABB(const glm::vec3& o, const glm::vec3& d,
                        const glm::vec3& bmin, const glm::vec3& bmax, float& tHit) {
        float tmin = -1e30f, tmax = 1e30f;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(d[a]) < 1e-8f) {
                if (o[a] < bmin[a] || o[a] > bmax[a]) return false;
            } else {
                float t1 = (bmin[a] - o[a]) / d[a], t2 = (bmax[a] - o[a]) / d[a];
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
            }
        }
        if (tmax < std::max(tmin, 0.0f)) return false;
        tHit = tmin > 0.0f ? tmin : tmax;
        return tHit >= 0.0f;
    }

    void handleCameraAndPieces() {
        ImGuiIO& io = ImGui::GetIO();
        bool overUI = io.WantCaptureMouse;

        if (m_hasLevel && m_screen == Screen::Title) {
            if (Input::isMouseButtonPressed(Input::MOUSE_LEFT) ||
                Input::isMouseButtonPressed(Input::MOUSE_RIGHT) ||
                Input::isKeyPressed(Input::KEY_SPACE) || Input::isKeyPressed(Input::KEY_ENTER))
                beginGame();
            return;
        }
        if (m_hasLevel && m_screen == Screen::CharCreate) return;   // ImGui-driven wizard
        if (m_hasLevel) { handleTokenDrag(overUI); handleLevelCamera(overUI); return; }

        // Zoom (scroll): smaller ortho size = closer.
        float scroll = Input::getScrollDelta();
        if (scroll != 0.0f && !overUI) {
            m_orthoSize = std::clamp(m_orthoSize - scroll * 2.0f, 4.0f, 60.0f);
            m_camera.setOrthoSize(m_orthoSize);
        }

        // Pan (middle-drag): "grab" the board point under the cursor at press and
        // keep it pinned there as the mouse moves. We use absolute mouse->board
        // unprojection rather than Input::getMouseDelta() — that delta is only
        // tracked while the mouse is captured (first-person mode), which the
        // tabletop never does, so it would always read zero here.
        if (Input::isMouseButtonPressed(Input::MOUSE_MIDDLE) && !overUI) {
            if (mouseOnBoard(m_panAnchor)) m_panning = true;
        }
        if (m_panning && Input::isMouseButtonDown(Input::MOUSE_MIDDLE)) {
            glm::vec2 cur;
            if (mouseOnBoard(cur)) {
                // Move the camera so the anchored point slides back under the
                // cursor. Ortho mapping is linear in camera position, so this
                // converges in one step and the grabbed point stays put.
                glm::vec2 shift = m_panAnchor - cur;
                glm::vec3 p = m_camera.getPosition();
                p.x += shift.x;
                p.z += shift.y;
                m_camera.setPosition(p);
            }
        } else {
            m_panning = false;
        }

        // Move the active mini (left button). You may only move the combatant
        // whose turn it is, and only by grabbing its own cell; on release the
        // move commits if the target cell is within movement range.
        glm::vec2 board;
        bool overBoard = mouseOnBoard(board);
        if (overBoard) { m_hoverCx = worldToCell(board.x); m_hoverCy = worldToCell(board.y); }

        // Player input only drives hero turns; foes are run by the AI.
        if (Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !overUI && overBoard &&
            m_enc.hasActive() && !m_enc.active().foe) {
            const auto& a = m_enc.active();
            if (m_hoverCx == a.cx && m_hoverCy == a.cy) {
                m_dragging = true;              // grabbed its own cell: move it
            } else {
                int tid = combatantAt(m_hoverCx, m_hoverCy);   // clicked another mini?
                if (tid >= 0 && m_enc.canAttack(tid)) doAttack(tid);
            }
        }
        if (m_dragging && !Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
            // Released: commit if reachable (commitMove rejects illegal moves and
            // resolves any opportunity attacks provoked by leaving melee).
            if (overBoard) commitMove(m_hoverCx, m_hoverCy);
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

    // ----- level preview (load a terrain_editor .edenbin, render it textured) -----
    void loadLevel(const std::string& path) {
        eden::BinaryLevelReader reader;
        eden::BinaryLevelData data = reader.load(path);
        if (!data.success) {
            std::cerr << "level load FAILED: " << path << "  (" << data.error << ")\n";
            return;
        }
        std::cerr << "level loaded: " << path << " — " << data.meshes.size() << " meshes, "
                  << data.textures.size() << " textures, " << data.objects.size() << " objects\n";

        // One GPU model per binary mesh, carrying its baked texture (if any).
        m_levelMeshHandles.assign(data.meshes.size(), 0u);
        for (size_t i = 0; i < data.meshes.size(); ++i) {
            const auto& mesh = data.meshes[i];
            const unsigned char* px = nullptr; int w = 0, h = 0;
            if (mesh.textureId >= 0 && mesh.textureId < static_cast<int>(data.textures.size())) {
                const auto& t = data.textures[mesh.textureId];
                if (!t.pixels.empty()) { px = t.pixels.data(); w = t.width; h = t.height; }
            }
            m_levelMeshHandles[i] =
                m_modelRenderer->createModel(mesh.vertices, mesh.indices, px, w, h);
        }

        // One draw per object at its transform; accumulate the floor-plan bounds.
        glm::vec2 mn(1e9f), mx(-1e9f);
        for (const auto& o : data.objects) {
            if (!o.visible || o.meshId < 0 ||
                o.meshId >= static_cast<int>(m_levelMeshHandles.size())) continue;
            glm::mat4 m = glm::translate(glm::mat4(1.0f), o.position);
            m = glm::rotate(m, glm::radians(o.rotation.y), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(o.rotation.x), glm::vec3(1, 0, 0));
            m = glm::rotate(m, glm::radians(o.rotation.z), glm::vec3(0, 0, 1));
            m = glm::scale(m, o.scale);
            m_levelDraws.push_back({o.meshId, m, o.transparent});
            glm::vec2 c(o.position.x, o.position.z);
            glm::vec2 half(std::abs(o.scale.x) * 0.5f, std::abs(o.scale.z) * 0.5f);
            mn = glm::min(mn, c - half);
            mx = glm::max(mx, c + half);
        }
        if (m_levelDraws.empty()) return;
        m_levelMin = mn; m_levelMax = mx;
        auto slash = path.find_last_of("/\\");
        m_levelName = (slash == std::string::npos) ? path : path.substr(slash + 1);
        m_hasLevel = true;
    }

    // 5-ft grid (1 unit = 1 ft), aligned to world 5-ft lines but CLIPPED to the
    // floor plan (no margin), drawn just above the floor.
    std::vector<glm::vec3> buildLevelGrid() const {
        std::vector<glm::vec3> lines;
        const float step = 5.0f;
        const float y = 0.35f;   // a bit over the floor slab (top at y=0.1) so it reads at all angles
        float x0 = std::ceil(m_levelMin.x / step) * step, x1 = std::floor(m_levelMax.x / step) * step;
        float z0 = std::ceil(m_levelMin.y / step) * step, z1 = std::floor(m_levelMax.y / step) * step;
        for (float x = x0; x <= x1 + 0.01f; x += step) {
            lines.push_back({x, y, m_levelMin.y}); lines.push_back({x, y, m_levelMax.y});
        }
        for (float z = z0; z <= z1 + 0.01f; z += step) {
            lines.push_back({m_levelMin.x, y, z}); lines.push_back({m_levelMax.x, y, z});
        }
        return lines;
    }

    void frameCameraOnLevel() {
        glm::vec2 c = (m_levelMin + m_levelMax) * 0.5f;
        glm::vec2 size = m_levelMax - m_levelMin;
        float span = std::max({size.x, size.y, 8.0f});
        // Start on a natural 3/4 PERSPECTIVE angle around the room; the player can
        // orbit/pan/dolly freely, or hit T for a clean orthographic top-down.
        m_camTarget = glm::vec3(c.x, 2.0f, c.y);   // a bit above the floor
        m_camYaw = -90.0f;
        m_camPitch = -45.0f;
        m_camDist = span * 1.6f;                    // perspective dolly distance
        m_orthoSize = span * 0.5f * 1.15f;          // used by the T top-down view
        m_levelOrtho = false;
        applyOrbitCamera();
    }

    // Position the camera from the orbit target/yaw/pitch. Free view is
    // perspective (natural recession); the T tactical view is orthographic.
    void applyOrbitCamera() {
        m_camera.setYaw(m_camYaw);
        m_camera.setPitch(m_camPitch);
        if (m_levelOrtho) {
            m_camera.setProjectionMode(ProjectionMode::Orthographic);
            m_camera.setOrthoSize(m_orthoSize);
            m_camera.setPosition(m_camTarget - m_camera.getFront() * 500.0f);
        } else {
            m_camera.setProjectionMode(ProjectionMode::Perspective);
            m_camera.setFov(40.0f);
            m_camera.setPosition(m_camTarget - m_camera.getFront() * m_camDist);
        }
    }

    // Free tabletop camera: right-drag orbit, middle-drag pan, scroll dolly/zoom,
    // T = snap to a clean orthographic top-down. (Input::getMouseDelta only works
    // while the mouse is captured, so we track our own per-frame delta.)
    void handleLevelCamera(bool overUI) {
        glm::vec2 mouse = Input::getMousePosition();
        glm::vec2 delta = m_haveLastMouse ? (mouse - m_lastMouse) : glm::vec2(0.0f);
        m_lastMouse = mouse;
        m_haveLastMouse = true;

        float scroll = Input::getScrollDelta();
        if (scroll != 0.0f && !overUI) {
            if (m_levelOrtho) m_orthoSize = std::clamp(m_orthoSize - scroll * 2.0f, 3.0f, 200.0f);
            else              m_camDist   = std::clamp(m_camDist * (1.0f - scroll * 0.1f), 4.0f, 500.0f);
        }
        if (Input::isMouseButtonDown(Input::MOUSE_RIGHT) && !overUI) {   // orbit -> free perspective
            m_camYaw   += delta.x * 0.3f;
            m_camPitch  = std::clamp(m_camPitch - delta.y * 0.3f, -85.0f, -3.0f);
            m_levelOrtho = false;
        }
        if (Input::isMouseButtonDown(Input::MOUSE_MIDDLE) && !overUI) {  // pan
            float h = static_cast<float>(getWindow().getHeight());
            float zoom = m_levelOrtho ? m_orthoSize : m_camDist;
            float wpp = m_levelOrtho ? (2.0f * m_orthoSize) / h
                                     : (2.0f * m_camDist * std::tan(glm::radians(20.0f))) / h;
            // Boost pan when zoomed in so close-up traversal isn't sluggish; the
            // far/default view keeps its 1:1 feel (boost clamps to 1).
            wpp *= std::clamp(50.0f / std::max(zoom, 4.0f), 1.0f, 6.0f);
            m_camTarget -= m_camera.getRight() * (delta.x * wpp);
            m_camTarget += m_camera.getUp()    * (delta.y * wpp);
        }
        if (Input::isKeyPressed(Input::KEY_T) && !ImGui::GetIO().WantTextInput) {
            m_camYaw = -90.0f;
            m_camPitch = -89.9f;
            m_levelOrtho = true;   // clean orthographic top-down tactical view
        }
        applyOrbitCamera();
    }

    // Disposition — decides what "click an adjacent token" does. Hostile is
    // attacked; everyone else is talked to. Peaceful NPCs can never be attacked
    // by a normal click (that path just doesn't exist for them).
    enum class Attitude { Player, Ally, Neutral, Hostile };

    // A character token: GLB meshes + the transform data to stand it upright,
    // feet on the floor, centered on a 5-ft grid cell.
    struct Token {
        std::string name;
        std::vector<uint32_t> meshHandles;
        float scale = 1.0f;            // uniform, to fit target height
        float minY = 0.0f;             // local bounds min.y (to set feet on floor)
        glm::vec2 centerXZ{0.0f};      // local XZ center (to center on the cell)
        int cx = 0, cy = 0;            // grid cell
        Attitude attitude = Attitude::Neutral;
        bool merchant = false;        // opens a shop when talked to
        std::string dialog;           // greeting line (Neutral/Ally NPCs)
        float faceYaw = 0.0f;         // current facing (radians, world)
        float defaultYaw = 0.0f;      // facing when not engaged
    };

    // Offset for the GLB's local "front" so faceYaw points that front at a
    // target. 0 = model faces +Z; adjust by pi / +-pi/2 if it faces away/sideways.
    static constexpr float kModelFrontYaw = 0.0f;

    // ----- title screen -----
    void startTitleMusic() {
        // miniaudio handles mp3/ogg/wav/flac; try a few names, first that loads wins.
        const char* candidates[] = {
            "assets/music/title.ogg", "assets/music/title.mp3", "assets/music/title.wav",
            "assets/music/savage_lands.ogg", "assets/music/savage_lands.mp3",
        };
        for (const char* path : candidates) {
            m_musicLoop = eden::Audio::getInstance().startLoop(path, 0.5f);
            if (m_musicLoop >= 0) { std::cerr << "title music: " << path << "\n"; return; }
        }
        std::cerr << "title music: none found (drop a title.mp3/ogg/wav in assets/music/)\n";
    }
    void stopTitleMusic() {
        if (m_musicLoop >= 0) { eden::Audio::getInstance().stopLoop(m_musicLoop); m_musicLoop = -1; }
    }
    // Title -> character creation. Silence the theme for the creator.
    void beginGame() {
        m_screen = Screen::CharCreate;
        stopTitleMusic();
        m_rollsUsed = 1;        // the initial roll counts as the first of three
        m_halfElfBonus.fill(false);
        m_skillPick.fill(false);
        m_selectedPortrait = -1;
        m_previewPortrait = -1;
        m_classIdx = 0; m_bgIdx = 0; m_alignIdx = 4;   // player chooses (or hits "Roll a Random Hero")
        scanPortraits();
        rollAbilityScores();
        assignHouse();
    }

    // ----- character creation -----
    int roll4d6DropLowest() {
        int d[4]; for (int i = 0; i < 4; ++i) d[i] = rollDie(6);
        int lo = std::min({d[0], d[1], d[2], d[3]});
        return d[0] + d[1] + d[2] + d[3] - lo;
    }
    void rollAbilityScores() {
        for (int i = 0; i < 6; ++i) m_rolled[i] = roll4d6DropLowest();
        for (int a = 0; a < 6; ++a) m_assign[a] = -1;   // unassigned; player drags them (or auto-assign)
        // Personality scores are rolled as part of the same act (they re-roll with abilities).
        m_bravery = roll4d6DropLowest();                        // higher = braver (to a fault)
        m_narcissism = rollDie(6) + rollDie(6) + rollDie(6);    // 3d6 bell; middle is ideal
        m_willToPower = rollDie(6) + rollDie(6) + rollDie(6);   // 3d6 bell; low=compliant, high=dominant
        m_carnality = rollDie(6) + rollDie(6) + rollDie(6);     // 3d6; ideal mid-low
        m_cruelty = rollDie(6) + rollDie(6) + rollDie(6);       // 3d6; ideal middle
        m_sociability = rollDie(6) + rollDie(6) + rollDie(6);   // 3d6; ideal middle
        m_skepticism = rollDie(6) + rollDie(6) + rollDie(6);    // 3d6; ideal discerning
        auto d3 = [&]() { return rollDie(6) + rollDie(6) + rollDie(6); };
        m_honor = d3(); m_piety = d3(); m_greed = d3(); m_temper = d3();
        m_diligence = d3(); m_compassion = d3(); m_curiosity = d3();
    }
    bool isAssigned(int rolledIdx) const {
        for (int a = 0; a < 6; ++a) if (m_assign[a] == rolledIdx) return true;
        return false;
    }
    bool allAssigned() const {
        for (int a = 0; a < 6; ++a) if (m_assign[a] < 0) return false;
        return true;
    }
    // Drop rolled value `rolledIdx` onto `ability`. If it was on another ability,
    // swap; if it came from the pool, the ability's old value drops back to the pool.
    void assignRoll(int ability, int rolledIdx) {
        int src = -1;
        for (int b = 0; b < 6; ++b) if (m_assign[b] == rolledIdx) src = b;
        int old = m_assign[ability];
        m_assign[ability] = rolledIdx;
        if (src >= 0 && src != ability) m_assign[src] = old;
    }
    bool isHalfElf() const { return std::string(rpgc::raceOptions()[m_raceIdx]) == "Half-Elf"; }
    int  halfElfPickCount() const {
        int n = 0; for (bool b : m_halfElfBonus) if (b) ++n; return n;
    }
    // Full racial bonuses: the fixed table plus the Half-Elf +1/+1 choices.
    std::array<int, rpgc::ABILITY_COUNT> effectiveRaceBonus() const {
        auto b = rpgc::raceAbilityBonuses(rpgc::raceOptions()[m_raceIdx]);
        if (isHalfElf())
            for (int a = 0; a < rpgc::ABILITY_COUNT; ++a) if (m_halfElfBonus[a]) b[a] += 1;
        return b;
    }

    // Put the highest rolls in the abilities this class cares about most.
    void autoAssignForClass() {
        auto pr = rpgc::classAbilityPriority(rpgc::classOptions()[m_classIdx]);
        std::array<int, 6> order = {0, 1, 2, 3, 4, 5};
        std::sort(order.begin(), order.end(), [&](int a, int b) { return m_rolled[a] > m_rolled[b]; });
        for (int i = 0; i < 6; ++i) m_assign[pr[i]] = order[i];
    }
    static int classHitDie(const std::string& cls) {
        std::string s; for (char c : cls) s += static_cast<char>(std::tolower((unsigned char)c));
        if (s == "barbarian") return 12;
        if (s == "fighter" || s == "paladin" || s == "ranger") return 10;
        if (s == "sorcerer" || s == "wizard") return 6;
        return 8;   // d8: bard, cleric, druid, monk, rogue, warlock
    }
    // Turn the wizard choices into the player's character + name the world token.
    void finishCharCreate() {
        m_pc = rpgc::Character{};
        m_pc.name = m_nameBuf;
        m_pc.race = rpgc::raceOptions()[m_raceIdx];
        m_pc.className = rpgc::classOptions()[m_classIdx];
        m_pc.background = rpgc::backgroundOptions()[m_bgIdx];
        m_pc.alignment = rpgc::alignmentOptions()[m_alignIdx];
        m_pc.gender = m_female ? "Female" : "Male";
        if (rpgw::isHouseRace(m_pc.race)) {
            if (m_houseIdx < 0) assignHouse();
            m_pc.house = rpgw::houses()[m_houseIdx].name;
            m_pc.standing = m_houseStanding;
            m_pc.surname = m_family.pcSurname;
            m_pc.ironLegacy = m_family.legacyName;
            m_pc.origin.clear();
        } else if (rpgw::isDrow(m_pc.race)) {      // drow: the Sundered
            m_pc.house.clear(); m_pc.standing.clear(); m_pc.surname.clear(); m_pc.ironLegacy.clear();
            m_pc.origin = rpgw::sunderedBlurb();
        } else if (rpgw::isElf(m_pc.race)) {       // elves: an Aelvarin lineage
            if (m_elfIdx < 0) assignElfLineage();
            m_pc.house = rpgw::elfHouses()[m_elfIdx].name;
            m_pc.standing = rpgw::strandName(rpgw::elfHouses()[m_elfIdx].strand);
            m_pc.surname.clear(); m_pc.ironLegacy.clear();
            m_pc.origin = "Of Aelvarin, the Verdant Reaches";
        } else if (rpgw::isDwarf(m_pc.race)) {     // dwarves: a Kadmurn clan
            if (m_dwarfIdx < 0) assignDwarfClan();
            m_pc.house = rpgw::dwarfClans()[m_dwarfIdx].name;
            m_pc.standing = rpgw::dstrandName(rpgw::dwarfClans()[m_dwarfIdx].strand);
            m_pc.surname.clear(); m_pc.ironLegacy.clear();
            m_pc.origin = "Of Kadmurn, the Deep Holds";
        } else if (rpgw::isDragonborn(m_pc.race)) { // dragonborn: a Vharok clan
            if (m_dragonIdx < 0) assignDragonClan();
            const auto& dc = rpgw::dragonClans()[m_dragonIdx];
            m_pc.house = dc.name;
            m_pc.standing = std::string(dc.ancestry) + " dragon-blood (" + dc.breath + " breath)";
            m_pc.surname.clear(); m_pc.ironLegacy.clear();
            m_pc.origin = "Of Vharok, the ember-clans";
        } else if (rpgw::isFey(m_pc.race)) {       // gnomes/halflings: an Evermere house
            if (m_feyIdx < 0) assignFeyHouse();
            m_pc.house = rpgw::feyHouses()[m_feyIdx].name;
            m_pc.standing = "of Evermere, the Bright Court";
            m_pc.surname.clear(); m_pc.ironLegacy.clear();
            m_pc.origin = "Of Evermere, the fey Bright Court (not of this world)";
        } else if (rpgw::isTiefling(m_pc.race)) {   // tieflings: infernal house, or a mortal-line scandal
            m_pc.surname.clear(); m_pc.ironLegacy.clear();
            if (m_houseIdx >= 0) {
                m_pc.house = rpgw::houses()[m_houseIdx].name;
                m_pc.standing = m_houseStanding;
                m_pc.origin = "Infernal blood - a scandal to a mortal line";
            } else {
                if (m_infernalIdx < 0) assignInfernalHouse();
                m_pc.house = rpgw::infernalHouses()[m_infernalIdx].name;
                m_pc.standing = rpgw::infernalTierName(rpgw::infernalHouses()[m_infernalIdx].tier);
                m_pc.origin = "Of the Brass Reach, the Iron Hells";
            }
        } else {                                   // other outsiders: an Origin, no house
            if (m_origin.homeland.empty()) assignHouse();
            m_pc.house.clear(); m_pc.standing.clear(); m_pc.surname.clear(); m_pc.ironLegacy.clear();
            m_pc.origin = m_origin.blurb;
        }
        m_pc.level = 1;
        auto rb = effectiveRaceBonus();
        for (int a = 0; a < 6; ++a) m_pc.abilities[a] = m_rolled[m_assign[a]] + rb[a];
        m_pc.hitDieSize = classHitDie(m_pc.className);
        m_pc.hitDiceTotal = 1;
        m_pc.maxHP = m_pc.curHP = m_pc.hitDieSize + m_pc.mod(rpgc::CON);   // level-1 max hit die + CON
        m_pc.speed = 30;
        m_pc.armorClass = 10 + m_pc.mod(rpgc::DEX);
        // Class proficiencies: fixed saving throws + chosen skills.
        auto cp = rpgc::classProficiencies(m_pc.className);
        m_pc.saveProf[cp.save1] = true;
        m_pc.saveProf[cp.save2] = true;
        for (int i = 0; i < 18; ++i) m_pc.skillProf[i] = m_skillPick[i];
        // Background grants two more fixed skill proficiencies (on top of class skills).
        auto bi = rpgc::backgroundInfo(m_pc.background);
        m_pc.skillProf[bi.skill1] = true;
        m_pc.skillProf[bi.skill2] = true;
        // One-time starting wealth by class (rolled), so there's coin to spend at Orlen.
        auto sw = rpgc::startingWealthForClass(m_pc.className);
        if (sw.known && !m_pc.startingWealthTaken) {
            int sum = 0; for (int i = 0; i < sw.d4count; ++i) sum += rollDie(4);
            m_pc.startingWealthGp = sum * sw.mult;
            m_pc.gold += m_pc.startingWealthGp;
            m_pc.startingWealthTaken = true;
            std::cerr << "starting wealth: " << m_pc.className << " " << sw.d4count << "d4x"
                      << sw.mult << " = " << m_pc.startingWealthGp << " gp\n";
        }
        // Personality scores (rolled with the abilities, shown in the creator).
        m_pc.bravery = m_bravery;
        m_pc.narcissism = m_narcissism;
        m_pc.willToPower = m_willToPower;
        m_pc.carnality = m_carnality;
        m_pc.cruelty = m_cruelty;
        m_pc.sociability = m_sociability;
        m_pc.skepticism = m_skepticism;
        m_pc.honor = m_honor; m_pc.piety = m_piety; m_pc.greed = m_greed; m_pc.temper = m_temper;
        m_pc.diligence = m_diligence; m_pc.compassion = m_compassion; m_pc.curiosity = m_curiosity;
        if (m_selectedPortrait >= 0 && m_selectedPortrait < (int)m_portraits.size())
            m_pc.portraitPath = m_portraits[m_selectedPortrait].path;
        int pt = playerTokenIndex();
        if (pt >= 0) m_tokens[pt].name = m_pc.name;
        std::cerr << "created: " << m_pc.name << " the " << m_pc.race << " " << m_pc.className
                  << " (HP " << m_pc.maxHP << ", AC " << m_pc.armorClass
                  << ", bravery " << m_pc.bravery << "/" << rpgc::braveryTier(m_pc.bravery).name
                  << ", +13 temperament traits)\n";
        m_screen = Screen::Game;
        stopTitleMusic();
    }

    // ----- portrait gallery -----
    static std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
        return s;
    }
    // Map a race (incl. subraces) to its portrait folder.
    static std::string baseRaceFolder(const std::string& race) {
        auto has = [&](const char* s) { return race.find(s) != std::string::npos; };
        if (has("Half-Elf")) return "half-elf";
        if (has("Half-Orc")) return "half-orc";
        if (has("Dwarf"))    return "dwarf";
        if (has("Elf") || has("Drow")) return "elf";
        if (has("Halfling")) return "halfling";
        if (has("Gnome"))    return "gnome";
        if (has("Dragonborn")) return "dragonborn";
        if (has("Tiefling")) return "tiefling";
        if (has("Human"))    return "human";
        return lower(race);
    }

    // Load an image file into a Vulkan texture + ImGui descriptor (mirrors the
    // editor's ImageReferences pattern).
    bool loadPortraitTexture(const std::string& path, Portrait& p) {
        int w, h, ch;
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
        if (!pixels) return false;
        p.w = w; p.h = h;
        VkDevice device = getContext().getDevice();
        VkDeviceSize sz = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer sbuf; VkDeviceMemory smem;
        getContext().createBuffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sbuf, smem);
        void* data; vkMapMemory(device, smem, 0, sz, 0, &data);
        std::memcpy(data, pixels, sz); vkUnmapMemory(device, smem);
        stbi_image_free(pixels);

        VkImageCreateInfo ii{}; ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_SRGB;
        ii.extent = {(uint32_t)w, (uint32_t)h, 1}; ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(device, &ii, nullptr, &p.image);
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(device, p.image, &mr);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = getContext().findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &ai, nullptr, &p.memory);
        vkBindImageMemory(device, p.image, p.memory, 0);

        VkCommandBuffer cmd = getContext().beginSingleTimeCommands();
        VkImageMemoryBarrier b{}; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = p.image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy rg{}; rg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        rg.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
        vkCmdCopyBufferToImage(cmd, sbuf, p.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        getContext().endSingleTimeCommands(cmd);
        vkDestroyBuffer(device, sbuf, nullptr); vkFreeMemory(device, smem, nullptr);

        VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = p.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_SRGB;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vi, nullptr, &p.view);
        VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &si, nullptr, &p.sampler);
        p.descriptor = ImGui_ImplVulkan_AddTexture(p.sampler, p.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    }

    void scanPortraits() {
        if (m_portraitsScanned) return;
        m_portraitsScanned = true;
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path root = "assets/portraits";
        if (!fs::exists(root, ec)) return;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string ext = lower(it->path().extension().string());
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".webp" && ext != ".bmp") continue;
            Portrait p; p.path = it->path().string();
            fs::path rel = fs::relative(it->path(), root, ec);
            if (rel.has_parent_path()) p.race = lower(rel.begin()->string());
            std::string lp = lower(p.path);
            if (lp.find("female") != std::string::npos) p.gender = "female";
            else if (lp.find("male") != std::string::npos) p.gender = "male";
            if (loadPortraitTexture(p.path, p)) m_portraits.push_back(std::move(p));
        }
        std::cerr << "portraits: loaded " << m_portraits.size() << " from assets/portraits/\n";
    }

    void destroyPortraits() {
        VkDevice device = getContext().getDevice();
        for (auto& p : m_portraits) {
            if (p.descriptor) ImGui_ImplVulkan_RemoveTexture(p.descriptor);
            if (p.sampler) vkDestroySampler(device, p.sampler, nullptr);
            if (p.view) vkDestroyImageView(device, p.view, nullptr);
            if (p.image) vkDestroyImage(device, p.image, nullptr);
            if (p.memory) vkFreeMemory(device, p.memory, nullptr);
        }
        m_portraits.clear();
    }

    // Native file chooser (zenity) -> load as a portrait, return its index or -1.
    int uploadPortrait() {
        FILE* pipe = popen("zenity --file-selection --title='Choose a portrait image' "
                           "--file-filter='Images | *.png *.jpg *.jpeg *.webp *.bmp' 2>/dev/null", "r");
        if (!pipe) return -1;
        char buf[1024] = {0};
        char* got = fgets(buf, sizeof buf, pipe);
        pclose(pipe);
        if (!got) return -1;
        std::string path(buf);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
        if (path.empty()) return -1;
        Portrait p; p.path = path;
        std::string lp = lower(path);
        if (lp.find("female") != std::string::npos) p.gender = "female";
        else if (lp.find("male") != std::string::npos) p.gender = "male";
        if (!loadPortraitTexture(path, p)) return -1;
        m_portraits.push_back(std::move(p));
        return static_cast<int>(m_portraits.size()) - 1;
    }

    // Fit an image to a box while preserving its aspect ratio.
    static ImVec2 fitBox(const Portrait& p, float maxW, float maxH) {
        float w = p.w > 0 ? (float)p.w : 1.0f, h = p.h > 0 ? (float)p.h : 1.0f;
        float s = std::min(maxW / w, maxH / h);
        return ImVec2(w * s, h * s);
    }

    void renderPortraitGallery() {
        ImGui::TextUnformatted("Portrait");
        ImGui::SameLine(); ImGui::Checkbox("All races", &m_showAllPortraits);
        ImGui::SameLine();
        if (ImGui::Button("Upload...")) { int i = uploadPortrait(); if (i >= 0) m_previewPortrait = i; }

        std::string wantRace = baseRaceFolder(rpgc::raceOptions()[m_raceIdx]);
        const float panelH = 300.0f;

        // ---- left: thumbnail grid ----
        ImGui::BeginChild("##galleryGrid", ImVec2(320, panelH), true);
        const float cell = 135.0f;      // thumbnail box (aspect-fit within)
        int shown = 0;
        for (int i = 0; i < static_cast<int>(m_portraits.size()); ++i) {
            Portrait& p = m_portraits[i];
            bool raceOk = m_showAllPortraits || p.race.empty() || p.race == wantRace;
            if (!raceOk) continue;
            if (shown % 2 != 0) ImGui::SameLine();
            ++shown;
            ImGui::PushID(i);
            ImGui::Image((ImTextureID)p.descriptor, fitBox(p, cell, cell));
            if (ImGui::IsItemClicked()) m_previewPortrait = i;
            ImU32 border = 0;
            if (i == m_selectedPortrait)      border = IM_COL32(90, 220, 120, 255);  // green = chosen
            else if (i == m_previewPortrait)  border = IM_COL32(255, 210, 90, 255);   // gold = previewing
            if (border)
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                    border, 0.0f, 0, 3.0f);
            ImGui::PopID();
        }
        if (shown == 0)
            ImGui::TextDisabled("No portraits here yet -\ndrop images in\nassets/portraits/%s/\n(or use Upload).",
                                wantRace.c_str());
        ImGui::EndChild();

        // ---- right: large preview + confirm ----
        ImGui::SameLine();
        ImGui::BeginChild("##galleryPreview", ImVec2(0, panelH), true);
        int pv = m_previewPortrait;
        if (pv >= 0 && pv < static_cast<int>(m_portraits.size())) {
            Portrait& p = m_portraits[pv];
            float availW = ImGui::GetContentRegionAvail().x;
            ImVec2 sz = fitBox(p, availW, panelH - 70.0f);
            float indent = (availW - sz.x) * 0.5f;
            if (indent > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            ImGui::Image((ImTextureID)p.descriptor, sz);
            if (pv == m_selectedPortrait) {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "  In use");
            } else {
                if (ImGui::Button("Use This Portrait", ImVec2(-1, 0))) m_selectedPortrait = pv;
            }
        } else {
            ImGui::TextDisabled("Click a portrait to\npreview it here, then\nconfirm your choice.");
        }
        ImGui::EndChild();
    }

    // ----- house & heraldry -----
    static ImU32 tinctureCol(rpgw::Tincture t) {
        auto c = rpgw::tinctureRGB(t); return IM_COL32(c.r, c.g, c.b, 255);
    }
    // Draw a heraldic charge (emblematic sigil) centered at c, half-size s.
    void drawCharge(ImDrawList* dl, ImVec2 c, float s, const std::string& name, ImU32 col) {
        auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
        float th = s * 0.20f;
        auto polyf = [&](std::vector<ImVec2> v) { dl->AddConvexPolyFilled(v.data(), (int)v.size(), col); };
        auto pl = [&](std::vector<ImVec2> v) { dl->AddPolyline(v.data(), (int)v.size(), col, 0, th); };
        auto disc = [&](float x, float y, float r) { dl->AddCircleFilled(P(x, y), r * s, col, 24); };
        auto tri = [&](ImVec2 a, ImVec2 b, ImVec2 d) { dl->AddTriangleFilled(a, b, d, col); };
        auto quad = [&](ImVec2 a, ImVec2 b, ImVec2 d, ImVec2 e) { dl->AddQuadFilled(a, b, d, e, col); };
        if (name == "anvil") {
            quad(P(-0.75f,-0.20f),P(0.55f,-0.20f),P(0.55f,0.12f),P(-0.75f,0.12f));
            tri(P(0.55f,-0.20f),P(0.92f,-0.02f),P(0.55f,0.12f));
            quad(P(-0.16f,0.12f),P(0.16f,0.12f),P(0.16f,0.42f),P(-0.16f,0.42f));
            quad(P(-0.50f,0.42f),P(0.50f,0.42f),P(0.66f,0.66f),P(-0.66f,0.66f));
        } else if (name == "hammer") {
            quad(P(-0.55f,-0.50f),P(0.55f,-0.50f),P(0.55f,-0.12f),P(-0.55f,-0.12f));
            quad(P(-0.13f,-0.12f),P(0.13f,-0.12f),P(0.13f,0.62f),P(-0.13f,0.62f));
        } else if (name == "tower") {
            quad(P(-0.45f,-0.20f),P(0.45f,-0.20f),P(0.45f,0.62f),P(-0.45f,0.62f));
            quad(P(-0.45f,-0.45f),P(-0.20f,-0.45f),P(-0.20f,-0.20f),P(-0.45f,-0.20f));
            quad(P(-0.12f,-0.45f),P(0.12f,-0.45f),P(0.12f,-0.20f),P(-0.12f,-0.20f));
            quad(P(0.20f,-0.45f),P(0.45f,-0.45f),P(0.45f,-0.20f),P(0.20f,-0.20f));
        } else if (name == "dagger") {
            tri(P(0.0f,-0.72f),P(0.16f,-0.10f),P(-0.16f,-0.10f));
            quad(P(-0.42f,-0.10f),P(0.42f,-0.10f),P(0.42f,0.02f),P(-0.42f,0.02f));
            quad(P(-0.10f,0.02f),P(0.10f,0.02f),P(0.10f,0.50f),P(-0.10f,0.50f));
            disc(0.0f,0.58f,0.12f);
        } else if (name == "flame") {
            polyf({P(0.0f,-0.75f),P(0.20f,-0.35f),P(0.40f,0.10f),P(0.34f,0.46f),P(0.14f,0.68f),
                   P(-0.14f,0.68f),P(-0.34f,0.46f),P(-0.40f,0.10f),P(-0.20f,-0.35f)});
        } else if (name == "oak") {
            disc(0.0f,-0.22f,0.50f); disc(-0.36f,-0.02f,0.30f); disc(0.36f,-0.02f,0.30f);
            quad(P(-0.12f,0.20f),P(0.12f,0.20f),P(0.12f,0.70f),P(-0.12f,0.70f));
        } else if (name == "wheatsheaf") {
            pl({P(0.0f,0.60f),P(0.0f,-0.62f)}); pl({P(0.0f,0.60f),P(-0.30f,-0.45f)});
            pl({P(0.0f,0.60f),P(0.30f,-0.45f)}); pl({P(0.0f,0.60f),P(-0.52f,-0.18f)});
            pl({P(0.0f,0.60f),P(0.52f,-0.18f)});
            quad(P(-0.32f,0.12f),P(0.32f,0.12f),P(0.32f,0.30f),P(-0.32f,0.30f));
        } else if (name == "gull") {
            pl({P(-0.72f,0.12f),P(-0.34f,-0.32f),P(0.0f,0.14f),P(0.34f,-0.32f),P(0.72f,0.12f)});
        } else if (name == "serpent") {
            pl({P(-0.45f,0.62f),P(-0.45f,0.15f),P(0.12f,-0.02f),P(0.12f,-0.40f),P(-0.28f,-0.55f)});
            tri(P(-0.50f,-0.66f),P(-0.12f,-0.58f),P(-0.36f,-0.34f));
        } else if (name == "stag") {
            disc(0.0f,0.34f,0.28f);
            pl({P(-0.12f,0.12f),P(-0.30f,-0.22f),P(-0.52f,-0.46f)}); pl({P(-0.32f,-0.20f),P(-0.16f,-0.36f)});
            pl({P(0.12f,0.12f),P(0.30f,-0.22f),P(0.52f,-0.46f)}); pl({P(0.32f,-0.20f),P(0.16f,-0.36f)});
            disc(-0.22f,0.16f,0.09f); disc(0.22f,0.16f,0.09f);
        } else if (name == "kraken") {
            disc(0.0f,-0.12f,0.42f);
            pl({P(-0.30f,0.20f),P(-0.40f,0.45f),P(-0.30f,0.66f)}); pl({P(-0.10f,0.28f),P(-0.14f,0.55f),P(-0.08f,0.72f)});
            pl({P(0.10f,0.28f),P(0.14f,0.55f),P(0.08f,0.72f)}); pl({P(0.30f,0.20f),P(0.40f,0.45f),P(0.30f,0.66f)});
        } else if (name == "raven") {
            disc(-0.05f,0.16f,0.34f);
            tri(P(-0.62f,0.02f),P(-0.28f,0.34f),P(-0.28f,-0.02f));
            disc(0.34f,-0.12f,0.20f);
            tri(P(0.50f,-0.15f),P(0.78f,-0.06f),P(0.50f,0.03f));
        } else if (name == "star") {                                  // elven: 4-point star
            tri(P(0.0f,-0.9f),P(-0.16f,0.0f),P(0.16f,0.0f));
            tri(P(0.9f,0.0f),P(0.0f,-0.16f),P(0.0f,0.16f));
            tri(P(0.0f,0.9f),P(-0.16f,0.0f),P(0.16f,0.0f));
            tri(P(-0.9f,0.0f),P(0.0f,-0.16f),P(0.0f,0.16f));
            disc(0.0f,0.0f,0.12f);
        } else if (name == "crescent") {
            pl({P(0.45f,-0.62f),P(-0.12f,-0.72f),P(-0.58f,-0.28f),P(-0.62f,0.28f),P(-0.2f,0.68f),P(0.42f,0.6f)});
        } else if (name == "harp") {
            pl({P(-0.42f,0.7f),P(-0.56f,0.18f),P(-0.34f,-0.42f),P(0.22f,-0.6f),P(0.46f,0.02f),P(0.5f,0.7f)});
            pl({P(-0.12f,-0.38f),P(-0.03f,0.62f)}); pl({P(0.12f,-0.42f),P(0.2f,0.6f)});
        } else if (name == "leaf") {
            polyf({P(0.0f,-0.85f),P(0.4f,-0.2f),P(0.5f,0.2f),P(0.28f,0.6f),P(0.0f,0.82f),
                   P(-0.28f,0.6f),P(-0.5f,0.2f),P(-0.4f,-0.2f)});
        } else if (name == "thorn") {
            pl({P(-0.55f,0.72f),P(-0.15f,0.35f),P(0.0f,-0.1f),P(0.0f,-0.5f),P(0.35f,-0.7f)});
            pl({P(0.0f,-0.1f),P(0.3f,-0.04f)}); pl({P(0.0f,-0.42f),P(-0.3f,-0.52f)});
            pl({P(-0.15f,0.35f),P(-0.42f,0.22f)});
        } else if (name == "comet") {
            tri(P(0.35f,-0.72f),P(0.27f,-0.35f),P(0.43f,-0.35f));
            tri(P(0.72f,-0.35f),P(0.35f,-0.43f),P(0.35f,-0.27f));
            tri(P(0.35f,0.02f),P(0.27f,-0.35f),P(0.43f,-0.35f));
            tri(P(-0.02f,-0.35f),P(0.35f,-0.43f),P(0.35f,-0.27f));
            pl({P(0.18f,-0.18f),P(-0.6f,0.66f)});
        } else if (name == "mountain") {                              // dwarven: peaks
            tri(P(0.0f,-0.62f),P(-0.5f,0.52f),P(0.5f,0.52f));
            tri(P(-0.5f,-0.18f),P(-0.86f,0.52f),P(-0.14f,0.52f));
            tri(P(0.5f,-0.18f),P(0.14f,0.52f),P(0.86f,0.52f));
        } else if (name == "pick") {
            pl({P(-0.72f,-0.3f),P(-0.35f,-0.5f),P(0.0f,-0.54f),P(0.35f,-0.5f),P(0.72f,-0.3f)});
            quad(P(-0.09f,-0.5f),P(0.09f,-0.5f),P(0.09f,0.62f),P(-0.09f,0.62f));
        } else if (name == "axe") {
            quad(P(-0.12f,-0.7f),P(0.05f,-0.7f),P(0.05f,0.7f),P(-0.12f,0.7f));
            polyf({P(0.05f,-0.58f),P(0.5f,-0.48f),P(0.66f,-0.12f),P(0.5f,0.28f),P(0.05f,0.2f)});
        } else if (name == "gem") {
            polyf({P(0.0f,-0.8f),P(0.55f,-0.05f),P(0.0f,0.8f),P(-0.55f,-0.05f)});
        } else if (name == "rune") {
            pl({P(-0.34f,-0.7f),P(-0.34f,0.7f)});
            pl({P(-0.34f,-0.38f),P(0.32f,-0.7f)});
            pl({P(-0.34f,-0.06f),P(0.32f,-0.38f)});
            pl({P(-0.34f,-0.06f),P(0.36f,0.52f)});
        } else if (name == "tankard") {
            quad(P(-0.4f,-0.5f),P(0.4f,-0.5f),P(0.32f,0.66f),P(-0.32f,0.66f));
            pl({P(0.4f,-0.28f),P(0.74f,-0.12f),P(0.74f,0.28f),P(0.4f,0.36f)});
        } else if (name == "bolt") {                                  // dragonborn: lightning
            polyf({P(0.16f,-0.8f),P(-0.4f,0.05f),P(-0.04f,0.05f),P(-0.2f,0.8f),P(0.48f,-0.16f),P(0.08f,-0.16f)});
        } else if (name == "claw") {
            pl({P(-0.44f,0.7f),P(-0.4f,0.1f),P(-0.56f,-0.5f)});
            pl({P(0.0f,0.72f),P(0.04f,0.05f),P(-0.04f,-0.6f)});
            pl({P(0.44f,0.7f),P(0.4f,0.1f),P(0.56f,-0.5f)});
        } else if (name == "sun") {
            disc(0.0f,0.0f,0.36f);
            pl({P(0.0f,-0.55f),P(0.0f,-0.82f)}); pl({P(0.0f,0.55f),P(0.0f,0.82f)});
            pl({P(-0.55f,0.0f),P(-0.82f,0.0f)}); pl({P(0.55f,0.0f),P(0.82f,0.0f)});
            pl({P(-0.4f,-0.4f),P(-0.6f,-0.6f)}); pl({P(0.4f,0.4f),P(0.6f,0.6f)});
            pl({P(0.4f,-0.4f),P(0.6f,-0.6f)}); pl({P(-0.4f,0.4f),P(-0.6f,0.6f)});
        } else if (name == "wing") {
            polyf({P(-0.7f,-0.3f),P(0.2f,-0.2f),P(0.72f,0.5f),P(0.34f,0.42f),P(0.18f,0.62f),
                   P(0.0f,0.4f),P(-0.16f,0.56f),P(-0.3f,0.34f),P(-0.5f,0.44f),P(-0.6f,0.1f)});
        } else if (name == "shield") {
            polyf({P(0.0f,-0.72f),P(0.6f,-0.45f),P(0.6f,0.1f),P(0.0f,0.78f),P(-0.6f,0.1f),P(-0.6f,-0.45f)});
        } else if (name == "coin") {
            disc(0.0f,0.0f,0.6f);
        } else if (name == "lion") {                                  // fey
            disc(0.0f,0.1f,0.34f);
            tri(P(-0.5f,-0.5f),P(-0.05f,-0.1f),P(-0.55f,0.0f));
            tri(P(0.5f,-0.5f),P(0.05f,-0.1f),P(0.55f,0.0f));
            tri(P(-0.3f,0.45f),P(0.0f,0.7f),P(0.3f,0.45f));
        } else if (name == "unicorn") {
            pl({P(-0.35f,0.72f),P(-0.15f,0.1f),P(0.1f,-0.15f),P(0.28f,-0.4f)});
            pl({P(0.28f,-0.4f),P(0.62f,-0.82f)});
            disc(0.14f,-0.28f,0.07f);
        } else if (name == "cog") {
            dl->AddCircle(c, s * 0.32f, col, 24, s * 0.16f);
            for (int k = 0; k < 8; ++k) {
                float a = k * 45.0f * 3.14159265f / 180.0f;
                pl({ImVec2(c.x + cosf(a)*s*0.42f, c.y + sinf(a)*s*0.42f),
                     ImVec2(c.x + cosf(a)*s*0.66f, c.y + sinf(a)*s*0.66f)});
            }
        } else if (name == "mushroom") {
            polyf({P(-0.66f,0.0f),P(-0.4f,-0.4f),P(0.0f,-0.52f),P(0.4f,-0.4f),P(0.66f,0.0f)});
            quad(P(-0.2f,0.0f),P(0.2f,0.0f),P(0.14f,0.6f),P(-0.14f,0.6f));
        } else if (name == "crown") {                                 // infernal
            polyf({P(-0.62f,0.4f),P(-0.62f,-0.2f),P(-0.28f,0.12f),P(0.0f,-0.5f),P(0.28f,0.12f),P(0.62f,-0.2f),P(0.62f,0.4f)});
        } else if (name == "chain") {
            pl({P(-0.55f,0.55f),P(-0.2f,0.2f)}); pl({P(0.2f,-0.2f),P(0.55f,-0.55f)});
            dl->AddCircle(ImVec2(c.x - 0.12f*s, c.y + 0.12f*s), s * 0.24f, col, 20, s * 0.12f);
            dl->AddCircle(ImVec2(c.x + 0.12f*s, c.y - 0.12f*s), s * 0.24f, col, 20, s * 0.12f);
        } else {
            disc(0.0f,0.0f,0.40f);
        }
    }
    void drawShield(ImDrawList* dl, ImVec2 tl, float w, const rpgw::House& h) {
        float ht = w * 1.16f;
        float x0 = tl.x, x1 = tl.x + w, y = tl.y, mid = tl.x + w * 0.5f;
        ImVec2 pts[9] = {
            {x0, y}, {x1, y}, {x1, y + 0.46f * ht},
            {x0 + 0.86f * w, y + 0.72f * ht}, {x0 + 0.62f * w, y + 0.92f * ht}, {mid, y + ht},
            {x0 + 0.38f * w, y + 0.92f * ht}, {x0 + 0.14f * w, y + 0.72f * ht}, {x0, y + 0.46f * ht}
        };
        dl->AddConvexPolyFilled(pts, 9, tinctureCol(h.field));
        ImU32 border = (h.rank == rpgw::ROYAL) ? IM_COL32(194, 160, 107, 255) : IM_COL32(74, 84, 95, 255);
        dl->AddPolyline(pts, 9, border, ImDrawFlags_Closed, 2.5f);
        drawCharge(dl, ImVec2(mid, y + 0.5f * ht), w * 0.30f, h.charge, tinctureCol(h.chargeColor));
    }
    // Elven emblems are drawn in a round medallion, not stamped on a war-shield.
    void drawMedallion(ImDrawList* dl, ImVec2 c, float radius, const std::string& sigil, ImU32 col) {
        dl->AddCircleFilled(c, radius, IM_COL32(22, 24, 34, 255), 44);
        dl->AddCircle(c, radius, IM_COL32(84, 76, 112, 255), 44, 1.6f);
        drawCharge(dl, c, radius * 0.60f, sigil, col);
    }
    // Dwarven marks sit in a gem-cut hexagon, forge-branded into stone.
    void drawCartouche(ImDrawList* dl, ImVec2 c, float radius, const std::string& sigil, ImU32 col) {
        ImVec2 hex[6];
        for (int i = 0; i < 6; ++i) {
            float a = 3.14159265f / 180.0f * (60.0f * i - 90.0f);
            hex[i] = ImVec2(c.x + radius * cosf(a), c.y + radius * sinf(a));
        }
        dl->AddConvexPolyFilled(hex, 6, IM_COL32(27, 20, 13, 255));
        dl->AddPolyline(hex, 6, IM_COL32(90, 74, 49, 255), ImDrawFlags_Closed, 2.0f);
        drawCharge(dl, c, radius * 0.55f, sigil, col);
    }
    // Dragonborn marks sit in a dragon-scale lozenge, tinted by the blood.
    void drawScale(ImDrawList* dl, ImVec2 c, float halfW, const std::string& sigil, rpgw::RGB rgb) {
        float halfH = halfW * 1.15f;
        ImVec2 pts[4] = {{c.x, c.y - halfH}, {c.x + halfW, c.y}, {c.x, c.y + halfH}, {c.x - halfW, c.y}};
        ImU32 col = IM_COL32(rgb.r, rgb.g, rgb.b, 255);
        dl->AddConvexPolyFilled(pts, 4, IM_COL32(20, 14, 13, 255));
        dl->AddPolyline(pts, 4, col, ImDrawFlags_Closed, 2.2f);
        drawCharge(dl, c, halfW * 0.5f, sigil, col);
    }
    // Fey emblems bloom bright on vellum, ringed with radiant gold.
    void drawBloom(ImDrawList* dl, ImVec2 c, float r, const std::string& sigil, rpgw::RGB rgb) {
        for (int i = 0; i < 16; ++i) {
            float a = i * 22.5f * 3.14159265f / 180.0f;
            dl->AddLine(ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r),
                        ImVec2(c.x + cosf(a) * (r + 5.0f), c.y + sinf(a) * (r + 5.0f)),
                        IM_COL32(200, 153, 47, 255), 1.4f);
        }
        dl->AddCircleFilled(c, r, IM_COL32(248, 242, 214, 255), 40);   // bright vellum
        dl->AddCircle(c, r, IM_COL32(156, 118, 32, 255), 40, 1.8f);
        drawCharge(dl, c, r * 0.60f, sigil, IM_COL32(rgb.r, rgb.g, rgb.b, 255));
    }
    // Infernal marks sit in a horned seal, branded in brass and blood.
    void drawHornedSeal(ImDrawList* dl, ImVec2 c, float r, const std::string& sigil, rpgw::RGB rgb) {
        ImU32 col = IM_COL32(rgb.r, rgb.g, rgb.b, 255);
        dl->AddBezierQuadratic(ImVec2(c.x - r * 0.55f, c.y - r * 0.6f), ImVec2(c.x - r * 1.0f, c.y - r * 1.35f),
                               ImVec2(c.x - r * 0.5f, c.y - r * 1.15f), col, 2.6f);
        dl->AddBezierQuadratic(ImVec2(c.x + r * 0.55f, c.y - r * 0.6f), ImVec2(c.x + r * 1.0f, c.y - r * 1.35f),
                               ImVec2(c.x + r * 0.5f, c.y - r * 1.15f), col, 2.6f);
        dl->AddCircleFilled(c, r, IM_COL32(18, 13, 22, 255), 40);
        dl->AddCircle(c, r, col, 40, 1.8f);
        drawCharge(dl, c, r * 0.56f, sigil, col);
    }

    // Your rung within the house, flavored by background.
    std::string standingFor(const std::string& bg, rpgw::Rank rank) {
        if (rank == rpgw::GREAT || rank == rpgw::ROYAL || bg == "Noble") return "highborn of";
        if (bg == "Soldier")       return "a sworn sword of";
        if (bg == "Criminal")      return "disavowed by";
        if (bg == "Urchin")        return "a bastard of";
        if (bg == "Charlatan")     return "a false claimant to";
        if (bg == "Folk Hero")     return "risen from the smallfolk of";
        if (bg == "Outlander")     return "an estranged child of";
        if (bg == "Acolyte")       return "pledged to the Iron Temple by";
        if (bg == "Hermit")        return "an exile of";
        if (bg == "Sage")          return "a scholar in service to";
        if (bg == "Guild Artisan") return "a guild-sworn of";
        if (bg == "Entertainer")   return "a minstrel of";
        if (bg == "Sailor")        return "a deckhand of";
        return "sworn to";
    }
    void genFamily() {
        if (m_houseIdx < 0) return;
        m_family = rpgw::generateFamily(rpgw::houses()[m_houseIdx],
                                        rpgc::backgroundOptions()[m_bgIdx], m_female, m_rng);
    }
    // Pick an elven lineage by subrace: High Elf -> Court, Wood Elf -> Wild, Elf -> any.
    void assignElfLineage() {
        std::string race = rpgc::raceOptions()[m_raceIdx];
        if (rpgw::isDrow(race)) { m_elfIdx = -1; return; }     // drow are the Sundered - no lineage
        std::vector<int> pool =
            race == "High Elf" ? rpgw::elfHouseIndicesByStrand(rpgw::COURT, false)
          : race == "Wood Elf" ? rpgw::elfHouseIndicesByStrand(rpgw::WILD, false)
                               : rpgw::elfHouseIndicesAll(false);
        if (pool.empty()) { m_elfIdx = 0; return; }
        std::uniform_int_distribution<int> d(0, static_cast<int>(pool.size()) - 1);
        m_elfIdx = pool[d(m_rng)];
    }
    // Pick a dwarven clan by subrace: Mountain -> Deep, Hill -> Hill, Dwarf -> any.
    void assignDwarfClan() {
        std::string race = rpgc::raceOptions()[m_raceIdx];
        std::vector<int> pool =
            race == "Mountain Dwarf" ? rpgw::dwarfClanIndicesByStrand(rpgw::DEEP, false)
          : race == "Hill Dwarf"     ? rpgw::dwarfClanIndicesByStrand(rpgw::HILL, false)
                                     : rpgw::dwarfClanIndicesAll(false);
        if (pool.empty()) { m_dwarfIdx = 0; return; }
        std::uniform_int_distribution<int> d(0, static_cast<int>(pool.size()) - 1);
        m_dwarfIdx = pool[d(m_rng)];
    }
    // Dragonborn: a clan of any bloodline (they have no subraces here).
    void assignDragonClan() {
        std::vector<int> pool = rpgw::dragonClanIndicesAll(false);   // exclude the eldest clan
        if (pool.empty()) { m_dragonIdx = 0; return; }
        std::uniform_int_distribution<int> d(0, static_cast<int>(pool.size()) - 1);
        m_dragonIdx = pool[d(m_rng)];
    }
    // Fey house for gnomes/halflings; infernal house for tieflings.
    void assignFeyHouse() {
        std::string race = rpgc::raceOptions()[m_raceIdx];
        auto pool = rpgw::feyHousesForRace(race.find("Gnome") != std::string::npos,
                                           race.find("Halfling") != std::string::npos);
        if (pool.empty()) { m_feyIdx = 0; return; }
        std::uniform_int_distribution<int> d(0, static_cast<int>(pool.size()) - 1);
        m_feyIdx = pool[d(m_rng)];
    }
    void assignInfernalHouse() {
        auto pool = rpgw::infernalHousesAll(false);
        std::uniform_int_distribution<int> d(0, static_cast<int>(pool.size()) - 1);
        m_infernalIdx = pool[d(m_rng)];
    }

    // Assign the character's origin by race: an Aldermarch House (humans/half-bloods),
    // an Aelvarin lineage (elves; Sundered for drow), a Kadmurn clan (dwarves), a
    // Vharok clan (dragonborn), an Evermere house (gnomes/halflings), an infernal
    // house or a mortal-line scandal (tieflings), else a generic Origin.
    void assignHouse() {
        std::string race = rpgc::raceOptions()[m_raceIdx];
        m_houseIdx = -1; m_elfIdx = -1; m_dwarfIdx = -1; m_dragonIdx = -1; m_feyIdx = -1; m_infernalIdx = -1;
        m_houseStanding.clear(); m_family = rpgw::Family{};

        if (rpgw::isElf(race))       { assignElfLineage();  return; }
        if (rpgw::isDwarf(race))     { assignDwarfClan();   return; }
        if (rpgw::isDragonborn(race)){ assignDragonClan();  return; }
        if (rpgw::isFey(race))       { assignFeyHouse();    return; }
        if (rpgw::isTiefling(race)) {
            std::uniform_int_distribution<int> coin(0, 1);
            if (coin(m_rng) == 0) {                          // born to a mortal line - a scandal
                auto pool = rpgw::lesserHouseIndices();
                std::uniform_int_distribution<int> d(0, static_cast<int>(pool.size()) - 1);
                m_houseIdx = pool[d(m_rng)];
                m_houseStanding = "an infernal-blooded scandal to";
            } else {                                          // claimed by an infernal house
                assignInfernalHouse();
            }
            return;
        }
        if (!rpgw::isHouseRace(race)) {                       // other outsiders -> Origin
            m_origin = rpgw::originFor(race, m_rng);
            return;
        }
        bool noble = (rpgc::backgroundOptions()[m_bgIdx] == std::string("Noble"));
        auto pool = noble ? rpgw::greatHouseIndices() : rpgw::lesserHouseIndices();
        if (pool.empty()) { m_houseIdx = 0; }
        else {
            std::uniform_int_distribution<int> d(0, static_cast<int>(pool.size()) - 1);
            m_houseIdx = pool[d(m_rng)];
        }
        m_houseStanding = standingFor(rpgc::backgroundOptions()[m_bgIdx], rpgw::houses()[m_houseIdx].rank);
        genFamily();   // a new house means a new family
    }

    // The dice decide your calling: rank classes by the rolled abilities (with race
    // bonuses) and temperament, and adopt the best fit.
    bool determineClass() {   // returns true if the calling changed
        auto rb = effectiveRaceBonus();
        std::array<int, 6> ab;
        for (int i = 0; i < 6; ++i) ab[i] = m_rolled[i] + rb[i];
        rpgc::Character p;
        p.bravery = m_bravery; p.narcissism = m_narcissism; p.willToPower = m_willToPower;
        p.temper = m_temper; p.carnality = m_carnality; p.cruelty = m_cruelty;
        p.sociability = m_sociability; p.skepticism = m_skepticism; p.honor = m_honor;
        p.piety = m_piety; p.greed = m_greed; p.diligence = m_diligence;
        p.compassion = m_compassion; p.curiosity = m_curiosity;
        m_classRanked = rpgcf::rankClasses(ab, p);
        int idx = 0;
        const auto& opts = rpgc::classOptions();
        for (int i = 0; i < static_cast<int>(opts.size()); ++i)
            if (m_classRanked.front().cls == opts[i]) { idx = i; break; }
        if (idx != m_classIdx) { m_classIdx = idx; m_skillPick.fill(false); return true; }
        return false;
    }

    int backgroundIndex(const std::string& name) const {
        const auto& bg = rpgc::backgroundOptions();
        for (int i = 0; i < static_cast<int>(bg.size()); ++i)
            if (name == bg[i]) return i;
        return 0;
    }
    // In the roll-down system the background is not chosen: after the class is
    // fixed, a 25% roll makes you Noble (and eligible for the better houses),
    // otherwise you get a background that suits the class the dice gave you.
    void allotBackground() {
        std::uniform_int_distribution<int> pct(1, 100);
        if (pct(m_rng) <= 25) {
            m_bgIdx = backgroundIndex("Noble");
        } else {
            auto fits = rpgc::classBackgrounds(rpgc::classOptions()[m_classIdx]);
            std::uniform_int_distribution<int> d(0, static_cast<int>(fits.size()) - 1);
            m_bgIdx = backgroundIndex(fits[d(m_rng)]);
        }
    }
    // Generate a complete character from the dice (the "Roll a Random Hero" button;
    // also the seed of later random-NPC generation). Fills every choice; the player
    // can still edit afterward.
    void fullRandom() {
        std::uniform_int_distribution<int> coin(0, 1);
        m_female = (coin(m_rng) == 1);
        rollAbilityScores();               // fresh six + personality
        determineClass();                  // best-fit class from the rolls
        autoAssignForClass();              // slot the rolls by the class's priorities
        allotBackground();                 // 25% Noble, else a class-fitting past
        if (isHalfElf()) {                 // Half-Elf's two +1s, at random
            m_halfElfBonus.fill(false);
            std::array<int, 6> ord = {0, 1, 2, 3, 4, 5};
            std::shuffle(ord.begin(), ord.end(), m_rng);
            int n = 0; for (int a : ord) { if (a == rpgc::CHA) continue; m_halfElfBonus[a] = true; if (++n == 2) break; }
        }
        assignHouse();                     // race + background -> house/lineage/clan + family
        // alignment derived from temperament: honor -> lawful axis, cruelty/compassion -> moral axis
        int lawAxis   = (m_honor >= 13) ? 0 : (m_honor <= 8) ? 2 : 1;         // 0 lawful,1 neutral,2 chaotic
        int moralAxis = (m_cruelty >= 13) ? 2 : (m_compassion >= 13) ? 0 : 1; // 0 good,1 neutral,2 evil
        m_alignIdx = moralAxis * 3 + lawAxis;
        // random class skills
        m_skillPick.fill(false);
        auto cp = rpgc::classProficiencies(rpgc::classOptions()[m_classIdx]);
        std::vector<int> pool = cp.skillList;
        std::shuffle(pool.begin(), pool.end(), m_rng);
        for (int i = 0; i < cp.skillCount && i < (int)pool.size(); ++i) m_skillPick[pool[i]] = true;
        // a random name to fit the gender
        const auto& names = m_female ? rpgw::fdetail::femaleNames() : rpgw::fdetail::maleNames();
        std::uniform_int_distribution<int> nd(0, (int)names.size() - 1);
        std::snprintf(m_nameBuf, sizeof(m_nameBuf), "%s", names[nd(m_rng)].c_str());
        // a random portrait for the race, if any are loaded
        if (!m_portraits.empty()) {
            std::string want = baseRaceFolder(rpgc::raceOptions()[m_raceIdx]);
            std::vector<int> pics;
            for (int i = 0; i < (int)m_portraits.size(); ++i)
                if (m_portraits[i].race.empty() || m_portraits[i].race == want) pics.push_back(i);
            if (!pics.empty()) { std::uniform_int_distribution<int> pd(0, (int)pics.size() - 1); m_selectedPortrait = pics[pd(m_rng)]; }
        }
    }

    void renderHouseCard() {
        if (m_houseIdx < 0) assignHouse();
        const auto& h = rpgw::houses()[m_houseIdx];
        ImGui::TextUnformatted("Your House");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cast lots again")) assignHouse();

        ImGui::BeginChild("##housecard", ImVec2(0, 158), true);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float bw = 96.0f;
        drawShield(ImGui::GetWindowDrawList(), ImVec2(p0.x + 6.0f, p0.y + 4.0f), bw, h);
        ImGui::Dummy(ImVec2(bw + 18.0f, bw * 1.16f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.91f, 0.89f, 0.85f, 1.0f), "%s", h.name);
        ImGui::TextColored(ImVec4(0.76f, 0.63f, 0.42f, 1.0f), "\"%s\"", h.words);
        ImGui::TextWrapped("You are %s %s, of %s in %s.", m_houseStanding.c_str(), h.name, h.seat, h.region);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.76f, 0.63f, 0.42f, 1.0f), "House Gift: %s  (?)", h.trait);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(340.0f);
            ImGui::TextUnformatted(rpgw::giftEffect(h.trait));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::TextWrapped("%s", h.traitDesc);
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::TextDisabled("Blazon: %s   -   sworn to %s",
                            rpgw::blazon(h).c_str(), h.liege[0] ? h.liege : "the Crown itself");
    }

    // Non-house races (outsiders) get an Origin instead of a House card.
    void renderOriginCard() {
        if (m_origin.homeland.empty()) m_origin = rpgw::originFor(rpgc::raceOptions()[m_raceIdx], m_rng);
        ImGui::TextUnformatted("Your Origin");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cast lots again##origin"))
            m_origin = rpgw::originFor(rpgc::raceOptions()[m_raceIdx], m_rng);
        ImGui::BeginChild("##origincard", ImVec2(0, 118), true);
        ImGui::TextColored(ImVec4(0.62f, 0.72f, 0.85f, 1.0f), "%s of %s",
                           rpgc::raceOptions()[m_raceIdx], m_origin.homeland.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_origin.blurb.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("No Aldermarch house will claim you. (Homelands - and houses of your own - to come.)");
        ImGui::EndChild();
    }

    // Elves belong to an Aelvarin lineage (a medallion, not a war-shield).
    void renderElfHouseCard() {
        if (m_elfIdx < 0) assignElfLineage();
        const auto& h = rpgw::elfHouses()[m_elfIdx];
        ImU32 col = h.royal ? IM_COL32(198, 180, 238, 255)
                  : (h.strand == rpgw::COURT ? IM_COL32(216, 210, 236, 255)
                                             : IM_COL32(132, 182, 144, 255));
        ImGui::TextUnformatted("Your Lineage");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cast lots again##elf")) assignElfLineage();

        ImGui::BeginChild("##elfcard", ImVec2(0, 176), true);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float R = 46.0f;
        drawMedallion(ImGui::GetWindowDrawList(), ImVec2(p0.x + 6.0f + R, p0.y + 6.0f + R), R, h.sigil, col);
        ImGui::Dummy(ImVec2(2.0f * R + 22.0f, 2.0f * R + 8.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.93f, 0.91f, 0.96f, 1.0f), "%s", h.name);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "\"%s\"", h.words);
        ImGui::TextDisabled("%s  -  %s", rpgw::strandName(h.strand), h.seat[0] ? h.seat : "no seat; they wander");
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "The Art: %s", h.art);
        ImGui::TextWrapped("%s", h.gift);
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::TextDisabled("On the Fading: %s", h.stance);
        ImGui::TextDisabled("Led by its Elder - among the Aelvar, women lead as freely as men.");
    }

    // Dwarves belong to a Kadmurn clan (a gem-cut cartouche, forge-branded).
    void renderDwarfClanCard() {
        if (m_dwarfIdx < 0) assignDwarfClan();
        const auto& c = rpgw::dwarfClans()[m_dwarfIdx];
        ImU32 col = c.royal ? IM_COL32(230, 207, 156, 255)
                  : (c.strand == rpgw::DEEP ? IM_COL32(216, 162, 94, 255)
                                            : IM_COL32(201, 162, 75, 255));
        ImGui::TextUnformatted("Your Clan");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cast lots again##dwarf")) assignDwarfClan();

        ImGui::BeginChild("##dwarfcard", ImVec2(0, 176), true);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float R = 46.0f;
        drawCartouche(ImGui::GetWindowDrawList(), ImVec2(p0.x + 6.0f + R, p0.y + 6.0f + R), R, c.sigil, col);
        ImGui::Dummy(ImVec2(2.0f * R + 22.0f, 2.0f * R + 8.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.90f, 0.81f, 0.61f, 1.0f), "%s", c.name);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "\"%s\"", c.words);
        ImGui::TextDisabled("%s  -  %s", rpgw::dstrandName(c.strand), c.hold[0] ? c.hold : "no hold; they walk under oath");
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "The Craft: %s", c.craft);
        ImGui::TextWrapped("%s", c.gift);
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::TextDisabled("The Cause: %s", c.cause);
        ImGui::TextDisabled("Led by seniority and worth - among the dwarrow, women stand equal in hall and hold.");
    }

    // Dragonborn belong to a Vharok clan (a dragon-scale lozenge, tinted by blood).
    void renderDragonClanCard() {
        if (m_dragonIdx < 0) assignDragonClan();
        const auto& c = rpgw::dragonClans()[m_dragonIdx];
        ImU32 col = IM_COL32(c.color.r, c.color.g, c.color.b, 255);
        ImGui::TextUnformatted("Your Clan");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cast lots again##dragon")) assignDragonClan();

        ImGui::BeginChild("##dragoncard", ImVec2(0, 182), true);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float R = 48.0f;
        drawScale(ImGui::GetWindowDrawList(), ImVec2(p0.x + 6.0f + R, p0.y + 6.0f + R * 1.15f), R, c.sigil, c.color);
        ImGui::Dummy(ImVec2(2.0f * R + 22.0f, R * 2.3f + 6.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.94f, 0.82f, 0.69f, 1.0f), "%s", c.name);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s dragon-blood  -  %s breath", c.ancestry, c.breath);
        ImGui::TextDisabled("\"%s\"", c.words);
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "The Blood: %s", c.art);
        ImGui::TextWrapped("%s", c.gift);
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::TextDisabled("The Ember: %s", c.ember);
        ImGui::TextDisabled("No crown, no birthright - leadership is earned by deed; women and men stand equal.");
    }

    // Gnomes & halflings belong to a fey house of Evermere (a bright bloom emblem).
    void renderFeyHouseCard() {
        if (m_feyIdx < 0) assignFeyHouse();
        const auto& h = rpgw::feyHouses()[m_feyIdx];
        ImU32 col = IM_COL32(h.color.r, h.color.g, h.color.b, 255);
        ImGui::TextUnformatted("Your Fey House");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cast lots again##fey")) assignFeyHouse();

        ImGui::BeginChild("##feycard", ImVec2(0, 176), true);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float R = 46.0f;
        drawBloom(ImGui::GetWindowDrawList(), ImVec2(p0.x + 8.0f + R, p0.y + 8.0f + R), R, h.sigil, h.color);
        ImGui::Dummy(ImVec2(2.0f * R + 26.0f, 2.0f * R + 12.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.55f, 0.78f, 0.60f, 1.0f), "%s", h.name);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "\"%s\"", h.words);
        ImGui::TextDisabled("Evermere, the Bright Court  -  %s", h.seat[0] ? h.seat : "of the fairy-roads");
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "The Court: %s", h.art);
        ImGui::TextWrapped("%s", h.gift);
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::TextDisabled("Against the Long Dusk: %s", h.stance);
        ImGui::TextDisabled("A subject of the Dawn-Lion Aurelior - a fey realm not of this world.");
    }

    // Tieflings claimed by an infernal house of the Brass Reach (a horned seal).
    void renderInfernalCard() {
        if (m_infernalIdx < 0) assignInfernalHouse();
        const auto& h = rpgw::infernalHouses()[m_infernalIdx];
        ImU32 col = IM_COL32(h.color.r, h.color.g, h.color.b, 255);
        ImGui::TextUnformatted("Your Infernal House");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cast lots again##inf")) assignInfernalHouse();

        ImGui::BeginChild("##infcard", ImVec2(0, 182), true);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float R = 48.0f;
        drawHornedSeal(ImGui::GetWindowDrawList(), ImVec2(p0.x + 8.0f + R, p0.y + 14.0f + R), R, h.sigil, h.color);
        ImGui::Dummy(ImVec2(2.0f * R + 26.0f, 2.0f * R + 18.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.85f, 0.81f, 0.84f, 1.0f), "%s", h.name);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", h.portfolio);
        ImGui::TextDisabled("\"%s\"", h.words);
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "The Portfolio: %s", h.art);
        ImGui::TextWrapped("%s", h.gift);
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::TextDisabled("In the Reach: %s", h.stance);
        ImGui::TextDisabled("Your blood is bound to the Brass Reach - an infernal plane of pact and ledger.");
    }

    // Drow are the Sundered: exiles of Aelvarin, of no lineage.
    void renderSunderedCard() {
        ImGui::TextUnformatted("Your Origin");
        ImGui::BeginChild("##sundered", ImVec2(0, 138), true);
        ImGui::TextColored(ImVec4(0.69f, 0.42f, 0.52f, 1.0f), "The Sundered  -  drow, exiled beneath Aelvarin");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", rpgw::sunderedBlurb());
        ImGui::EndChild();
    }

    // Draw one kin node (box + name + relation) and register a hover tooltip.
    void kinNode(ImDrawList* dl, ImVec2 c, float w, float hgt, const std::string& name,
                 const std::string& relation, const std::string& status, const std::string& tip,
                 bool you, bool heir, bool holdsSeat, int id) {
        ImVec2 tl(c.x - w * 0.5f, c.y - hgt * 0.5f), br(c.x + w * 0.5f, c.y + hgt * 0.5f);
        bool dead = (status == "dead");
        bool away = (status == "estranged" || status == "missing");
        ImU32 bg = you ? IM_COL32(46, 40, 28, 255) : IM_COL32(34, 39, 46, 255);
        ImU32 border = you ? IM_COL32(194, 160, 107, 255)
                     : dead ? IM_COL32(66, 66, 74, 255)
                     : away ? IM_COL32(150, 110, 60, 255)
                            : IM_COL32(74, 84, 95, 255);
        dl->AddRectFilled(tl, br, bg, 4.0f);
        dl->AddRect(tl, br, border, 4.0f, 0, you ? 2.0f : 1.4f);
        dl->PushClipRect(tl, br, true);
        ImU32 nameCol = dead ? IM_COL32(140, 140, 146, 255) : IM_COL32(232, 228, 218, 255);
        ImVec2 ns = ImGui::CalcTextSize(name.c_str());
        dl->AddText(ImVec2(c.x - ns.x * 0.5f, c.y - 15.0f), nameCol, name.c_str());
        std::string rel = relation + (heir ? "  * HEIR" : "");
        ImVec2 rs = ImGui::CalcTextSize(rel.c_str());
        dl->AddText(ImVec2(c.x - rs.x * 0.5f, c.y + 2.0f), IM_COL32(150, 159, 169, 255), rel.c_str());
        dl->PopClipRect();
        if (holdsSeat) dl->AddCircleFilled(ImVec2(tl.x + 8.0f, tl.y + 8.0f), 3.5f, IM_COL32(194, 160, 107, 255));
        ImGui::SetCursorScreenPos(tl);
        ImGui::PushID(id);
        ImGui::InvisibleButton("kn", ImVec2(w, hgt));
        if (ImGui::IsItemHovered() && !tip.empty()) {
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(300.0f);
            ImGui::TextUnformatted(tip.c_str());
            ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    void renderFamilyTree() {
        if (m_houseIdx < 0) assignHouse();
        const rpgw::Family& fam = m_family;
        ImGui::TextUnformatted("Your Family");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cast lots again##fam")) genFamily();

        ImGui::BeginChild("##familytree", ImVec2(0, 322), true);
        // Seat header
        ImGui::TextColored(ImVec4(0.76f, 0.63f, 0.42f, 1.0f), "The Seat of %s, %s",
                           (m_houseIdx >= 0 ? rpgw::houses()[m_houseIdx].name : ""),
                           (m_houseIdx >= 0 ? rpgw::houses()[m_houseIdx].seat : ""));
        ImGui::TextDisabled("held by %s (%s)", fam.seatHolder.c_str(), fam.seatRelation.c_str());

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float canvasW = ImGui::GetContentRegionAvail().x;
        const float canvasH = 210.0f;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(canvasW, canvasH));

        // ---- parents row ----
        float pW = 158.0f, nodeH = 46.0f;
        float parY = p0.y + 30.0f;
        ImVec2 fc(p0.x + canvasW * 0.33f, parY), mc(p0.x + canvasW * 0.64f, parY);
        auto kinTip = [](const rpgw::Kin& k) {
            return k.trait + "\nToward you: " + k.disposition + "\nStatus: " + k.status;
        };

        // ---- children row (elders, YOU, youngers) ----
        struct Node { std::string name, rel, status, tip; bool you, heir, seat; };
        std::vector<Node> kids;
        for (const auto& s : fam.siblings) if (s.elder)
            kids.push_back({s.name, s.relation, s.status, kinTip(s), false, s.heir, s.holdsSeat});
        std::string youName = (m_nameBuf[0] ? std::string(m_nameBuf) : std::string("You"));
        if (!fam.pcSurname.empty()) youName += " " + fam.pcSurname;
        std::string youRel = fam.bastard ? "baseborn - no claim"
                           : (fam.successionRank == 1 ? "Heir"
                              : rpgw::fdetail::ordinal(fam.successionRank) + " child");
        kids.push_back({youName, youRel, "living", fam.successionLine,
                        true, (!fam.bastard && fam.successionRank == 1), fam.seatRelation == "you"});
        for (const auto& s : fam.siblings) if (!s.elder)
            kids.push_back({s.name, s.relation, s.status, kinTip(s), false, s.heir, s.holdsSeat});

        int nkids = (int)kids.size();
        float reserveBet = fam.hasBetrothed ? 150.0f : 0.0f;
        float availW = canvasW - reserveBet - 8.0f;
        float gap = 10.0f;
        float cw = std::min(148.0f, (availW - (nkids - 1) * gap) / std::max(1, nkids));
        cw = std::max(cw, 96.0f);
        float total = nkids * cw + (nkids - 1) * gap;
        float startx = p0.x + 4.0f + (availW - total) * 0.5f + cw * 0.5f;
        float kidY = p0.y + 150.0f;

        // ---- connectors ----
        ImU32 line = IM_COL32(90, 100, 110, 255);
        float pBot = parY + nodeH * 0.5f;
        float jx = (fc.x + mc.x) * 0.5f, jy = pBot + 12.0f;
        dl->AddLine(ImVec2(fc.x, pBot), ImVec2(fc.x, jy), line, 1.6f);
        dl->AddLine(ImVec2(mc.x, pBot), ImVec2(mc.x, jy), line, 1.6f);
        dl->AddLine(ImVec2(fc.x, jy), ImVec2(mc.x, jy), line, 1.6f);
        float busY = kidY - nodeH * 0.5f - 16.0f;
        dl->AddLine(ImVec2(jx, jy), ImVec2(jx, busY), line, 1.6f);
        float firstX = startx, lastX = startx + (nkids - 1) * (cw + gap);
        dl->AddLine(ImVec2(std::min(firstX, jx), busY), ImVec2(std::max(lastX, jx), busY), line, 1.6f);
        for (int i = 0; i < nkids; ++i) {
            float cx = startx + i * (cw + gap);
            dl->AddLine(ImVec2(cx, busY), ImVec2(cx, kidY - nodeH * 0.5f), line, 1.6f);
        }

        // ---- nodes ----
        int id = 0;
        kinNode(dl, fc, pW, nodeH, fam.father.name, "Father", fam.father.status, kinTip(fam.father), false, false, fam.father.holdsSeat, id++);
        kinNode(dl, mc, pW, nodeH, fam.mother.name, "Mother", fam.mother.status, kinTip(fam.mother), false, false, fam.mother.holdsSeat, id++);
        float youX = startx;
        for (int i = 0; i < nkids; ++i) {
            float cx = startx + i * (cw + gap);
            if (kids[i].you) youX = cx;
            kinNode(dl, ImVec2(cx, kidY), cw, nodeH, kids[i].name, kids[i].rel, kids[i].status,
                    kids[i].tip, kids[i].you, kids[i].heir, kids[i].seat, id++);
        }
        // ---- betrothed ----
        if (fam.hasBetrothed) {
            ImVec2 bc(p0.x + canvasW - 78.0f, kidY);
            dl->AddLine(ImVec2(youX + cw * 0.5f, kidY), ImVec2(bc.x - 74.0f, kidY), IM_COL32(150, 110, 60, 200), 1.4f);
            dl->AddText(ImVec2((youX + cw * 0.5f + bc.x - 74.0f) * 0.5f - 26.0f, kidY - 16.0f),
                        IM_COL32(150, 110, 60, 255), "betrothed");
            kinNode(dl, bc, 148.0f, nodeH, fam.betrothed.name, "Betrothed", "living", fam.betrothed.trait, false, false, false, id++);
        }

        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + canvasH));
        ImGui::TextWrapped("%s", fam.successionLine.c_str());
        ImGui::TextColored(ImVec4(0.62f, 0.72f, 0.85f, 1.0f), "Iron Legacy: %s  (?)", fam.legacyName.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(320.0f);
            ImGui::Text("%s", fam.legacyDesc.c_str());
            ImGui::Spacing();
            ImGui::TextUnformatted(fam.legacyEffect.c_str());
            ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
        ImGui::EndChild();
    }

    // Color for an ideal-band stat: green inside the band, amber just outside, red far.
    static ImU32 idealColor(int v, int lo, int hi) {
        if (v >= lo && v <= hi) return IM_COL32(96, 200, 116, 255);
        int d = v < lo ? lo - v : v - hi;
        return d <= 2 ? IM_COL32(212, 182, 92, 255) : IM_COL32(212, 96, 84, 255);
    }
    // A small 3-18 gauge. If idealLo>0 an ideal band is shaded (extremes read red);
    // otherwise it's a directional meter filled in meterColor.
    void statGauge(int value, int idealLo, int idealHi, ImU32 meterColor) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = 264.0f, h = 12.0f;
        float x0 = p.x, y0 = p.y + 2.0f;
        auto px = [&](float v) { return x0 + (v - 3.0f) / 15.0f * w; };
        bool banded = (idealLo > 0 && idealHi >= idealLo);
        ImU32 mark = banded ? idealColor(value, idealLo, idealHi) : meterColor;
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(28, 32, 38, 255), 3.0f);
        if (banded) {
            dl->AddRectFilled(ImVec2(px((float)idealLo), y0), ImVec2(px((float)idealHi + 1), y0 + h),
                              IM_COL32(50, 110, 62, 120), 0.0f);
        } else {
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(px((float)value), y0 + h), mark, 3.0f);
        }
        dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(60, 68, 78, 255), 3.0f);
        float mx = px((float)value);
        dl->AddCircleFilled(ImVec2(mx, y0 + h * 0.5f), 5.5f, mark);
        dl->AddCircle(ImVec2(mx, y0 + h * 0.5f), 5.5f, IM_COL32(18, 20, 24, 255), 12, 1.5f);
        ImGui::Dummy(ImVec2(w, h + 6.0f));
    }

    // One ideal-band personality row (self-regard, carnality, cruelty, sociability).
    void tempRow(const char* label, int val, int lo, int hi, const char* tierName,
                 const char* desc, const char* poles, const char* effect) {
        ImGui::Text("%s", label); ImGui::SameLine(120.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(idealColor(val, lo, hi)), "%d  -  %s", val, tierName);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(320.0f);
            ImGui::TextUnformatted(desc);
            ImGui::Spacing(); ImGui::TextDisabled("%s", poles);
            ImGui::Spacing(); ImGui::TextUnformatted(effect);
            ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
        statGauge(val, lo, hi, 0);
    }

    // Color for a directional virtue meter: low = warm brown, mid = neutral, high = gold.
    static ImU32 virtueColor(int v) {
        if (v <= 8)  return IM_COL32(190, 120, 96, 255);
        if (v <= 12) return IM_COL32(150, 160, 170, 255);
        return IM_COL32(200, 165, 90, 255);
    }
    // One directional-meter personality row (bravery-style, generic tooltip).
    void tempMeter(const char* label, int val, const char* tierName,
                   const char* desc, const char* poles, const char* effect) {
        ImU32 col = virtueColor(val);
        ImGui::Text("%s", label); ImGui::SameLine(120.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%d  -  %s", val, tierName);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(320.0f);
            ImGui::TextUnformatted(desc);
            ImGui::Spacing(); ImGui::TextDisabled("%s", poles);
            ImGui::Spacing(); ImGui::TextUnformatted(effect);
            ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
        statGauge(val, 0, 0, col);
    }
    void tempGroup(const char* title) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.62f, 0.72f, 0.85f, 1.0f), "%s", title);
    }

    void renderTemperament() {
        ImGui::TextUnformatted("Temperament");
        ImGui::SameLine(); ImGui::TextDisabled("(rolled with your abilities - hover any trait)");

        // ── Nerve & Drive ──
        tempGroup("Nerve & Drive");
        auto bt = rpgc::braveryTier(m_bravery);         // Bravery keeps its morale tooltip
        ImGui::Text("Bravery"); ImGui::SameLine(120.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(virtueColor(m_bravery)), "%d  -  %s", m_bravery, bt.name);
        if (ImGui::IsItemHovered()) {
            std::string tip = std::string(bt.desc) + "\n\n";
            if (m_bravery >= 18) tip += "Morale: never breaks - but may charge in when retreat is wiser.";
            else tip += "Morale: when a fight turns grim, roll d20 " +
                        std::string(rpgc::braveryMod(m_bravery) >= 0 ? "+" : "") +
                        std::to_string(rpgc::braveryMod(m_bravery)) + " or break and flee.";
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(320.0f);
            ImGui::TextUnformatted(tip.c_str());
            ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
        statGauge(m_bravery, 0, 0, virtueColor(m_bravery));
        tempRow("Temper", m_temper, 8, 12, rpgc::temperTier(m_temper).name, rpgc::temperTier(m_temper).desc,
                "Cold  <-----  composed  ----->  Wrathful", rpgc::temperEffectNote());
        tempMeter("Will to Power", m_willToPower, rpgc::willTier(m_willToPower).name,
                  rpgc::willTier(m_willToPower).desc, "Compliant  <-----  measured  ----->  Dominant",
                  rpgc::willEffectNote());
        tempMeter("Diligence", m_diligence, rpgc::diligenceTier(m_diligence).name,
                  rpgc::diligenceTier(m_diligence).desc, "Slothful  <-----  steady  ----->  Tireless",
                  rpgc::diligenceEffectNote());

        // ── Heart ──
        tempGroup("Heart");
        tempRow("Self-Regard", m_narcissism, 9, 12, rpgc::narcissismTier(m_narcissism).name,
                rpgc::narcissismTier(m_narcissism).desc,
                "Inferiority  <-----  balanced  ----->  Narcissism", rpgc::regardEffectNote());
        tempMeter("Compassion", m_compassion, rpgc::compassionTier(m_compassion).name,
                  rpgc::compassionTier(m_compassion).desc, "Callous  <-----  kindly  ----->  Tender-Hearted",
                  rpgc::compassionEffectNote());
        tempRow("Cruelty", m_cruelty, 9, 12, rpgc::crueltyTier(m_cruelty).name, rpgc::crueltyTier(m_cruelty).desc,
                "Masochistic  <-----  balanced  ----->  Sadistic", rpgc::crueltyEffectNote());
        tempRow("Carnality", m_carnality, 5, 9, rpgc::carnalityTier(m_carnality).name,
                rpgc::carnalityTier(m_carnality).desc, "Chaste  <-----  temperate  ----->  Deviant",
                rpgc::carnalityEffectNote());
        tempRow("Sociability", m_sociability, 8, 13, rpgc::sociabilityTier(m_sociability).name,
                rpgc::sociabilityTier(m_sociability).desc,
                "Reclusive  <-----  sociable  ----->  Overbearing", rpgc::sociabilityEffectNote());

        // ── Mind & Faith ──
        tempGroup("Mind & Faith");
        tempRow("Skepticism", m_skepticism, 9, 13, rpgc::skepticismTier(m_skepticism).name,
                rpgc::skepticismTier(m_skepticism).desc,
                "Gullible  <-----  discerning  ----->  Cynical", rpgc::skepticismEffectNote());
        tempRow("Curiosity", m_curiosity, 8, 13, rpgc::curiosityTier(m_curiosity).name,
                rpgc::curiosityTier(m_curiosity).desc, "Hidebound  <-----  curious  ----->  Heterodox",
                rpgc::curiosityEffectNote());
        tempRow("Piety", m_piety, 9, 14, rpgc::pietyTier(m_piety).name, rpgc::pietyTier(m_piety).desc,
                "Impious  <-----  faithful  ----->  Zealot", rpgc::pietyEffectNote());

        // ── Honor & Coin ──
        tempGroup("Honor & Coin");
        tempMeter("Honor", m_honor, rpgc::honorTier(m_honor).name, rpgc::honorTier(m_honor).desc,
                  "Treacherous  <-----  honest  ----->  Oathbound", rpgc::honorEffectNote());
        tempRow("Greed", m_greed, 7, 12, rpgc::greedTier(m_greed).name, rpgc::greedTier(m_greed).desc,
                "Prodigal  <-----  prudent  ----->  Avaricious", rpgc::greedEffectNote());
    }

    // ----- party relationships (demo) -----
    void buildCompanions() {
        auto mk = [](const char* name, const char* race, const char* cls, int wtp, int narc, int honor,
                     int comp, int cruel, int temper, int skept, int soc, int piety, int cur) {
            rpgc::Character c;
            c.name = name; c.race = race; c.className = cls;
            c.willToPower = wtp; c.narcissism = narc; c.honor = honor; c.compassion = comp;
            c.cruelty = cruel; c.temper = temper; c.skepticism = skept; c.sociability = soc;
            c.piety = piety; c.curiosity = cur; c.carnality = 10; c.greed = 10; c.diligence = 11;
            return c;
        };
        m_companions = {
            mk("Willa",       "Human",    "Cleric",  5,  7, 13, 15,  6,  9,  6, 10, 11,  9),
            mk("Ser Aldric",  "Human",    "Fighter",12, 10, 16, 12,  8, 10, 11, 11, 13,  9),
            mk("Sister Enna", "Human",    "Paladin", 9,  8, 13, 17,  3,  8,  8, 11, 15, 10),
            mk("Zyrix",       "Tiefling", "Warlock",11, 12, 11, 12,  8, 10, 13, 12,  5, 16),
        };
    }
    static ImU32 dynamicColor(const std::string& d, bool toxic) {
        if (toxic)                return IM_COL32(196, 90, 150, 255);   // exploitation - orchid
        if (d == "Feud")          return IM_COL32(212, 84, 78, 255);    // red
        if (d == "Cold")          return IM_COL32(150, 128, 120, 255);  // dull
        if (d == "Rivalry")       return IM_COL32(214, 150, 78, 255);   // orange
        if (d == "Oathbond")      return IM_COL32(200, 165, 90, 255);   // gold
        if (d == "Friendship")    return IM_COL32(110, 200, 120, 255);  // green
        if (d == "Amicable")      return IM_COL32(120, 180, 150, 255);  // soft green
        if (d == "Dominance")     return IM_COL32(120, 160, 210, 255);  // blue
        return IM_COL32(150, 159, 169, 255);                            // neutral
    }
    void renderRelationsPanel() {
        if (m_companions.empty()) buildCompanions();
        std::vector<const rpgc::Character*> roster;
        if (!m_pc.name.empty()) roster.push_back(&m_pc);
        for (const auto& c : m_companions) roster.push_back(&c);

        ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(340, 60), ImGuiCond_FirstUseEver);
        ImGui::Begin("Party & Relationships", &m_showRelations,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::TextDisabled("How your party would regard one another (from their personalities).");
        ImGui::Spacing();
        for (size_t i = 0; i < roster.size(); ++i)
            for (size_t j = i + 1; j < roster.size(); ++j) {
                auto r = rpgr::assess(*roster[i], *roster[j]);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dynamicColor(r.dynamic, r.toxic)),
                                   "%-12s", r.dynamic.c_str());
                ImGui::SameLine(120.0f);
                ImGui::Text("%s  &  %s", roster[i]->name.c_str(), roster[j]->name.c_str());
                ImGui::SameLine(); ImGui::TextDisabled("(%+d / %+d)", r.opinionAB, r.opinionBA);
                ImGui::SameLine(120.0f); ImGui::NewLine();
                ImGui::Indent(120.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 176, 184, 255));
                ImGui::TextWrapped("%s", r.note.c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent(120.0f);
                ImGui::Spacing();
            }
        ImGui::End();
    }

    void renderCharCreate() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(640, std::min(disp.y - 24.0f, 940.0f)), ImGuiCond_Always);
        ImGui::Begin("Create Your Character", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::InputText("Name", m_nameBuf, sizeof(m_nameBuf));
        // Gender: in Aldermarch a woman rarely bears the first name of a house.
        int gender = m_female ? 1 : 0, prevGender = gender;
        ImGui::TextUnformatted("Gender"); ImGui::SameLine(120.0f);
        ImGui::RadioButton("Man", &gender, 0); ImGui::SameLine();
        ImGui::RadioButton("Woman", &gender, 1);
        m_female = (gender == 1);
        auto combo = [](const char* label, int& idx, const std::vector<const char*>& opts) {
            if (ImGui::BeginCombo(label, opts[idx])) {
                for (int i = 0; i < static_cast<int>(opts.size()); ++i)
                    if (ImGui::Selectable(opts[i], idx == i)) idx = i;
                ImGui::EndCombo();
            }
        };
        int prevRace = m_raceIdx;
        combo("Race", m_raceIdx, rpgc::raceOptions());
        // Class is not chosen - it is determined by your rolls (shown below).
        // Race decides house-vs-origin; gender reshapes the succession.
        if (m_raceIdx != prevRace) assignHouse();
        else if (gender != prevGender) genFamily();

        int prevClass = m_classIdx;
        combo("Class", m_classIdx, rpgc::classOptions());
        if (m_classIdx != prevClass) m_skillPick.fill(false);   // new class -> new skill list

        // Background (your choice; Noble opens the great houses).
        int prevBg = m_bgIdx;
        if (ImGui::BeginCombo("Background", rpgc::backgroundOptions()[m_bgIdx])) {
            for (int i = 0; i < static_cast<int>(rpgc::backgroundOptions().size()); ++i) {
                if (ImGui::Selectable(rpgc::backgroundOptions()[i], m_bgIdx == i)) m_bgIdx = i;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", rpgc::backgroundInfo(rpgc::backgroundOptions()[i]).desc);
            }
            ImGui::EndCombo();
        }
        if (m_bgIdx != prevBg) {
            std::string race = rpgc::raceOptions()[m_raceIdx];
            if (rpgw::isHouseRace(race) || rpgw::isTiefling(race)) {   // bg only shapes human/tiefling houses
                bool wasNoble = rpgc::backgroundOptions()[prevBg] == std::string("Noble");
                bool isNoble  = rpgc::backgroundOptions()[m_bgIdx] == std::string("Noble");
                if (wasNoble != isNoble) assignHouse();
                else if (m_houseIdx >= 0) {
                    m_houseStanding = standingFor(rpgc::backgroundOptions()[m_bgIdx], rpgw::houses()[m_houseIdx].rank);
                    genFamily();
                }
            }
        }
        {
            auto bi = rpgc::backgroundInfo(rpgc::backgroundOptions()[m_bgIdx]);
            ImGui::TextDisabled("Background skills: %s & %s", rpgc::skills()[bi.skill1].name, rpgc::skills()[bi.skill2].name);
        }
        combo("Alignment", m_alignIdx, rpgc::alignmentOptions());

        // Half-Elf uniquely gets +1 to two abilities of the player's choice.
        if (isHalfElf()) {
            int picks = halfElfPickCount();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                               "Half-Elf: +2 CHA, plus +1 to two of your choice:");
            for (int a = 0; a < rpgc::ABILITY_COUNT; ++a) {
                if (a == rpgc::CHA) continue;   // CHA already gets the +2
                ImGui::SameLine();
                bool checked = m_halfElfBonus[a];
                ImGui::BeginDisabled(!checked && picks >= 2);   // cap at two
                if (ImGui::Checkbox(rpgc::abilityAbbr(a), &checked)) m_halfElfBonus[a] = checked;
                ImGui::EndDisabled();
            }
            if (picks != 2) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "(choose two)");
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Roll a Random Hero")) fullRandom();
        ImGui::SameLine();
        ImGui::TextDisabled("(rolls the lot: stats, class, background, house, temperament...)");

        ImGui::Separator();
        ImGui::TextUnformatted("Ability scores  -  roll, then drag each value onto an ability (hover a name for help)");
        int rerollsLeft = 3 - m_rollsUsed;
        std::string rlabel = rerollsLeft > 0 ? ("Re-roll (" + std::to_string(rerollsLeft) + " left)")
                                             : "No re-rolls left";
        ImGui::BeginDisabled(rerollsLeft <= 0);
        if (ImGui::Button(rlabel.c_str())) { rollAbilityScores(); ++m_rollsUsed; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(("Auto-assign for " + std::string(rpgc::classOptions()[m_classIdx])).c_str()))
            autoAssignForClass();
        ImGui::TextDisabled("You get 3 rolls total (1 initial + 2 re-rolls).");

        // Pool of unassigned rolls (drag sources).
        ImGui::TextUnformatted("Rolled:");
        bool anyInPool = false;
        for (int i = 0; i < 6; ++i) {
            if (isAssigned(i)) continue;
            anyInPool = true;
            ImGui::SameLine();
            ImGui::PushID(2000 + i);
            ImGui::Button(std::to_string(m_rolled[i]).c_str(), ImVec2(40, 0));
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ROLL", &i, sizeof(int));
                ImGui::Text("%d", m_rolled[i]);
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
        if (!anyInPool) { ImGui::SameLine(); ImGui::TextDisabled("(all assigned)"); }

        ImGui::Spacing();
        auto raceBonus = effectiveRaceBonus();
        for (int a = 0; a < 6; ++a) {
            ImGui::PushID(a);
            ImGui::TextUnformatted(rpgc::abilityName(a));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", rpgc::abilityDesc(a));
            ImGui::SameLine(150.0f);
            std::string face = (m_assign[a] >= 0) ? std::to_string(m_rolled[m_assign[a]]) : "  --  ";
            ImGui::Button(face.c_str(), ImVec2(56, 0));
            if (m_assign[a] >= 0 && ImGui::BeginDragDropSource()) {
                int idx = m_assign[a];
                ImGui::SetDragDropPayload("ROLL", &idx, sizeof(int));
                ImGui::Text("%d", m_rolled[idx]);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ROLL"))
                    assignRoll(a, *static_cast<const int*>(pl->Data));
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (raceBonus[a] != 0) ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%+d race", raceBonus[a]);
            else ImGui::TextDisabled("      ");
            if (m_assign[a] >= 0) {
                int total = m_rolled[m_assign[a]] + raceBonus[a];
                ImGui::SameLine();
                ImGui::Text("=  %2d  (%+d)", total, rpgc::abilityMod(total));
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        renderTemperament();

        // Skills: choose the class's allotment; the class saves are automatic.
        ImGui::Separator();
        auto cp = rpgc::classProficiencies(rpgc::classOptions()[m_classIdx]);
        int skPicks = 0; for (int s : cp.skillList) if (m_skillPick[s]) ++skPicks;
        ImGui::Text("Skills - choose %d for your %s  (%d/%d)", cp.skillCount,
                    rpgc::classOptions()[m_classIdx], skPicks, cp.skillCount);
        for (int k = 0; k < static_cast<int>(cp.skillList.size()); ++k) {
            if (k % 2 == 1) ImGui::SameLine(280.0f);
            int sk = cp.skillList[k];
            bool checked = m_skillPick[sk];
            ImGui::BeginDisabled(!checked && skPicks >= cp.skillCount);
            std::string label = std::string(rpgc::skills()[sk].name) + " (" +
                                rpgc::abilityAbbr(rpgc::skills()[sk].ability) + ")";
            if (ImGui::Checkbox(label.c_str(), &checked)) m_skillPick[sk] = checked;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", rpgc::skillDesc(sk));
        }
        ImGui::TextDisabled("Saving throws (from class): %s & %s",
                            rpgc::abilityName(cp.save1), rpgc::abilityName(cp.save2));

        ImGui::Separator();
        renderPortraitGallery();

        ImGui::Separator();
        {
            std::string race = rpgc::raceOptions()[m_raceIdx];
            if (rpgw::isHouseRace(race)) {
                renderHouseCard();
                ImGui::Separator();
                renderFamilyTree();
            } else if (rpgw::isDrow(race)) {
                renderSunderedCard();
            } else if (rpgw::isElf(race)) {
                renderElfHouseCard();
            } else if (rpgw::isDwarf(race)) {
                renderDwarfClanCard();
            } else if (rpgw::isDragonborn(race)) {
                renderDragonClanCard();
            } else if (rpgw::isFey(race)) {
                renderFeyHouseCard();
            } else if (rpgw::isTiefling(race)) {
                if (m_houseIdx >= 0) {   // born to a mortal line; infernal blood a scandal
                    renderHouseCard();
                    ImGui::TextColored(ImVec4(0.72f, 0.42f, 0.52f, 1.0f),
                                       "Your infernal blood is a scandal to this house - hushed, never quite forgotten.");
                } else {
                    renderInfernalCard();
                }
            } else {
                renderOriginCard();
            }
        }

        ImGui::Separator();
        bool halfElfOk = !isHalfElf() || halfElfPickCount() == 2;
        bool ready = m_nameBuf[0] != '\0' && allAssigned() && halfElfOk && skPicks == cp.skillCount;
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("Begin Adventure", ImVec2(200, 0))) finishCharCreate();
        ImGui::EndDisabled();
        if (!ready) {
            const char* why = m_nameBuf[0] == '\0' ? "enter a name"
                            : !allAssigned()       ? "assign all six abilities"
                            : !halfElfOk           ? "choose Half-Elf's two +1 abilities"
                                                   : "choose your class skills";
            ImGui::SameLine();
            ImGui::TextDisabled("%s", why);
        }
        ImGui::TextDisabled("(skills, portrait & equipment coming next)");
        ImGui::End();
    }

    void renderTitleScreen() {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 disp = io.DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(disp);
        ImGui::Begin("##title", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);
        auto center = [&](const char* txt, float scale, ImVec4 col) {
            ImGui::SetWindowFontScale(scale);
            float tw = ImGui::CalcTextSize(txt).x;
            ImGui::SetCursorPosX((disp.x - tw) * 0.5f);
            ImGui::TextColored(col, "%s", txt);
            ImGui::SetWindowFontScale(1.0f);
        };
        const ImVec4 dim(0.62f, 0.62f, 0.66f, 1.0f);
        // Auto-fit the title to ~90% of the window width (capped) so long titles
        // don't overflow.
        const char* title = "CHRONICLES OF THE IRON TEMPLE";
        ImGui::SetWindowFontScale(1.0f);
        float titleScale = std::min(5.0f, (disp.x * 0.9f) / std::max(ImGui::CalcTextSize(title).x, 1.0f));
        ImGui::SetCursorPosY(disp.y * 0.22f);
        center(title, titleScale, ImVec4(0.86f, 0.74f, 0.42f, 1.0f));
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        center("a CRPG by Jason Mark Dub\xc3\xa9", 1.8f, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        center("Coded by Claude 4.8", 1.2f, dim);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        center("Pre-Alpha  \xc2\xb7  v0.1.0  \xc2\xb7  July 2026", 1.0f, dim);

        ImGui::SetCursorPosY(disp.y * 0.72f);
        float a = 0.5f + 0.5f * std::sin(m_titlePulse * 3.0f);
        center("Click or press any key to begin", 1.6f, ImVec4(0.92f, 0.86f, 0.6f, a));

        ImGui::SetCursorPosY(disp.y - 72.0f);
        center("This work includes material from the System Reference Document 5.1",
               1.0f, ImVec4(0.58f, 0.58f, 0.58f, 1.0f));
        center("by Wizards of the Coast LLC, licensed under CC-BY-4.0.",
               1.0f, ImVec4(0.58f, 0.58f, 0.58f, 1.0f));
        ImGui::End();
    }

    // Load the character GLBs as movable tokens: one model per mesh (textured),
    // uniformly scaled to a target height in feet with feet on the floor and the
    // XZ centered on a grid cell.
    void loadCharacters() {
        struct Spawn { const char* path; const char* name; int cx, cy; float heightFt;
                       Attitude attitude; bool merchant; const char* dialog; };
        const Spawn spawns[] = {
            {"assets/characters/percy_the_knight.glb",   "Percy", 2, 2, 6.0f,
             Attitude::Player, false, ""},
            {"assets/characters/orlen_the_merchant.glb", "Orlen", 7, 7, 5.8f,
             Attitude::Neutral, true, "Welcome to Orlens Wares, traveler. Have a look at my goods."},
        };
        for (const auto& sp : spawns) {
            eden::LoadResult r = eden::GLBLoader::load(sp.path);
            if (!r.success || r.meshes.empty()) {
                std::cerr << "character load FAILED: " << sp.path << "  (" << r.error << ")\n";
                continue;
            }
            glm::vec3 mn(1e9f), mx(-1e9f);
            for (const auto& m : r.meshes) { mn = glm::min(mn, m.bounds.min); mx = glm::max(mx, m.bounds.max); }
            Token t;
            t.name = sp.name; t.cx = sp.cx; t.cy = sp.cy;
            t.attitude = sp.attitude; t.merchant = sp.merchant; t.dialog = sp.dialog;
            t.scale = sp.heightFt / std::max(mx.y - mn.y, 0.001f);
            t.minY = mn.y;
            t.centerXZ = { (mn.x + mx.x) * 0.5f, (mn.z + mx.z) * 0.5f };
            for (const auto& m : r.meshes) {
                const unsigned char* px = m.hasTexture ? m.texture.data.data() : nullptr;
                int w = m.hasTexture ? m.texture.width : 0, h = m.hasTexture ? m.texture.height : 0;
                t.meshHandles.push_back(m_modelRenderer->createModel(m.vertices, m.indices, px, w, h));
            }
            std::cerr << "character loaded: " << sp.name << " — " << r.meshes.size()
                      << " meshes, scale " << t.scale << "\n";
            m_tokens.push_back(std::move(t));
        }
    }

    // World transform placing a token upright on its grid cell (5-ft cells),
    // rotated to its facing.
    glm::mat4 tokenMatrix(const Token& t) const {
        float wx = t.cx * 5.0f + 2.5f, wz = t.cy * 5.0f + 2.5f;
        glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(wx, 0.12f, wz));
        M = glm::rotate(M, t.faceYaw + kModelFrontYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        M = glm::scale(M, glm::vec3(t.scale));
        M = glm::translate(M, glm::vec3(-t.centerXZ.x, -t.minY, -t.centerXZ.y));
        return M;
    }

    // Yaw (radians) that points a +Z-forward model from one cell toward another.
    static float yawToFace(int fromCx, int fromCy, int toCx, int toCy) {
        float dx = static_cast<float>(toCx - fromCx), dz = static_cast<float>(toCy - fromCy);
        if (dx == 0.0f && dz == 0.0f) return 0.0f;
        return std::atan2(dx, dz);
    }

    // When your piece stands next to an NPC, both turn to face each other; when
    // not engaged, tokens return to their default facing.
    void updateFacing() {
        int p = playerTokenIndex();
        bool playerEngaged = false;
        for (int i = 0; i < static_cast<int>(m_tokens.size()); ++i) {
            if (i == p) continue;
            Token& t = m_tokens[i];
            if (p >= 0 && tokensAdjacent(m_tokens[p], t)) {
                t.faceYaw = yawToFace(t.cx, t.cy, m_tokens[p].cx, m_tokens[p].cy);
                if (!playerEngaged) {
                    m_tokens[p].faceYaw = yawToFace(m_tokens[p].cx, m_tokens[p].cy, t.cx, t.cy);
                    playerEngaged = true;
                }
            } else {
                t.faceYaw = t.defaultYaw;
            }
        }
        if (p >= 0 && !playerEngaged) m_tokens[p].faceYaw = m_tokens[p].defaultYaw;
    }

    // Snap a world coordinate to a level grid cell index, clamped to the floor.
    static int cellFromWorld(float w, float lo, float hi) {
        return std::clamp(static_cast<int>(std::floor(w / 5.0f)),
                          static_cast<int>(std::floor(lo / 5.0f)),
                          static_cast<int>(std::floor((hi - 0.01f) / 5.0f)));
    }

    // Token under the cursor: ray-cast against each token's standing box (its
    // grid cell footprint, ~7 ft tall) so you can click the character itself,
    // not the floor spot the ray hits behind him. Nearest hit wins; else -1.
    int pickToken() const {
        glm::vec3 o, d;
        mouseRay(o, d);
        int best = -1;
        float bestT = 1e30f;
        for (int i = 0; i < static_cast<int>(m_tokens.size()); ++i) {
            float wx = m_tokens[i].cx * 5.0f, wz = m_tokens[i].cy * 5.0f;
            glm::vec3 bmin(wx, 0.0f, wz), bmax(wx + 5.0f, 7.0f, wz + 5.0f);
            float t;
            if (rayAABB(o, d, bmin, bmax, t) && t < bestT) { bestT = t; best = i; }
        }
        return best;
    }

    int playerTokenIndex() const {
        for (int i = 0; i < static_cast<int>(m_tokens.size()); ++i)
            if (m_tokens[i].attitude == Attitude::Player) return i;
        return -1;
    }
    static bool tokensAdjacent(const Token& a, const Token& b) {
        return std::max(std::abs(a.cx - b.cx), std::abs(a.cy - b.cy)) == 1;
    }

    // Left-click a token: drag your own piece to move it, or interact with an NPC
    // (right/middle stay camera). Interaction requires being adjacent.
    void handleTokenDrag(bool overUI) {
        if (m_dialogActive || m_shopOpen) return;   // dialog/shop owns input while open
        if (Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !overUI) {
            int picked = pickToken();
            if (picked >= 0) {
                if (m_tokens[picked].attitude == Attitude::Player) m_dragToken = picked;
                else                                               interactWith(picked);
            }
        }
        if (m_dragToken >= 0 && Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
            glm::vec2 floorPt;
            if (mouseOnBoard(floorPt)) {
                Token& t = m_tokens[m_dragToken];
                t.cx = cellFromWorld(floorPt.x, m_levelMin.x, m_levelMax.x);
                t.cy = cellFromWorld(floorPt.y, m_levelMin.y, m_levelMax.y);
            }
        } else {
            m_dragToken = -1;
        }
    }

    // Move-adjacent-then-click interaction: Hostile -> attack (combat comes
    // later), everyone else -> talk. Peaceful NPCs are never attacked.
    void interactWith(int tokenIdx) {
        int p = playerTokenIndex();
        if (p < 0) return;
        const Token& target = m_tokens[tokenIdx];
        if (!tokensAdjacent(m_tokens[p], target)) {
            m_hint = "Move next to " + target.name + " to interact.";
            m_hintTimer = 2.5f;
        } else if (target.attitude == Attitude::Hostile) {
            m_hint = "Combat in this scene isn't wired up yet.";
            m_hintTimer = 2.5f;
        } else {
            m_dialogActive = true;
            m_dialogName = target.name;
            m_dialogText = target.dialog.empty() ? "..." : target.dialog;
            m_dialogMerchant = target.merchant;
        }
    }

    // ── shop / trade ──
    static long totalCp(const rpgc::Character& c) {
        return c.platinum * 1000L + c.gold * 100L + c.silver * 10L + c.copper;
    }
    static void setCp(rpgc::Character& c, long cp) {
        if (cp < 0) cp = 0;
        c.platinum = 0;                       // gold is the working coin; don't hoard platinum
        c.gold     = (int)(cp / 100); cp %= 100;
        c.silver   = (int)(cp / 10);  cp %= 10;
        c.copper   = (int)cp;
    }
    // House Halewyn's "Merchant Ties" gift makes buying cheaper and selling dearer.
    bool merchantTies() const {
        int i = rpgw::houseIndexByName(m_pc.house);
        return i >= 0 && std::string(rpgw::houses()[i].trait) == "Merchant Ties";
    }
    long buyPriceCp(int listCp) const { return merchantTies() ? (long)listCp * 85 / 100 : listCp; }
    long sellGainCp(const std::string& name) const {
        int base = rpgs::sellPriceCp(name);
        return merchantTies() ? (long)base * 120 / 100 : base;
    }
    void buyWare(const rpgs::Ware& w) {
        long price = buyPriceCp(w.priceCp);
        if (totalCp(m_pc) < price) return;
        setCp(m_pc, totalCp(m_pc) - price);
        for (auto& it : m_pc.items) if (it.name == w.name) { it.qty++; return; }
        rpgc::Item it; it.name = w.name; it.qty = 1; it.weight = w.weight;
        m_pc.items.push_back(it);
    }
    void sellItem(int idx) {
        if (idx < 0 || idx >= (int)m_pc.items.size()) return;
        long gain = sellGainCp(m_pc.items[idx].name);
        if (gain <= 0) return;
        setCp(m_pc, totalCp(m_pc) + gain);
        if (--m_pc.items[idx].qty <= 0) m_pc.items.erase(m_pc.items.begin() + idx);
    }
    float packWeight() const {
        float w = 0.0f; for (const auto& it : m_pc.items) w += it.weight * it.qty; return w;
    }

    // Orlen's Wares (left, buy) beside Your Pack (right, sell).
    void renderShop() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(760, 476), ImGuiCond_Always);
        ImGui::Begin("Orlens Wares", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        ImGui::TextColored(ImVec4(0.76f, 0.63f, 0.42f, 1.0f), "Coin:  %s", rpgs::priceStr(totalCp(m_pc)).c_str());
        if (merchantTies()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "(Merchant Ties: better prices)"); }
        float encMax = m_pc.abilities[rpgc::STR] * 15.0f;   // 5e carrying capacity
        ImGui::SameLine(); ImGui::TextDisabled("     Load %.0f / %.0f lb", packWeight(), encMax);
        ImGui::Separator();

        // Left: buy
        ImGui::BeginChild("##wares", ImVec2(372, 366), true);
        ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.7f, 1.0f), "Orlen's Wares");
        ImGui::Separator();
        std::string cat;
        for (const auto& w : rpgs::orlensWares()) {
            if (cat != w.cat) { cat = w.cat; ImGui::Spacing(); ImGui::TextDisabled("%s", cat.c_str()); }
            long price = buyPriceCp(w.priceCp);
            ImGui::PushID(w.name);
            ImGui::BeginDisabled(totalCp(m_pc) < price);
            if (ImGui::SmallButton("Buy")) buyWare(w);
            ImGui::EndDisabled();
            ImGui::SameLine(); ImGui::Text("%-21s %s", w.name, rpgs::priceStr(price).c_str());
            const char* wst = rpgs::itemStats(w.name);
            if (*wst && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", wst);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Right: sell
        ImGui::BeginChild("##pack", ImVec2(0, 366), true);
        ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.7f, 1.0f), "Your Pack");
        ImGui::Separator();
        if (m_pc.items.empty()) ImGui::TextDisabled("(empty - buy some gear!)");
        int sellIdx = -1;
        for (int i = 0; i < (int)m_pc.items.size(); ++i) {
            const auto& it = m_pc.items[i];
            long gain = sellGainCp(it.name);
            ImGui::PushID(i);
            ImGui::BeginDisabled(gain <= 0);
            if (ImGui::SmallButton("Sell")) sellIdx = i;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (it.qty > 1) ImGui::Text("%s  x%d", it.name.c_str(), it.qty);
            else            ImGui::Text("%s", it.name.c_str());
            const char* ist = rpgs::itemStats(it.name);
            if (*ist && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ist);
            if (gain > 0) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", rpgs::priceStr(gain).c_str()); }
            ImGui::PopID();
        }
        if (sellIdx >= 0) sellItem(sellIdx);
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Done", ImVec2(120, 0))) m_shopOpen = false;
        ImGui::SameLine(); ImGui::TextDisabled("Orlen buys back his own goods at half the list price.");
        ImGui::End();
    }

    // The reachable region for a mover at (ax,ay) with `cells` of movement is a
    // square (Chebyshev metric), clamped to the board. Draw it as a sub-grid so
    // the individual reachable squares read clearly over the base grid.
    std::vector<glm::vec3> buildReach(int ax, int ay, int cells) const {
        std::vector<glm::vec3> lines;
        const float y = 0.03f;
        int c0 = std::max(0, ax - cells), c1 = std::min(kGridN - 1, ax + cells);
        int r0 = std::max(0, ay - cells), r1 = std::min(kGridN - 1, ay + cells);
        for (int c = c0; c <= c1 + 1; ++c) {
            lines.push_back({cellEdge(c), y, cellEdge(r0)});
            lines.push_back({cellEdge(c), y, cellEdge(r1 + 1)});
        }
        for (int r = r0; r <= r1 + 1; ++r) {
            lines.push_back({cellEdge(c0), y, cellEdge(r)});
            lines.push_back({cellEdge(c1 + 1), y, cellEdge(r)});
        }
        return lines;
    }

    std::vector<glm::vec3> buildRing(float cx, float cz, float radius, float y) const {
        std::vector<glm::vec3> pts;
        const int seg = 28;
        const float tau = 6.28318530718f;
        for (int i = 0; i < seg; ++i) {
            float a0 = tau * i / seg, a1 = tau * (i + 1) / seg;
            pts.push_back({cx + radius * std::cos(a0), y, cz + radius * std::sin(a0)});
            pts.push_back({cx + radius * std::cos(a1), y, cz + radius * std::sin(a1)});
        }
        return pts;
    }

    // ----- combat helpers -----
    int rollDie(int sides) { std::uniform_int_distribution<int> d(1, sides); return d(m_rng); }
    int rollD20() { return rollDie(20); }

    // Index of a living combatant standing on a cell, or -1.
    int combatantAt(int cx, int cy) const {
        const auto& cs = m_enc.combatants();
        for (int i = 0; i < static_cast<int>(cs.size()); ++i)
            if (!cs[i].isDown() && cs[i].cx == cx && cs[i].cy == cy) return i;
        return -1;
    }

    // Roll and resolve the active combatant's attack against a target, then log
    // it. Dodge on the target imposes disadvantage; a natural 20 doubles the
    // damage dice (rolled twice), per RAW.
    void doAttack(int targetId) {
        const rpgtt::Combatant& a = m_enc.active();
        const rpgtt::Combatant& t = m_enc.combatants()[targetId];
        bool adv = m_enc.isFlanking(m_enc.activeId(), targetId);   // house-rule flanking
        bool dis = t.dodging;                                      // target is Dodging
        // Advantage and disadvantage cancel to one straight roll, no matter how
        // many sources of each (RAW).
        int d20;
        const char* mode = "";
        if (adv == dis)   d20 = rollD20();
        else if (adv)   { d20 = std::max(rollD20(), rollD20()); mode = "  (flanking)"; }
        else            { d20 = std::min(rollD20(), rollD20()); mode = "  (disadvantage)"; }

        int sets = (d20 == 20) ? 2 : 1;
        int dice = 0;
        for (int s = 0; s < sets; ++s)
            for (int i = 0; i < a.dmgDice; ++i) dice += rollDie(a.dmgSides);
        rpgtt::AttackOutcome o = m_enc.attack(targetId, d20, dice);
        if (o.valid) logAttack(o, m_enc.combatants()[targetId], mode);
    }

    void logAttack(const rpgtt::AttackOutcome& o, const rpgtt::Combatant& t, const char* mode) {
        char buf[208];
        if (o.hit)
            std::snprintf(buf, sizeof buf, "%s %s %s (d20 %d%s)%s - %d dmg  [%s %d/%d]%s",
                          o.attacker.c_str(), o.crit ? "CRITS" : "hits", o.target.c_str(),
                          o.d20, o.crit ? "!" : "", mode, o.damage, t.name.c_str(), t.hp,
                          t.maxHp, o.dropped ? "  DOWN!" : "");
        else
            std::snprintf(buf, sizeof buf, "%s misses %s (%d vs AC %d)%s",
                          o.attacker.c_str(), o.target.c_str(), o.total, t.ac, mode);
        m_log.emplace_back(buf);
        if (m_log.size() > 5) m_log.erase(m_log.begin());
    }

    // Move the active combatant to (toX,toY), first resolving any opportunity
    // attacks provoked by leaving a foe's reach. Used by both the player (drag
    // release) and the AI. If an OA drops the mover, it falls where it stood.
    void commitMove(int toX, int toY) {
        if (!m_enc.hasActive() || !m_enc.canActiveReach(toX, toY, kGridN)) return;
        int me = m_enc.activeId();
        int fromX = m_enc.active().cx, fromY = m_enc.active().cy;
        if (toX == fromX && toY == fromY) return;
        for (int eid : m_enc.provokers(me, fromX, fromY, toX, toY)) {
            const rpgtt::Combatant& atk = m_enc.combatants()[eid];
            const rpgtt::Combatant& mov = m_enc.combatants()[me];
            int d20 = mov.dodging ? std::min(rollD20(), rollD20()) : rollD20();
            int sets = (d20 == 20) ? 2 : 1;
            int dice = 0;
            for (int s = 0; s < sets; ++s)
                for (int i = 0; i < atk.dmgDice; ++i) dice += rollDie(atk.dmgSides);
            rpgtt::AttackOutcome o = m_enc.opportunityAttack(eid, me, d20, dice);
            if (o.valid) logAttack(o, m_enc.combatants()[me], "  (opportunity)");
            if (m_enc.combatants()[me].isDown()) break;   // dropped mid-move
        }
        if (!m_enc.active().isDown()) m_enc.moveActiveTo(toX, toY, kGridN);
    }

    // ----- enemy AI driver -----
    // A foe's turn plays out over a few beats so it's watchable: move, then
    // strike, then end the turn. Heroes are left entirely to the player.
    // True once the party can no longer act or recover (no conscious or dying
    // heroes remain — all are dead or merely stable).
    bool partyDefeated() const {
        for (const auto& c : m_enc.combatants())
            if (!c.foe && (c.hp > 0 || c.isDying())) return false;
        return true;
    }

    // Roll one death save for the active dying hero and log the result.
    void autoDeathSave() {
        int d20 = rollD20();
        rpgtt::SaveResult r = m_enc.deathSave(d20);
        const rpgtt::Combatant& c = m_enc.active();
        const char* res = r == rpgtt::SaveResult::Revived    ? "REVIVES at 1 HP!"
                        : r == rpgtt::SaveResult::Stabilized ? "stabilizes"
                        : r == rpgtt::SaveResult::Died       ? "DIES"
                        : r == rpgtt::SaveResult::Success    ? "success"
                        : r == rpgtt::SaveResult::Fail       ? "failure" : "";
        char buf[192];
        std::snprintf(buf, sizeof buf, "%s death save (d20 %d): %s  [%d succ / %d fail]",
                      c.name.c_str(), d20, res, c.deathSuccesses, c.deathFailures);
        m_log.emplace_back(buf);
        if (m_log.size() > 5) m_log.erase(m_log.begin());
    }

    // Drives every non-player turn on timed beats: foe AI, and a dying hero's
    // automatic death save. Conscious heroes are left to the player.
    void stepAI(float dt) {
        if (!m_enc.hasActive()) return;
        int aid = m_enc.activeId();
        if (aid != m_lastActiveId) {          // a new turn just began
            m_lastActiveId = aid;
            m_aiPhase = 0;
            m_aiTimer = kAIMoveDelay;
            m_deathTurnActive = false;
        }
        if (partyDefeated()) return;
        const rpgtt::Combatant& a = m_enc.active();

        // A dying hero's turn: roll its death save, then end the turn — unless
        // the save revived it (nat 20), in which case the player takes over.
        if (a.isDying() && m_aiPhase == 0) m_deathTurnActive = true;
        if (m_deathTurnActive) {
            m_aiTimer -= dt;
            if (m_aiTimer <= 0.0f) {
                if (m_aiPhase == 0) {
                    autoDeathSave();
                    if (m_enc.active().hp > 0) m_deathTurnActive = false;   // revived
                    else { m_aiPhase = 2; m_aiTimer = kAIEndDelay; }
                } else {
                    m_enc.endTurn();
                    m_deathTurnActive = false;
                }
            }
            return;
        }

        if (!a.foe) return;                                    // conscious hero: player
        if (m_enc.living(true) == 0 || a.isDown()) return;     // fight won, or foe down
        m_aiTimer -= dt;
        if (m_aiTimer > 0.0f) return;
        if (m_aiPhase == 0)      { aiMove();   m_aiPhase = 1; m_aiTimer = kAIStrikeDelay; }
        else if (m_aiPhase == 1) { aiStrike(); m_aiPhase = 2; m_aiTimer = kAIEndDelay; }
        else                     { m_enc.endTurn(); }
    }

    // Move the active foe toward the nearest hero, engaging melee if it can.
    // If it can't reach, it Dashes to close (spending the action, so no attack).
    void aiMove() {
        int tid = m_enc.aiTarget();
        if (tid < 0) return;
        rpgtt::GridCell d = m_enc.aiDestination(tid, kGridN);
        commitMove(d.x, d.y);                       // may provoke from other heroes
        if (m_enc.active().isDown()) return;        // cut down on the approach
        const rpgtt::Combatant& a = m_enc.active();
        const rpgtt::Combatant& t = m_enc.combatants()[tid];
        if (rpgtt::cellDistance(a.cx, a.cy, t.cx, t.cy) > a.reachCells && !a.actionUsed) {
            if (m_enc.dash()) {
                rpgtt::GridCell d2 = m_enc.aiDestination(tid, kGridN);
                commitMove(d2.x, d2.y);
            }
        }
    }

    void aiStrike() {
        int tid = m_enc.aiTarget();
        if (tid >= 0 && m_enc.canAttack(tid)) doAttack(tid);
    }

    void renderUI() {
        ImGui::NewFrame();

        if (m_hasLevel && m_screen == Screen::Title) {
            renderTitleScreen();
            ImGui::Render();
            return;
        }
        if (m_hasLevel && m_screen == Screen::CharCreate) {
            renderCharCreate();
            ImGui::Render();
            return;
        }

        if (m_hasLevel) {
            // Pinned HUD: no move/resize so it can't wander over the viewport or
            // flash resize cursors that fight the camera for the mouse.
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::Begin("Level preview", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("%s", m_levelName.c_str());
            ImGui::Text("Floor plan:  X %.0f..%.0f ft", m_levelMin.x, m_levelMax.x);
            ImGui::Text("             Z %.0f..%.0f ft", m_levelMin.y, m_levelMax.y);
            ImGui::Text("Size:  %.0f x %.0f ft  (%.0f x %.0f squares)",
                        m_levelMax.x - m_levelMin.x, m_levelMax.y - m_levelMin.y,
                        (m_levelMax.x - m_levelMin.x) / 5.0f, (m_levelMax.y - m_levelMin.y) / 5.0f);
            ImGui::Separator();
            ImGui::TextDisabled("Left-drag your piece to move it");
            ImGui::TextDisabled("Move next to an NPC, click to talk");
            ImGui::TextDisabled("Right-drag orbit  \xc2\xb7  Middle-drag pan");
            ImGui::TextDisabled("Scroll zoom  \xc2\xb7  T = top-down tactical");
            ImGui::Separator();
            if (ImGui::Button(m_showRelations ? "Hide Relationships" : "Party & Relationships"))
                m_showRelations = !m_showRelations;
            ImGui::End();

            if (m_showRelations) renderRelationsPanel();

            // Transient hint (e.g. "move closer").
            if (m_hintTimer > 0.0f && !m_hint.empty()) {
                ImVec2 disp = ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, 44.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
                ImGui::Begin("##hint", nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoInputs);
                ImGui::TextUnformatted(m_hint.c_str());
                ImGui::End();
            }

            // Dialog box.
            if (m_dialogActive) {
                ImVec2 disp = ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.74f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Always);
                ImGui::Begin(m_dialogName.c_str(), nullptr,
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
                ImGui::TextWrapped("%s", m_dialogText.c_str());
                ImGui::Spacing();
                ImGui::Separator();
                if (m_dialogMerchant) {
                    if (ImGui::Button("Show me your wares", ImVec2(200, 0))) {
                        m_shopOpen = true; m_dialogActive = false;
                    }
                    ImGui::SameLine();
                }
                if (ImGui::Button("Close", ImVec2(120, 0))) m_dialogActive = false;
                ImGui::End();
            }

            if (m_shopOpen) renderShop();

            ImGui::Render();
            return;
        }

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Encounter");

        ImGui::Text("Round %d", m_enc.round());
        ImGui::Separator();

        // Initiative order, current mover marked and highlighted.
        ImGui::TextUnformatted("Initiative order");
        const auto& order = m_enc.order();
        for (int oi = 0; oi < static_cast<int>(order.size()); ++oi) {
            const auto& c = m_enc.combatants()[order[oi]];
            bool current = (oi == m_enc.turnIndex());
            ImGui::ColorButton((std::string("##sw") + std::to_string(oi)).c_str(),
                               ImVec4(c.cr, c.cg, c.cb, 1.0f),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs,
                               ImVec2(12, 12));
            ImGui::SameLine();
            if (c.dead)
                ImGui::TextDisabled("   %s  (dead)", c.name.c_str());
            else if (c.hp <= 0 && c.stable)
                ImGui::TextDisabled("   %s  (stable)", c.name.c_str());
            else if (c.hp <= 0)   // dying: show death-save tally
                ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.35f, 1.0f),
                                   "%s%s  dying (%d/%d)", current ? "> " : "   ",
                                   c.name.c_str(), c.deathSuccesses, c.deathFailures);
            else if (current)
                ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.4f, 1.0f),
                                   "> %s  %d/%d", c.name.c_str(), c.hp, c.maxHp);
            else
                ImGui::Text("   %s  %d/%d", c.name.c_str(), c.hp, c.maxHp);
        }

        ImGui::Separator();
        if (m_enc.hasActive()) {
            const rpgtt::Combatant& a = m_enc.active();
            ImGui::Text("Turn: %s%s", a.name.c_str(), a.foe ? "  (foe)" : "");

            // Action: Dash/Dodge/Disengage all spend the turn's one action, so
            // grey them out once it's used. Dash feeds straight back into the
            // movement budget (and the reachable highlight grows to match).
            ImGui::TextUnformatted("Action:");
            ImGui::SameLine();
            ImGui::BeginDisabled(a.actionUsed);
            if (ImGui::Button("Dash"))      m_enc.dash();
            ImGui::SameLine();
            if (ImGui::Button("Dodge"))     m_enc.dodge();
            ImGui::SameLine();
            if (ImGui::Button("Disengage")) m_enc.disengage();
            ImGui::EndDisabled();

            // Read state back live (the buttons above may have just changed it).
            const rpgtt::Combatant& a2 = m_enc.active();
            const char* actState = !a2.actionUsed ? "ready"
                                 : a2.dodging      ? "used (Dodging)"
                                 : a2.disengaging  ? "used (Disengaging)"
                                                   : "used";
            ImGui::Text("Action: %s", actState);
            ImGui::Text("Movement: %d ft  (%d squares)", a2.moveLeftFeet, m_enc.cellsLeft());
            ImGui::Text("Bonus action: %s   Reaction: %s",
                        a2.bonusUsed ? "used" : "ready",
                        a2.reactionUsed ? "used" : "ready");
        }

        // Victory when the foes are wiped; defeat only once no hero can act or
        // recover (a dying hero might still nat-20 back up).
        if (m_enc.living(true) == 0)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.45f, 1.0f), "Foes defeated - victory!");
        else if (partyDefeated())
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "The party has fallen.");

        ImGui::Spacing();
        if (ImGui::Button("End Turn")) m_enc.endTurn();
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            m_enc.combatants() = m_spawn;
            m_enc.start();
            m_dragging = false;
            m_lastActiveId = -1;   // let the AI re-init for whoever acts first
            m_deathTurnActive = false;
            m_log.clear();
        }

        if (!m_log.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Log");
            for (const auto& line : m_log) ImGui::TextWrapped("%s", line.c_str());
        }

        ImGui::Separator();
        ImGui::TextDisabled("Drag the highlighted mini to move");
        ImGui::TextDisabled("Click an adjacent foe to attack");
        ImGui::TextDisabled("Flank (ally opposite) = advantage");
        ImGui::TextDisabled("Leaving melee provokes; Disengage avoids");
        ImGui::TextDisabled("Middle-drag pan  \xc2\xb7  Scroll zoom");
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
    std::vector<uint32_t> m_miniHandles;   // one per combatant, indexed by combatant id
    std::vector<glm::vec3> m_grid;

    // Title -> character creation -> game flow (game/level mode only).
    enum class Screen { Title, CharCreate, Game };
    Screen m_screen = Screen::Game;   // set to Title when a level is loaded
    int    m_musicLoop = -1;          // title-music loop id (-1 = none)
    float  m_titlePulse = 0.0f;       // for the "press to begin" pulse

    // Character creation state.
    rpgc::Character m_pc;             // the player's character
    char  m_nameBuf[48] = "";
    int   m_raceIdx = 0, m_classIdx = 4;   // default class = Fighter
    int   m_rolled[6] = {0, 0, 0, 0, 0, 0};
    int   m_assign[6] = {0, 1, 2, 3, 4, 5};// ability a gets score m_rolled[m_assign[a]]
    int   m_rollsUsed = 0;                 // 1 initial + up to 2 re-rolls = 3 total
    int   m_bravery = 10;                  // rolled personality scores (shown in UI)
    int   m_narcissism = 10;
    int   m_willToPower = 10;
    int   m_carnality = 10;
    int   m_cruelty = 10;
    int   m_sociability = 10;
    int   m_skepticism = 10;
    int   m_honor = 10, m_piety = 10, m_greed = 10, m_temper = 10;
    int   m_diligence = 10, m_compassion = 10, m_curiosity = 10;
    std::array<bool, rpgc::ABILITY_COUNT> m_halfElfBonus{};  // Half-Elf: +1 to two of your choice
    std::array<bool, 18> m_skillPick{};    // chosen class skill proficiencies
    int m_bgIdx = 0;                       // chosen background index
    int m_alignIdx = 4;                    // chosen alignment (4 = True Neutral)
    int m_houseIdx = -1;                   // assigned house (index into rpgw::houses())
    std::string m_houseStanding;           // rung within the house, from background
    rpgw::Family m_family;                  // generated family tree
    rpgw::Origin m_origin;                  // for non-house races (outsiders)
    int m_elfIdx = -1;                       // elven lineage (index into rpgw::elfHouses())
    int m_dwarfIdx = -1;                      // dwarven clan (index into rpgw::dwarfClans())
    int m_dragonIdx = -1;                     // dragonborn clan (index into rpgw::dragonClans())
    int m_feyIdx = -1;                        // fey house (gnomes/halflings; rpgw::feyHouses())
    int m_infernalIdx = -1;                   // infernal house (tieflings; rpgw::infernalHouses())
    bool m_female = false;                  // succession favors men in Aldermarch
    std::vector<rpgcf::ClassScore> m_classRanked;   // class fit for the rolled scores

    // Portrait gallery (scanned from assets/portraits/, drop-and-appear).
    std::vector<Portrait> m_portraits;
    bool m_portraitsScanned = false;
    int  m_selectedPortrait = -1;      // confirmed portrait (via "Use This Portrait")
    int  m_previewPortrait  = -1;      // clicked/being-previewed portrait
    bool m_showAllPortraits = false;

    // Level preview (loaded from a terrain_editor .edenbin via TABLETOP_LEVEL)
    std::string m_levelPath, m_levelName;
    bool m_hasLevel = false;
    std::vector<uint32_t> m_levelMeshHandles;              // one per binary mesh
    struct LevelDraw { int meshIdx; glm::mat4 model; bool transparent; };
    std::vector<LevelDraw> m_levelDraws;                   // one per object
    glm::vec2 m_levelMin{0.0f}, m_levelMax{0.0f};          // floor-plan XZ bounds (feet)

    // Character tokens (GLB), placed on the level's 5-ft grid.
    std::vector<Token> m_tokens;
    int m_dragToken = -1;                  // token being dragged (level mode), or -1

    // Dialog + transient on-screen hint (interaction feedback).
    std::vector<rpgc::Character> m_companions;   // demo party for the relationships panel
    bool m_showRelations = false;
    bool m_dialogActive = false;
    bool m_dialogMerchant = false;   // the current NPC runs a shop
    bool m_shopOpen = false;         // Orlen's trade overlay is up
    std::string m_dialogName, m_dialogText;
    std::string m_hint;
    float m_hintTimer = 0.0f;

    // Free orbit camera (level mode): perspective free-look + ortho top-down (T).
    glm::vec3 m_camTarget{0.0f};
    float m_camYaw = -90.0f, m_camPitch = -45.0f;
    float m_camDist = 80.0f;               // perspective dolly distance
    bool  m_levelOrtho = false;            // T toggles the clean top-down ortho view
    glm::vec2 m_lastMouse{0.0f};
    bool m_haveLastMouse = false;

    rpgtt::Encounter m_enc;                // turn/round/movement state (rules in encounter.hpp)
    std::vector<rpgtt::Combatant> m_spawn; // starting layout, for Reset
    std::vector<std::string> m_log;        // recent combat-log lines (last 5)
    std::mt19937 m_rng{std::random_device{}()};  // dice RNG
    int  m_hoverCx = 0, m_hoverCy = 0;     // grid cell under the cursor this frame

    int   m_lastActiveId = -1;             // detect turn changes to (re)start the AI
    int   m_aiPhase = 0;                   // 0 = move, 1 = strike, 2 = end turn
    float m_aiTimer = 0.0f;                // seconds until the next AI beat
    bool  m_deathTurnActive = false;       // active hero is auto-rolling a death save

    bool      m_dragging = false;          // dragging the active mini
    bool      m_panning  = false;
    glm::vec2 m_panAnchor{0.0f, 0.0f};     // board point grabbed at pan start (world XZ)
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
