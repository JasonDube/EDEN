// Encounter — the tabletop's turn/round/movement rules, UI- and engine-free so
// it can be unit-tested on its own (see encounter_test.cpp). main.cpp owns all
// ImGui and rendering and drives this model; the grid here is purely logical
// (cell coordinates), with world<->cell mapping living in main.cpp.
//
// Movement uses the SRD's default grid metric: a diagonal step costs the same
// 5 ft as a straight step (Chebyshev distance). Each move spends movement equal
// to the distance from the mover's current cell, so multiple hops in one turn
// accumulate correctly.
#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace rpgtt {

constexpr int kFeetPerCell = 5;   // one grid square = 5 ft

struct Combatant {
    std::string name;
    int   initiative = 10;        // higher acts first
    int   speedFeet  = 30;        // walking speed (feet per turn)
    int   cx = 0, cy = 0;         // grid cell (column, row)
    float cr = 0.80f, cg = 0.20f, cb = 0.20f;  // display color
    bool  foe = false;            // team flag (heroes vs foes), for later use
    int   moveLeftFeet = 0;       // movement remaining this turn
};

// Chebyshev distance in cells: diagonals cost the same as orthogonal steps.
inline int cellDistance(int ax, int ay, int bx, int by) {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

class Encounter {
public:
    void add(const Combatant& c) { m_c.push_back(c); }
    void clear() { m_c.clear(); m_order.clear(); m_turn = 0; m_round = 1; }

    bool empty() const { return m_c.empty(); }
    int  round() const { return m_round; }
    int  turnIndex() const { return m_turn; }            // index into order()
    const std::vector<int>& order() const { return m_order; }  // combatant ids, init order
    std::vector<Combatant>&       combatants()       { return m_c; }
    const std::vector<Combatant>& combatants() const { return m_c; }

    // Roll call: sort combatants into initiative order (descending; ties keep
    // insertion order) and start round 1 with the first mover ready to move.
    void start() {
        m_order.resize(m_c.size());
        for (int i = 0; i < static_cast<int>(m_c.size()); ++i) m_order[i] = i;
        std::stable_sort(m_order.begin(), m_order.end(),
                         [&](int a, int b) { return m_c[a].initiative > m_c[b].initiative; });
        m_turn = 0;
        m_round = 1;
        refreshActive();
    }

    bool hasActive() const { return !m_order.empty(); }
    int  activeId() const { return m_order[m_turn]; }     // index into combatants()
    Combatant&       active()       { return m_c[m_order[m_turn]]; }
    const Combatant& active() const { return m_c[m_order[m_turn]]; }

    // Advance to the next combatant; wrapping past the last starts a new round.
    // The new mover's movement budget is refreshed.
    void endTurn() {
        if (m_order.empty()) return;
        ++m_turn;
        if (m_turn >= static_cast<int>(m_order.size())) { m_turn = 0; ++m_round; }
        refreshActive();
    }

    // Movement the active mover has left, in whole cells.
    int cellsLeft() const { return hasActive() ? active().moveLeftFeet / kFeetPerCell : 0; }

    // Can the active mover reach (x,y) on an N×N board with its remaining move?
    bool canActiveReach(int x, int y, int gridN) const {
        if (!hasActive() || x < 0 || y < 0 || x >= gridN || y >= gridN) return false;
        const Combatant& a = active();
        return cellDistance(a.cx, a.cy, x, y) * kFeetPerCell <= a.moveLeftFeet;
    }

    // Move the active mover to (x,y) if reachable, spending the path cost.
    bool moveActiveTo(int x, int y, int gridN) {
        if (!canActiveReach(x, y, gridN)) return false;
        Combatant& a = active();
        a.moveLeftFeet -= cellDistance(a.cx, a.cy, x, y) * kFeetPerCell;
        a.cx = x;
        a.cy = y;
        return true;
    }

private:
    void refreshActive() {
        if (!m_order.empty()) active().moveLeftFeet = active().speedFeet;
    }

    std::vector<Combatant> m_c;
    std::vector<int>       m_order;   // combatant ids sorted by initiative
    int m_turn  = 0;                  // index into m_order
    int m_round = 1;
};

}  // namespace rpgtt
