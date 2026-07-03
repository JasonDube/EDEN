// houses.hpp — the Kingdom of Aldermarch and its twelve founding houses.
//
// Authored world data for "Chronicles of the Iron Temple." Houses are
// established canon (players are assigned one, they do not invent them):
//   - Noble background      -> a Great House (real standing)
//   - any other background  -> randomly sworn into a Lesser House
// Heraldry travels with the house, so the banner is inherited, not invented.
//
// This is deliberately a flat, data-only header (like character.hpp) so the
// creation UI and later house-management / intrigue / mass-combat systems can
// all read the same source of truth. "later we may add more" — just append.
#pragma once

#include <string>
#include <vector>

namespace rpgw {

// ── Heraldic tinctures ────────────────────────────────────────────────────
enum Tincture { OR, ARGENT, GULES, AZURE, SABLE, VERT, PURPURE };

struct RGB { unsigned char r, g, b; };

inline RGB tinctureRGB(Tincture t) {
    switch (t) {
        case OR:      return {200, 164, 60};   // gold
        case ARGENT:  return {223, 226, 231};   // silver
        case GULES:   return {162, 38, 38};     // red
        case AZURE:   return {42, 75, 141};     // blue
        case SABLE:   return {28, 28, 32};      // black
        case VERT:    return {46, 107, 62};     // green
        case PURPURE: return {94, 42, 110};     // purple
    }
    return {128, 128, 128};
}
inline const char* tinctureName(Tincture t) {
    switch (t) {
        case OR: return "Or"; case ARGENT: return "Argent"; case GULES: return "Gules";
        case AZURE: return "Azure"; case SABLE: return "Sable"; case VERT: return "Vert";
        case PURPURE: return "Purpure";
    }
    return "?";
}

// ── House rank ────────────────────────────────────────────────────────────
enum Rank { ROYAL, GREAT, LESSER };
inline const char* rankName(Rank r) {
    switch (r) { case ROYAL: return "Royal"; case GREAT: return "Great House"; case LESSER: return "Lesser House"; }
    return "?";
}

// ── A house ───────────────────────────────────────────────────────────────
struct House {
    const char* name;          // "House Corvane"
    const char* words;         // motto
    const char* seat;          // castle / holding
    const char* region;
    Rank        rank;
    const char* charge;        // heraldic charge (silhouette id): "raven", "anvil", ...
    Tincture    field;         // field (background) tincture
    Tincture    chargeColor;   // charge tincture
    const char* trait;         // short trait name (mechanical seed)
    const char* traitDesc;     // what it grants / will grow into
    const char* liege;         // whom they are sworn to ("" = the Crown itself)
    const char* rep;           // one-line reputation / hook
};

// Blazon helper: e.g. "Sable, a raven Argent"
inline std::string blazon(const House& h) {
    return std::string(tinctureName(h.field)) + ", a " + h.charge + " " + tinctureName(h.chargeColor);
}

// What a House Gift actually does, in game terms. Keyed by trait name.
// Honest about scope: "In play" = active now; "Planned" = waiting on a subsystem
// (house management / mass combat / intrigue) that isn't built yet.
inline const char* giftEffect(const std::string& trait) {
    if (trait == "Iron-Sworn")
        return "In play: advantage on social checks that invoke law, oath, or the Crown's name; "
               "the Iron-Sworn and crown-loyal defer to you.\n"
               "Planned: command authority as the intrigue system grows.";
    if (trait == "Martial Tradition")
        return "Planned (mass combat): your levies are cheaper to raise and hit harder, and you "
               "gain a bonus when commanding troops in the field.";
    if (trait == "Merchant Ties")
        return "In play: better prices buying and selling (Orlen and other merchants), and steadier "
               "income for your House.";
    if (trait == "Seafarers")
        return "Planned: free passage aboard sailing ships, naval levies, and trade contacts in "
               "foreign ports.";
    if (trait == "Warden Blood")
        return "In play: advantage on Survival in the wilds and resilience against cold and "
               "exhaustion.\nPlanned: hardy frontier levies and rangers.";
    if (trait == "Old Blood")
        return "Planned (intrigue): nobles receive you as a peer; extra weight in council and at "
               "court.";
    if (trait == "Ironwrights")
        return "In play: discounts on weapons and armor, and access to master-forged (superior) "
               "gear.\nPlanned: repair and upgrade your equipment.";
    if (trait == "Dutiful Garrison")
        return "In play: a small, loyal garrison to rebuild - and your restoration questline, "
               "retaking Greywatch.\nPlanned: hold and improve the holding.";
    if (trait == "Cunning")
        return "Planned (intrigue): a network of informants; you learn plots, rumors, and secrets "
               "before they surface.";
    if (trait == "Old Debts")
        return "Planned (intrigue): leverage over those who owe you - coin, favors, and blackmail - "
               "offset by a distrusted name.";
    if (trait == "Wreckers")
        return "Planned: smuggling and salvage income, and access to black-market goods others "
               "cannot buy.";
    if (trait == "Arcane Lineage")
        return "In play: access to arcane tutors and Temple lore.\nPlanned: a boon for spellcasters "
               "and rare scrolls and rituals.";
    return "The gift's game effects are still being designed.";
}

// ── The realm ─────────────────────────────────────────────────────────────
struct Kingdom {
    const char* name;
    const char* capital;
    const char* faith;
    const char* faithDesc;
    const char* tension;
};
inline const Kingdom& kingdom() {
    static const Kingdom k = {
        "The Kingdom of Aldermarch",
        "Ferrowhold",
        "The Iron Temple",
        "The realm's oldest institution and binding faith. Its creed is the sanctity "
        "of the sworn word: fealty, marriage, and treaty are all consecrated in iron "
        "before the Temple's grey priests. To break an oath is the realm's deepest "
        "crime. The Temple ordains the Iron-Sworn, keeps the forge-shrines, and its "
        "influence grows quietly against the secular lords.",
        "King Aldric Corvane is old and his succession contested; Halewyn's gold "
        "underwrites a crown it privately disdains; Drommond's swords resent paying "
        "for Halewyn's grain; and the Temple's rising hand unsettles them all.",
    };
    return k;
}

// ── The twelve houses ─────────────────────────────────────────────────────
// Order: the Crown, then five Great Houses, then six Lesser Houses.
inline const std::vector<House>& houses() {
    static const std::vector<House> v = {
        // The Crown
        {"House Corvane", "By Iron Bound", "Ferrowhold", "the Crownlands", ROYAL,
         "raven", SABLE, ARGENT, "Iron-Sworn",
         "The Temple's blessing: authority over sworn men and weight behind your oaths.",
         "", "The royal house; keepers of the oath. Aging, and fraying at the edges."},

        // Great Houses (highborn-eligible)
        {"House Drommond", "Unbroken", "Craghold", "the Highmarch", GREAT,
         "anvil", GULES, OR, "Martial Tradition",
         "Stronger, cheaper levies and an edge leading troops in the field.",
         "House Corvane (the Crown)", "Proud, stubborn wardens of the mountain passes."},
        {"House Halewyn", "Sown in Gold", "Amberhall", "the Sunmarch", GREAT,
         "wheatsheaf", OR, VERT, "Merchant Ties",
         "Deeper coffers: better prices and more coin flowing to your House.",
         "House Corvane (the Crown)", "The realm's richest house, and its most ambitious."},
        {"House Vashqar", "The Tide Remembers", "Saltspire", "the Saltreach", GREAT,
         "kraken", AZURE, ARGENT, "Seafarers",
         "Fleets, foreign trade, and passage and reach that landlocked houses lack.",
         "House Corvane (the Crown)", "Aloof and worldly; forever looking overseas."},
        {"House Ostred", "We Hold the Cold", "Hoarfrost", "the Frostmark", GREAT,
         "stag", ARGENT, SABLE, "Warden Blood",
         "Hardy soldiers and rangers; an edge in frontier defense and survival.",
         "House Corvane (the Crown)", "Grim, honorable wardens guarding what lies north."},
        {"House Bryndel", "Deep Roots", "Elderford", "the Weald", GREAT,
         "oak", VERT, OR, "Old Blood",
         "Ancient prestige: sway in council and among the highborn.",
         "House Corvane (the Crown)", "The realm's political fulcrum; quiet kingmakers."},

        // Lesser Houses (landed knights & sworn vassals)
        {"House Ferris", "By Hammer and Flame", "the Forgeholt", "the Highmarch", LESSER,
         "hammer", GULES, OR, "Ironwrights",
         "Master smiths: finer arms and armor, and the Iron Temple's regard.",
         "House Drommond", "They forge the realm's steel; the Temple's favored smiths."},
        {"House Cawood", "First to the Wall", "Greywatch", "the Frostmark", LESSER,
         "tower", SABLE, ARGENT, "Dutiful Garrison",
         "Loyal, disciplined defenders — but a poor, dwindling seat to rebuild.",
         "House Ostred", "A fallen house: half its lands lost, its watchtower crumbling."},
        {"House Threndle", "Patient and Sharp", "Thornkeep", "the Sunmarch", LESSER,
         "dagger", PURPURE, ARGENT, "Cunning",
         "Spies and whispers: you hear secrets before they break.",
         "House Halewyn", "A schemer's house; its ledgers are written in blackmail."},
        {"House Maldacht", "What Is Owed", "Blackmyre", "the Weald", LESSER,
         "serpent", SABLE, GULES, "Old Debts",
         "Leverage over others through coin and blackmail — and a dark name.",
         "House Bryndel", "Shadowed and distrusted; rumored kinslayers and usurers."},
        {"House Penhollow", "Ride the Storm", "Wrackstrand", "the Saltreach", LESSER,
         "gull", AZURE, ARGENT, "Wreckers",
         "Salvage and smuggling: illicit coin and useful coastal contacts.",
         "House Vashqar", "Coastal opportunists, half-pirate when the tide is out."},
        {"House Ivrenne", "The Flame Kept", "Emberhall", "the Crownlands", LESSER,
         "flame", PURPURE, ARGENT, "Arcane Lineage",
         "Mage-blood and old lore: access to magic and the Temple's deeper mysteries.",
         "", "Keepers of old lore, sworn to the Crown; eyed warily by the pious."},
    };
    return v;
}

// ── Convenience selectors ─────────────────────────────────────────────────
inline std::vector<int> houseIndicesByRank(Rank r) {
    std::vector<int> out;
    const auto& v = houses();
    for (int i = 0; i < static_cast<int>(v.size()); ++i)
        if (v[i].rank == r) out.push_back(i);
    return out;
}
inline std::vector<int> greatHouseIndices() { return houseIndicesByRank(GREAT); }
inline std::vector<int> lesserHouseIndices() { return houseIndicesByRank(LESSER); }

inline int houseIndexByName(const std::string& name) {
    const auto& v = houses();
    for (int i = 0; i < static_cast<int>(v.size()); ++i)
        if (name == v[i].name) return i;
    return -1;
}

}  // namespace rpgw
