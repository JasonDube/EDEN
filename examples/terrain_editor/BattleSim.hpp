#pragma once

// The battle test -- two squads of cubes, boids movement, d20/d6 combat,
// RTS-style box selection and formation hotkeys -- as a thing you can put down.
//
// It lived directly in main.cpp, which meant it ran (and bound B, 1/2/3, 8/9/0,
// and a chunk of the left mouse button) in EVERY level, editor-wide, whether
// the level was about squad battles or a shipyard. A whole different game idea
// keyed into everything. Same story and same shape as TribeSim: the host owns
// the world, this owns the battle, the Host struct below is the entire
// coupling, and it is OFF unless a level asks for it (M panel -> Battle sim).
//
// Ported originally from Desktop/PYTHON_PROJECTS/war_game/boids_battle.py.

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace eden {
class Camera;
class ModelRenderer;
class SceneObject;
class Terrain;
class Window;
}

struct BattleSimHost {
    std::vector<std::unique_ptr<eden::SceneObject>>* sceneObjects = nullptr;
    eden::Terrain*       terrain = nullptr;
    eden::Camera*        camera  = nullptr;
    eden::ModelRenderer* models  = nullptr;
    eden::Window*        window  = nullptr;

    // Selection needs to know whether the play-mode cursor is free.
    const bool* cursorVisible = nullptr;

    // The host's transient on-screen message line.
    std::function<void(const std::string&)> showMessage;
};

class BattleSim {
public:
    void setHost(const BattleSimHost& host) { m_host = host; }

    // Off unless a level asks for it. This is the whole point of the file.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    // Forget every unit and selection, removing unit objects from the scene.
    void reset();

    // B spawns the two squads; 1/2/3 reform blue, 8/9/0 reform red.
    void handleInput(bool isPlayMode, bool guiWantsKeys);

    // One frame of combat. Play mode only; the host gates it.
    void update(float dt);

    // Overlays: HP bars over units, and the box-select rectangle.
    void renderHpBars();
    void updateSelection(bool isPlayMode);

    std::size_t unitCount() const { return m_battleUnits.size(); }

    // Public for the empty-level check, which spawns without a keyboard.
    void spawnTest();

private:
    std::vector<std::unique_ptr<eden::SceneObject>>& sceneObjects() { return *m_host.sceneObjects; }

    void reformTeam(int team, int cols, int rows, float colSp, float rowSp);
    void updateBattle(float dt);

    BattleSimHost m_host;
    bool m_enabled = false;

    // Formation/spawn key edges (were function-local statics in main.cpp).
    bool m_wasB = false;
    bool m_was1 = false, m_was2 = false, m_was3 = false;
    bool m_was8 = false, m_was9 = false, m_was0 = false;

    // Battle test (B key): 10 red vs 10 blue 1m cubes — boids movement + d20/d6 combat.
    // Ported from Desktop/PYTHON_PROJECTS/war_game/boids_battle.py
    struct BattleUnit {
        eden::SceneObject* obj = nullptr;
        int team = 0;                  // 0 = red, 1 = blue
        bool alive = true;
        glm::vec2 vel{0.0f, 0.0f};     // XZ velocity (m/s); .y holds Z
        float hp = 25.0f;
        float maxHp = 25.0f;
        float maxSpeed = 3.0f;
        float aggression = 1.0f;
        float wanderAmt = 0.5f;
        float cooldown = 0.0f;         // s until next attack
        int   targetIdx = -1;
        float targetTimer = 0.0f;      // s until target reacquire
    };
    std::vector<BattleUnit> m_battleUnits;
    // RTS-style unit selection (LMB click + LMB-drag box)
    std::set<int> m_selectedUnits;       // indices into m_battleUnits
    bool m_boxSelectActive = false;
    glm::vec2 m_boxSelectStart{0.0f};    // pixel space, top-left origin
    glm::vec2 m_boxSelectEnd{0.0f};
};
