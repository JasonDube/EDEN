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

#include "encounter.hpp"

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
#include <cmath>
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

// World<->cell mapping. Cells are indexed [0, kGridN); cell centers sit on the
// grid squares, cell edges on the grid lines.
inline float cellCenter(int c) { return -kBoardHalf + (c + 0.5f) * kGridStep; }
inline float cellEdge(int i)   { return -kBoardHalf + i * kGridStep; }
inline int   worldToCell(float w) {
    return std::clamp(static_cast<int>(std::floor((w + kBoardHalf) / kGridStep)), 0, kGridN - 1);
}
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
        // ModelRenderer draws glm's GL-convention (Y-up) projection into a
        // positive-height Vulkan viewport with no flip, which mirrors the frame
        // vertically. computeViewProj() is the un-mirrored matrix, so mirror the
        // screen-Y here (2*m.y/h - 1, not 1 - 2*m.y/h) to unproject onto the
        // board the way it's actually displayed — otherwise the Z axis inverts.
        float ndcX = 2.0f * m.x / w - 1.0f;
        float ndcY = 2.0f * m.y / h - 1.0f;
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

        if (Input::isMouseButtonPressed(Input::MOUSE_LEFT) && !overUI && overBoard &&
            m_enc.hasActive()) {
            const auto& a = m_enc.active();
            if (m_hoverCx == a.cx && m_hoverCy == a.cy) {
                m_dragging = true;              // grabbed its own cell: move it
            } else {
                int tid = combatantAt(m_hoverCx, m_hoverCy);   // clicked another mini?
                if (tid >= 0 && m_enc.canAttack(tid)) doAttack(tid);
            }
        }
        if (m_dragging && !Input::isMouseButtonDown(Input::MOUSE_LEFT)) {
            // Released: commit if reachable (moveActiveTo rejects illegal moves).
            if (overBoard) m_enc.moveActiveTo(m_hoverCx, m_hoverCy, kGridN);
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

    void renderUI() {
        ImGui::NewFrame();
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
            if (c.isDown())
                ImGui::TextDisabled("   %s  (down)", c.name.c_str());
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

        // Victory / defeat once a side is wiped out.
        int foesLeft = m_enc.living(true), heroesLeft = m_enc.living(false);
        if (foesLeft == 0)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.45f, 1.0f), "Foes defeated - victory!");
        else if (heroesLeft == 0)
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "The party has fallen.");

        ImGui::Spacing();
        if (ImGui::Button("End Turn")) m_enc.endTurn();
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            m_enc.combatants() = m_spawn;
            m_enc.start();
            m_dragging = false;
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

    rpgtt::Encounter m_enc;                // turn/round/movement state (rules in encounter.hpp)
    std::vector<rpgtt::Combatant> m_spawn; // starting layout, for Reset
    std::vector<std::string> m_log;        // recent combat-log lines (last 5)
    std::mt19937 m_rng{std::random_device{}()};  // dice RNG
    int  m_hoverCx = 0, m_hoverCy = 0;     // grid cell under the cursor this frame

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
