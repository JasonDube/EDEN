// shop.hpp — Orlen's wares (a general arms-and-goods merchant).
//
// Prices are SRD 5.1 equipment costs, stored in COPPER (1 gp = 100 cp, 1 sp = 10 cp)
// so the till never rounds. Weights are per-unit pounds. This is the stock Orlen
// sells; he buys back what he stocks at half the list price.
#pragma once

#include <string>
#include <vector>

namespace rpgs {

struct Ware {
    const char* name;
    int         priceCp;   // list price, in copper
    float       weight;    // per unit, lb
    const char* cat;       // "Weapon" | "Armor" | "Gear"
};

inline const std::vector<Ware>& orlensWares() {
    static const std::vector<Ware> v = {
        // ── Weapons ──
        {"Dagger",            200, 1.0f, "Weapon"},
        {"Quarterstaff",       20, 4.0f, "Weapon"},
        {"Spear",             100, 3.0f, "Weapon"},
        {"Handaxe",           500, 2.0f, "Weapon"},
        {"Mace",              500, 4.0f, "Weapon"},
        {"Shortsword",       1000, 2.0f, "Weapon"},
        {"Battleaxe",        1000, 4.0f, "Weapon"},
        {"Longsword",        1500, 3.0f, "Weapon"},
        {"Warhammer",        1500, 2.0f, "Weapon"},
        {"Shortbow",         2500, 2.0f, "Weapon"},
        {"Light Crossbow",   2500, 5.0f, "Weapon"},
        {"Longbow",          5000, 2.0f, "Weapon"},
        {"Arrows (20)",       100, 1.0f, "Weapon"},

        // ── Armor ──
        {"Padded Armor",      500,  8.0f, "Armor"},
        {"Leather Armor",    1000, 10.0f, "Armor"},
        {"Shield",           1000,  6.0f, "Armor"},
        {"Studded Leather",  4500, 13.0f, "Armor"},
        {"Chain Shirt",      5000, 20.0f, "Armor"},
        {"Scale Mail",       5000, 45.0f, "Armor"},
        {"Chain Mail",       7500, 55.0f, "Armor"},

        // ── Adventuring Gear ──
        {"Torch",               1, 1.0f, "Gear"},
        {"Oil (flask)",        10, 1.0f, "Gear"},
        {"Rations (1 day)",    50, 2.0f, "Gear"},
        {"Tinderbox",          50, 1.0f, "Gear"},
        {"Waterskin",          20, 5.0f, "Gear"},
        {"Bedroll",           100, 7.0f, "Gear"},
        {"Rope, Hempen (50ft)",100,10.0f, "Gear"},
        {"Grappling Hook",    200, 4.0f, "Gear"},
        {"Crowbar",           200, 5.0f, "Gear"},
        {"Backpack",          200, 5.0f, "Gear"},
        {"Hooded Lantern",    500, 2.0f, "Gear"},
        {"Healer's Kit",      500, 3.0f, "Gear"},
        {"Holy Symbol",       500, 1.0f, "Gear"},
        {"Thieves' Tools",   2500, 1.0f, "Gear"},
        {"Component Pouch",  2500, 2.0f, "Gear"},
        {"Potion of Healing",5000, 0.5f, "Gear"},
    };
    return v;
}

// SRD combat/use stats for the tooltip - damage & properties for weapons, AC for
// armor, and the relevant effect for gear. "" for items with nothing to show.
inline const char* itemStats(const std::string& n) {
    // ── weapons ──
    if (n == "Dagger")          return "Simple melee - 1d4 piercing. Finesse, light, thrown (20/60 ft).";
    if (n == "Quarterstaff")    return "Simple melee - 1d6 bludgeoning. Versatile (1d8).";
    if (n == "Spear")           return "Simple melee - 1d6 piercing. Thrown (20/60 ft), versatile (1d8).";
    if (n == "Handaxe")         return "Simple melee - 1d6 slashing. Light, thrown (20/60 ft).";
    if (n == "Mace")            return "Simple melee - 1d6 bludgeoning.";
    if (n == "Shortsword")      return "Martial melee - 1d6 piercing. Finesse, light.";
    if (n == "Battleaxe")       return "Martial melee - 1d8 slashing. Versatile (1d10).";
    if (n == "Longsword")       return "Martial melee - 1d8 slashing. Versatile (1d10).";
    if (n == "Warhammer")       return "Martial melee - 1d8 bludgeoning. Versatile (1d10).";
    if (n == "Shortbow")        return "Simple ranged - 1d6 piercing. Ammunition, range 80/320, two-handed.";
    if (n == "Light Crossbow")  return "Simple ranged - 1d8 piercing. Ammunition, range 80/320, loading, two-handed.";
    if (n == "Longbow")         return "Martial ranged - 1d8 piercing. Ammunition, range 150/600, heavy, two-handed.";
    if (n == "Arrows (20)")     return "Ammunition for bows.";
    // ── armor ──
    if (n == "Padded Armor")    return "Light armor - AC 11 + Dex. Disadvantage on Stealth.";
    if (n == "Leather Armor")   return "Light armor - AC 11 + Dex.";
    if (n == "Shield")          return "+2 AC (held in one hand).";
    if (n == "Studded Leather") return "Light armor - AC 12 + Dex.";
    if (n == "Chain Shirt")     return "Medium armor - AC 13 + Dex (max 2).";
    if (n == "Scale Mail")      return "Medium armor - AC 14 + Dex (max 2). Disadvantage on Stealth.";
    if (n == "Chain Mail")      return "Heavy armor - AC 16. Requires Str 13. Disadvantage on Stealth.";
    // ── gear with a mechanical effect ──
    if (n == "Potion of Healing") return "Drink (an action) to regain 2d4+2 hit points.";
    if (n == "Healer's Kit")    return "10 uses - stabilize a dying creature with no check.";
    if (n == "Torch")           return "Bright light 20 ft; 1d4 fire as an improvised weapon.";
    if (n == "Oil (flask)")     return "Throw for a 5-ft splash; 5 fire damage if lit.";
    if (n == "Thieves' Tools")  return "Proficiency to pick locks and disarm traps.";
    if (n == "Holy Symbol")     return "Spellcasting focus for clerics and paladins.";
    if (n == "Component Pouch") return "Spellcasting focus (material components).";
    if (n == "Hooded Lantern")  return "Bright light 30 ft; burns 6 hours per flask of oil.";
    if (n == "Rope, Hempen (50ft)") return "2 HP; bursts on a DC 17 Strength check.";
    if (n == "Grappling Hook")  return "Anchor a rope to a ledge or battlement.";
    if (n == "Crowbar")         return "Advantage on Strength checks where leverage helps.";
    return "";
}

// Half the list price is what Orlen pays for goods he stocks; 0 if he won't buy it.
inline int sellPriceCp(const std::string& name) {
    for (const auto& w : orlensWares()) if (name == w.name) return w.priceCp / 2;
    return 0;
}

// Format a copper amount gold-first, e.g. "12 gp 5 sp" or "2 sp". Gold is the coin
// players reckon in, so platinum is folded into gp rather than shown separately.
inline std::string priceStr(long cp) {
    if (cp <= 0) return "0 gp";
    long gp = cp / 100; cp %= 100;
    long sp = cp / 10;  cp %= 10;
    std::string s;
    auto add = [&](long n, const char* u) { if (n) { if (!s.empty()) s += " "; s += std::to_string(n); s += " "; s += u; } };
    add(gp, "gp"); add(sp, "sp"); add(cp, "cp");
    return s.empty() ? "0 gp" : s;
}

}  // namespace rpgs
