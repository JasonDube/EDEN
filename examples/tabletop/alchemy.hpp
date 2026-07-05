// alchemy.hpp — foraged ingredients and the recipes that brew them into potions.
//
// DRAFT (2026-07-05): the data model agreed in chat —
//   - exact-ish recipes: 2-3 NAMED ingredients (+ an `essence` tag for fuzzy
//     matching later)
//   - recipes are DISCOVERED: you start knowing Healing, learn the rest
//   - brewed at camp with Alchemist's Supplies (an INT check vs the recipe DC)
//   - rarity is GATED BY INGREDIENT SOURCE: rare potions need rare ingredients
//     from harder biomes, so WHERE you travel decides WHAT you can make.
//
// Ingredients reuse the bestiary's Biome bitmask, so foraging a terrain yields
// that terrain's ingredients for free. This file is DATA ONLY; the forage action,
// the ingredient pouch, and the brew UI are wired on top of it separately.
#pragma once

#include "bestiary.hpp"   // rpgb::Biome bitmask + kSurface/kAnywhere

#include <cstdint>
#include <string>
#include <vector>

namespace rpga {   // alchemy

// How often foraging turns an ingredient up (and, loosely, how "hard" the
// potions it enables should be). Rare ingredients live in harder biomes.
enum Rarity { Common = 0, Uncommon = 1, Rare = 2, VeryRare = 3 };
inline const char* rarityName(int r) {
    switch (r) { case Common: return "Common"; case Uncommon: return "Uncommon";
                 case Rare: return "Rare"; default: return "Very Rare"; }
}

// A foraged component. `biomes` is an OR of rpgb::Biome flags — the terrains it
// grows in. `essence` is a loose category ("healing", "fire", "vigor", "mind",
// "solvent", ...) that recipes can match fuzzily once we want that.
struct Ingredient {
    const char* name;
    uint16_t    biomes;
    int         rarity;    // Rarity
    const char* essence;
    const char* desc;
};

inline const std::vector<Ingredient>& ingredients() {
    using namespace rpgb;
    static const std::vector<Ingredient> v = {
        // ── grassland/forest: the starter healing line ──
        {"Redcap Berry", Grassland | Forest,               Common,   "healing",
         "Sweet crimson berries that knit small wounds."},
        {"Kingsleaf",    Grassland | Hill,                  Common,   "healing",
         "A hardy roadside herb, bitter but restorative."},
        {"Springwater",  Grassland | Forest | Hill | Coastal, Common, "solvent",
         "Clean running water — the base of most brews."},
        // ── other biomes: seeds for future recipes (terrain-gated) ──
        {"Ember Salt",   Desert | Mountain,                 Uncommon, "fire",
         "Red mineral crust from sun-baked flats; warm to the touch."},
        {"Frostmoss",    Arctic | Mountain,                 Uncommon, "cold",
         "Pale lichen that never thaws."},
        {"Ironvine Root",Hill | Mountain,                   Uncommon, "vigor",
         "A knotted root prized for the strength it lends."},
        {"Whispercap",   Underdark,                         Rare,     "mind",
         "A cave mushroom that hums faintly; loosens the mind's ear."},
    };
    return v;
}

inline const Ingredient* ingredient(const std::string& name) {
    for (const auto& i : ingredients()) if (name == i.name) return &i;
    return nullptr;
}

// Everything foragable in a given biome (for the forage action to draw from).
inline std::vector<const Ingredient*> ingredientsInBiome(rpgb::Biome b) {
    std::vector<const Ingredient*> out;
    for (const auto& i : ingredients()) if (i.biomes & b) out.push_back(&i);
    return out;
}

// ── recipes ──────────────────────────────────────────────────────────────────
struct RecipePart { const char* name; int qty; };
struct Recipe {
    const char*             output;        // potion produced (matches an itemDef name)
    std::vector<RecipePart> parts;         // 2-3 named ingredients + quantities
    int                     dc;            // Alchemist's Supplies (INT) DC to brew
    bool                    knownAtStart;  // true = you begin knowing it
    const char*             essence;       // loose category (for fuzzy matching later)
    const char*             desc;
};

inline const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> v = {
        {"Potion of Healing",
         { {"Redcap Berry", 2}, {"Springwater", 1} }, 10, /*known*/true, "healing",
         "Restores 2d4+2 HP. The first draught every apothecary learns."},
        {"Potion of Greater Healing",
         { {"Redcap Berry", 3}, {"Kingsleaf", 1}, {"Springwater", 1} }, 13, false, "healing",
         "Restores 4d4+4 HP."},
        {"Potion of Fire Resistance",
         { {"Ember Salt", 1}, {"Springwater", 1} }, 12, false, "fire",
         "Resistance to fire damage for 1 hour."},
    };
    return v;
}

inline const Recipe* recipe(const std::string& output) {
    for (const auto& r : recipes()) if (output == r.output) return &r;
    return nullptr;
}

}  // namespace rpga
