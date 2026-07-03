// family.hpp — procedural family trees for Aldermarch characters.
//
// Every character is generated a small three-tier family (the Seat / parents /
// your generation) seeded by their assigned House, standing, and background.
// The tree fixes the character's PLACE in the feudal order — their succession
// rank — which is the engine of their personal story. It also carries an
// "Iron Legacy": a heritable personal trait, distinct from the House Gift.
//
// Data + generator only (like houses.hpp). The RNG is passed in so the app owns
// the seed. "Cast lots again" simply re-generates.
#pragma once

#include "houses.hpp"

#include <random>
#include <string>
#include <vector>

namespace rpgw {

struct Kin {
    std::string name;         // "Lord Aldric Cawood"
    std::string relation;     // "Father", "Mother", "Elder brother", "Betrothed", ...
    std::string status;       // "living" | "dead" | "estranged" | "missing"
    std::string trait;        // one-line
    std::string disposition;  // toward you: "loving","dutiful","cold","rival","scheming","wary"
    bool elder = false;       // (siblings) older than you
    bool male = true;         // sons take precedence in the succession
    bool heir = false;        // stands to inherit (the presumptive "first name")
    bool holdsSeat = false;   // currently holds the house seat
};

struct Family {
    std::string houseSurname;   // "Cawood"
    std::string pcSurname;      // "Cawood", or a bastard name like "Frost"
    bool bastard = false;
    Kin father, mother;
    std::vector<Kin> siblings;  // 0-3, mixed elder/younger
    bool hasBetrothed = false;
    Kin betrothed;
    std::string seatHolder;     // who holds the seat (name)
    std::string seatRelation;   // "your father", "you", "your elder sister", ...
    int successionRank = 0;     // 1 = heir; 2 = spare; ...; 0 = outside the line (bastard)
    std::string successionLine; // the one-sentence motivation
    // Iron Legacy (heritable personal trait)
    std::string legacyName, legacyDesc, legacyEffect;
};

// ── pools ─────────────────────────────────────────────────────────────────
namespace fdetail {
    inline int roll(std::mt19937& r, int lo, int hi) { std::uniform_int_distribution<int> d(lo, hi); return d(r); }
    inline bool chance(std::mt19937& r, int pct) { return roll(r, 1, 100) <= pct; }
    template <class T> const T& pick(const std::vector<T>& v, std::mt19937& r) { return v[roll(r, 0, (int)v.size() - 1)]; }

    inline const std::vector<std::string>& maleNames() {
        static const std::vector<std::string> v = {"Aldric","Brynn","Corwin","Darel","Edmund","Garrick",
            "Halden","Joris","Kael","Lorin","Marek","Osric","Perrin","Roderick","Stefan","Torvald",
            "Ulric","Wystan","Alaric","Bern"}; return v;
    }
    inline const std::vector<std::string>& femaleNames() {
        static const std::vector<std::string> v = {"Enna","Maeve","Brenna","Cora","Elspeth","Gwyn","Halia",
            "Isolde","Katrin","Lysa","Mirel","Nesta","Orla","Rhian","Saela","Talia","Wenna","Yseult",
            "Alis","Rowena"}; return v;
    }
    inline const std::vector<std::string>& fatherTraits() {
        static const std::vector<std::string> v = {
            "A stern man who values duty above love.","Old wounds have left him bitter.",
            "Respected, but stretched thin by debts.","Pious to the point of severity.",
            "Ambitious, and never satisfied.","A hard man softened only by the smallfolk."}; return v;
    }
    inline const std::vector<std::string>& motherTraits() {
        static const std::vector<std::string> v = {
            "Sharper than the men who underestimate her.","Devout before the Iron Temple.",
            "She schemes quietly for your future.","Kind, and mourned by all who knew her.",
            "Cold since the losses of her youth.","The true power behind the seat."}; return v;
    }
    inline const std::vector<std::string>& sibTraits() {
        static const std::vector<std::string> v = {
            "quick with a blade and quicker to anger","gentle, and out of place among nobles",
            "clever, and always calculating","the favorite of your parents",
            "reckless, and adored by the smallfolk","pious, and bound for the Temple",
            "sickly, but iron-willed"}; return v;
    }
    // (name, flavor, planned effect)
    struct Legacy { const char* name; const char* desc; const char* effect; };
    inline const std::vector<Legacy>& legacies() {
        static const std::vector<Legacy> v = {
            {"Grey Sight","An ancestor bargained with the grey priests for sight beyond sight.","Planned: advantage on checks to see through illusions and disguises."},
            {"Oathborn","Your blood remembers every vow sworn before the Iron Temple.","Planned: once per rest, reroll a save to keep a promise or resist compulsion."},
            {"Forge-Marked","Born beneath a forge-shrine; heat has never troubled your line.","Planned: resistance to fire."},
            {"The Long Memory","Your family forgets no slight and no old debt.","Planned: advantage on History about the realm's houses and grievances."},
            {"Iron Constitution","Poisoners have tried your line for generations, and failed.","Planned: advantage on saves against poison and disease."},
            {"The Warden's Eye","Frontier blood; nothing crosses your land unseen.","Planned: advantage on Perception outdoors; you are not surprised in the wild."},
            {"Cold Blood","The deep north runs in your veins.","Planned: resistance to cold; advantage on saves against fear."},
            {"Silver of the Line","A honeyed tongue is your family's oldest inheritance.","Planned: once per day, gain advantage on a Persuasion check."},
        };
        return v;
    }

    // Regional bastard surnames (original to Aldermarch).
    inline std::string bastardSurname(const std::string& region) {
        if (region.find("Crownlands") != std::string::npos) return "Ash";
        if (region.find("Highmarch")  != std::string::npos) return "Crag";
        if (region.find("Sunmarch")   != std::string::npos) return "Field";
        if (region.find("Saltreach")  != std::string::npos) return "Salt";
        if (region.find("Frostmark")  != std::string::npos) return "Frost";
        if (region.find("Weald")      != std::string::npos) return "Wode";
        return "Stone";
    }
    inline std::string ordinal(int n) {
        switch (n) { case 1: return "eldest"; case 2: return "second-born"; case 3: return "third";
                     case 4: return "fourth"; default: return std::to_string(n) + "th"; }
    }
}  // namespace fdetail

// ── the generator ──────────────────────────────────────────────────────────
inline Family generateFamily(const House& h, const std::string& bg, bool pcFemale, std::mt19937& rng) {
    using namespace fdetail;
    Family f;
    std::string surname = std::string(h.name).substr(6);  // strip "House "
    f.houseSurname = surname;
    bool bastard = (bg == "Urchin");
    f.bastard = bastard;
    f.pcSurname = bastard ? bastardSurname(h.region) : surname;

    bool grim   = (surname == "Maldacht");
    bool fallen = (surname == "Cawood");
    bool titled = (h.rank <= GREAT);  // ROYAL(0) < GREAT(1) < LESSER(2)

    auto mkStatus = [&](int deadPct) -> std::string {
        int r = roll(rng, 1, 100);
        if (r <= deadPct) return "dead";
        if (r <= deadPct + (grim ? 16 : 7)) return chance(rng, 50) ? "estranged" : "missing";
        return "living";
    };

    // parents
    f.father.relation = "Father";
    f.father.name = (titled ? "Lord " : "") + pick(maleNames(), rng) + " " + surname;
    f.father.status = mkStatus(fallen ? 45 : grim ? 30 : 22);
    f.father.trait = fallen ? "Once a lord, now clings to a crumbling seat." : pick(fatherTraits(), rng);
    f.father.disposition = grim ? "cold" : (chance(rng, 70) ? "dutiful" : "loving");

    f.mother.relation = "Mother";
    f.mother.name = (titled ? "Lady " : "") + pick(femaleNames(), rng) + " " + surname;
    f.mother.status = mkStatus(fallen ? 35 : 20);
    f.mother.trait = pick(motherTraits(), rng);
    f.mother.disposition = chance(rng, 75) ? "loving" : "cold";

    // siblings (0-3)
    int r = roll(rng, 1, 100);
    int nsib = titled ? (r <= 15 ? 0 : r <= 45 ? 1 : r <= 75 ? 2 : 3)
                      : (r <= 30 ? 0 : r <= 65 ? 1 : r <= 90 ? 2 : 3);
    for (int i = 0; i < nsib; ++i) {
        Kin s;
        bool male = chance(rng, 50);
        bool elder = chance(rng, 50);
        s.elder = elder;
        s.male = male;
        s.name = (male ? pick(maleNames(), rng) : pick(femaleNames(), rng)) + " " + surname;
        s.relation = std::string(elder ? "Elder " : "Younger ") + (male ? "brother" : "sister");
        s.status = mkStatus(grim ? 22 : 12);
        s.trait = pick(sibTraits(), rng);
        // an elder sibling ahead of you in the line more often resents or rivals you
        if (grim) s.disposition = chance(rng, 55) ? "scheming" : "rival";
        else      s.disposition = chance(rng, 45) ? "rival" : (chance(rng, 60) ? "loyal" : "cold");
        f.siblings.push_back(s);
    }

    // ── succession: male-preference primogeniture ──
    // Sons take precedence over daughters regardless of birth order. A woman
    // rarely bears the "first name" of a house - only if no son of the line
    // remains, and then by vote or as regent.
    Kin* eldestSon = nullptr;
    for (auto& s : f.siblings) if (s.male && s.elder && s.status == "living") { eldestSon = &s; break; }
    if (!eldestSon) for (auto& s : f.siblings) if (s.male && !s.elder && s.status == "living") { eldestSon = &s; break; }
    int livingElderBrothers = 0;
    for (auto& s : f.siblings) if (s.male && s.elder && s.status == "living") ++livingElderBrothers;

    const std::string houseName = std::string(h.name);
    bool youAreFirstName = false;
    if (bastard) {
        f.successionRank = 0;
        f.successionLine = "Baseborn and unlanded, you carry your father's blood but no claim to " +
                           houseName + " - a name to make, or to prove.";
    } else if (!pcFemale) {
        f.successionRank = livingElderBrothers + 1;
        youAreFirstName = (livingElderBrothers == 0);
        f.successionLine = youAreFirstName
            ? "As eldest son, you are heir to " + houseName + " of " + h.seat +
              " - the seat will be yours, if you live to hold it."
            : "A younger son of " + houseName + " - your brothers stand between you and the seat; "
              "you must make your own name.";
    } else if (eldestSon) {
        f.successionRank = 0;
        f.successionLine = "A daughter of " + houseName + ". By custom the first name passes to your brother " +
                           eldestSon->name + " - though a woman may yet be wed to a seat, voted to one, "
                           "or take it by other means.";
    } else {
        f.successionRank = 1;   // no son of the line remains
        youAreFirstName = true;
        f.successionLine = "No son remains to " + houseName + " of " + h.seat + ". A woman seldom bears the "
                           "first name - yet you may be voted to the seat, or hold it in regency.";
    }
    // the presumptive first name, if it is a sibling
    if (!bastard && !youAreFirstName && eldestSon) eldestSon->heir = true;

    // ── who currently holds the seat ──
    if (f.father.status == "living") {
        f.father.holdsSeat = true; f.seatHolder = f.father.name; f.seatRelation = "your father";
    } else if (youAreFirstName) {
        f.seatHolder = "you"; f.seatRelation = pcFemale ? "you, holding in regency" : "you";
    } else if (eldestSon) {
        eldestSon->holdsSeat = true; f.seatHolder = eldestSon->name; f.seatRelation = "your " + eldestSon->relation;
    } else if (f.mother.status == "living") {
        f.mother.holdsSeat = true; f.seatHolder = f.mother.name; f.seatRelation = "your mother, holding in regency";
    } else {
        f.seatHolder = "you"; f.seatRelation = pcFemale ? "you, holding in regency" : "you";
        if (!bastard) f.successionRank = 1;
    }

    // betrothal (a tie to another house)
    int betPct = bastard ? 5 : (titled ? 60 : 30);
    if (chance(rng, betPct)) {
        // choose a different house
        const auto& all = houses();
        int oi = roll(rng, 0, (int)all.size() - 1);
        for (int guard = 0; guard < 8 && all[oi].name == h.name; ++guard) oi = roll(rng, 0, (int)all.size() - 1);
        const House& oh = all[oi];
        bool male = chance(rng, 50);
        std::string osur = std::string(oh.name).substr(6);
        f.betrothed.relation = "Betrothed";
        f.betrothed.name = (male ? "Ser " : "Lady ") + (male ? pick(maleNames(), rng) : pick(femaleNames(), rng)) + " " + osur;
        f.betrothed.status = "living";
        f.betrothed.trait = "Of " + std::string(oh.name) + " - " +
                            (chance(rng, 50) ? "a match you did not choose." : "you have never met.");
        f.betrothed.disposition = "wary";
        f.hasBetrothed = true;
    }

    // Iron Legacy
    const auto& lg = pick(legacies(), rng);
    f.legacyName = lg.name; f.legacyDesc = lg.desc; f.legacyEffect = lg.effect;
    return f;
}

}  // namespace rpgw
