#pragma once
// Rarity — scores how *notable* a scanned system is, RELATIVE TO ITS LOCAL BASELINE. The same
// find scores differently by location: a tech-9 empire is unremarkable in the civilized Core but
// astonishing out in the Unexplored corners; a stone-age holdout is ordinary on the frontier but
// a curiosity deep in the Core. Deviation from the local "normal" — in EITHER direction — is what
// makes Clara's sensors chime. Three axes: civilization tech, habitable worlds, exotic resources.
// A per-super-sector TRAIT shifts the baseline (and seeds the future super-sector flavor system).

#include "GalaxyGen.hpp"
#include "Geography.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>

namespace galaxy {

enum class Rarity { Ubiquitous = 0, Common, Uncommon, Rare, VeryRare, Exceptional };
inline const char* rarityName(Rarity r) {
    switch (r) {
        case Rarity::Ubiquitous: return "ubiquitous";
        case Rarity::Common:     return "common";
        case Rarity::Uncommon:   return "uncommon";
        case Rarity::Rare:       return "rare";
        case Rarity::VeryRare:   return "very rare";
        default:                 return "exceptional";
    }
}
inline Rarity rarityFromScore(int s) { return (Rarity)std::clamp(s, 0, 5); }

// ── Super-sector traits (10x10-sector "neighborhood" character) ───────────────
// Deterministic per super-sector. Most are Unremarkable; the special traits shift the local
// baseline so rarity varies WITHIN a region and Clara gets richer context to riff on.
enum class SuperTrait { Unremarkable, MineralRich, AncientRuins, Verdant, DeadZone,
                        PirateHaven, HighCivEnclave, AnomalyField };
inline const char* superTraitName(SuperTrait t) {
    switch (t) {
        case SuperTrait::MineralRich:    return "mineral-rich";
        case SuperTrait::AncientRuins:   return "thick with ancient ruins";
        case SuperTrait::Verdant:        return "unusually verdant";
        case SuperTrait::DeadZone:       return "a stellar dead zone";
        case SuperTrait::PirateHaven:    return "a pirate haven";
        case SuperTrait::HighCivEnclave: return "a high-civilization enclave";
        case SuperTrait::AnomalyField:   return "an anomaly field";
        default:                         return "unremarkable";
    }
}
inline SuperTrait superSectorTrait(int ssx, int ssy) {
    uint32_t h = sectorSeed(ssx * 7919 + 13, ssy * 104729 + 71);
    if ((h % 100u) < 55u) return SuperTrait::Unremarkable;   // ~55% plain
    switch ((h >> 8) % 7u) {
        case 0: return SuperTrait::MineralRich;
        case 1: return SuperTrait::AncientRuins;
        case 2: return SuperTrait::Verdant;
        case 3: return SuperTrait::DeadZone;
        case 4: return SuperTrait::PirateHaven;
        case 5: return SuperTrait::HighCivEnclave;
        default: return SuperTrait::AnomalyField;
    }
}

// The DOMINANT tech level for a tier — the local "normal" deviations are measured against. Tech
// 7-8 runs most of settled space; it's the common backdrop of the Core/Mid. (Tech 9-10 are NOT
// the norm anywhere — they're near-magical reality-benders that keep to themselves; see the hard
// floors in assessSystem, which make them rare regardless of tier.)
inline float expectedTech(Tier t) {
    switch (t) { case Tier::Core: return 7.5f; case Tier::Mid: return 7.0f;
                 case Tier::Rim:  return 6.5f; default: return 6.0f; }
}

inline bool isExoticResource(const std::string& r) {
    for (auto& e : rareResources()) if (r == e) return true;
    return r == "Unobtainium";
}

struct ScanRarity {
    Rarity       overall = Rarity::Common;
    std::string  headline;             // the single most notable finding, phrased factually for Clara
    std::vector<std::string> notes;    // secondary findings (also factual)
    SuperTrait   trait = SuperTrait::Unremarkable;
    Tier         tier  = Tier::Mid;
};

// Assess a scanned system at absolute sector (sx,sy).
inline ScanRarity assessSystem(const StarSystem& s, int sx, int sy) {
    ScanRarity out;
    out.tier = regionAt(sx, sy).tier;
    out.trait = superSectorTrait(sx / kSectorsPerSuper, sy / kSectorsPerSuper);

    struct Finding { Rarity r; std::string text; };
    std::vector<Finding> finds;

    // A. Civilization — contextual tech rarity (deviation from the local normal, either way).
    if (s.controlled && s.techLevel > 0) {
        float exp = expectedTech(out.tier);
        if (out.trait == SuperTrait::HighCivEnclave) exp += 1.0f;   // dense tech-7/8; a civ is expected here
        if (out.trait == SuperTrait::DeadZone)       exp -= 1.5f;
        float dev  = (float)s.techLevel - exp;
        int   score = (int)std::floor(std::fabs(dev));
        // Tech 9-10 are near-magical reality-benders — reclusive, and RARE EVERYWHERE, not clustered
        // in the core. Hard floors put them well above the ordinary deviation math.
        if (s.techLevel == 9)  score = std::max(score, 3);
        if (s.techLevel >= 10) score = std::max(score, 5);
        if (out.tier == Tier::Unexplored) score += 1;   // any civ in the corners is notable
        Rarity rr = rarityFromScore(1 + score);         // a civ is at least "common"
        std::string who = "a tech-" + std::to_string(s.techLevel) + " " + s.government + " (" + s.species + ")";
        std::string txt;
        if (s.techLevel >= 9)
            txt = who + " — a reality-shaping power; such near-transcendent civilizations are reclusive and rarely encountered anywhere";
        else if (dev >= 1.5f)
            txt = who + " — unusually advanced for " + std::string(tierName(out.tier)) + " space";
        else if (dev <= -1.5f)
            txt = who + " — a surprisingly primitive holdout amid " + std::string(tierName(out.tier)) + " space";
        else if (out.tier == Tier::Core || out.tier == Tier::Mid)
            txt = who + " — one of the dominant powers that run settled space";
        else
            txt = who;
        finds.push_back({ rr, txt });
    } else {
        if (out.tier == Tier::Core)      finds.push_back({ Rarity::Uncommon, "an uninhabited system, deep in the civilized core" });
        else if (out.tier == Tier::Mid)  finds.push_back({ Rarity::Common,   "an uninhabited system" });
        // On the Rim / Unexplored an empty system is the norm — not worth a remark.
    }

    // B. Worlds — habitable / garden (intrinsically scarce; more so in hostile tiers).
    int gardens = 0, habitable = 0;
    for (auto& p : s.planets) {
        if (p.habitability >= 70) gardens++;
        else if (p.habitability >= 50) habitable++;
    }
    bool hostile = (out.tier == Tier::Rim || out.tier == Tier::Unexplored);
    if (gardens > 0) {
        Rarity rr = hostile ? Rarity::VeryRare : Rarity::Rare;
        if (out.trait == SuperTrait::Verdant) rr = hostile ? Rarity::Rare : Rarity::Uncommon;
        finds.push_back({ rr, std::to_string(gardens) + " garden world" + (gardens > 1 ? "s" : "") + " (highly habitable)" });
    } else if (habitable > 0) {
        Rarity rr = hostile ? Rarity::Rare : Rarity::Uncommon;
        if (out.trait == SuperTrait::Verdant) rr = Rarity::Common;
        finds.push_back({ rr, std::to_string(habitable) + " marginally habitable world" + (habitable > 1 ? "s" : "") });
    }

    // C. Resources — the exotic pool.
    std::vector<std::string> exotics;
    for (auto& p : s.planets) {
        for (auto& r : p.resources) if (isExoticResource(r)) exotics.push_back(r);
        for (auto& m : p.moons) for (auto& r : m.resources) if (isExoticResource(r)) exotics.push_back(r);
    }
    if (!exotics.empty()) {
        Rarity rr = Rarity::VeryRare;
        bool onlyArtifacts = true;
        for (auto& e : exotics) if (e != "Ancient Artifacts") onlyArtifacts = false;
        if (out.trait == SuperTrait::AncientRuins && onlyArtifacts) rr = Rarity::Uncommon;  // expected here
        if (out.trait == SuperTrait::MineralRich) rr = Rarity::Rare;
        std::string list = exotics[0];
        for (size_t i = 1; i < exotics.size() && i < 3; ++i) list += ", " + exotics[i];
        finds.push_back({ rr, "exotic resources present: " + list });
    }

    if (finds.empty()) { out.overall = Rarity::Common; out.headline = "an unremarkable system"; return out; }
    size_t best = 0;
    for (size_t i = 1; i < finds.size(); ++i) if ((int)finds[i].r > (int)finds[best].r) best = i;
    out.overall  = finds[best].r;
    out.headline = finds[best].text;
    for (size_t i = 0; i < finds.size(); ++i) if (i != best) out.notes.push_back(finds[i].text);
    return out;
}

} // namespace galaxy
