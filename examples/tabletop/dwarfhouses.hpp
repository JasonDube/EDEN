// dwarfhouses.hpp — Kadmûrn, the dwarven Deep Holds, and its clans.
//
// The dwarven counterpart to houses.hpp / elfhouses.hpp. Dwarven society is built
// on CLANS that hold great fortress-cities (holds), and on the RECKONING - the
// graven tally of every oath, debt, and grudge. Two things drive its politics:
// the Reckoning (what is owed is paid; what is wronged is answered) and the LOST
// HOLDS (halls that fell to the deep dark, waiting to be reclaimed). Dwarven
// leadership passes by seniority and worth, confirmed by the clan-moot - women
// stand equal in hall and hold.
//
// Mountain Dwarves belong to the Deep clans, Hill Dwarves to the Hill clans.
#pragma once

#include "houses.hpp"   // for Tincture

#include <random>
#include <string>
#include <vector>

namespace rpgw {

inline bool isDwarf(const std::string& race) { return race.find("Dwarf") != std::string::npos; }

enum DwarfStrand { DEEP, HILL };
inline const char* dstrandName(DwarfStrand s) { return s == DEEP ? "Deep (Mountain Dwarf)" : "Hill (Hill Dwarf)"; }

struct DwarfClan {
    const char* name;
    const char* words;
    const char* hold;        // "" for Grudd (the wandering oathsworn)
    DwarfStrand strand;
    const char* craft;       // the clan's mastery
    const char* gift;        // mechanical seed
    const char* sigil;       // mountain/anvil/pick/axe/gem/rune/tankard/hammer
    Tincture    sigilColor;
    const char* cause;       // stance on the Lost Holds / the Reckoning
    const char* rep;
    bool royal;
};

struct DwarfRealm {
    const char* name;
    const char* capital;
    const char* thane;
    const char* reckoning;   // the central law/faith
    const char* lostHolds;   // the great cause of the age
    const char* tension;
};
inline const DwarfRealm& kadmurn() {
    static const DwarfRealm r = {
        "Kadmurn, the Deep Holds",
        "Dumgar, the Deepgate",
        "the High Thane, who holds the Stone Throne at Dumgar",
        "The Reckoning: every oath, debt, and grudge is graven in the clan-tallies. What is owed is "
        "paid; what is wronged is answered. A dwarf's word is cut in rock.",
        "Ages past, something rose from the deep dark and swallowed hold after hold. The dwarrow are a "
        "diminished people now, and reclaiming the fallen halls - and their honored dead - is the great "
        "cause of the age.",
        "Durgath would delve ever deeper for ore and glory, heedless of what stirs below, while Grimbek "
        "and Naethyr's kin would seal the deeps; Grumdek's gold could buy the reclaiming, if the counting-"
        "clans would spend rather than hoard.",
    };
    return r;
}

// ── the clans ────────────────────────────────────────────────────────────
inline const std::vector<DwarfClan>& dwarfClans() {
    static const std::vector<DwarfClan> v = {
        // The High Clan
        {"Clan Karrun", "The Stone Endures", "Dumgar, the Deepgate", DEEP,
         "The Stone Throne & the Reckoning",
         "The High Thane's line, keepers of the law and the graven tally: authority over the holds, and the weight of a diminished realm.",
         "mountain", OR, "Hold the dwarrow together, and weigh reclaiming against ruin.",
         "Grave and unbending; they carry the memory of a greater age.", true},

        // The Deep (Mountain Dwarves)
        {"Clan Thurgin", "By Flame and Hammer", "Emberforge", DEEP,
         "Smithcraft & the Great Forge",
         "Master smiths and keepers of the sacred forge-fire: the finest arms, armor, and rune-work of the mountains.",
         "anvil", OR, "Forge the arms to take back the lost holds.",
         "Proud and exacting; their steel has turned the deep dark for an age.", false},
        {"Clan Durgath", "Ever Downward", "the Deepmaw", DEEP,
         "Deep-Delving & the Far Veins",
         "Those who dig deepest for ore and glory: unmatched tunnellers and prospectors of the perilous under-deep.",
         "pick", GULES, "Delve deeper for wealth and renown - heedless of what stirs below.",
         "Bold to the point of folly; some say their greed will wake the dark anew.", false},
        {"Clan Grimbek", "None Shall Pass the Gate", "Grimhollow", DEEP,
         "War & the Hold-Guard",
         "The deep-fighters and gate-wardens: the shield-wall that stands between the holds and the dark below.",
         "axe", AZURE, "Reclaim the lost holds by axe and shield.",
         "Grim and iron-willed; the first to the breach and the last to fall.", false},

        // The Hill (Hill Dwarves)
        {"Clan Grumdek", "Deep and Rich", "the Golddeep", HILL,
         "Mining, Gold & Trade",
         "The counting-clans: masters of the deep veins, the coin-halls, and every bargain struck above and below.",
         "gem", VERT, "Gold buys the reclaiming - though some would sooner hoard it.",
         "Shrewd and wealthy; their word on a price is as good as graven stone.", false},
        {"Clan Thaggr", "Graven and Kept", "the Vault of Ages", HILL,
         "Runes, Lore & the Reckoning",
         "Runesmiths and keepers of the honored dead: the living memory of the dwarrow, wardens of every debt and grudge.",
         "rune", AZURE, "Honor the dead, settle every grudge, and let nothing be forgotten.",
         "Solemn and precise; they forget no name and forgive no slight.", false},
        {"Clan Bryndal", "Deep Roots, Full Cups", "Hearthdeep", HILL,
         "Hearth, Ale & the Long Feast",
         "Keepers of the hearth and brewing-vats: the warmth and provender that hold a hard folk together through hard years.",
         "tankard", OR, "The hearth endures; keep the folk fed, hale, and of good heart.",
         "Broad and generous; a Bryndal feast can mend a grudge no reckoning could.", false},
        {"Clan Grudd", "A Debt Unpaid", "", HILL,
         "Oath-keepers & Grudge-settlers Abroad",
         "Those who leave the hold under a great oath - to answer a wrong or redeem a shame - and walk the wide world until it is paid.",
         "hammer", GULES, "Leave the hold, and do not return until the oath is kept.",
         "Seatless wanderers under oath; most dwarrow you meet abroad are of Grudd.", false},
    };
    return v;
}

inline std::vector<int> dwarfClanIndicesByStrand(DwarfStrand s, bool includeRoyal) {
    std::vector<int> out;
    const auto& v = dwarfClans();
    for (int i = 0; i < (int)v.size(); ++i)
        if (v[i].strand == s && (includeRoyal || !v[i].royal)) out.push_back(i);
    return out;
}
inline std::vector<int> dwarfClanIndicesAll(bool includeRoyal) {
    std::vector<int> out;
    const auto& v = dwarfClans();
    for (int i = 0; i < (int)v.size(); ++i) if (includeRoyal || !v[i].royal) out.push_back(i);
    return out;
}

}  // namespace rpgw
