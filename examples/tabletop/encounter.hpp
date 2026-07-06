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
    // Optional attack-bonus breakdown for the combat log. The hero fills these in
    // (prof + ability split); monsters carry a flat SRD bonus and leave this off.
    bool  hasAtkBreakdown = false;
    int   atkProf = 0;            // proficiency portion of attackBonus
    int   atkAbil = 0;            // ability-mod portion of attackBonus
    int   dmgAbil = 0;            // ability-mod portion of dmgBonus
    const char* atkAbilName = "STR";

    // Dying state (heroes only — foes die outright at 0 HP). A creature at 0 HP
    // is unconscious; if not yet dead/stable it is "dying" and makes a death
    // saving throw at the start of each of its turns.
    int   deathSuccesses = 0;
    int   deathFailures  = 0;
    bool  stable = false;         // stabilized at 0 HP (no longer making saves)
    bool  dead   = false;

    bool isDown()  const { return hp <= 0; }                         // unconscious or dead
    bool isDying() const { return hp <= 0 && !dead && !stable; }     // still making saves
    bool isOut()   const { return dead || stable; }                 // no longer takes turns
};

// Outcome of a single death saving throw (for logging / feedback).
enum class SaveResult { None, Success, Fail, Stabilized, Died, Revived };

struct GridCell { int x = 0, y = 0; };

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
        // Skip combatants that are out of the fight (dead or stabilized). A dying
        // hero is NOT skipped — it still gets a turn to make its death save.
        for (int guard = 0; guard < static_cast<int>(m_order.size()); ++guard) {
            ++m_turn;
            if (m_turn >= static_cast<int>(m_order.size())) { m_turn = 0; ++m_round; }
            if (!active().isOut()) break;
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
        if (!canAttack(targetId)) return {};   // valid == false
        active().actionUsed = true;
        return resolveAttack(activeId(), targetId, d20, damageDiceTotal);
    }

    // Core hit/damage math shared by normal attacks and opportunity attacks.
    // No action-economy side effects — the caller decides what resource it costs.
    AttackOutcome resolveAttack(int attackerId, int targetId, int d20, int damageDiceTotal) {
        AttackOutcome o;
        Combatant& a = m_c[attackerId];
        Combatant& t = m_c[targetId];
        o.valid = true;
        o.attacker = a.name;
        o.target = t.name;
        o.d20 = d20;
        o.crit = (d20 == 20);
        o.total = d20 + a.attackBonus;
        o.hit = o.crit || (d20 != 1 && o.total >= t.ac);
        if (o.hit) {
            o.damage = std::max(0, damageDiceTotal + a.dmgBonus);
            bool wasUp = t.hp > 0;
            applyDamage(t, o.damage, o.crit);
            o.dropped = wasUp && t.hp <= 0;   // this hit knocked them out
        }
        return o;
    }

    // Apply damage, handling the 0-HP rules: foes die outright; a hero dropped
    // to 0 falls unconscious and dying (massive damage = instant death); a hero
    // already at 0 suffers death-save failures (two on a crit).
    void applyDamage(Combatant& t, int dmg, bool crit) {
        if (dmg <= 0 || t.dead) return;
        if (t.hp > 0) {
            int over = dmg - t.hp;            // damage past 0
            t.hp = std::max(0, t.hp - dmg);
            if (t.hp == 0) {
                if (t.foe || over >= t.maxHp) t.dead = true;   // monsters drop; PCs: massive damage kills
                else { t.stable = false; t.deathSuccesses = t.deathFailures = 0; }  // now dying
            }
        } else {
            // Hit while already at 0 HP: a death-save failure (two on a crit).
            if (dmg >= t.maxHp) { t.dead = true; return; }
            t.stable = false;
            t.deathFailures += crit ? 2 : 1;
            if (t.deathFailures >= 3) t.dead = true;
        }
    }

    // ----- opportunity attacks (reactions) -----

    // Could `attackerId` make an opportunity attack against `targetId` right now?
    // Needs an unspent reaction, a living opposite-team target within reach.
    bool canOpportunityAttack(int attackerId, int targetId) const {
        if (attackerId < 0 || attackerId >= static_cast<int>(m_c.size())) return false;
        if (targetId  < 0 || targetId  >= static_cast<int>(m_c.size())) return false;
        const Combatant& a = m_c[attackerId];
        const Combatant& t = m_c[targetId];
        if (a.isDown() || t.isDown() || a.reactionUsed || a.foe == t.foe) return false;
        return cellDistance(a.cx, a.cy, t.cx, t.cy) <= a.reachCells;
    }

    // Resolve an opportunity attack, spending the attacker's reaction.
    AttackOutcome opportunityAttack(int attackerId, int targetId, int d20, int dice) {
        if (!canOpportunityAttack(attackerId, targetId)) return {};
        m_c[attackerId].reactionUsed = true;
        return resolveAttack(attackerId, targetId, d20, dice);
    }

    // Which enemies get an opportunity attack when `moverId` moves from
    // (fromX,fromY) to (toX,toY)? A foe provokes when the mover leaves its reach
    // (was in reach, now isn't) — unless the mover is Disengaging. Only foes with
    // a reaction available and line to strike are returned. (We check the move's
    // endpoints, not every 5 ft, since moves here are a direct reposition.)
    std::vector<int> provokers(int moverId, int fromX, int fromY, int toX, int toY) const {
        std::vector<int> out;
        if (moverId < 0 || moverId >= static_cast<int>(m_c.size())) return out;
        const Combatant& m = m_c[moverId];
        if (m.disengaging) return out;
        for (int i = 0; i < static_cast<int>(m_c.size()); ++i) {
            if (i == moverId) continue;
            const Combatant& e = m_c[i];
            if (e.isDown() || e.reactionUsed || e.foe == m.foe) continue;
            bool wasInReach = cellDistance(e.cx, e.cy, fromX, fromY) <= e.reachCells;
            bool nowInReach = cellDistance(e.cx, e.cy, toX, toY) <= e.reachCells;
            if (wasInReach && !nowInReach) out.push_back(i);
        }
        return out;
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

    // Make one death saving throw for the active combatant (call at the start of
    // its turn when it's dying). The caller supplies the raw d20. 10+ is a
    // success, under 10 a failure; a nat 1 is two failures, a nat 20 revives at
    // 1 HP. Three successes stabilize; three failures kill.
    SaveResult deathSave(int d20) {
        if (!hasActive()) return SaveResult::None;
        Combatant& c = active();
        if (!c.isDying()) return SaveResult::None;
        if (d20 == 20) {
            c.hp = 1; c.stable = false; c.deathSuccesses = c.deathFailures = 0;
            return SaveResult::Revived;
        }
        if (d20 == 1) {
            c.deathFailures += 2;
            if (c.deathFailures >= 3) { c.dead = true; return SaveResult::Died; }
            return SaveResult::Fail;
        }
        if (d20 >= 10) {
            if (++c.deathSuccesses >= 3) {
                c.stable = true; c.deathSuccesses = c.deathFailures = 0;
                return SaveResult::Stabilized;
            }
            return SaveResult::Success;
        }
        if (++c.deathFailures >= 3) { c.dead = true; return SaveResult::Died; }
        return SaveResult::Fail;
    }

    // Count living combatants on a side — for victory/defeat checks.
    int living(bool foe) const {
        int n = 0;
        for (const auto& c : m_c) if (c.foe == foe && !c.isDown()) ++n;
        return n;
    }

    // Is a cell occupied by a living combatant other than `exceptId`?
    bool occupied(int x, int y, int exceptId) const {
        for (int i = 0; i < static_cast<int>(m_c.size()); ++i) {
            if (i == exceptId || m_c[i].isDown()) continue;
            if (m_c[i].cx == x && m_c[i].cy == y) return true;
        }
        return false;
    }

    // ----- enemy AI planning (deterministic; the app supplies the dice) -----

    // Nearest living enemy of combatant `actorId`, by cell distance; -1 if none.
    int nearestEnemy(int actorId) const {
        if (actorId < 0 || actorId >= static_cast<int>(m_c.size())) return -1;
        const Combatant& a = m_c[actorId];
        int best = -1, bestD = 1 << 30;
        for (int i = 0; i < static_cast<int>(m_c.size()); ++i) {
            const Combatant& c = m_c[i];
            if (i == actorId || c.isDown() || c.foe == a.foe) continue;
            int d = cellDistance(a.cx, a.cy, c.cx, c.cy);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }
    int aiTarget() const { return hasActive() ? nearestEnemy(activeId()) : -1; }

    // Best cell for the ACTIVE combatant to move to this turn to engage
    // `targetId`: among cells reachable with its current movement (on board,
    // unoccupied), the one that gets closest to the target — i.e. into melee
    // reach if possible, otherwise as near as it can get. Returns the actor's
    // current cell if it's already in reach or can't improve. Deterministic.
    GridCell aiDestination(int targetId, int gridN) const {
        const Combatant& a = active();
        const Combatant& t = m_c[targetId];
        GridCell start{a.cx, a.cy};
        if (cellDistance(a.cx, a.cy, t.cx, t.cy) <= a.reachCells) return start;

        int reach = cellsLeft();
        GridCell best = start;
        int bestToTarget = cellDistance(start.x, start.y, t.cx, t.cy);
        int bestStep = 0;
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx) {
                int x = a.cx + dx, y = a.cy + dy;
                if (x < 0 || y < 0 || x >= gridN || y >= gridN) continue;
                if (occupied(x, y, activeId())) continue;
                int step = cellDistance(a.cx, a.cy, x, y);   // Chebyshev <= reach
                int toT = cellDistance(x, y, t.cx, t.cy);
                // Prefer getting closest to the target; break ties by moving less.
                if (toT < bestToTarget || (toT == bestToTarget && step < bestStep)) {
                    bestToTarget = toT; bestStep = step; best = {x, y};
                }
            }
        }
        return best;
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
