// Standalone sanity checks for the encounter turn/movement rules (no engine).
//   c++ -std=c++17 examples/tabletop/encounter_test.cpp -o /tmp/enc && /tmp/enc
#include "encounter.hpp"

#include <cassert>
#include <cstdio>

using namespace rpgtt;

int main() {
    constexpr int N = 16;

    // Chebyshev metric: diagonals cost the same as straight steps.
    assert(cellDistance(0, 0, 3, 0) == 3);
    assert(cellDistance(0, 0, 3, 3) == 3);
    assert(cellDistance(2, 5, 5, 1) == 4);

    Encounter e;
    e.add({"Mera",   20, 35, 5, 8, 0, 0, 0, false, 0});  // fastest, acts first
    e.add({"Aldric", 17, 30, 6, 8, 0, 0, 0, false, 0});
    e.add({"Bandit", 14, 30, 10, 8, 0, 0, 0, true,  0});
    e.add({"Doran",  12, 25, 6, 9, 0, 0, 0, false, 0});
    e.start();

    // Initiative order is descending; ties would keep insertion order.
    assert(e.order().size() == 4);
    assert(e.active().name == "Mera");
    assert(e.round() == 1);

    // Movement budget refreshes to speed at the start of a turn.
    assert(e.active().moveLeftFeet == 35);
    assert(e.cellsLeft() == 7);           // 35 / 5

    // Reaching within budget spends the path cost; out of budget is rejected.
    assert(e.canActiveReach(5 + 7, 8, N));       // exactly 7 cells away
    assert(!e.canActiveReach(5 + 8, 8, N));      // 8 cells > budget
    assert(!e.canActiveReach(-1, 8, N));         // off the board
    assert(e.moveActiveTo(9, 8, N));             // 4 cells (Chebyshev) = 20 ft
    assert(e.active().cx == 9 && e.active().cy == 8);
    assert(e.active().moveLeftFeet == 15);       // 35 - 20
    assert(e.cellsLeft() == 3);
    assert(!e.moveActiveTo(14, 8, N));           // 5 cells = 25 ft > 15 ft left: rejected
    assert(e.active().cx == 9);                  // stayed put on the failed move
    assert(e.moveActiveTo(11, 8, N));            // 2 cells = 10 ft, leaves 5 ft
    assert(e.active().moveLeftFeet == 5);
    assert(e.cellsLeft() == 1);

    // Turn order marches through the roster, then the round rolls over.
    e.endTurn();
    assert(e.active().name == "Aldric" && e.active().moveLeftFeet == 30);

    // Action economy: fresh turn has everything available.
    assert(!e.active().actionUsed && !e.active().bonusUsed && !e.active().reactionUsed);

    // Dash spends the action and grants extra movement equal to speed.
    assert(e.dash());
    assert(e.active().actionUsed);
    assert(e.active().moveLeftFeet == 60);        // 30 + 30
    assert(!e.dash());                            // action already spent
    assert(!e.dodge() && !e.disengage());         // ditto — one action per turn

    // Bonus action and reaction are independent of the action.
    assert(e.useBonusAction());
    assert(!e.useBonusAction());
    assert(e.useReaction());
    assert(!e.useReaction());

    e.endTurn();
    assert(e.active().name == "Bandit");
    assert(e.dodge());                            // Dodge sets its status + spends the action
    assert(e.active().dodging && e.active().actionUsed);
    e.endTurn();
    assert(e.active().name == "Doran" && e.active().moveLeftFeet == 25);
    assert(e.round() == 1);
    e.endTurn();                                  // wrap
    assert(e.active().name == "Mera");
    assert(e.round() == 2);
    assert(e.active().moveLeftFeet == 35);        // fully refreshed on the new round
    assert(!e.active().actionUsed);               // action refreshed too

    // Aldric's Dash/bonus/reaction from round 1 are cleared when his turn returns.
    e.endTurn();
    assert(e.active().name == "Aldric");
    assert(e.active().moveLeftFeet == 30 && !e.active().actionUsed &&
           !e.active().bonusUsed && !e.active().reactionUsed);

    // ----- combat: attacks, HP, downing, victory -----
    Encounter b;
    Combatant hero;  hero.name = "Hero";  hero.foe = false; hero.cx = 0; hero.cy = 0;
    hero.ac = 15; hero.maxHp = hero.hp = 20; hero.attackBonus = 5; hero.dmgBonus = 3;
    Combatant orc;   orc.name = "Orc";    orc.foe = true;  orc.cx = 1; orc.cy = 0;
    orc.ac = 13; orc.maxHp = orc.hp = 15; orc.attackBonus = 4; orc.dmgBonus = 2;
    b.add(hero);
    b.add(orc);
    b.start();
    assert(b.active().name == "Hero");   // both init 10 -> insertion order

    int heroId = 0, orcId = 1;
    assert(b.canAttack(orcId));          // adjacent foe, action ready
    assert(!b.canAttack(heroId));        // can't attack a teammate/self

    // A hit: d20=12, +5 = 17 vs AC 13. Damage dice total 6, +3 bonus = 9.
    AttackOutcome o = b.attack(orcId, 12, 6);
    assert(o.valid && o.hit && !o.crit && o.damage == 9);
    assert(b.combatants()[orcId].hp == 6);          // 15 - 9
    assert(b.active().actionUsed);                   // attacking spent the action
    assert(!b.canAttack(orcId));                     // one attack per action

    // A miss doesn't change HP or drop the target (also confirms auto-miss on 1).
    b.endTurn();                                      // Orc's turn
    assert(b.active().name == "Orc");
    AttackOutcome m = b.attack(heroId, 1, 6);         // natural 1 always misses
    assert(m.valid && !m.hit && m.damage == 0);
    assert(b.combatants()[heroId].hp == 20);

    // Next round: Hero crits and drops the Orc. Caller doubles the dice on a
    // crit, so it passes the already-doubled total (say 10).
    b.endTurn();                                      // back to Hero, round 2
    assert(b.active().name == "Hero" && b.round() == 2);
    AttackOutcome c = b.attack(orcId, 20, 10);        // nat 20: auto-hit + crit
    assert(c.valid && c.hit && c.crit && c.damage == 13);  // 10 + 3
    assert(b.combatants()[orcId].hp == 0 && c.dropped);
    assert(b.combatants()[orcId].isDown());
    assert(b.living(true) == 0 && b.living(false) == 1);   // foes wiped

    // Turn order skips the downed Orc: endTurn returns to the Hero.
    b.endTurn();
    assert(b.active().name == "Hero" && b.round() == 3);

    // ----- flanking (house rule, DMG grid variant) -----
    Encounter f;
    Combatant h1; h1.name = "H1"; h1.foe = false; h1.cx = 4; h1.cy = 4;   // west of target
    Combatant h2; h2.name = "H2"; h2.foe = false; h2.cx = 6; h2.cy = 4;   // east of target
    Combatant en; en.name = "En"; en.foe = true;  en.cx = 5; en.cy = 4;   // between them
    f.add(h1); f.add(h2); f.add(en);
    int a1 = 0, a2 = 1, foeId = 2;

    // H1 and H2 sit on opposite sides of En -> both flank it.
    assert(f.isFlanking(a1, foeId));
    assert(f.isFlanking(a2, foeId));
    // Not flanking a teammate, and the foe doesn't flank a lone hero.
    assert(!f.isFlanking(foeId, a1));

    // Move H2 to a diagonal that is NOT opposite H1 -> no flank.
    f.combatants()[a2].cx = 6; f.combatants()[a2].cy = 5;  // SE corner, H1 due west
    assert(!f.isFlanking(a1, foeId));
    // Opposite corners do count: put H1 at NW, H2 at SE of En.
    f.combatants()[a1].cx = 4; f.combatants()[a1].cy = 3;  // NW corner
    f.combatants()[a2].cx = 6; f.combatants()[a2].cy = 5;  // SE corner
    assert(f.isFlanking(a1, foeId) && f.isFlanking(a2, foeId));
    // A downed ally can't help flank.
    f.combatants()[a2].hp = 0;
    assert(!f.isFlanking(a1, foeId));

    // ----- enemy AI planning -----
    Encounter g;
    Combatant gh; gh.name = "GH"; gh.foe = false; gh.cx = 10; gh.cy = 0;
    gh.initiative = 5;  gh.maxHp = gh.hp = 10;
    Combatant gf; gf.name = "GF"; gf.foe = true; gf.cx = 0; gf.cy = 0;
    gf.initiative = 20; gf.speedFeet = 30; gf.reachCells = 1; gf.maxHp = gf.hp = 10;
    g.add(gh); g.add(gf);
    g.start();
    assert(g.active().name == "GF");             // foe goes first
    int ghId = 0;
    assert(g.nearestEnemy(g.activeId()) == ghId);

    // Speed 30 = 6 cells; from x=0 the closest reachable cell to a hero at x=10
    // is (6,0).
    GridCell dst = g.aiDestination(ghId, 16);
    assert(dst.x == 6 && dst.y == 0);

    // With (6,0) blocked by an ally, the AI picks an unoccupied cell that still
    // gets as close as possible (distance 4 to the target).
    Combatant blocker; blocker.name = "Block"; blocker.foe = true;
    blocker.cx = 6; blocker.cy = 0; blocker.maxHp = blocker.hp = 10;
    g.combatants().push_back(blocker);
    GridCell dst2 = g.aiDestination(ghId, 16);
    assert(!(dst2.x == 6 && dst2.y == 0));
    assert(!g.occupied(dst2.x, dst2.y, g.activeId()));
    assert(cellDistance(dst2.x, dst2.y, 10, 0) == 4);

    // Standing adjacent already: stay put.
    g.combatants()[g.activeId()].cx = 9;         // adjacent to hero at (10,0)
    assert(g.aiDestination(ghId, 16).x == 9);

    // ----- opportunity attacks -----
    Encounter oa;
    Combatant om; om.name = "M"; om.foe = false; om.cx = 5; om.cy = 5;
    om.initiative = 20; om.speedFeet = 30; om.maxHp = om.hp = 20; om.ac = 10;
    Combatant oe; oe.name = "E"; oe.foe = true; oe.cx = 6; oe.cy = 5;
    oe.initiative = 5; oe.reachCells = 1; oe.attackBonus = 10; oe.dmgBonus = 3;
    oe.maxHp = oe.hp = 20;
    oa.add(om); oa.add(oe);
    oa.start();
    int mId = 0, eId = 1;

    // Mover leaves the adjacent foe's reach -> that foe provokes.
    auto p = oa.provokers(mId, 5, 5, 5, 3);
    assert(p.size() == 1 && p[0] == eId);
    // Moving but staying adjacent -> no provoke.
    assert(oa.provokers(mId, 5, 5, 6, 4).empty());
    // Disengage suppresses opportunity attacks entirely.
    oa.combatants()[mId].disengaging = true;
    assert(oa.provokers(mId, 5, 5, 5, 3).empty());
    oa.combatants()[mId].disengaging = false;

    // Resolve the opportunity attack: d20 15 + 10 vs AC 10 hits; 4 dice + 3 = 7.
    assert(oa.canOpportunityAttack(eId, mId));
    AttackOutcome oo = oa.opportunityAttack(eId, mId, 15, 4);
    assert(oo.valid && oo.hit && oo.damage == 7);
    assert(oa.combatants()[mId].hp == 13);
    assert(oa.combatants()[eId].reactionUsed);
    // Reaction spent: no second opportunity attack until the foe's turn refreshes.
    assert(!oa.canOpportunityAttack(eId, mId));
    assert(oa.provokers(mId, 5, 5, 5, 3).empty());

    std::puts("encounter_test: all checks passed");
    return 0;
}
