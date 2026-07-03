// Character model for the RPG companion: a 5e SRD character sheet's data plus
// the derived math (ability modifiers, proficiency bonus, skill/save bonuses,
// passive perception) and a simple line-based save format. UI-free and
// graphics-free, so it can be unit-tested on its own.
#pragma once

#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace rpgc {

// The six abilities, in the canonical SRD order.
enum Ability { STR = 0, DEX, CON, INT, WIS, CHA, ABILITY_COUNT };

inline const char* abilityName(int a) {
    static const char* kNames[ABILITY_COUNT] = {
        "Strength", "Dexterity", "Constitution",
        "Intelligence", "Wisdom", "Charisma"};
    return (a >= 0 && a < ABILITY_COUNT) ? kNames[a] : "?";
}
inline const char* abilityAbbr(int a) {
    static const char* kAbbr[ABILITY_COUNT] = {"STR", "DEX", "CON", "INT", "WIS", "CHA"};
    return (a >= 0 && a < ABILITY_COUNT) ? kAbbr[a] : "?";
}

// The 18 SRD skills, each governed by one ability.
struct SkillDef { const char* name; int ability; };
inline const std::array<SkillDef, 18>& skills() {
    static const std::array<SkillDef, 18> kSkills = {{
        {"Acrobatics", DEX},      {"Animal Handling", WIS}, {"Arcana", INT},
        {"Athletics", STR},       {"Deception", CHA},       {"History", INT},
        {"Insight", WIS},         {"Intimidation", CHA},    {"Investigation", INT},
        {"Medicine", WIS},        {"Nature", INT},          {"Perception", WIS},
        {"Performance", CHA},     {"Persuasion", CHA},      {"Religion", INT},
        {"Sleight of Hand", DEX}, {"Stealth", DEX},         {"Survival", WIS},
    }};
    return kSkills;
}
inline int perceptionSkillIndex() { return 11; }  // "Perception" above

// Plain-language help for each skill (indices match skills() above).
inline const char* skillDesc(int i) {
    static const char* d[18] = {
        "Acrobatics (DEX) - keep your balance, tumble, escape a grapple.",
        "Animal Handling (WIS) - calm or control animals, sense their intent, ride.",
        "Arcana (INT) - recall lore about spells, magic items, planes, symbols.",
        "Athletics (STR) - climb, jump, swim, and grapple; raw physical feats.",
        "Deception (CHA) - lie convincingly, mislead, hide your true intent.",
        "History (INT) - recall past events, people, kingdoms, wars, legends.",
        "Insight (WIS) - read a creature's true intentions and detect lies.",
        "Intimidation (CHA) - sway others with threats and force of presence.",
        "Investigation (INT) - search for clues, deduce, find hidden things.",
        "Medicine (WIS) - stabilize the dying, diagnose illness, treat wounds.",
        "Nature (INT) - recall lore about terrain, plants, animals, weather.",
        "Perception (WIS) - notice things by sight, sound, or smell. Very common.",
        "Performance (CHA) - entertain a crowd with music, dance, or acting.",
        "Persuasion (CHA) - win people over with tact, warmth, and good faith.",
        "Religion (INT) - recall lore about deities, rites, and the divine.",
        "Sleight of Hand (DEX) - pickpocket, palm objects, manual trickery.",
        "Stealth (DEX) - hide and move unseen and unheard.",
        "Survival (WIS) - track, forage, navigate the wild, predict weather.",
    };
    return (i >= 0 && i < 18) ? d[i] : "";
}

// ── identity pick-lists ──────────────────────────────────────────────────────
// Options for the character sheet's dropdowns. The underlying text fields stay
// editable, so homebrew entries are still allowed — these just prevent typos on
// the common choices. Races/subraces and classes follow the SRD; backgrounds use
// the familiar PHB set (the SRD only defines Acolyte mechanically).

inline const std::vector<const char*>& raceOptions() {
    static const std::vector<const char*> v = {
        "Human", "Dwarf", "Hill Dwarf", "Mountain Dwarf",
        "Elf", "High Elf", "Wood Elf", "Drow (Dark Elf)",
        "Halfling", "Lightfoot Halfling", "Stout Halfling",
        "Dragonborn", "Gnome", "Rock Gnome",
        "Half-Elf", "Half-Orc", "Tiefling",
    };
    return v;
}
inline const std::vector<const char*>& classOptions() {
    static const std::vector<const char*> v = {
        "Barbarian", "Bard", "Cleric", "Druid", "Fighter", "Monk",
        "Paladin", "Ranger", "Rogue", "Sorcerer", "Warlock", "Wizard",
    };
    return v;
}
inline const std::vector<const char*>& backgroundOptions() {
    static const std::vector<const char*> v = {
        "Acolyte", "Charlatan", "Criminal", "Entertainer", "Folk Hero",
        "Guild Artisan", "Hermit", "Noble", "Outlander", "Sage",
        "Sailor", "Soldier", "Urchin",
    };
    return v;
}
// Background = two fixed skill proficiencies + a social feature + a plain-language
// blurb for new players. Skill indices match skills() below.
struct BackgroundInfo { int skill1 = 0, skill2 = 0; const char* desc = ""; };
inline BackgroundInfo backgroundInfo(const std::string& bg) {
    // skill indices: 0 Acrobatics 1 AnimalHandling 2 Arcana 3 Athletics 4 Deception
    // 5 History 6 Insight 7 Intimidation 8 Investigation 9 Medicine 10 Nature
    // 11 Perception 12 Performance 13 Persuasion 14 Religion 15 SleightOfHand 16 Stealth 17 Survival
    if (bg == "Acolyte")       return {6, 14, "ACOLYTE\nA servant of a temple, versed in rites and doctrine.\nSkills: Insight, Religion.\nFeature: Shelter of the Faithful - temples of your faith give you aid and lodging."};
    if (bg == "Charlatan")     return {4, 15, "CHARLATAN\nA smooth-talking con artist who lives by the scam.\nSkills: Deception, Sleight of Hand.\nFeature: False Identity - you have a second persona and can forge documents."};
    if (bg == "Criminal")      return {4, 16, "CRIMINAL\nA thief, smuggler, or enforcer of the underworld.\nSkills: Deception, Stealth.\nFeature: Criminal Contact - a reliable go-between to the criminal network."};
    if (bg == "Entertainer")   return {0, 12, "ENTERTAINER\nA performer who thrives before a crowd.\nSkills: Acrobatics, Performance.\nFeature: By Popular Demand - you can always find a place to perform for food and lodging."};
    if (bg == "Folk Hero")     return {1, 17, "FOLK HERO\nA commoner who stood up for the downtrodden.\nSkills: Animal Handling, Survival.\nFeature: Rustic Hospitality - common folk shelter and hide you."};
    if (bg == "Guild Artisan") return {6, 13, "GUILD ARTISAN\nA skilled craftsperson and guild member.\nSkills: Insight, Persuasion.\nFeature: Guild Membership - your guild offers lodging, aid, and political sway."};
    if (bg == "Hermit")        return {9, 14, "HERMIT\nOne who lived in seclusion, seeking insight.\nSkills: Medicine, Religion.\nFeature: Discovery - you learned a unique and powerful secret in your isolation."};
    if (bg == "Noble")         return {5, 13, "NOBLE\nBorn to title and privilege - well suited to a world of feudal intrigue.\nSkills: History, Persuasion.\nFeature: Position of Privilege - highborn folk receive you as one of their own."};
    if (bg == "Outlander")     return {3, 17, "OUTLANDER\nRaised in the wilds, far from civilization.\nSkills: Athletics, Survival.\nFeature: Wanderer - you can always find food and water and never forget terrain you've crossed."};
    if (bg == "Sage")          return {2,  5, "SAGE\nA scholar of lore, magic, and history.\nSkills: Arcana, History.\nFeature: Researcher - you know where and from whom to learn what you don't know."};
    if (bg == "Sailor")        return {3, 11, "SAILOR\nA salt of the sea who has weathered many voyages.\nSkills: Athletics, Perception.\nFeature: Ship's Passage - you can secure free passage on a sailing ship for you and companions."};
    if (bg == "Soldier")       return {3,  7, "SOLDIER\nA veteran of a militia, mercenary band, or army.\nSkills: Athletics, Intimidation.\nFeature: Military Rank - soldiers loyal to your old force recognize your authority."};
    if (bg == "Urchin")        return {15, 16, "URCHIN\nYou grew up on the streets, alone and clever.\nSkills: Sleight of Hand, Stealth.\nFeature: City Secrets - you know the hidden ways through any city, moving at double speed."};
    return {6, 14, "A background shapes your past, granting two skill proficiencies and a social feature."};
}

inline const std::vector<const char*>& alignmentOptions() {
    static const std::vector<const char*> v = {
        "Lawful Good", "Neutral Good", "Chaotic Good",
        "Lawful Neutral", "Neutral", "Chaotic Neutral",
        "Lawful Evil", "Neutral Evil", "Chaotic Evil",
    };
    return v;
}

// Plain-language help for a brand-new player deciding what to prioritize.
inline const char* abilityDesc(int a) {
    static const char* d[ABILITY_COUNT] = {
        "STRENGTH\nMelee attacks & damage, Athletics, carrying capacity.\nKey for: Barbarian, Fighter, Paladin.",
        "DEXTERITY\nArmor class, ranged/finesse attacks, initiative, Stealth.\nKey for: Rogue, Ranger, Monk (and everyone's AC).",
        "CONSTITUTION\nHit points and stamina. Almost every class wants some.",
        "INTELLIGENCE\nWizard spellcasting; Arcana, History, Investigation.",
        "WISDOM\nCleric/Druid/Ranger casting; Perception, Insight, willpower saves.",
        "CHARISMA\nBard/Sorcerer/Warlock/Paladin casting; Persuasion, Deception.",
    };
    return (a >= 0 && a < ABILITY_COUNT) ? d[a] : "";
}

// Racial ability score increases (SRD). Matches the option strings above.
// (Half-Elf also grants +1 to two abilities of the player's choice — deferred.)
inline std::array<int, ABILITY_COUNT> raceAbilityBonuses(const std::string& race) {
    std::array<int, ABILITY_COUNT> b{{0, 0, 0, 0, 0, 0}};
    if (race == "Human")                 { for (auto& x : b) x = 1; }
    else if (race == "Dwarf")            { b[CON] += 2; }
    else if (race == "Hill Dwarf")       { b[CON] += 2; b[WIS] += 1; }
    else if (race == "Mountain Dwarf")   { b[CON] += 2; b[STR] += 2; }
    else if (race == "Elf")              { b[DEX] += 2; }
    else if (race == "High Elf")         { b[DEX] += 2; b[INT] += 1; }
    else if (race == "Wood Elf")         { b[DEX] += 2; b[WIS] += 1; }
    else if (race == "Drow (Dark Elf)")  { b[DEX] += 2; b[CHA] += 1; }
    else if (race == "Halfling")         { b[DEX] += 2; }
    else if (race == "Lightfoot Halfling"){ b[DEX] += 2; b[CHA] += 1; }
    else if (race == "Stout Halfling")   { b[DEX] += 2; b[CON] += 1; }
    else if (race == "Dragonborn")       { b[STR] += 2; b[CHA] += 1; }
    else if (race == "Gnome")            { b[INT] += 2; }
    else if (race == "Rock Gnome")       { b[INT] += 2; b[CON] += 1; }
    else if (race == "Half-Elf")         { b[CHA] += 2; }
    else if (race == "Half-Orc")         { b[STR] += 2; b[CON] += 1; }
    else if (race == "Tiefling")         { b[INT] += 1; b[CHA] += 2; }
    return b;
}

// Ability priority for a class (highest rolled goes to the first). Used by the
// "auto-assign" button so newcomers get a sensible spread.
inline std::array<int, ABILITY_COUNT> classAbilityPriority(const std::string& cls) {
    if (cls == "Barbarian") return {{STR, CON, DEX, WIS, CHA, INT}};
    if (cls == "Bard")      return {{CHA, DEX, CON, WIS, INT, STR}};
    if (cls == "Cleric")    return {{WIS, CON, STR, DEX, CHA, INT}};
    if (cls == "Druid")     return {{WIS, CON, DEX, INT, CHA, STR}};
    if (cls == "Fighter")   return {{STR, CON, DEX, WIS, CHA, INT}};
    if (cls == "Monk")      return {{DEX, WIS, CON, STR, INT, CHA}};
    if (cls == "Paladin")   return {{STR, CHA, CON, WIS, DEX, INT}};
    if (cls == "Ranger")    return {{DEX, WIS, CON, STR, INT, CHA}};
    if (cls == "Rogue")     return {{DEX, CON, INT, CHA, WIS, STR}};
    if (cls == "Sorcerer")  return {{CHA, CON, DEX, WIS, INT, STR}};
    if (cls == "Warlock")   return {{CHA, CON, DEX, WIS, INT, STR}};
    if (cls == "Wizard")    return {{INT, CON, DEX, WIS, CHA, STR}};
    return {{STR, CON, DEX, WIS, CHA, INT}};
}

// Class-granted proficiencies (SRD): how many skills you choose, the skill list
// to choose from (indices into skills()), and the two fixed saving throws.
struct ClassProfs {
    int skillCount = 2;
    std::vector<int> skillList;
    int save1 = STR, save2 = CON;
};
inline ClassProfs classProficiencies(const std::string& cls) {
    if (cls == "Barbarian") return {2, {1, 3, 7, 10, 11, 17}, STR, CON};
    if (cls == "Bard")      return {3, {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17}, DEX, CHA};
    if (cls == "Cleric")    return {2, {5, 6, 9, 13, 14}, WIS, CHA};
    if (cls == "Druid")     return {2, {2, 1, 6, 9, 10, 11, 14, 17}, INT, WIS};
    if (cls == "Fighter")   return {2, {0, 1, 3, 5, 6, 7, 11, 17}, STR, CON};
    if (cls == "Monk")      return {2, {0, 3, 5, 6, 14, 16}, STR, DEX};
    if (cls == "Paladin")   return {2, {3, 6, 7, 9, 13, 14}, WIS, CHA};
    if (cls == "Ranger")    return {3, {1, 3, 6, 8, 10, 11, 16, 17}, STR, DEX};
    if (cls == "Rogue")     return {4, {0, 3, 4, 6, 7, 8, 11, 12, 13, 15, 16}, DEX, INT};
    if (cls == "Sorcerer")  return {2, {2, 4, 6, 7, 13, 14}, CON, CHA};
    if (cls == "Warlock")   return {2, {2, 4, 5, 7, 8, 10, 14}, WIS, CHA};
    if (cls == "Wizard")    return {2, {2, 5, 6, 8, 9, 14}, INT, WIS};
    return {2, {}, STR, CON};
}

// ── starting wealth by class ─────────────────────────────────────────────────
// 5e "Starting Wealth by Class": gold = (sum of d4count d4s) * mult. This only
// describes the class table; the actual roll happens in the app via the dice
// engine, and the grant is a one-time, class-independent event on the character.
struct StartingWealth { bool known = false; int d4count = 0; int mult = 0; };

inline StartingWealth startingWealthForClass(const std::string& cls) {
    std::string s;
    for (char c : cls) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "barbarian" || s == "druid")                            return {true, 2, 10};
    if (s == "sorcerer")                                             return {true, 3, 10};
    if (s == "rogue" || s == "warlock" || s == "wizard")            return {true, 4, 10};
    if (s == "bard" || s == "cleric" || s == "fighter" ||
        s == "paladin" || s == "ranger")                            return {true, 5, 10};
    if (s == "monk")                                                 return {true, 5, 1};
    return {false, 0, 0};  // unknown / homebrew: enter coins manually
}

// round(d4count * 2.5 * mult), the take-average option.
inline int startingWealthAverageGp(const StartingWealth& w) {
    return (w.d4count * w.mult * 5 + 1) / 2;
}

// ── derived math ─────────────────────────────────────────────────────────────

// 5e ability modifier: floor((score - 10) / 2). Integer floor division in C++
// truncates toward zero, so shift negatives before dividing.
inline int abilityMod(int score) {
    int d = score - 10;
    return (d >= 0) ? (d / 2) : -(((-d) + 1) / 2);
}

// Proficiency bonus by character level (SRD table): +2 at 1-4, +3 at 5-8, ... +6 at 17-20.
inline int proficiencyBonus(int level) {
    if (level < 1) level = 1;
    if (level > 20) level = 20;
    return 2 + (level - 1) / 4;
}

// ── the character ────────────────────────────────────────────────────────────

// One inventory line. `slot` and `imagePath` are unused by the Stage-1 list UI
// but are stored now so the later paper-doll / per-item-image features need no
// data migration.
struct Item {
    std::string name;
    int         qty      = 1;
    float       weight   = 0.0f;   // per-unit, in pounds
    bool        equipped = false;
    std::string slot;              // future: "armor", "main", "off", ...
    std::string imagePath;         // future: per-item image
};

struct Character {
    // identity
    std::string name, className, race, background, alignment;
    std::string house;          // assigned house (see houses.hpp), e.g. "House Cawood"
    std::string standing;       // rung within the house, e.g. "sworn sword of"
    std::string surname;        // family name, e.g. "Cawood" or a bastard name "Frost"
    std::string ironLegacy;     // heritable personal trait (see family.hpp)
    int level = 1;
    int xp    = 0;

    // ability scores
    std::array<int, ABILITY_COUNT> abilities{{10, 10, 10, 10, 10, 10}};

    // proficiencies
    std::array<bool, ABILITY_COUNT> saveProf{};   // saving-throw proficiency per ability
    std::array<bool, 18> skillProf{};             // skill proficiency
    std::array<bool, 18> skillExpert{};           // expertise (double proficiency)

    // combat
    int armorClass = 10;
    int speed      = 30;
    int maxHP = 0, curHP = 0, tempHP = 0;
    int hitDiceTotal = 1;
    int hitDieSize   = 8;   // e.g. 8 => d8
    int deathSuccess = 0, deathFail = 0;

    // currency (electrum omitted — rarely used at the table)
    int platinum = 0, gold = 0, silver = 0, copper = 0;

    // starting wealth: a one-time creation grant, deliberately independent of the
    // class field so swapping class can't re-trigger it (no exploit).
    bool startingWealthTaken = false;
    int  startingWealthGp    = 0;   // amount granted, for the locked readout

    // inventory
    std::vector<Item> items;

    // free text
    std::string attacks;            // weapons/spell attacks, one per line
    std::string equipment;
    std::string features;           // features & traits
    std::string proficienciesLangs; // other proficiencies & languages
    std::string personality;        // traits, ideals, bonds, flaws
    std::string notes;
    std::string portraitPath;       // absolute path to a portrait image, if any

    // ---- derived accessors ----
    int mod(int ability) const { return abilityMod(abilities[ability]); }
    int profBonus() const { return proficiencyBonus(level); }
    int initiative() const { return mod(DEX); }

    int saveBonus(int ability) const {
        return mod(ability) + (saveProf[ability] ? profBonus() : 0);
    }
    int skillBonus(int i) const {
        int base = mod(skills()[i].ability);
        int p    = skillExpert[i] ? profBonus() * 2 : (skillProf[i] ? profBonus() : 0);
        return base + p;
    }
    int passivePerception() const { return 10 + skillBonus(perceptionSkillIndex()); }

    // Encumbrance (5e): carrying capacity is STR x 15 lb; the variant encumbered /
    // heavily-encumbered thresholds are STR x 5 and STR x 10.
    float totalWeight() const {
        float w = 0.0f;
        for (const auto& it : items) w += it.qty * it.weight;
        return w;
    }
    float carryingCapacity()  const { return abilities[STR] * 15.0f; }
    float encumberedAt()      const { return abilities[STR] * 5.0f; }
    float heavilyEncumberedAt() const { return abilities[STR] * 10.0f; }
};

// ── serialization (line-based key=value; multiline text is length-prefixed) ───
//
// Format is deliberately plain and greppable, matching the project's sidecar-note
// philosophy. Version tag lets us migrate later.

namespace detail {
inline void putStr(std::ostringstream& o, const char* k, const std::string& v) {
    // length-prefixed so embedded newlines survive a round trip
    o << k << ' ' << v.size() << '\n' << v << '\n';
}
inline std::string getStr(std::istringstream& in, size_t n) {
    std::string v;
    v.resize(n);
    if (n) in.read(&v[0], static_cast<std::streamsize>(n));
    std::string discardNl;
    std::getline(in, discardNl);  // consume trailing newline
    return v;
}
}  // namespace detail

inline std::string serialize(const Character& c) {
    std::ostringstream o;
    o << "rpg-character 1\n";
    detail::putStr(o, "name", c.name);
    detail::putStr(o, "class", c.className);
    detail::putStr(o, "race", c.race);
    detail::putStr(o, "background", c.background);
    detail::putStr(o, "alignment", c.alignment);
    detail::putStr(o, "house", c.house);
    detail::putStr(o, "standing", c.standing);
    detail::putStr(o, "surname", c.surname);
    detail::putStr(o, "legacy", c.ironLegacy);
    o << "level " << c.level << '\n';
    o << "xp " << c.xp << '\n';
    o << "abilities";
    for (int a = 0; a < ABILITY_COUNT; ++a) o << ' ' << c.abilities[a];
    o << '\n';
    o << "saveprof";
    for (int a = 0; a < ABILITY_COUNT; ++a) o << ' ' << (c.saveProf[a] ? 1 : 0);
    o << '\n';
    o << "skillprof";
    for (int i = 0; i < 18; ++i) o << ' ' << (c.skillProf[i] ? 1 : 0);
    o << '\n';
    o << "skillexpert";
    for (int i = 0; i < 18; ++i) o << ' ' << (c.skillExpert[i] ? 1 : 0);
    o << '\n';
    o << "combat " << c.armorClass << ' ' << c.speed << ' ' << c.maxHP << ' '
      << c.curHP << ' ' << c.tempHP << ' ' << c.hitDiceTotal << ' '
      << c.hitDieSize << ' ' << c.deathSuccess << ' ' << c.deathFail << '\n';
    o << "coins " << c.platinum << ' ' << c.gold << ' ' << c.silver << ' ' << c.copper << '\n';
    o << "startwealth " << (c.startingWealthTaken ? 1 : 0) << ' ' << c.startingWealthGp << '\n';
    o << "items " << c.items.size() << '\n';
    for (const auto& it : c.items) {
        detail::putStr(o, "iname", it.name);
        o << "iqty " << it.qty << " iweight " << it.weight
          << " iequip " << (it.equipped ? 1 : 0) << '\n';
        detail::putStr(o, "islot", it.slot);
        detail::putStr(o, "iimage", it.imagePath);
    }
    detail::putStr(o, "attacks", c.attacks);
    detail::putStr(o, "equipment", c.equipment);
    detail::putStr(o, "features", c.features);
    detail::putStr(o, "proflangs", c.proficienciesLangs);
    detail::putStr(o, "personality", c.personality);
    detail::putStr(o, "notes", c.notes);
    detail::putStr(o, "portrait", c.portraitPath);
    return o.str();
}

// Parse a serialized character. Returns true on a recognized format; unknown or
// missing keys leave defaults in place rather than failing hard.
inline bool deserialize(const std::string& text, Character& out) {
    std::istringstream in(text);
    std::string tag;
    int version = 0;
    in >> tag >> version;
    if (tag != "rpg-character") return false;
    in.ignore();  // trailing newline after the version line

    std::string key;
    while (in >> key) {
        if (key == "name" || key == "class" || key == "race" ||
            key == "background" || key == "alignment" || key == "house" ||
            key == "standing" || key == "surname" || key == "legacy" || key == "attacks" ||
            key == "equipment" || key == "features" || key == "proflangs" ||
            key == "personality" || key == "notes" || key == "portrait") {
            size_t n = 0; in >> n; in.ignore();  // eat the single space/newline before body
            std::string v = detail::getStr(in, n);
            if      (key == "name")        out.name = v;
            else if (key == "class")       out.className = v;
            else if (key == "race")        out.race = v;
            else if (key == "background")  out.background = v;
            else if (key == "alignment")   out.alignment = v;
            else if (key == "house")       out.house = v;
            else if (key == "standing")    out.standing = v;
            else if (key == "surname")     out.surname = v;
            else if (key == "legacy")      out.ironLegacy = v;
            else if (key == "attacks")     out.attacks = v;
            else if (key == "equipment")   out.equipment = v;
            else if (key == "features")    out.features = v;
            else if (key == "proflangs")   out.proficienciesLangs = v;
            else if (key == "personality") out.personality = v;
            else if (key == "notes")       out.notes = v;
            else if (key == "portrait")    out.portraitPath = v;
        } else if (key == "level")   { in >> out.level; }
        else if (key == "xp")        { in >> out.xp; }
        else if (key == "abilities") { for (int a = 0; a < ABILITY_COUNT; ++a) in >> out.abilities[a]; }
        else if (key == "saveprof")  { for (int a = 0; a < ABILITY_COUNT; ++a) { int b; in >> b; out.saveProf[a] = b != 0; } }
        else if (key == "skillprof") { for (int i = 0; i < 18; ++i) { int b; in >> b; out.skillProf[i] = b != 0; } }
        else if (key == "skillexpert"){ for (int i = 0; i < 18; ++i) { int b; in >> b; out.skillExpert[i] = b != 0; } }
        else if (key == "combat")    {
            in >> out.armorClass >> out.speed >> out.maxHP >> out.curHP >> out.tempHP
               >> out.hitDiceTotal >> out.hitDieSize >> out.deathSuccess >> out.deathFail;
        }
        else if (key == "coins")     { in >> out.platinum >> out.gold >> out.silver >> out.copper; }
        else if (key == "startwealth") { int t = 0; in >> t >> out.startingWealthGp; out.startingWealthTaken = t != 0; }
        else if (key == "items") {
            size_t n = 0; in >> n; in.ignore();
            out.items.clear();
            for (size_t i = 0; i < n; ++i) {
                Item it; std::string k; size_t len = 0; int eq = 0;
                in >> k >> len; in.ignore(); it.name = detail::getStr(in, len);      // iname
                in >> k >> it.qty >> k >> it.weight >> k >> eq; it.equipped = eq != 0; // iqty/iweight/iequip
                in >> k >> len; in.ignore(); it.slot = detail::getStr(in, len);      // islot
                in >> k >> len; in.ignore(); it.imagePath = detail::getStr(in, len); // iimage
                out.items.push_back(std::move(it));
            }
        }
        else {
            std::string discard; std::getline(in, discard);  // skip unknown line
        }
    }
    return true;
}

}  // namespace rpgc
