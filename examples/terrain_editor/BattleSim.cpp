#include "BattleSim.hpp"

// The bodies below are the code that was in main.cpp, moved rather than
// rewritten -- the battle behaves exactly as it did, and the only edits are
// the ones that turn reaching into the editor's members into asking the host.

#include "Editor/SceneObject.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Renderer/ModelRenderer.hpp"

#include <eden/Camera.hpp>
#include <eden/Input.hpp>
#include <eden/Terrain.hpp>
#include <eden/Window.hpp>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace eden;

void BattleSim::setEnabled(bool on) {
    if (m_enabled == on) return;
    m_enabled = on;
    if (!on) reset();
}

// Forget the battle, and take the units out of the world -- the same removal
// spawnTest does before a respawn, so disabling mid-level leaves no cubes.
void BattleSim::reset() {
    for (auto& u : m_battleUnits) {
        if (!u.obj) continue;
        for (auto it = sceneObjects().begin(); it != sceneObjects().end(); ++it) {
            if (it->get() == u.obj) { sceneObjects().erase(it); break; }
        }
    }
    m_battleUnits.clear();
    m_selectedUnits.clear();
    m_boxSelectActive = false;
}

// The keys that used to be bound editor-wide: B spawns, 1/2/3 reform blue,
// 8/9/0 reform red. They exist only while the sim is enabled.
void BattleSim::handleInput(bool isPlayMode, bool guiWantsKeys) {
    if (!m_enabled || !isPlayMode || guiWantsKeys) return;

    bool b = Input::isKeyDown(66); // GLFW_KEY_B
    if (b && !m_wasB) spawnTest();
    m_wasB = b;

    bool k1 = Input::isKeyDown(49), k2 = Input::isKeyDown(50), k3 = Input::isKeyDown(51);
    bool k8 = Input::isKeyDown(56), k9 = Input::isKeyDown(57), k0 = Input::isKeyDown(48);
    if (k1 && !m_was1) reformTeam(1, 5,  2, 2.0f, 1.5f);
    if (k2 && !m_was2) reformTeam(1, 2,  5, 2.0f, 1.5f);
    if (k3 && !m_was3) reformTeam(1, 10, 1, 4.0f, 0.0f);
    if (k8 && !m_was8) reformTeam(0, 10, 1, 4.0f, 0.0f);
    if (k9 && !m_was9) reformTeam(0, 5,  2, 2.0f, 1.5f);
    if (k0 && !m_was0) reformTeam(0, 2,  5, 2.0f, 1.5f);
    m_was1=k1; m_was2=k2; m_was3=k3; m_was8=k8; m_was9=k9; m_was0=k0;
}

void BattleSim::update(float dt) {
    if (!m_enabled) return;
    updateBattle(dt);
}

void BattleSim::spawnTest() {
    // Wipe any prior battle units
    for (auto& u : m_battleUnits) {
        if (!u.obj) continue;
        for (auto it = sceneObjects().begin(); it != sceneObjects().end(); ++it) {
            if (it->get() == u.obj) { sceneObjects().erase(it); break; }
        }
    }
    m_battleUnits.clear();

    // Spawn at fixed world positions: red square center (-25,0,0), blue (+25,0,0).
    // Each square is 50×50, so spawns are 25m from any edge of their square.
    auto rnd      = []() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); };
    auto rndRange = [&](float lo, float hi) { return lo + (hi - lo) * rnd(); };

    auto spawnTeam = [&](int team, const glm::vec3& origin, const glm::vec4& color, const char* prefix,
                         int cols, int rows, float colSp, float rowSp) {
        auto meshData = PrimitiveMeshBuilder::createCube(1.0f, color);
        int total = cols * rows;
        for (int i = 0; i < total; ++i) {
            int col = i % cols;
            int row = i / cols;

            static int s_unitSerial = 0;
            auto obj = std::make_unique<SceneObject>(std::string(prefix) + "_" + std::to_string(++s_unitSerial));
            uint32_t handle = m_host.models->createModel(meshData.vertices, meshData.indices);
            obj->setBufferHandle(handle);
            obj->setIndexCount(static_cast<uint32_t>(meshData.indices.size()));
            obj->setVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
            obj->setLocalBounds(meshData.bounds);
            obj->setPrimitiveType(PrimitiveType::Cube);
            obj->setPrimitiveSize(1.0f);
            obj->setPrimitiveColor(color);

            glm::vec3 pos = origin;
            pos.z += (static_cast<float>(col) - (cols - 1) * 0.5f) * colSp;
            pos.x += (static_cast<float>(row) - (rows - 1) * 0.5f) * rowSp
                     * (team == 0 ? +1.0f : -1.0f);
            pos.y = m_host.terrain->getHeightAt(pos.x, pos.z) + 0.5f;
            obj->getTransform().setPosition(pos);

            BattleUnit u;
            u.obj        = obj.get();
            u.team       = team;
            u.alive      = true;
            u.maxHp      = 25.0f;
            u.hp         = u.maxHp;
            u.maxSpeed   = rndRange(2.5f, 3.5f);
            u.aggression = rndRange(0.7f, 1.3f);
            u.wanderAmt  = rndRange(0.3f, 0.8f);
            m_battleUnits.push_back(u);
            sceneObjects().push_back(std::move(obj));
        }
    };

    // Spawns at the centers of two adjacent 50×50 grid cells (cells share X=0 edge).
    // Red cell:  X=-50..0,  Z=0..+50 → center (-25, 0, +25)
    // Blue cell: X=0..+50, Z=0..+50 → center (+25, 0, +25)
    spawnTeam(0, glm::vec3(-25.0f, 0.0f, +25.0f),
              glm::vec4(1.0f, 0.1f, 0.1f, 1.0f), "RedUnit",
              /*cols=*/5, /*rows=*/2, /*colSp=*/2.0f, /*rowSp=*/1.5f);
    spawnTeam(1, glm::vec3(+25.0f, 0.0f, +25.0f),
              glm::vec4(0.1f, 0.3f, 1.0f, 1.0f), "BlueUnit",
              /*cols=*/5, /*rows=*/2, /*colSp=*/2.0f, /*rowSp=*/1.5f);

    std::cout << "[Battle] Spawned 10 red vs 10 blue" << std::endl;
    if (m_host.showMessage) m_host.showMessage("Battle: 10 red vs 10 blue spawned");
}

// Teleport an alive team into a new formation (cols × rows) centered on its current
// center of mass, oriented to face the enemy team. Velocities reset.
void BattleSim::reformTeam(int team, int cols, int rows, float colSp, float rowSp) {
    glm::vec2 myCenter(0.0f), enemyCenter(0.0f);
    int myCount = 0, enemyCount = 0;
    for (auto& u : m_battleUnits) {
        if (!u.alive) continue;
        glm::vec3 p = u.obj->getTransform().getPosition();
        if (u.team == team) { myCenter += glm::vec2(p.x, p.z); myCount++; }
        else                { enemyCenter += glm::vec2(p.x, p.z); enemyCount++; }
    }
    if (myCount == 0) return;
    myCenter /= static_cast<float>(myCount);

    glm::vec2 forward(1.0f, 0.0f);
    if (enemyCount > 0) {
        enemyCenter /= static_cast<float>(enemyCount);
        glm::vec2 d = enemyCenter - myCenter;
        float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 0.001f) forward = d / len;
    }
    glm::vec2 lateral(-forward.y, forward.x);

    int slot = 0;
    for (auto& u : m_battleUnits) {
        if (!u.alive || u.team != team) continue;
        int col = slot % cols;
        int row = slot / cols;
        float lateralOff = (static_cast<float>(col) - (cols - 1) * 0.5f) * colSp;
        float forwardOff = -(static_cast<float>(row) - (rows - 1) * 0.5f) * rowSp;
        glm::vec2 xz = myCenter + lateral * lateralOff + forward * forwardOff;
        glm::vec3 newPos(xz.x, m_host.terrain->getHeightAt(xz.x, xz.y) + 0.5f, xz.y);
        u.obj->getTransform().setPosition(newPos);
        u.vel = glm::vec2(0.0f);  // reset so they don't drift from prior momentum
        slot++;
    }
    std::cout << "[Battle] Reformed team " << team << " into " << cols << "x" << rows << std::endl;
}

void BattleSim::updateBattle(float dt) {
    if (m_battleUnits.empty()) return;
    if (dt > 0.1f) dt = 0.1f;  // clamp big steps so the sim doesn't blow up

    // 1. Team centers (alive only)
    glm::vec2 centers[2] = { glm::vec2(0.0f), glm::vec2(0.0f) };
    int aliveCount[2]    = { 0, 0 };
    for (auto& u : m_battleUnits) {
        if (!u.alive) continue;
        glm::vec3 p = u.obj->getTransform().getPosition();
        centers[u.team] += glm::vec2(p.x, p.z);
        aliveCount[u.team]++;
    }
    if (aliveCount[0]) centers[0] /= static_cast<float>(aliveCount[0]);
    if (aliveCount[1]) centers[1] /= static_cast<float>(aliveCount[1]);

    // 2. Movement (boids: charge to enemy center + target seek + separation + wander)
    for (size_t i = 0; i < m_battleUnits.size(); ++i) {
        BattleUnit& u = m_battleUnits[i];
        if (!u.alive) continue;

        glm::vec3 p3 = u.obj->getTransform().getPosition();
        glm::vec2 pos(p3.x, p3.z);
        glm::vec2 force(0.0f);
        int enemy = 1 - u.team;

        // (a) Charge toward enemy team center
        if (aliveCount[enemy] > 0) {
            glm::vec2 toCenter = centers[enemy] - pos;
            float dSq = glm::dot(toCenter, toCenter);
            if (dSq > 9.0f) {
                float d = std::sqrt(dSq);
                force += (toCenter / d) * (1.2f * u.aggression * u.maxSpeed);
            }
        }

        // (b) Target reacquisition (every ~1s, or when current target is dead)
        u.targetTimer -= dt;
        BattleUnit* tgt = (u.targetIdx >= 0) ? &m_battleUnits[u.targetIdx] : nullptr;
        if (u.targetTimer <= 0.0f || !tgt || !tgt->alive) {
            float bestSq = 225.0f;  // 15m radius
            int   bestIdx = -1;
            for (size_t j = 0; j < m_battleUnits.size(); ++j) {
                BattleUnit& e = m_battleUnits[j];
                if (!e.alive || e.team == u.team) continue;
                glm::vec3 ep = e.obj->getTransform().getPosition();
                float dx = ep.x - pos.x, dz = ep.z - pos.y;
                float dSq = dx * dx + dz * dz;
                if (dSq < bestSq) { bestSq = dSq; bestIdx = static_cast<int>(j); }
            }
            u.targetIdx   = bestIdx;
            u.targetTimer = 1.0f;
            tgt = (bestIdx >= 0) ? &m_battleUnits[bestIdx] : nullptr;
        }

        // (c) Engage current target if it's between 1.5m and 15m
        if (tgt && tgt->alive) {
            glm::vec3 tp = tgt->obj->getTransform().getPosition();
            glm::vec2 toT(tp.x - pos.x, tp.z - pos.y);
            float dSq = glm::dot(toT, toT);
            if (dSq > 2.25f && dSq < 225.0f) {
                float d = std::sqrt(dSq);
                force += (toT / d) * (0.8f * u.aggression * u.maxSpeed);
            }
        }

        // (d) Separation from neighbors within 1.5m
        for (size_t j = 0; j < m_battleUnits.size(); ++j) {
            if (j == i) continue;
            BattleUnit& o = m_battleUnits[j];
            if (!o.alive) continue;
            glm::vec3 op = o.obj->getTransform().getPosition();
            float dx = pos.x - op.x, dz = pos.y - op.z;
            float dSq = dx * dx + dz * dz;
            if (dSq > 0.01f && dSq < 2.25f) {
                float d = std::sqrt(dSq);
                float strength = ((1.5f - d) / 1.5f) * 1.5f * u.maxSpeed;
                force += glm::vec2(dx, dz) / d * strength;
            }
        }

        // (e) Wander noise
        auto rnd01 = []() { return static_cast<float>(rand()) / static_cast<float>(RAND_MAX); };
        float jx = (rnd01() - 0.5f) * 2.0f * u.wanderAmt * u.maxSpeed;
        float jz = (rnd01() - 0.5f) * 2.0f * u.wanderAmt * u.maxSpeed;
        force += glm::vec2(jx, jz);

        // Smooth velocity toward force (framerate-independent ≈ 0.2 lerp at 60fps)
        float alpha = 1.0f - std::exp(-12.0f * dt);
        u.vel += (force - u.vel) * alpha;

        // Cap speed
        float spdSq = glm::dot(u.vel, u.vel);
        float maxSq = u.maxSpeed * u.maxSpeed;
        if (spdSq > maxSq) u.vel *= u.maxSpeed / std::sqrt(spdSq);

        // Integrate
        glm::vec2 newXZ = pos + u.vel * dt;
        p3.x = newXZ.x;
        p3.z = newXZ.y;
        u.obj->getTransform().setPosition(p3);
    }

    // 3. Resolve cube overlap
    const float contact = 1.0f;
    for (size_t i = 0; i < m_battleUnits.size(); ++i) {
        BattleUnit& a = m_battleUnits[i];
        if (!a.alive) continue;
        glm::vec3 ap = a.obj->getTransform().getPosition();
        for (size_t j = i + 1; j < m_battleUnits.size(); ++j) {
            BattleUnit& b = m_battleUnits[j];
            if (!b.alive) continue;
            glm::vec3 bp = b.obj->getTransform().getPosition();
            float dx = bp.x - ap.x, dz = bp.z - ap.z;
            float ax = std::abs(dx), az = std::abs(dz);
            if (ax < contact && az < contact) {
                if (contact - ax < contact - az) {
                    float push = (contact - ax) * 0.5f + 0.001f;
                    if (dx >= 0) { ap.x -= push; bp.x += push; }
                    else         { ap.x += push; bp.x -= push; }
                } else {
                    float push = (contact - az) * 0.5f + 0.001f;
                    if (dz >= 0) { ap.z -= push; bp.z += push; }
                    else         { ap.z += push; bp.z -= push; }
                }
                a.obj->getTransform().setPosition(ap);
                b.obj->getTransform().setPosition(bp);
            }
        }
    }

    // 4. Combat — d20 ≥ 10 to hit, d6 damage, 0.25s cooldown, 1.5m reach
    for (auto& u : m_battleUnits) {
        if (!u.alive) continue;
        if (u.cooldown > 0.0f) { u.cooldown -= dt; continue; }
        BattleUnit* tgt = (u.targetIdx >= 0) ? &m_battleUnits[u.targetIdx] : nullptr;
        if (!tgt || !tgt->alive) continue;

        glm::vec3 up = u.obj->getTransform().getPosition();
        glm::vec3 tp = tgt->obj->getTransform().getPosition();
        float dx = tp.x - up.x, dz = tp.z - up.z;
        if (dx * dx + dz * dz < 2.25f) {
            int roll = (rand() % 20) + 1;
            if (roll >= 10) {
                int dmg = (rand() % 6) + 1;
                tgt->hp -= static_cast<float>(dmg);
                if (tgt->hp <= 0.0f) {
                    tgt->alive = false;
                    if (tgt->obj) tgt->obj->setVisible(false);
                }
            }
            u.cooldown = 0.25f;
        }
    }

    // 5. Snap living units to terrain
    for (auto& u : m_battleUnits) {
        if (!u.alive) continue;
        glm::vec3 p = u.obj->getTransform().getPosition();
        p.y = m_host.terrain->getHeightAt(p.x, p.z) + 0.5f;
        u.obj->getTransform().setPosition(p);
    }
}

// HP bar over each living, damaged unit (skips full-HP to reduce clutter).
void BattleSim::renderHpBars() {
    if (m_battleUnits.empty()) return;
    float screenW = static_cast<float>(m_host.window->getWidth());
    float screenH = static_cast<float>(m_host.window->getHeight());
    float aspect  = screenW / screenH;

    glm::mat4 view = m_host.camera->getViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 5000.0f);
    glm::mat4 vp = proj * view;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float barW = 36.0f, barH = 5.0f;

    for (const auto& u : m_battleUnits) {
        if (!u.alive || !u.obj) continue;
        if (u.hp >= u.maxHp) continue;  // hide bar at full HP

        glm::vec3 p = u.obj->getTransform().getPosition();
        glm::vec3 above = p + glm::vec3(0.0f, 1.1f, 0.0f);
        glm::vec4 clip = vp * glm::vec4(above, 1.0f);
        if (clip.w <= 0.001f) continue;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z <= 0.0f || ndc.z >= 1.0f) continue;
        float sx = (ndc.x *  0.5f + 0.5f) * screenW;
        float sy = (ndc.y * -0.5f + 0.5f) * screenH;

        float ratio = std::clamp(u.hp / u.maxHp, 0.0f, 1.0f);
        ImVec2 tl(sx - barW * 0.5f, sy - barH * 0.5f);
        ImVec2 br(sx + barW * 0.5f, sy + barH * 0.5f);
        dl->AddRectFilled(tl, br, IM_COL32(50, 0, 0, 220));
        if (ratio > 0.0f) {
            ImVec2 fillBr(tl.x + barW * ratio, br.y);
            ImU32 col = (ratio > 0.5f)  ? IM_COL32(0, 200, 0, 230)
                      : (ratio > 0.25f) ? IM_COL32(220, 200, 0, 230)
                                        : IM_COL32(220, 60, 0, 230);
            dl->AddRectFilled(tl, fillBr, col);
        }
        dl->AddRect(tl, br, IM_COL32(0, 0, 0, 200));
    }
}

// 5-ft (5-unit) grid matching the game board, over a window around the working
// focus. White lines; off by default, toggled with G. Each line is subdivided
// so the segments hug terrain height.
// RTS-style unit selection: LMB click picks one, LMB-drag box-selects many.
// Shift adds to selection. Requires play-mode cursor visible (Alt+RMB toggles).
void BattleSim::updateSelection(bool isPlayMode) {
    if (!isPlayMode) return;
    if (m_battleUnits.empty()) { m_selectedUnits.clear(); m_boxSelectActive = false; return; }

    if (!(*m_host.cursorVisible)) {
        // Cancel any in-progress drag if cursor mode flips off
        m_boxSelectActive = false;
        return;
    }

    // Don't start selections through ImGui windows, but always allow finishing one
    ImGuiIO& io = ImGui::GetIO();
    bool overUI = io.WantCaptureMouse && !m_boxSelectActive;

    float windowW = static_cast<float>(m_host.window->getWidth());
    float windowH = static_cast<float>(m_host.window->getHeight());
    float aspect  = windowW / windowH;
    glm::mat4 view = m_host.camera->getViewMatrix();
    glm::mat4 proj = m_host.camera->getProjectionMatrix(aspect, 0.1f, 5000.0f);
    glm::mat4 vp   = proj * view;

    auto projectToScreen = [&](const glm::vec3& worldPos, glm::vec2& out) -> bool {
        glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        out.x = (ndc.x + 1.0f) * 0.5f * windowW;
        out.y = (1.0f - ndc.y) * 0.5f * windowH;
        return true;
    };

    glm::vec2 mousePos = Input::getMousePosition();
    bool lmbDown    = Input::isMouseButtonDown(Input::MOUSE_LEFT);
    bool shiftHeld  = Input::isKeyDown(Input::KEY_LEFT_SHIFT) || Input::isKeyDown(Input::KEY_RIGHT_SHIFT);

    static bool wasLmbDown = false;
    bool lmbPressed  =  lmbDown && !wasLmbDown;
    bool lmbReleased = !lmbDown &&  wasLmbDown;
    wasLmbDown = lmbDown;

    if (lmbPressed && !overUI) {
        m_boxSelectActive = true;
        m_boxSelectStart  = mousePos;
        m_boxSelectEnd    = mousePos;
    } else if (lmbDown && m_boxSelectActive) {
        m_boxSelectEnd = mousePos;
    } else if (lmbReleased && m_boxSelectActive) {
        glm::vec2 delta = m_boxSelectEnd - m_boxSelectStart;
        float dragLen = std::sqrt(delta.x * delta.x + delta.y * delta.y);

        std::set<int> hits;
        if (dragLen < 4.0f) {
            // Click: pick nearest alive unit within 30px of cursor
            int   bestIdx  = -1;
            float bestDist = 30.0f;
            for (size_t i = 0; i < m_battleUnits.size(); ++i) {
                const BattleUnit& u = m_battleUnits[i];
                if (!u.alive || !u.obj) continue;
                glm::vec2 sp;
                if (!projectToScreen(u.obj->getTransform().getPosition(), sp)) continue;
                float dx = sp.x - mousePos.x, dy = sp.y - mousePos.y;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d < bestDist) { bestDist = d; bestIdx = static_cast<int>(i); }
            }
            if (bestIdx >= 0) hits.insert(bestIdx);
        } else {
            float minX = std::min(m_boxSelectStart.x, m_boxSelectEnd.x);
            float maxX = std::max(m_boxSelectStart.x, m_boxSelectEnd.x);
            float minY = std::min(m_boxSelectStart.y, m_boxSelectEnd.y);
            float maxY = std::max(m_boxSelectStart.y, m_boxSelectEnd.y);
            for (size_t i = 0; i < m_battleUnits.size(); ++i) {
                const BattleUnit& u = m_battleUnits[i];
                if (!u.alive || !u.obj) continue;
                glm::vec2 sp;
                if (!projectToScreen(u.obj->getTransform().getPosition(), sp)) continue;
                if (sp.x >= minX && sp.x <= maxX && sp.y >= minY && sp.y <= maxY) {
                    hits.insert(static_cast<int>(i));
                }
            }
        }

        if (shiftHeld) {
            for (int idx : hits) m_selectedUnits.insert(idx);
        } else {
            m_selectedUnits = hits;
        }
        m_boxSelectActive = false;
    }

    // --- Render: drag box + selection rings ---
    auto* drawList = ImGui::GetForegroundDrawList();

    if (m_boxSelectActive) {
        float minX = std::min(m_boxSelectStart.x, m_boxSelectEnd.x);
        float maxX = std::max(m_boxSelectStart.x, m_boxSelectEnd.x);
        float minY = std::min(m_boxSelectStart.y, m_boxSelectEnd.y);
        float maxY = std::max(m_boxSelectStart.y, m_boxSelectEnd.y);
        drawList->AddRectFilled(ImVec2(minX, minY), ImVec2(maxX, maxY), IM_COL32(80, 220, 120, 40));
        drawList->AddRect      (ImVec2(minX, minY), ImVec2(maxX, maxY), IM_COL32(120, 255, 160, 220), 0.0f, 0, 1.5f);
    }

    // Rings around each selected unit. Centered on the unit, radius computed
    // from a camera-right offset so it stays aligned with the screen plane.
    glm::vec3 camRight = m_host.camera->getRight();
    for (int idx : m_selectedUnits) {
        if (idx < 0 || idx >= static_cast<int>(m_battleUnits.size())) continue;
        const BattleUnit& u = m_battleUnits[idx];
        if (!u.alive || !u.obj) continue;
        glm::vec3 center = u.obj->getTransform().getPosition();
        glm::vec2 sp, spEdge;
        if (!projectToScreen(center, sp)) continue;
        float radius = 12.0f;
        if (projectToScreen(center + camRight * 0.9f, spEdge)) {
            radius = std::clamp(glm::length(spEdge - sp), 8.0f, 60.0f);
        }
        drawList->AddCircle(ImVec2(sp.x, sp.y), radius, IM_COL32(120, 255, 160, 220), 28, 2.0f);
    }
}