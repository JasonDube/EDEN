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
