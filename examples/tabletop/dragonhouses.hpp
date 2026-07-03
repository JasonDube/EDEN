// dragonhouses.hpp — Vharok, the dragonborn ember-clans.
//
// The dragonborn counterpart to the other realms, and structured differently
// again: no crown (they cast down the last throne in the Breaking), clans led by
// the PROVEN - leadership earned by deed, women and men equal before the flame.
// A clan is defined by its draconic BLOODLINE, which grants a breath weapon (ties
// straight into the SRD dragonborn ancestry). Two temperaments: the chromatic
// Fierce (fire, storm, venom) and the metallic Tempered (honor and the guard).
#pragma once

#include "houses.hpp"   // for rpgw::RGB

#include <random>
#include <string>
#include <vector>

namespace rpgw {

inline bool isDragonborn(const std::string& race) { return race.find("Dragonborn") != std::string::npos; }

enum DragonStrand { FIERCE, TEMPERED };
inline const char* dragonStrandName(DragonStrand s) { return s == FIERCE ? "Chromatic (the Fierce)" : "Metallic (the Tempered)"; }

struct DragonClan {
    const char* name;
    const char* words;
    const char* seat;        // "" for Kethric (the wanderers)
    DragonStrand strand;
    const char* ancestry;    // "Red", "Silver", ...
    const char* breath;      // damage type of the breath weapon
    const char* art;         // the clan's calling
    const char* gift;        // mechanical seed
    const char* sigil;       // flame/bolt/serpent/claw/sun/wing/shield/coin
    RGB         color;       // the dragon-blood's hue
    const char* ember;       // stance / creed ("The Ember")
    const char* rep;
    bool eldest;             // Vharokar - first among equals, not a crown
};

struct DragonRealm {
    const char* name;
    const char* moot;        // where the clans meet (no capital, no throne)
    const char* breaking;    // the founding uprising
    const char* creed;       // the Ember
    const char* tension;
};
inline const DragonRealm& vharok() {
    static const DragonRealm r = {
        "Vharok, the Ashlands of the Ember-Clans",
        "the Kindling - the moot where the free clans meet as equals",
        "The Breaking: the uprising in which the dragonborn cast down the wyrm-lords who bred them as "
        "soldier-slaves. Freedom is now sacred; to kneel to a dragon again is the deepest shame.",
        "The Ember: the draconic fire in the blood, and the honor won by deed. Worth is not born but "
        "proven; leadership is earned, and women and men stand equal before the flame.",
        "The dragon-cults would bargain with the surviving wyrms or restore the old masters, while "
        "Morrach digs powers from the ruins that might be better left buried; old ancestry-feuds still "
        "smolder between the fierce clans.",
    };
    return r;
}

// ── the clans ────────────────────────────────────────────────────────────
inline const std::vector<DragonClan>& dragonClans() {
    static const std::vector<DragonClan> v = {
        // The Eldest
        {"Clan Vharokar", "We Kneel to None", "the Emberhold", FIERCE, "Red", "fire",
         "War-Leaders & Keepers of the Breaking",
         "The eldest clan, who lit the Breaking: a searing fire breath, and the fiercest war-leaders of the ashlands.",
         "flame", {213, 68, 46}, "Lead the free clans - though the clans will suffer no king, not even them.",
         "Proud keepers of the uprising's memory; first to war and slowest to bow.", true},

        // The Fierce (chromatic)
        {"Clan Skarn", "Ride the Storm", "the Stormreach", FIERCE, "Blue", "lightning",
         "Storm-Riders of the Ash-Plains",
         "Swift raiders who ride the ash-storms: a breath of forked lightning, and the fastest outriders in Vharok.",
         "bolt", {90, 143, 208}, "Strike fast, strike free; answer to no gate and no wall.",
         "Restless and fierce; here in a thunderclap, gone in the dust.", false},
        {"Clan Vetha", "Patience and Venom", "the Bramblehold", FIERCE, "Green", "poison",
         "The Cunning & the Envoys",
         "Schemers, poisoners, and silver tongues: a choking breath of venom, and a gift for the long game.",
         "serpent", {91, 163, 94}, "Win by guile what others spend blood on.",
         "Subtle and distrusted; they remember every favor owed and owing.", false},
        {"Clan Morrach", "From the Ashes", "the Cinderwaste", FIERCE, "Black", "acid",
         "Ruin-Delvers of the Old Empire",
         "Scavengers of the wyrm-lords' ashen ruins: a searing breath of acid, and a nose for buried power.",
         "claw", {154, 136, 180}, "Take back the old empire's power - though some dig up what should stay buried.",
         "Grim scavengers; not all trust what they drag out of the dark.", false},

        // The Tempered (metallic)
        {"Clan Aurox", "Honor is the Ember", "the Hall of Judgment", TEMPERED, "Gold", "fire",
         "Honor-Keepers & Arbiters",
         "Judges and keepers of the clan-codes: a golden fire breath, and the authority to settle quarrels between clans.",
         "sun", {217, 174, 82}, "Hold every clan to the honor won in the Breaking.",
         "Revered arbiters; an Aurox verdict can end a feud without a single blade.", false},
        {"Clan Argen", "Wings Over the Free", "the Aerie", TEMPERED, "Silver", "cold",
         "Sky-Wardens & Protectors",
         "Guardians of the free clans: a breath of killing frost, and wings ever over the weak and the wronged.",
         "wing", {196, 203, 212}, "Defend the free clans, and never again let a tyrant rise.",
         "Noble and steadfast; first to shield those who cannot shield themselves.", false},
        {"Clan Bronn", "The Line Holds", "the Ironmarch", TEMPERED, "Bronze", "lightning",
         "Shield-Bearers & Soldiers",
         "The disciplined standing warriors: a breath of rolling thunder, and a shield-wall that does not break.",
         "shield", {188, 129, 73}, "Stand the wall; discipline is its own kind of fire.",
         "Steadfast and drilled; where Bronn plants its banner, the line holds.", false},
        {"Clan Kethric", "The World is Wide", "", TEMPERED, "Copper", "acid",
         "Wayfarers, Traders & Tricksters",
         "Travelers who walk the wide world, even to human lands: an acid breath, a quick wit, and a knack for a bargain or a trick.",
         "coin", {200, 122, 68}, "Roam beyond the ashlands; the ember burns just as bright abroad.",
         "Seatless wanderers; most dragonborn you meet in Aldermarch are of Kethric.", false},
    };
    return v;
}

inline std::vector<int> dragonClanIndicesAll(bool includeEldest) {
    std::vector<int> out;
    const auto& v = dragonClans();
    for (int i = 0; i < (int)v.size(); ++i) if (includeEldest || !v[i].eldest) out.push_back(i);
    return out;
}

}  // namespace rpgw
