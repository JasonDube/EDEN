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
        // simple melee
        {"Club",               10, 2.0f, "Weapon"},
        {"Greatclub",          20,10.0f, "Weapon"},
        {"Javelin",            50, 2.0f, "Weapon"},
        {"Light Hammer",      200, 2.0f, "Weapon"},
        {"Sickle",            100, 2.0f, "Weapon"},
        {"Dart",                5, 0.25f,"Weapon"},
        {"Sling",              10, 0.0f, "Weapon"},
        // martial melee
        {"Flail",            1000, 2.0f, "Weapon"},
        {"Glaive",           2000, 6.0f, "Weapon"},
        {"Greataxe",         3000, 7.0f, "Weapon"},
        {"Greatsword",       5000, 6.0f, "Weapon"},
        {"Halberd",          2000, 6.0f, "Weapon"},
        {"Lance",            1000, 6.0f, "Weapon"},
        {"Maul",             1000,10.0f, "Weapon"},
        {"Morningstar",      1500, 4.0f, "Weapon"},
        {"Pike",              500,18.0f, "Weapon"},
        {"Rapier",           2500, 2.0f, "Weapon"},
        {"Scimitar",         2500, 3.0f, "Weapon"},
        {"Trident",           500, 4.0f, "Weapon"},
        {"War Pick",          500, 2.0f, "Weapon"},
        {"Whip",              200, 3.0f, "Weapon"},
        // martial ranged
        {"Blowgun",          1000, 1.0f, "Weapon"},
        {"Hand Crossbow",    7500, 3.0f, "Weapon"},
        {"Heavy Crossbow",   5000,18.0f, "Weapon"},
        {"Net",               100, 3.0f, "Weapon"},

        // ── Armor ──
        {"Padded Armor",      500,  8.0f, "Armor"},
        {"Leather Armor",    1000, 10.0f, "Armor"},
        {"Shield",           1000,  6.0f, "Armor"},
        {"Studded Leather",  4500, 13.0f, "Armor"},
        {"Chain Shirt",      5000, 20.0f, "Armor"},
        {"Scale Mail",       5000, 45.0f, "Armor"},
        {"Chain Mail",       7500, 55.0f, "Armor"},
        {"Hide Armor",       1000, 12.0f, "Armor"},
        {"Breastplate",     40000, 20.0f, "Armor"},
        {"Half Plate",      75000, 40.0f, "Armor"},
        {"Ring Mail",        3000, 40.0f, "Armor"},
        {"Splint Armor",    20000, 60.0f, "Armor"},
        {"Plate Armor",    150000, 65.0f, "Armor"},

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
        {"Alchemist's Supplies", 5000, 8.0f, "Gear"},
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

// Structured mechanics for equipping: what a thing is, and its numbers.
enum ItemKind { GEAR, WEAPON, ARMOR, SHIELD };
struct ItemDef {
    ItemKind    kind = GEAR;
    // weapon
    const char* dmg = "";       // "1d8"
    const char* dmgType = "";   // "slashing"
    const char* versatile = ""; // two-handed damage, e.g. "1d10", else ""
    bool finesse = false, ranged = false, twoHanded = false, light = false, thrown = false;
    // armor
    int  baseAC = 0;            // 0 = not armor
    int  dexCap = 10;          // max Dex to AC (10 = light/uncapped, 2 = medium, 0 = heavy)
    bool stealthDis = false;
    int  strReq = 0;
    int  shieldBonus = 0;      // shields only (+2)
};

inline ItemDef itemDef(const std::string& n) {
    ItemDef d;
    auto W = [&](const char* dm, const char* ty) { d.kind = WEAPON; d.dmg = dm; d.dmgType = ty; return d; };
    if (n == "Dagger")        { d = W("1d4","piercing"); d.finesse = d.light = d.thrown = true; return d; }
    if (n == "Quarterstaff")  { d = W("1d6","bludgeoning"); d.versatile = "1d8"; return d; }
    if (n == "Spear")         { d = W("1d6","piercing"); d.thrown = true; d.versatile = "1d8"; return d; }
    if (n == "Handaxe")       { d = W("1d6","slashing"); d.light = d.thrown = true; return d; }
    if (n == "Mace")          { return W("1d6","bludgeoning"); }
    if (n == "Shortsword")    { d = W("1d6","piercing"); d.finesse = d.light = true; return d; }
    if (n == "Battleaxe")     { d = W("1d8","slashing"); d.versatile = "1d10"; return d; }
    if (n == "Longsword")     { d = W("1d8","slashing"); d.versatile = "1d10"; return d; }
    if (n == "Warhammer")     { d = W("1d8","bludgeoning"); d.versatile = "1d10"; return d; }
    if (n == "Shortbow")      { d = W("1d6","piercing"); d.ranged = d.twoHanded = true; return d; }
    if (n == "Light Crossbow"){ d = W("1d8","piercing"); d.ranged = d.twoHanded = true; return d; }
    if (n == "Longbow")       { d = W("1d8","piercing"); d.ranged = d.twoHanded = true; return d; }
    // simple melee
    if (n == "Club")          { d = W("1d4","bludgeoning"); d.light = true; return d; }
    if (n == "Greatclub")     { d = W("1d8","bludgeoning"); d.twoHanded = true; return d; }
    if (n == "Javelin")       { d = W("1d6","piercing"); d.thrown = true; return d; }
    if (n == "Light Hammer")  { d = W("1d4","bludgeoning"); d.light = d.thrown = true; return d; }
    if (n == "Sickle")        { d = W("1d4","slashing"); d.light = true; return d; }
    // simple ranged
    if (n == "Dart")          { d = W("1d4","piercing"); d.finesse = d.thrown = true; return d; }
    if (n == "Sling")         { d = W("1d4","bludgeoning"); d.ranged = true; return d; }
    // martial melee
    if (n == "Flail")         { return W("1d8","bludgeoning"); }
    if (n == "Glaive")        { d = W("1d10","slashing"); d.twoHanded = true; return d; }   // heavy, reach
    if (n == "Greataxe")      { d = W("1d12","slashing"); d.twoHanded = true; return d; }   // heavy
    if (n == "Greatsword")    { d = W("2d6","slashing"); d.twoHanded = true; return d; }    // heavy
    if (n == "Halberd")       { d = W("1d10","slashing"); d.twoHanded = true; return d; }   // heavy, reach
    if (n == "Lance")         { return W("1d12","piercing"); }                              // reach, special
    if (n == "Maul")          { d = W("2d6","bludgeoning"); d.twoHanded = true; return d; } // heavy
    if (n == "Morningstar")   { return W("1d8","piercing"); }
    if (n == "Pike")          { d = W("1d10","piercing"); d.twoHanded = true; return d; }   // heavy, reach
    if (n == "Rapier")        { d = W("1d8","piercing"); d.finesse = true; return d; }
    if (n == "Scimitar")      { d = W("1d6","slashing"); d.finesse = d.light = true; return d; }
    if (n == "Trident")       { d = W("1d6","piercing"); d.thrown = true; d.versatile = "1d8"; return d; }
    if (n == "War Pick")      { return W("1d8","piercing"); }
    if (n == "Whip")          { d = W("1d4","slashing"); d.finesse = true; return d; }      // reach
    // martial ranged
    if (n == "Blowgun")       { d = W("1","piercing"); d.ranged = true; return d; }
    if (n == "Hand Crossbow") { d = W("1d6","piercing"); d.ranged = d.light = true; return d; }
    if (n == "Heavy Crossbow"){ d = W("1d10","piercing"); d.ranged = d.twoHanded = true; return d; } // heavy
    if (n == "Net")           { d = W("-","special"); d.thrown = true; return d; }

    auto A = [&](int ac, int cap, bool st, int str) { d.kind = ARMOR; d.baseAC = ac; d.dexCap = cap; d.stealthDis = st; d.strReq = str; return d; };
    if (n == "Padded Armor")   { return A(11, 10, true,  0); }
    if (n == "Leather Armor")  { return A(11, 10, false, 0); }
    if (n == "Studded Leather"){ return A(12, 10, false, 0); }
    if (n == "Chain Shirt")    { return A(13, 2,  false, 0); }
    if (n == "Scale Mail")     { return A(14, 2,  true,  0); }
    if (n == "Chain Mail")     { return A(16, 0,  true, 13); }
    if (n == "Hide Armor")     { return A(12, 2,  false, 0); }
    if (n == "Breastplate")    { return A(14, 2,  false, 0); }
    if (n == "Half Plate")     { return A(15, 2,  true,  0); }
    if (n == "Ring Mail")      { return A(14, 0,  true,  0); }
    if (n == "Splint Armor")   { return A(17, 0,  true, 15); }
    if (n == "Plate Armor")    { return A(18, 0,  true, 15); }
    if (n == "Shield")         { d.kind = SHIELD; d.shieldBonus = 2; return d; }
    return d;   // GEAR
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
