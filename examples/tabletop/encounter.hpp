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

    // Action economy for the current turn (all refreshed at the start of a turn).
    bool  actionUsed   = false;   // the one action is spent
    bool  bonusUsed    = false;   // the one bonus action is spent
    bool  reactionUsed = false;   // reaction spent (refreshes at the start of your turn)
    bool  dodging      = false;   // Dodge: attackers have disadvantage until your next turn
    bool  disengaging  = false;   // Disengage: movement doesn't provoke opportunity attacks

    // Combat stats. Damage is `dmgDice`d`dmgSides` + `dmgBonus` (e.g. 1d8+3).
    int   maxHp = 10;
    int   hp    = 10;
    int   ac    = 12;
    int   attackBonus = 4;
    int   dmgDice  = 1;
    int   dmgSides = 6;
    int   dmgBonus = 2;
    int   reachCells = 1;         // melee reach, in cells (1 = adjacent incl. diagonal)

    bool isDown() const { return hp <= 0; }
};

// Result of resolving one attack (returned by Encounter::attack).
struct AttackOutcome {
    bool valid = false;   // false if the attack was illegal (no state changed)
    bool hit   = false;
    bool crit  = false;
    int  d20   = 0;
    int  total = 0;       // d20 + attacker's attack bonus
    int  damage = 0;      // 0 on a miss
    bool dropped = false; // this attack reduced the target to 0 HP
    std::string attacker;
    std::string target;
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

    // Advance to the next living combatant; wrapping past the last starts a new
    // round. Downed combatants are skipped. The new mover's turn resources are
    // refreshed. If nobody is left standing, we stop on the next slot as-is.
    void endTurn() {
        if (m_order.empty()) return;
        for (int guard = 0; guard < static_cast<int>(m_order.size()); ++guard) {
            ++m_turn;
            if (m_turn >= static_cast<int>(m_order.size())) { m_turn = 0; ++m_round; }
            if (!active().isDown()) break;
        }
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

    // ----- actions in combat (all spend the turn's single action) -----

    // Dash: gain extra movement equal to your speed.
    bool dash() {
        if (!hasActive() || active().actionUsed) return false;
        active().actionUsed = true;
        active().moveLeftFeet += active().speedFeet;
        return true;
    }
    // Dodge: attackers roll against you at disadvantage until your next turn.
    bool dodge() {
        if (!hasActive() || active().actionUsed) return false;
        active().actionUsed = true;
        active().dodging = true;
        return true;
    }
    // Disengage: your movement this turn doesn't provoke opportunity attacks.
    bool disengage() {
        if (!hasActive() || active().actionUsed) return false;
        active().actionUsed = true;
        active().disengaging = true;
        return true;
    }
    // Bonus action / reaction: tracked here so features and triggers can spend
    // them later (no default source of either yet).
    bool useBonusAction() {
        if (!hasActive() || active().bonusUsed) return false;
        active().bonusUsed = true;
        return true;
    }
    bool useReaction() {
        if (!hasActive() || active().reactionUsed) return false;
        active().reactionUsed = true;
        return true;
    }

    // ----- attacks -----

    // May the active combatant attack combatant `targetId` right now? Requires
    // an unspent action, a living foe of the opposite team within melee reach.
    bool canAttack(int targetId) const {
        if (!hasActive() || active().actionUsed) return false;
        if (targetId < 0 || targetId >= static_cast<int>(m_c.size())) return false;
        if (targetId == activeId()) return false;
        const Combatant& a = active();
        const Combatant& t = m_c[targetId];
        if (a.isDown() || t.isDown() || t.foe == a.foe) return false;
        return cellDistance(a.cx, a.cy, t.cx, t.cy) <= a.reachCells;
    }

    // Resolve an attack by the active combatant against `targetId`, spending the
    // action. The caller supplies the rolls: `d20` (already the lower of two on
    // disadvantage / higher on advantage) and `damageDiceTotal` (the summed
    // damage dice — the caller rolls the dice a second time on a crit, since RAW
    // doubles the dice, not the flat modifier). A natural 20 always hits and
    // crits; a natural 1 always misses.
    AttackOutcome attack(int targetId, int d20, int damageDiceTotal) {
        AttackOutcome o;
        if (!canAttack(targetId)) return o;   // o.valid == false
        Combatant& a = active();
        Combatant& t = m_c[targetId];
        a.actionUsed = true;
        o.valid = true;
        o.attacker = a.name;
        o.target = t.name;
        o.d20 = d20;
        o.crit = (d20 == 20);
        o.total = d20 + a.attackBonus;
        o.hit = o.crit || (d20 != 1 && o.total >= t.ac);
        if (o.hit) {
            o.damage = std::max(0, damageDiceTotal + a.dmgBonus);
            t.hp = std::max(0, t.hp - o.damage);
            o.dropped = t.isDown();
        }
        return o;
    }

    // Flanking (house rule — the DMG optional grid variant, not core SRD):
    // the attacker and a living ally on the exact opposite side/corner of the
    // target, both adjacent, grant the attacker advantage on the melee attack.
    // On a grid, "opposite" means the ally stands on the target cell reflected
    // through — i.e. attacker + ally sum to twice the target's cell.
    bool isFlanking(int attackerId, int targetId) const {
        if (attackerId < 0 || attackerId >= static_cast<int>(m_c.size())) return false;
        if (targetId  < 0 || targetId  >= static_cast<int>(m_c.size())) return false;
        const Combatant& a = m_c[attackerId];
        const Combatant& t = m_c[targetId];
        if (a.isDown() || t.isDown()) return false;
        if (cellDistance(a.cx, a.cy, t.cx, t.cy) != 1) return false;  // attacker adjacent
        int ox = 2 * t.cx - a.cx, oy = 2 * t.cy - a.cy;              // opposite cell
        for (int i = 0; i < static_cast<int>(m_c.size()); ++i) {
            if (i == attackerId || i == targetId) continue;
            const Combatant& ally = m_c[i];
            if (ally.isDown() || ally.foe != a.foe) continue;         // must be attacker's ally
            if (ally.cx == ox && ally.cy == oy) return true;
        }
        return false;
    }

    // Count living combatants on a side — for victory/defeat checks.
    int living(bool foe) const {
        int n = 0;
        for (const auto& c : m_c) if (c.foe == foe && !c.isDown()) ++n;
        return n;
    }

private:
    // Refresh the active mover's per-turn resources at the start of its turn.
    void refreshActive() {
        if (m_order.empty()) return;
        Combatant& a = active();
        a.moveLeftFeet = a.speedFeet;
        a.actionUsed = a.bonusUsed = a.reactionUsed = false;
        a.dodging = a.disengaging = false;
    }

    std::vector<Combatant> m_c;
    std::vector<int>       m_order;   // combatant ids sorted by initiative
    int m_turn  = 0;                  // index into m_order
    int m_round = 1;
};

}  // namespace rpgtt
