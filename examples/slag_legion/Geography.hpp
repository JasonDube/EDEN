#pragma once
// Geography — Slag Legion's large-scale galactic map.
//
//   Galaxy        1000 x 1000 SECTORS          (each sector = one starfield you fly)
//   Super-sector  10 x 10 sectors              -> 100 x 100 = 10,000 super-sectors
//   Region        200 x 200 sectors            -> 5 x 5     = 25 named regions
//
// Tiers radiate out from the galactic core: Core -> Mid -> Rim -> Unexplored. Star density,
// civilization chance and danger all scale with the tier (denser/safer/richer toward the
// centre, sparse/lawless/anomalous at the corners). Addressing is deterministic so the
// player always knows where they are. Adapted from spacegame's hypersector.py +
// star_generator.py (density/control gradient). Pure data/logic — no engine dependency.

#include <string>
#include <cstdint>
#include <algorithm>

namespace galaxy {

inline constexpr int kGalaxySectors    = 1000;                              // galaxy edge, in sectors
inline constexpr int kSectorsPerSuper  = 10;                               // super-sector = 10x10 sectors
inline constexpr int kSectorsPerRegion = 200;                              // region = 200x200 sectors
inline constexpr int kRegionGrid       = kGalaxySectors / kSectorsPerRegion; // 5  (5x5 = 25 regions)
inline constexpr int kSuperGrid        = kGalaxySectors / kSectorsPerSuper;  // 100 (100x100 super-sectors)

enum class Tier { Core, Mid, Rim, Unexplored };

struct RegionInfo { const char* name; Tier tier; };

// The 5x5 grid of named regions, row-major [cy][cx] with cy=0 at the top. Tiers radiate from
// the centre cell (2,2): d=0 Core, d=1 Mid, d=2 Rim — except the 4 corners, which are Unexplored.
inline const RegionInfo& regionCell(int cx, int cy) {
    static const RegionInfo kRegions[kRegionGrid][kRegionGrid] = {
        // cy = 0  (outer top)
        {{"The Dark Sectors",      Tier::Unexplored}, {"Sentinel Boundary",  Tier::Rim},
         {"Frontier Zone Sigma",   Tier::Rim},        {"Hegemony Outlands",  Tier::Rim},
         {"Void Territories",      Tier::Unexplored}},
        // cy = 1
        {{"Arcadian Periphery",    Tier::Rim},        {"Veil Nebula",        Tier::Mid},
         {"Cygnus Corridor",       Tier::Mid},        {"Aurigan Reach",      Tier::Mid},
         {"Nomad Territories",     Tier::Rim}},
        // cy = 2  (centre row)
        {{"Far Reach",             Tier::Rim},        {"Nova Meridian",      Tier::Mid},
         {"Alpha Core",            Tier::Core},       {"Meridian Exchange",  Tier::Mid},
         {"Quantum Frontier",      Tier::Rim}},
        // cy = 3
        {{"Hinterland Consortium", Tier::Rim},        {"Orion's Belt",       Tier::Mid},
         {"Hydrian Collective",    Tier::Mid},        {"Tannhauser Expanse", Tier::Mid},
         {"Helion Federation",     Tier::Rim}},
        // cy = 4  (outer bottom)
        {{"Uncharted Expanse",     Tier::Unexplored}, {"Stellar Dawn",       Tier::Rim},
         {"Epsilon Trade Route",   Tier::Rim},        {"Corsair's Passage",  Tier::Rim},
         {"Outer Rim Wilderness",  Tier::Unexplored}},
    };
    cx = std::clamp(cx, 0, kRegionGrid - 1);
    cy = std::clamp(cy, 0, kRegionGrid - 1);
    return kRegions[cy][cx];
}

// Region containing an absolute sector.
inline const RegionInfo& regionAt(int sx, int sy) {
    return regionCell(sx / kSectorsPerRegion, sy / kSectorsPerRegion);
}

// ── Tier-driven world properties ──────────────────────────────────
inline double civChance(Tier t) {   // probability a star here is a settled/controlled system
    switch (t) { case Tier::Core: return 0.90; case Tier::Mid: return 0.50;
                 case Tier::Rim:  return 0.20; default: return 0.10; }
}
inline float densityMul(Tier t) {   // star-count multiplier (core is densest)
    switch (t) { case Tier::Core: return 1.00f; case Tier::Mid: return 0.75f;
                 case Tier::Rim:  return 0.50f; default: return 0.35f; }
}
inline const char* dangerName(Tier t) {
    switch (t) { case Tier::Core: return "Low"; case Tier::Mid: return "Medium";
                 case Tier::Rim:  return "High"; default: return "Extreme"; }
}
inline const char* tierName(Tier t) {
    switch (t) { case Tier::Core: return "Core"; case Tier::Mid: return "Mid";
                 case Tier::Rim:  return "Rim";  default: return "Unexplored"; }
}

// Continuous density falloff from the galactic centre (in addition to the tier multiplier),
// mirroring star_generator.py: 1.0 at the core, ~0.3 at the corners.
inline float radialDensity(int sx, int sy) {
    float cx = kGalaxySectors * 0.5f, cy = kGalaxySectors * 0.5f;
    float dx = (sx - cx) / cx, dy = (sy - cy) / cy;
    float d = std::min(1.0f, std::sqrt(dx * dx + dy * dy));   // 0 centre .. 1 corner
    return 1.0f - 0.7f * d;
}

// ── Deterministic addressing ──────────────────────────────────────
// Seed for a sector's starfield: same sector -> same galaxy, forever (never serialize).
inline uint32_t sectorSeed(int sx, int sy) {
    uint64_t h = (uint64_t)(uint32_t)sx * 0x1F1F1F1F1F1F1F1Full
               ^ (uint64_t)(uint32_t)sy * 0x9E3779B97F4A7C15ull;
    h ^= h >> 16; h *= 0x7FEB352D9E3779B1ull; h ^= h >> 15;
    return (uint32_t)(h ? h : 0xC0FFEEu);
}

struct SectorAddr {
    int         sx, sy;       // absolute sector       [0, 999]
    int         ssx, ssy;     // super-sector          [0, 99]
    int         lx, ly;       // sector within super-sector [0, 9]
    const char* region;       // named 200x200 region
    Tier        tier;
};
inline SectorAddr addressOf(int sx, int sy) {
    sx = std::clamp(sx, 0, kGalaxySectors - 1);
    sy = std::clamp(sy, 0, kGalaxySectors - 1);
    const RegionInfo& r = regionAt(sx, sy);
    return { sx, sy, sx / kSectorsPerSuper, sy / kSectorsPerSuper,
             sx % kSectorsPerSuper, sy % kSectorsPerSuper, r.name, r.tier };
}

} // namespace galaxy
