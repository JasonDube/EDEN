// infernalhouses.hpp — the Brass Reach, the infernal plane whose houses claim the
// tieflings.
//
// Tieflings carry INFERNAL (devil) blood - lawful, hierarchical, pact-bound - so
// unlike the chaotic demons of the Abyss, the devils are organized into houses. A
// tiefling is bound by blood to one of these houses, wanted or not. (Many are also
// tied to a mortal Aldermarch line - a scandal - which the creator handles too.)
// Original lore: no WotC Nine Hells here (no Asmodeus/Baator/Avernus).
#pragma once

#include "houses.hpp"   // for rpgw::RGB

#include <random>
#include <string>
#include <vector>

namespace rpgw {

inline bool isTiefling(const std::string& race) { return race.find("Tiefling") != std::string::npos; }

enum InfernalTier { HIGH_INF, IRON_INF };
inline const char* infernalTierName(InfernalTier t) { return t == HIGH_INF ? "Highborn (the ruling & subtle)" : "Ironbound (the brutal & bound)"; }

struct InfernalHouse {
    const char* name;
    const char* words;
    const char* seat;        // "" for the Forsaken (the fled)
    InfernalTier tier;
    const char* portfolio;   // the sin/domain it holds
    const char* art;         // the house's role
    const char* gift;        // mechanical seed
    const char* sigil;       // crown/coin/serpent/dagger/axe/flame/chain
    RGB         color;
    const char* stance;      // in the Reach
    const char* rep;
    bool royal;
};

struct InfernalRealm {
    const char* name;
    const char* prince;
    const char* pact;
    const char* ledger;
    const char* tension;
};
inline const InfernalRealm& brassReach() {
    static const InfernalRealm r = {
        "the Brass Reach, the Iron Hells",
        "the Iron Prince, who holds the Brass Throne",
        "The Pact: law in the Hells is contract. A bargain sealed cannot be broken by devil or mortal, "
        "and the price is always paid - in coin, in service, or in soul. A tiefling's blood is bound to "
        "a house whether they will it or not.",
        "The Ledger: advancement is earned by cunning and cruelty, by climbing over one's betters. Every "
        "favor is recorded, every slight remembered, every debt called due.",
        "Every house schemes to climb the next tier; Vexcarion owns the debts, Nysseth whispers the "
        "poison, Sablis settles what the Ledger cannot - and all of them watch the Iron Prince, and "
        "each other, and wait.",
    };
    return r;
}

// ── the houses ─────────────────────────────────────────────────────────────
inline const std::vector<InfernalHouse>& infernalHouses() {
    static const std::vector<InfernalHouse> v = {
        // The Brass Throne
        {"The Brass Throne", "All Kneel, in Time", "the Brass Citadel", HIGH_INF, "Tyranny & Law",
         "The Iron Prince",
         "The archducal seat of the Reach: dominion, infernal law, and the cold authority that binds every house to the throne.",
         "crown", {199, 154, 78}, "Rule the tiers, and let ambition devour the ambitious.",
         "Patient and absolute; the Iron Prince has outlasted a thousand pretenders.", true},

        // The Highborn (ruling & subtle)
        {"House Vexcarion", "Everything Has a Price", "the Counting Pits", HIGH_INF, "Greed & Soul-Broking",
         "The Ledger-Keepers",
         "The soul-brokers of the Reach: masters of the contract, the debt, and the price of a mortal will. What is owed, they collect.",
         "coin", {199, 154, 78}, "Own the debts, and you own the debtors - and everyone owes.",
         "Unhurried and inescapable; a Vexcarion bargain is a noose that tightens by the year.", false},
        {"House Nysseth", "Only the Truth Wounds", "the Whispering Court", HIGH_INF, "Deceit & Temptation",
         "The Tempters",
         "Liars, spies, and silver tongues: those who ruin by the whispered word and the sweet, poisoned bargain.",
         "serpent", {110, 138, 74}, "Unmake the strong from within; a lie well-placed topples what no army could.",
         "Smiling and serpentine; never quite lying, and never once trusted.", false},
        {"House Sablis", "The Debt is Paid in Full", "the Umbral Vault", HIGH_INF, "Shadow & Murder",
         "The Cold Blades",
         "The assassins and shadow-walkers of the Reach: the settling of debts that cannot be settled in coin.",
         "dagger", {122, 106, 140}, "When the Ledger cannot collect, Sablis does.",
         "Silent and certain; you never see a Sablis contract until it is already closed.", false},

        // The Ironbound (brutal & bound)
        {"House Malgareth", "Burn What Will Not Bow", "the Legion Marches", IRON_INF, "Wrath & War",
         "The Infernal Legions",
         "The armies of the Hells: discipline forged in fury, and the blades the throne unleashes when the Ledger is not enough.",
         "axe", {178, 58, 52}, "Answer defiance with fire and the marching legion.",
         "Grim and relentless; where Malgareth marches, the tier is remade in ash.", false},
        {"House Ashvael", "Suffering Instructs", "the Screaming Pits", IRON_INF, "Fire & Torment",
         "The Tormentors",
         "Keepers of the fire-pits and the arts of punishment: the terror that keeps the tiers obedient.",
         "flame", {208, 86, 46}, "Let the punished be a lesson written in fire.",
         "Merciless and exact; the pits of Ashvael are the Reach's deepest dread.", false},
        {"House Dolmurr", "None Slip the Chain", "the Warden Halls", IRON_INF, "Chains & Pacts",
         "The Pact-Wardens",
         "The jailers and enforcers of every bargain: the chains that bind the damned and the wardens who never sleep.",
         "chain", {138, 138, 150}, "Hold every soul the Ledger claims; a pact unbroken is the Reach's whole law.",
         "Immovable and thorough; a Dolmurr chain has never once been slipped - so they say.", false},
        {"The Forsaken", "A Chain Once Broken", "", IRON_INF, "The Cast-Out & the Fled",
         "The Broken-Chained",
         "Tieflings and half-blooded devils who fled the Hells or were cast from it: the infernal gift kept, the infernal master forsworn.",
         "chain", {158, 138, 122}, "Slip the Reach and the house that claims you; owe the Hells nothing more.",
         "Hunted and free; most tieflings in the mortal world are, in truth, Forsaken.", false},
    };
    return v;
}

inline std::vector<int> infernalHousesAll(bool includeRoyal) {
    std::vector<int> out;
    const auto& v = infernalHouses();
    for (int i = 0; i < (int)v.size(); ++i) if (includeRoyal || !v[i].royal) out.push_back(i);
    return out;
}

}  // namespace rpgw
