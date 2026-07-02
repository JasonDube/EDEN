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

    std::puts("encounter_test: all checks passed");
    return 0;
}
