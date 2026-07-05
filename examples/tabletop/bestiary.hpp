// bestiary.hpp — the monster roster for overland (and later, dungeon) encounters.
//
// DRAFT (2026-07-04): first cut, reflecting the plan agreed in chat —
//   1) biomes = the SRD's 11 "environments" (so monster tags lift from SRD data)
//   2) each monster carries a CR + a set of biomes it can appear in
//   3) level-appropriateness comes from CR -> XP vs the party's XP budget
// This file is DATA ONLY and is NOT yet wired into the encounter/combat system.
// Right now the overland map is all Grassland, so the starter roster is a small
// set of low-CR creatures that can plausibly show up in grassland. Expand biome
// by biome once the encounter->combat pipeline is proven.
//
// A MonsterDef mirrors the combat fields of encounter.hpp's Combatant, so a foe
// is instantiated by copying these stats onto a Combatant (done later, in the
// encounter builder).
#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace rpgb {   // bestiary

// The 11 SRD environments, as bit flags so a monster can hold several at once.
enum Biome : uint16_t {
    Grassland  = 1u << 0,
    Forest     = 1u << 1,
    Hill       = 1u << 2,
    Mountain   = 1u << 3,
    Desert     = 1u << 4,
    Swamp      = 1u << 5,
    Coastal    = 1u << 6,
    Arctic     = 1u << 7,
    Urban      = 1u << 8,   // towns/cities
    Underdark  = 1u << 9,   // caves/dungeons
    Underwater = 1u << 10,  // seas/lakes
};

// Handy masks. kSurface = every walkable overland biome (what the map is made of);
// kAnywhere = truly everywhere (add Urban/Underdark/Underwater as needed).
constexpr uint16_t kSurface  = Grassland | Forest | Hill | Mountain |
                               Desert | Swamp | Coastal | Arctic;
constexpr uint16_t kAnywhere = kSurface | Urban | Underdark | Underwater;

inline const char* biomeName(Biome b) {
    switch (b) {
        case Grassland:  return "Grassland";
        case Forest:     return "Forest";
        case Hill:       return "Hill";
        case Mountain:   return "Mountain";
        case Desert:     return "Desert";
        case Swamp:      return "Swamp";
        case Coastal:    return "Coastal";
        case Arctic:     return "Arctic";
        case Urban:      return "Urban";
        case Underdark:  return "Underdark";
        case Underwater: return "Underwater";
    }
    return "?";
}

// One monster template. Combat fields mirror encounter.hpp's Combatant so a foe
// is built by copying them across. Damage is dmgDice d dmgSides + dmgBonus.
struct MonsterDef {
    const char* name;
    float       cr;          // Challenge Rating (0, 0.125, 0.25, 0.5, 1, 2, ...)
    uint16_t    biomes;      // OR of Biome flags this creature can appear in
    int         maxHp;
    int         ac;
    int         speedFeet;
    int         attackBonus; // to-hit
    int         dmgDice;     // e.g. 2  (of a 2d4+2)
    int         dmgSides;    // e.g. 4
    int         dmgBonus;    // e.g. 2
    float       heightFt;    // token height, for scaling the model on the grid
    // NOTE: single-attack model for now; multiattackers (Thug, etc.) are folded
    // into one swing until the combat system grows multiattack.
};

// Path to a monster's token model: lowercase, spaces->underscores, apostrophes
// dropped. "Giant Rat" -> "assets/characters/monster/giant_rat.glb". A missing
// file just means that foe falls back to a colored mini.
inline std::string monsterModelPath(const std::string& name) {
    std::string s;
    for (char c : name) {
        if (c == ' ') s += '_';
        else if (c >= 'A' && c <= 'Z') s += static_cast<char>(c - 'A' + 'a');
        else if (c == '\'') continue;
        else s += c;
    }
    return "assets/characters/monster/" + s + ".glb";
}

// CR -> XP (SRD 5.1 experience-by-CR table, low end; extend upward as needed).
inline int xpForCR(float cr) {
    if (cr <= 0.0f)   return 10;
    if (cr <= 0.125f) return 25;
    if (cr <= 0.25f)  return 50;
    if (cr <= 0.5f)   return 100;
    if (cr <= 1.0f)   return 200;
    if (cr <= 2.0f)   return 450;
    if (cr <= 3.0f)   return 700;
    if (cr <= 4.0f)   return 1100;
    if (cr <= 5.0f)   return 1800;
    return 2300;   // CR 6; higher tiers to be filled in when we need them
}

// Starter grassland roster. Stats are SRD 5.1 stat blocks (single-attack model).
// Every entry includes Grassland so it can appear on the current all-grassland map.
inline const std::vector<MonsterDef>& bestiary() {
    static const std::vector<MonsterDef> v = {
        // name          CR      biomes                                              HP  AC  spd  +hit dice sides bonus  htFt
        {"Jackal",       0.0f,   Grassland | Forest | Desert,                          3, 12, 40,   1,   1,   4,   -1,  2.5f},
        {"Giant Rat",    0.125f, Grassland | Forest | Swamp | Desert | Urban | Underdark, 7, 12, 30, 4,  1,   4,    2,  1.5f},
        {"Bandit",       0.125f, kSurface | Urban,                                    11, 12, 30,   3,   1,   6,    1,  6.0f},
        {"Cultist",      0.125f, Grassland | Forest | Swamp | Coastal | Urban,         9, 12, 30,   3,   1,   6,    1,  6.0f},
        {"Wolf",         0.25f,  Forest | Grassland | Hill,                           11, 13, 40,   4,   2,   4,    2,  3.0f},
        {"Goblin",       0.25f,  Forest | Grassland | Hill | Underdark,                7, 15, 30,   4,   1,   6,    2,  4.5f},
        {"Boar",         0.25f,  Forest | Grassland | Hill,                           11, 11, 40,   3,   1,   6,    1,  3.5f},
        // Scout discontinued per design (2026-07-04) — didn't fit as a random foe.
    };
    return v;
}

// Every monster that can appear in a given biome (for the encounter builder to
// then filter by CR / XP budget against the party's level).
inline std::vector<const MonsterDef*> monstersInBiome(Biome b) {
    std::vector<const MonsterDef*> out;
    for (const auto& m : bestiary())
        if (m.biomes & b) out.push_back(&m);
    return out;
}

// ── encounter building (level-appropriateness) ───────────────────────────────
// The SRD's XP thresholds per character level. Party threshold = sum over the
// party (here, just the solo hero). An encounter is sized so its ADJUSTED XP
// lands in a target band without blowing past "deadly".
struct XPThresholds { int easy, medium, hard, deadly; };
inline XPThresholds thresholdsForLevel(int lvl) {
    static const XPThresholds t[] = {
        {  0,   0,   0,   0},   // 0 (unused)
        { 25,  50,  75, 100},   // 1
        { 50, 100, 150, 200},   // 2
        { 75, 150, 225, 400},   // 3
        {125, 250, 375, 500},   // 4
        {250, 500, 750,1100},   // 5
        {300, 600, 900,1400},   // 6
        {350, 750,1100,1700},   // 7
        {450, 900,1400,2100},   // 8
        {550,1100,1600,2400},   // 9
        {600,1200,1900,2800},   // 10
    };
    return t[std::clamp(lvl, 1, 10)];
}

// SRD encounter multiplier by number of monsters (returned ×100 to stay integer):
// more monsters hit harder than their raw XP suggests.
inline int encMultiplierX100(int n) {
    if (n <= 1) return 100;
    if (n == 2) return 150;
    if (n <= 6) return 200;
    if (n <= 10) return 250;
    if (n <= 14) return 300;
    return 400;
}

struct EncounterGroup {
    std::vector<const MonsterDef*> monsters;
    int totalXP = 0;      // raw sum of monster XP
    int adjustedXP = 0;   // × the count multiplier (the number the budget uses)
};

// Build a biome-appropriate encounter for a party. Picks a target difficulty band
// (mostly easy/medium, sometimes hard) and adds random biome monsters until the
// adjusted XP reaches the band — never intentionally overshooting "deadly".
inline EncounterGroup buildEncounter(Biome biome, int partyLevel, int partySize,
                                     std::mt19937& rng) {
    XPThresholds th = thresholdsForLevel(partyLevel);
    int easy = th.easy * partySize, medium = th.medium * partySize;
    int hard = th.hard * partySize, deadly = th.deadly * partySize;

    int r = std::uniform_int_distribution<int>(1, 100)(rng);   // 40% easy / 40% med / 20% hard
    int target = (r <= 40) ? easy : (r <= 80) ? medium : hard;

    auto pool = monstersInBiome(biome);
    EncounterGroup g;
    if (pool.empty()) return g;
    std::uniform_int_distribution<int> pick(0, static_cast<int>(pool.size()) - 1);

    int raw = 0;
    while (static_cast<int>(g.monsters.size()) < 8) {
        const MonsterDef* m = pool[pick(rng)];
        int newRaw = raw + xpForCR(m->cr);
        int newAdj = newRaw * encMultiplierX100(static_cast<int>(g.monsters.size()) + 1) / 100;
        if (!g.monsters.empty() && newAdj > deadly) break;   // adding this overshoots -> stop
        g.monsters.push_back(m);
        raw = newRaw;
        if (newAdj >= target) break;                          // reached the target band
    }
    g.totalXP = raw;
    g.adjustedXP = raw * encMultiplierX100(static_cast<int>(g.monsters.size())) / 100;
    return g;
}

inline std::string pluralize(const std::string& s) {
    if (!s.empty() && s.back() == 'f') return s.substr(0, s.size() - 1) + "ves";  // Wolf -> Wolves
    return s + "s";
}

// "a Wolf", "2 Wolves and a Goblin", "3 Giant Rats, a Boar and a Scout".
inline std::string describeGroup(const EncounterGroup& g) {
    if (g.monsters.empty()) return "nothing";
    std::vector<std::pair<std::string, int>> counts;      // preserve first-seen order
    for (auto* m : g.monsters) {
        bool found = false;
        for (auto& c : counts) if (c.first == m->name) { c.second++; found = true; break; }
        if (!found) counts.push_back({m->name, 1});
    }
    std::vector<std::string> parts;
    for (auto& c : counts)
        parts.push_back(c.second == 1 ? "a " + c.first
                                      : std::to_string(c.second) + " " + pluralize(c.first));
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i == 0)                    out = parts[i];
        else if (i == parts.size() - 1) out += " and " + parts[i];
        else                            out += ", " + parts[i];
    }
    return out;
}

}  // namespace rpgb
