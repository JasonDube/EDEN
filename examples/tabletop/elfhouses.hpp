// elfhouses.hpp — Aelvarin, the elven Verdant Reaches, and its lineages.
//
// The elven counterpart to houses.hpp, deliberately structured DIFFERENTLY from
// the human feudal houses:
//   - lineages defined by an inherited ART, not by land and levy
//   - led by their ELDER, chosen by seniority and accomplishment - never by sex
//     or strict birth-order (elven women lead as freely as men)
//   - their politics is the FADING: how each answers the slow dimming of the Song
// High Elves belong to the Court lineages, Wood Elves to the Wild; the drow are
// the Sundered, exiled below and belonging to no lineage.
#pragma once

#include "houses.hpp"   // for Tincture

#include <random>
#include <string>
#include <vector>

namespace rpgw {

inline bool isDrow(const std::string& race) { return race.find("Drow") != std::string::npos; }
inline bool isElf(const std::string& race) {
    return (race.find("Elf") != std::string::npos && race.find("Half") == std::string::npos) || isDrow(race);
}

enum ElfStrand { COURT, WILD };
inline const char* strandName(ElfStrand s) { return s == COURT ? "Court (High Elf)" : "Wild (Wood Elf)"; }

struct ElfHouse {
    const char* name;
    const char* words;
    const char* seat;        // "" for Elarion (wanderers)
    ElfStrand   strand;
    const char* art;         // the inherited art / domain
    const char* gift;        // mechanical seed
    const char* sigil;       // line-art emblem id (oak/star/crescent/harp/stag/leaf/thorn/comet)
    Tincture    sigilColor;
    const char* stance;      // stance on the Fading
    const char* rep;
    bool royal;
};

struct ElfRealm {
    const char* name;
    const char* capital;
    const char* sovereign;
    const char* fading;      // the realm's central condition (parallels the Iron Temple)
    const char* tension;
};
inline const ElfRealm& aelvarin() {
    static const ElfRealm r = {
        "Aelvarin, the Verdant Reaches",
        "Ilmaren, the City of Silver Dusk",
        "the Everqueen Sarathiel, who has reigned three thousand years",
        "The Song that keeps the Everwood green is dimming - the Fading. Magic wanes, the eldest "
        "pass beyond the last shore, and few children are born. How each lineage answers this slow "
        "ending is the whole of elven politics.",
        "Silvael clings while Elarion would leave; Aelthanor's hunger for new power is the very thing "
        "that once sundered the drow, and Naethyr watches them both; Vaelora believes life can heal "
        "the Song, while Ysoril counsels a dignified ending.",
    };
    return r;
}

// ── the lineages ───────────────────────────────────────────────────────────
inline const std::vector<ElfHouse>& elfHouses() {
    static const std::vector<ElfHouse> v = {
        // The sovereign line
        {"House Silvael", "The Song Endures", "Ilmaren, the City of Silver Dusk", COURT,
         "The Song & Sovereignty",
         "The royal line, keepers of the Song itself: authority among the Aelvar and a deep well of innate magic.",
         "oak", ARGENT, "Preserve, at any cost.",
         "The Everqueen's own blood; ancient, and weary with the weight of the Fading.", true},

        // The Court (High Elves)
        {"House Aelthanor", "By Star and Song", "the Astral Spire", COURT,
         "Star-lore & High Magic",
         "Mages and seers who read the turning stars: potent arcane power and true divination.",
         "star", PURPURE, "Seek new power to renew the Song - some brush the magic that sundered the drow.",
         "Brilliant and dangerous; the realm both needs them and fears them.", false},
        {"House Miriath", "Wrought to Last", "the Moonforge", COURT,
         "Artifice & Silversmithing",
         "Makers of elven blades and moon-silver wonders: superior arms, armor, and artifacts.",
         "crescent", ARGENT, "Pragmatic - what endures, they build to endure.",
         "Patient, exacting, and quietly proud of their craft.", false},
        {"House Ysoril", "We Are the Memory", "the Hall of Echoes", COURT,
         "Song, Memory & Lore",
         "Loremasters who are the living memory of Aelvarin: vast knowledge and the magic of the sung word.",
         "harp", ARGENT, "Record all that was, and counsel a dignified ending.",
         "Melancholy keepers; they have already begun to grieve.", false},

        // The Wild (Wood Elves)
        {"House Caelith", "The Wood Remembers", "the Greenwatch", WILD,
         "The Hunt & the Wardens",
         "Rangers and border-wardens: peerless tracking, marksmanship, and woodcraft.",
         "stag", VERT, "Guard what remains, tree by tree.",
         "Watchful and few; the first blades between the Everwood and the world.", false},
        {"House Vaelora", "Root and Bough", "the Heartgrove", WILD,
         "Druidry & the Living Wood",
         "Healers and growers, friends to beast and root: nature's magic and the mending of wounds.",
         "leaf", VERT, "Nurture - they alone believe the Fading can be turned by tending life.",
         "Gentle, stubborn, and full of a hope the Court has lost.", false},
        {"House Naethyr", "We Watch the Dark", "the Duskward", WILD,
         "Shadow-Wardens of the Deep",
         "Watchers of the sunless places and the drow-marches: stealth, vigilance, and resistance to dark magic.",
         "thorn", VERT, "Guard the border with the Sundered - though some fear they drift toward what they watch.",
         "Grim and secretive; not all trust where their long vigil leads.", false},
        {"House Elarion", "Beyond the Last Tree", "", WILD,
         "Wanderers & Exiles",
         "Those who walk beyond the last tree, even into human lands: worldliness, adaptability, survival abroad.",
         "comet", ARGENT, "Leave - pass beyond, or seek new lands while there is time.",
         "Restless and seatless; most elves you meet in Aldermarch are of this line.", false},
    };
    return v;
}

// The drow belong to no lineage - they are the Sundered.
inline const char* sunderedBlurb() {
    return "The Sundered. In ages past a lineage sought to arrest the Fading by delving below and "
           "drinking of forbidden power; it changed them, cut them from the Song, and drove them into "
           "the sunless deeps beneath Aelvarin. The Aelvar name them the drow, and speak of them only "
           "in grief and dread. To walk the surface is to be an exile twice over - from the light, and "
           "from the memory of your own kin.";
}

inline std::vector<int> elfHouseIndicesByStrand(ElfStrand s, bool includeRoyal) {
    std::vector<int> out;
    const auto& v = elfHouses();
    for (int i = 0; i < (int)v.size(); ++i)
        if (v[i].strand == s && (includeRoyal || !v[i].royal)) out.push_back(i);
    return out;
}
inline std::vector<int> elfHouseIndicesAll(bool includeRoyal) {
    std::vector<int> out;
    const auto& v = elfHouses();
    for (int i = 0; i < (int)v.size(); ++i) if (includeRoyal || !v[i].royal) out.push_back(i);
    return out;
}

}  // namespace rpgw
