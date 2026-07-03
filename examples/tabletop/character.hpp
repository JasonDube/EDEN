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

// ── Bravery / Morale (hidden stat) ─────────────────────────────────────────
// A hidden character trait rolled 4d6-drop-lowest (3-18), a callback to the old
// D&D morale check. When something frightening happens in a fight (badly wounded,
// an ally falls, hopelessly outmatched), a morale check decides whether a
// combatant holds or breaks and flees. Bravery gives the modifier.
struct BraveryTier { const char* name; const char* desc; };
inline BraveryTier braveryTier(int score) {
    if (score <= 5)  return {"Cowardly",  "Bolts at the first real danger; morale breaks almost at once."};
    if (score <= 8)  return {"Timid",     "Fearful under pressure; likely to falter when the fight turns."};
    if (score <= 11) return {"Cautious",  "Steady until things go badly, then looks hard for a way out."};
    if (score <= 14) return {"Stalwart",  "Holds the line; only breaks when the situation is truly dire."};
    if (score <= 16) return {"Brave",     "Stands firm against fear and rarely gives ground."};
    if (score <= 17) return {"Valiant",   "Nearly fearless; holds even as others around them flee."};
    return             {"Foolhardy", "Fears nothing - and may charge into danger when retreat is the wiser call."};
}
// Morale modifier reuses the ability-mod curve: -4 (score 3) .. +4 (score 18).
inline int braveryMod(int score) { return abilityMod(score); }
// The morale check itself. Roll d20 + braveryMod vs a difficulty that rises with
// how bad the situation is. Returns true if the combatant holds their nerve.
// Foolhardy never breaks (bravery 18); the Cowardly end should be rolled at
// disadvantage by the caller. Used by the encounter system.
inline bool moraleHolds(int score, int dc, int d20) {
    if (score >= 18) return true;                 // Foolhardy: holds (or charges) regardless
    return d20 + braveryMod(score) >= dc;
}

// ── Self-Regard: Inferiority <-> Narcissism (centered stat) ────────────────
// Unlike bravery, this is a CENTERED trait: the ideal is the middle. Too low is
// crippling self-doubt; too high is grandiose ego. Both extremes are meant to be
// detrimental. Rolled 3d6 (a bell curve, so "Grounded" is the common outcome).
// The real effects will drive a later relationship-dynamics system.
struct RegardTier { const char* name; const char* desc; };
inline RegardTier narcissismTier(int score) {
    if (score <= 5)  return {"Self-Loathing", "Crippling self-doubt; submits to stronger wills and undervalues all they do."};
    if (score <= 8)  return {"Meek",          "Quietly insecure; defers to others and rarely asserts their worth."};
    if (score <= 12) return {"Grounded",      "A steady, healthy sense of self - neither cowed nor conceited. The ideal balance."};
    if (score <= 15) return {"Prideful",      "Vain and quick to take offense; hungry for credit and status."};
    return             {"Narcissistic", "Grandiose and self-absorbed; craves admiration and struggles to value others."};
}
// How far off the ideal middle (0 = perfectly balanced .. larger = more extreme).
inline int regardImbalance(int score) { int d = score - 10; return d < 0 ? -d : d; }
inline const char* regardEffectNote() {
    return "Planned (relationship dynamics): the extremes strain bonds - the meek are "
           "dominated or exploited, the narcissistic alienate allies and burn goodwill. "
           "A Grounded character forms the healthiest relationships.";
}

// ── Will to Power: Compliant <-> Dominant (directional stat) ───────────────
// A dominance axis. High = dominant, ambitious, assertive, competitive. Low =
// compliant, cooperative, unpurposeful, easily bossed around. Neither pole is
// simply "good": a high score suits a would-be lord or royal; a low one suits a
// loyal follower. Rolled 3d6 (a bell curve; most fall in the moderate middle).
// Feeds the same relationship-dynamics system as Self-Regard: the strong-willed
// dominate the weak-willed.
struct WillTier { const char* name; const char* desc; };
inline WillTier willTier(int score) {
    if (score <= 5)  return {"Servile",   "Meek and indolent; easily dominated and content to be led. Others boss them at will."};
    if (score <= 8)  return {"Yielding",  "Cooperative and compliant; happy to follow and slow to press their own aims."};
    if (score <= 12) return {"Measured",  "A balanced drive - able to lead or to follow as the moment demands."};
    if (score <= 15) return {"Ambitious", "Assertive and competitive; hungry to rise and to have their way."};
    return             {"Imperious", "Commanding and relentless; born to dominate - the temper of a ruler, for good or ill."};
}
inline const char* willEffectNote() {
    return "Planned (relationship dynamics): the strong-willed dominate the weak-willed - "
           "high scores command obedience and push others aside; low scores are easily "
           "bossed around. For a would-be lord or royal a high score is an asset; for a "
           "loyal retainer, a low one keeps the peace.";
}

// ── Carnality: Chaste <-> Deviant (ideal is mid-to-low) ────────────────────
// Sexual appetite. Both extremes are trouble: the utterly chaste are thought dull
// and may leave no heirs; the deviant are sinful, disrespected, even shunned or
// punished. A temperate, respectable appetite is safest. Rolled 3d6.
struct CarnalityTier { const char* name; const char* desc; };
inline CarnalityTier carnalityTier(int score) {
    if (score <= 4)  return {"Chaste",     "Cold to such matters; respectable, but thought dull - and may sire no heirs."};
    if (score <= 8)  return {"Temperate",  "A modest, respectable appetite. Well thought of at court."};
    if (score <= 12) return {"Ardent",     "A hearty, healthy passion - and no shortage of heirs."};
    if (score <= 15) return {"Licentious", "Ruled by appetite; the quiet subject of gossip and scandal."};
    return             {"Deviant", "Perverse by the realm's lights; shunned, and in danger of the Temple's judgment."};
}
inline const char* carnalityEffectNote() {
    return "Planned: the chaste may fail to produce heirs; the deviant risk scandal, "
           "shunning, and the Temple's judgment. A temperate appetite is safest for both "
           "reputation and succession.";
}

// ── Cruelty: Masochistic <-> Sadistic (ideal is the middle) ────────────────
// Orientation toward pain. Both poles are dangerous: sadists are cruel to the
// point of madness and feared; masochists turn hurt inward and grow unstable.
// A steady middle is healthiest. Rolled 3d6.
struct CrueltyTier { const char* name; const char* desc; };
inline CrueltyTier crueltyTier(int score) {
    if (score <= 5)  return {"Masochistic",  "Takes strange comfort in their own suffering; deranged, and unsettling to others."};
    if (score <= 8)  return {"Self-Punishing","Turns hurt inward; prone to guilt and self-denial."};
    if (score <= 12) return {"Even-Tempered","Neither cruel nor self-destructive - a steady, healthy balance."};
    if (score <= 15) return {"Callous",      "A cruel streak; indifferent to others' pain, and willing to inflict it."};
    return             {"Sadistic", "Delights in others' suffering; cruel to the point of madness, and feared for it."};
}
inline const char* crueltyEffectNote() {
    return "Planned: sadists are feared and make enemies; masochists are unstable and "
           "easily broken. A steady temperament keeps both allies and sanity.";
}

// ── Sociability: Reclusive <-> Overbearing (ideal avoids the extremes) ─────
// How outgoing a character is. The painfully shy shun company and struggle to
// bond; the overbearing exhaust and alienate. A comfortable middle wears best.
// Rolled 3d6.
struct SociabilityTier { const char* name; const char* desc; };
inline SociabilityTier sociabilityTier(int score) {
    if (score <= 5)  return {"Reclusive",  "Painfully withdrawn; shuns company and struggles among people."};
    if (score <= 8)  return {"Reserved",   "Quiet and introverted; keeps their own counsel."};
    if (score <= 12) return {"Sociable",   "At ease with others without needing a crowd. A comfortable balance."};
    if (score <= 15) return {"Gregarious", "Outgoing and gregarious; thrives on company and attention."};
    return             {"Overbearing", "Exhaustingly extroverted; overwhelms others and cannot abide solitude."};
}
inline const char* sociabilityEffectNote() {
    return "Planned: recluses struggle to build bonds and alliances; the overbearing "
           "exhaust and alienate. A comfortable middle makes the best courtier.";
}

// ── Skepticism: Gullible <-> Cynical (ideal is discerning) ─────────────────
// Critical thinking vs innocent faith. The credulous swallow any lie; the cynical
// trust no one and grow bitter and isolated. A discerning mind - the wide middle,
// leaning a touch skeptical - sees through deceit without souring on the world.
// Rolled 3d6.
struct SkepticismTier { const char* name; const char* desc; };
inline SkepticismTier skepticismTier(int score) {
    if (score <= 5)  return {"Credulous",  "Believes almost anything; an easy mark, deceived and led astray."};
    if (score <= 8)  return {"Trusting",   "Takes people at their word; innocent, and sometimes naive."};
    if (score <= 13) return {"Discerning", "Weighs claims with sound, level judgment - sees through most deceit."};
    if (score <= 16) return {"Skeptical",  "Questions everything; hard to fool, but slow to trust."};
    return             {"Cynical", "Trusts no one; paranoid, bitter, and isolating."};
}
inline const char* skepticismEffectNote() {
    return "Planned: the credulous are easy marks for lies and schemes; the cynical trust "
           "no one and drive off allies. A discerning mind sees through deceit without "
           "souring on the world.";
}

// ── Honor: Treacherous <-> Oathbound (directional; the oath axis) ──────────
// The personality half of the Iron Oath system: who keeps their word and who
// breaks it. High is virtuous but can be rigid to a fault. Rolled 3d6.
struct HonorTier { const char* name; const char* desc; };
inline HonorTier honorTier(int score) {
    if (score <= 5)  return {"Treacherous","Breaks faith without a qualm; trusted by no one who knows them."};
    if (score <= 8)  return {"Pragmatic",  "Bends their word when it suits; honor is negotiable."};
    if (score <= 12) return {"Honest",     "Keeps their word in the main; a dependable sort."};
    if (score <= 15) return {"Honorable",  "Holds their oaths as sacred; widely and rightly trusted."};
    return             {"Oathbound", "Honor above all, even to a fault - cannot be made to bend, though wisdom counsel it."};
}
inline const char* honorEffectNote() {
    return "Planned: honor governs oath-keeping. The treacherous break iron oaths freely - "
           "flexible, but distrusted and cursed as oathbreakers; the oathbound keep every "
           "vow, and can be trapped by their own word.";
}

// ── Piety: Impious <-> Zealot (ideal is devout, not fanatic) ───────────────
struct PietyTier { const char* name; const char* desc; };
inline PietyTier pietyTier(int score) {
    if (score <= 5)  return {"Impious", "Scorns the Iron Temple; disfavored and quietly watched."};
    if (score <= 8)  return {"Lax",     "Indifferent to the faith; observes little."};
    if (score <= 13) return {"Faithful","Devout and observant; in good standing with the Temple."};
    if (score <= 16) return {"Devout",  "Deeply pious; counted among the Temple's favored."};
    return             {"Zealot", "Fanatical; sees heresy everywhere and burns to root it out."};
}
inline const char* pietyEffectNote() {
    return "Planned: piety governs standing with the Iron Temple and the weight your oaths carry. "
           "The impious win no Temple favor; zealots are feared and unbending.";
}

// ── Greed: Prodigal <-> Avaricious (ideal is prudent) ──────────────────────
struct GreedTier { const char* name; const char* desc; };
inline GreedTier greedTier(int score) {
    if (score <= 5)  return {"Prodigal",    "Spends and gives recklessly; keeps nothing for the morrow."};
    if (score <= 8)  return {"Open-Handed",  "Generous, sometimes to a fault."};
    if (score <= 12) return {"Prudent",     "Balances generosity and thrift with a steady hand."};
    if (score <= 15) return {"Grasping",    "Tight-fisted and acquisitive; parts with coin grudgingly."};
    return             {"Avaricious", "Consumed by greed; hoards, cheats, and never has enough."};
}
inline const char* greedEffectNote() {
    return "Planned: the prodigal squander their House's wealth; the avaricious hoard it and earn "
           "enemies. A prudent hand serves a lord best.";
}

// ── Temper: Cold <-> Wrathful (ideal is composed) ──────────────────────────
struct TemperTier { const char* name; const char* desc; };
inline TemperTier temperTier(int score) {
    if (score <= 5)  return {"Cold",         "Emotionally flat; detached, and hard to move to feeling."};
    if (score <= 8)  return {"Placid",       "Very calm; slow to feel and slower to act on passion."};
    if (score <= 12) return {"Composed",     "Feels deeply yet stays master of it - steady under strain."};
    if (score <= 15) return {"Hot-Tempered", "Quick to anger; often ruled by passion."};
    return             {"Wrathful", "Volatile and explosive; a danger to friend and foe alike."};
}
inline const char* temperEffectNote() {
    return "Planned: the wrathful lash out and make enemies; the cold feel too little to bond. "
           "A composed temper holds up best under pressure and at court.";
}

// ── Diligence: Slothful <-> Tireless (directional) ─────────────────────────
struct DiligenceTier { const char* name; const char* desc; };
inline DiligenceTier diligenceTier(int score) {
    if (score <= 5)  return {"Slothful",    "Idle and undisciplined; leaves duties undone."};
    if (score <= 8)  return {"Lax",         "Does the minimum; easily distracted."};
    if (score <= 12) return {"Steady",      "Reliable and reasonably diligent."};
    if (score <= 15) return {"Industrious", "Disciplined and hard-working; sees things through."};
    return             {"Tireless", "Relentless - driven to work to the point of never resting."};
}
inline const char* diligenceEffectNote() {
    return "Planned: the slothful let their affairs and holdings rot; the industrious build and "
           "maintain. Discipline pays across every long endeavor.";
}

// ── Compassion: Callous <-> Tender-Hearted (directional) ───────────────────
struct CompassionTier { const char* name; const char* desc; };
inline CompassionTier compassionTier(int score) {
    if (score <= 5)  return {"Callous",        "Cold to others' suffering; unmoved by pity."};
    if (score <= 8)  return {"Hard",           "Practical and unsentimental; feeling rarely stays their hand."};
    if (score <= 12) return {"Kindly",         "Warm and considerate in the main."};
    if (score <= 15) return {"Compassionate",  "Deeply empathetic; feels others' pain as their own."};
    return             {"Tender-Hearted", "So soft-hearted they are easily moved - and easily exploited."};
}
inline const char* compassionEffectNote() {
    return "Planned: the callous strike cruel bargains but form few true bonds; the tender-hearted "
           "are loved yet easily played upon. Warmth builds loyalty - to a point.";
}

// ── Curiosity: Hidebound <-> Heterodox (ideal is curious, not heretical) ───
struct CuriosityTier { const char* name; const char* desc; };
inline CuriosityTier curiosityTier(int score) {
    if (score <= 5)  return {"Hidebound",    "Clings to tradition; suspicious of anything new."};
    if (score <= 8)  return {"Conventional", "Content with the old ways and settled truths."};
    if (score <= 12) return {"Curious",      "Open to new ideas without losing their footing."};
    if (score <= 15) return {"Inventive",    "Restlessly curious; questions everything and tinkers."};
    return             {"Heterodox", "Chases novelty and forbidden knowledge - and courts heresy."};
}
inline const char* curiosityEffectNote() {
    return "Planned: the hidebound miss discovery and opportunity; the heterodox draw the Temple's "
           "suspicion and charges of heresy. A curious but grounded mind fares best.";
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
    std::string gender;         // "Male" | "Female" (feudal succession favors men)
    std::string origin;         // for non-house races: where they hail from (see origins.hpp)
    int level = 1;
    int xp    = 0;
    int bravery = 10;           // morale stat (3-18); see braveryTier()
    int narcissism = 10;        // self-regard (3-18); middle ideal; see narcissismTier()
    int willToPower = 10;       // dominance (3-18); low=compliant, high=dominant; see willTier()
    int carnality = 10;         // appetite (3-18); ideal mid-low; see carnalityTier()
    int cruelty = 10;           // sadism<->masochism (3-18); ideal middle; see crueltyTier()
    int sociability = 10;       // introvert<->extrovert (3-18); ideal middle; see sociabilityTier()
    int skepticism = 10;        // gullible<->cynical (3-18); ideal discerning; see skepticismTier()
    int honor = 10;             // treacherous<->oathbound (3-18); see honorTier()
    int piety = 10;             // impious<->zealot (3-18); ideal devout; see pietyTier()
    int greed = 10;             // prodigal<->avaricious (3-18); ideal prudent; see greedTier()
    int temper = 10;            // cold<->wrathful (3-18); ideal composed; see temperTier()
    int diligence = 10;         // slothful<->tireless (3-18); see diligenceTier()
    int compassion = 10;        // callous<->tender (3-18); see compassionTier()
    int curiosity = 10;         // hidebound<->heterodox (3-18); ideal curious; see curiosityTier()

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
    detail::putStr(o, "gender", c.gender);
    detail::putStr(o, "origin", c.origin);
    o << "level " << c.level << '\n';
    o << "xp " << c.xp << '\n';
    o << "bravery " << c.bravery << '\n';
    o << "narcissism " << c.narcissism << '\n';
    o << "willtopower " << c.willToPower << '\n';
    o << "carnality " << c.carnality << '\n';
    o << "cruelty " << c.cruelty << '\n';
    o << "sociability " << c.sociability << '\n';
    o << "skepticism " << c.skepticism << '\n';
    o << "honor " << c.honor << '\n';
    o << "piety " << c.piety << '\n';
    o << "greed " << c.greed << '\n';
    o << "temper " << c.temper << '\n';
    o << "diligence " << c.diligence << '\n';
    o << "compassion " << c.compassion << '\n';
    o << "curiosity " << c.curiosity << '\n';
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
            key == "standing" || key == "surname" || key == "legacy" ||
            key == "gender" || key == "origin" || key == "attacks" ||
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
            else if (key == "gender")      out.gender = v;
            else if (key == "origin")      out.origin = v;
            else if (key == "attacks")     out.attacks = v;
            else if (key == "equipment")   out.equipment = v;
            else if (key == "features")    out.features = v;
            else if (key == "proflangs")   out.proficienciesLangs = v;
            else if (key == "personality") out.personality = v;
            else if (key == "notes")       out.notes = v;
            else if (key == "portrait")    out.portraitPath = v;
        } else if (key == "level")   { in >> out.level; }
        else if (key == "xp")        { in >> out.xp; }
        else if (key == "bravery")   { in >> out.bravery; }
        else if (key == "narcissism"){ in >> out.narcissism; }
        else if (key == "willtopower"){ in >> out.willToPower; }
        else if (key == "carnality") { in >> out.carnality; }
        else if (key == "cruelty")   { in >> out.cruelty; }
        else if (key == "sociability"){ in >> out.sociability; }
        else if (key == "skepticism"){ in >> out.skepticism; }
        else if (key == "honor")     { in >> out.honor; }
        else if (key == "piety")     { in >> out.piety; }
        else if (key == "greed")     { in >> out.greed; }
        else if (key == "temper")    { in >> out.temper; }
        else if (key == "diligence") { in >> out.diligence; }
        else if (key == "compassion"){ in >> out.compassion; }
        else if (key == "curiosity") { in >> out.curiosity; }
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
