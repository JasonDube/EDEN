// relations_test.cpp — eyeball the relationship model against archetypes.
//   g++ -std=c++17 relations_test.cpp -o /tmp/rel && /tmp/rel
#include "relations.hpp"

#include <cstdio>
#include <vector>

using rpgc::Character;

// name, race, then the traits we care about here.
static Character mk(const char* name, const char* race, int wtp, int narc, int honor,
                    int comp, int cruel, int temper, int skept, int soc, int piety,
                    int cur, int greed, int dil, int carn) {
    Character c;
    c.name = name; c.race = race;
    c.willToPower = wtp; c.narcissism = narc; c.honor = honor; c.compassion = comp;
    c.cruelty = cruel; c.temper = temper; c.skepticism = skept; c.sociability = soc;
    c.piety = piety; c.curiosity = cur; c.greed = greed; c.diligence = dil; c.carnality = carn;
    return c;
}

int main() {
    std::vector<Character> cast = {
        //  name           race       WtP Nar Hon Cmp Cru Tmp Skp Soc Pty Cur Grd Dil Car
        mk("Lord Brannic", "Human",    17, 15, 14,  8, 11, 13, 12, 13, 11,  9, 12, 13, 10),
        mk("Willa",        "Human",     5,  7, 13, 15,  6,  9,  6, 10, 11,  9, 10, 11, 10),
        mk("Ser Aldric",   "Human",    12, 10, 16, 12,  8, 10, 11, 11, 13,  9,  9, 13,  9),
        mk("Maldrek",      "Human",    15, 16,  4,  4, 16, 14, 16, 12,  7, 12, 16, 12, 13),
        mk("Sister Enna",  "Human",     9,  8, 13, 17,  3,  8,  8, 11, 15, 10,  7, 13,  6),
        mk("Zyrix",        "Tiefling", 11, 12, 11, 12,  8, 10, 13, 12,  5, 16, 11, 11, 12),
    };

    printf("%-13s      %-13s   %-12s  dom     opinion       read\n", "A", "B", "dynamic");
    printf("---------------------------------------------------------------------------------------\n");
    for (size_t i = 0; i < cast.size(); ++i)
        for (size_t j = i + 1; j < cast.size(); ++j) {
            auto r = rpgr::assess(cast[i], cast[j]);
            printf("%-13s <-> %-13s  %-12s%s %2d/%-2d  %+4d/%-4d\n                 %s\n",
                   cast[i].name.c_str(), cast[j].name.c_str(), r.dynamic.c_str(),
                   r.toxic ? "*" : " ", r.domA, r.domB, r.opinionAB, r.opinionBA,
                   r.note.c_str());
        }
    return 0;
}
