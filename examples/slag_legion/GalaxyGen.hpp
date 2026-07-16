#pragma once
// GalaxyGen — native C++ port of the spacegame galaxy model (star_generator.py +
// planet_classifier.py + resource_data.py), using the reconciled canonical resource
// vocabulary. Pure data/logic (glm + std only) — no engine/render dependency. FlightMode
// generates a starfield with this and renders the stars; a data panel reads the systems.
//
// Hierarchy: field -> stars -> planets -> moons, with civilizations by region.
// Deterministic: same seed -> same galaxy (uses its own PRNG, not std::rand).

#include <glm/glm.hpp>
#include "SpeciesCatalog.hpp"   // 130 species (13 govs x tech 1-10), generated from docs/EDEN_Reference.md
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cctype>

namespace galaxy {

// ── Deterministic PRNG (xorshift64*) ─────────────────────────────
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
    double  d()                 { return (next() >> 11) * (1.0 / 9007199254740992.0); }   // [0,1)
    float   f(float lo, float hi){ return lo + (float)d() * (hi - lo); }
    int     range(int lo, int hi){ return lo + (int)(d() * (double)(hi - lo + 1)); }      // [lo,hi]
    bool    chance(double p)    { return d() < p; }
    template <class T> const T& pick(const std::vector<T>& v) { return v[(size_t)range(0, (int)v.size() - 1)]; }
};

// ── Data model ───────────────────────────────────────────────────
struct Moon   { std::string type; std::vector<std::string> resources; };
struct Planet { std::string name; std::string type;
                int  habitability = 0;      // 0-100, suitability for human life
                bool inhabited = false;      // settled by the system's governing species
                std::vector<std::string> resources; std::vector<Moon> moons; };
struct StarSystem {
    glm::vec3   pos{0};
    float       size = 10.0f;
    int         typeIndex = 0;
    std::string typeName;
    glm::vec3   color{1};
    std::string name;
    // Civilization (only controlled stars). Tech 6-10 == the 5 interstellar tiers; 1-5 are
    // pre-spaceflight and never appear on the map.
    bool        controlled = false;
    std::string species;        // the inhabiting race (e.g. "Kalgon Imperium")
    std::string speciesDesc;    // its physiology / appearance
    std::string culture;        // its society / values
    std::string homeworld;      // its homeworld description
    std::string government;
    int         techLevel = 0;
    std::string techName;
    long long   population = 0;
    std::vector<Planet> planets;
};

// ── Static tables (the reconciled model) ─────────────────────────
struct StarType { const char* name; float minSize, maxSize; glm::vec3 color; int weight; };
inline const std::vector<StarType>& starTypes() {
    // Saturated, distinct colours: pastel hues wash to white under the flight lighting, so
    // each type keeps at least one low channel to survive the multiply and read as itself.
    // Distinct, spread hues so the types are visually separable (not a warm-orange cluster).
    static const std::vector<StarType> t = {
        {"Red Giant",    25, 35, {1.00f, 0.12f, 0.08f}, 2},    // red
        {"Red Dwarf",     8, 12, {1.00f, 0.48f, 0.14f}, 15},   // orange
        {"White Dwarf",   7, 10, {0.85f, 0.92f, 1.00f}, 10},   // white
        {"Neutron Star",  5,  8, {0.12f, 0.68f, 1.00f}, 2},    // cyan (saturated so it survives the multiply)
        {"Super Giant",  35, 45, {0.28f, 0.52f, 1.00f}, 1},    // blue (saturated)
        {"Brown Dwarf",  10, 15, {0.60f, 0.38f, 0.18f}, 8},    // brown
        {"Yellow Dwarf", 12, 18, {1.00f, 0.90f, 0.12f}, 10},   // yellow
    };
    return t;
}

struct PlanetType { const char* name; int weight; std::vector<std::string> resources; };
inline const std::vector<PlanetType>& planetTypes() {
    static const std::vector<PlanetType> t = {
        {"Rocky Foundational World",     10, {"Iron", "Silicon", "Base Metals", "Aluminum"}},
        {"Arid Desert Planet",            8, {"Silicon", "Mineral Deposits", "Rare Crystals", "Quartz"}},
        {"Temperate Terrestrial World",   6, {"Water", "Carbon", "Silicon"}},
        {"Deep Oceanic Planet",           5, {"Water", "Salt Compounds", "Marine Biomass"}},
        {"Aquatic Transitional World",    5, {"Water", "Nitrogen", "Mineral Deposits"}},
        {"Gas Dwarf",                     5, {"Hydrogen", "Methane", "Helium-3"}},
        {"Crystalline Ice World",         5, {"Water Ice", "Methane", "Frozen Compounds"}},
        {"Gas Giant with Water Signature",4, {"Hydrogen", "Water", "Helium"}},
        {"Iron World",                    4, {"Iron", "Nickel", "Base Metals", "Gold"}},
        {"Super-Earth",                   4, {"Iron", "Silicon", "Base Metals", "Titanium"}},
        {"Extreme Volcanic World",        4, {"Iron", "Platinum", "Silver", "Geothermal Energy"}},
        {"Toxic Greenhouse Planet",       4, {"Sulfur", "Carbon Dioxide", "Uranium"}},
        {"Lush Jungle World",             3, {"Organic Matter", "Rare Flora", "Oxygen"}},
        {"Ammonia World",                 3, {"Ammonia", "Methane"}},
        {"Unstable Ringed Planet",        3, {"Raw Gases", "Metallic Compounds"}},
        {"Ringed Oceanic World",          2, {"Water", "Organic Matter", "Raw Gases"}},
        {"Carbon World",                  2, {"Carbon", "Diamond"}},
        {"Chthonian Planet",              2, {"Iron", "Platinum", "Raw Gases", "Unobtainium"}},
    };
    return t;
}

// Moon types by parent-planet zone (hot/habitable/cold), from the reconciled classify_moon().
struct MoonType { const char* name; std::vector<std::string> resources; };
inline const std::vector<MoonType>& moonTypes(int zone) {   // 0 hot, 1 habitable, 2 cold
    static const std::vector<MoonType> hot = {
        {"Volcanic Moon",     {"Sulfur", "Iron", "Platinum"}},
        {"Scorched Moon",     {"Iron", "Platinum", "Silicon"}},
        {"Barren Rocky Moon", {"Iron", "Silicon", "Aluminum"}},
    };
    static const std::vector<MoonType> hab = {
        {"Water Moon",       {"Water", "Hydrogen", "Oxygen"}},
        {"Volcanic Moon",    {"Sulfur", "Iron", "Platinum"}},
        {"Habitable Moon",   {"Iron", "Silicon", "Water", "Carbon"}},
        {"Terrestrial Moon", {"Iron", "Silicon", "Aluminum"}},
        {"Rocky Moon",       {"Iron", "Nickel"}},
    };
    static const std::vector<MoonType> cold = {
        {"Icy Moon",             {"Water Ice", "Methane", "Ammonia"}},
        {"Frozen Moon",          {"Water Ice", "Rare Crystals"}},
        {"Icy Terrestrial Moon", {"Water Ice", "Nitrogen", "Methane"}},
        {"Captured Asteroid",    {"Nickel", "Iron", "Gold"}},
    };
    return zone == 0 ? hot : (zone == 1 ? hab : cold);
}

inline const std::vector<std::string>& rareResources() {
    static const std::vector<std::string> r = {"Dark Matter", "Quantum Crystal", "Exotic Matter", "Ancient Artifacts"};
    return r;
}
inline const std::vector<std::string>& governments() {
    static const std::vector<std::string> g = {
        "Democracy", "Federation", "Republic", "Empire", "Monarchy", "Corporatocracy",
        "Technocracy", "Military", "Theocracy", "Anarchist", "Oligarchy", "Dictatorship", "Hivemind"};
    return g;
}
// Map a species' free-text homeworld description to a canonical planet type (keyword match),
// so generation can favour species whose world matches a planet actually in the system.
// Returns "" for artificial/exotic homes (stations, matrices) that have no natural biome.
inline std::string homeworldToPlanetType(const std::string& hw) {
    std::string s; s.reserve(hw.size());
    for (char c : hw) s += (char)std::tolower((unsigned char)c);
    auto has = [&](const char* k) { return s.find(k) != std::string::npos; };
    if (has("ocean") || has("aquatic") || has("marine") || has("archipel") || has("coastal") || has("water world") || has("reef")) return "Deep Oceanic Planet";
    if (has("desert") || has("arid") || has("dune") || has("sand"))                 return "Arid Desert Planet";
    if (has("volcan") || has("lava") || has("magma"))                               return "Extreme Volcanic World";
    if (has("jungle") || has("rainforest") || has("forest") || has("jungle"))       return "Lush Jungle World";
    if (has("crystal") || has("frozen") || has("ice ") || has("icy") || has("glacial") || has("tundra")) return "Crystalline Ice World";
    if (has("gas giant") || has("gas dwarf") || has("gaseous") || has("cloud city") || has("floating")) return "Gas Giant with Water Signature";
    if (has("iron") || has("metallic") || has("metal-rich") || has("metal "))       return "Iron World";
    if (has("toxic") || has("acid") || has("greenhouse") || has("sulfur"))          return "Toxic Greenhouse Planet";
    if (has("carbon") || has("diamond") || has("graphite"))                         return "Carbon World";
    if (has("super-earth") || has("high-grav") || has("heavy grav") || has("dense") ) return "Super-Earth";
    if (has("ammonia"))                                                             return "Ammonia World";
    if (has("mountain") || has("rocky") || has("cave") || has("canyon") || has("cliff") || has("crag")) return "Rocky Foundational World";
    if (has("temperate") || has("terrestrial") || has("earth") || has("grassland") || has("plain") ||
        has("savanna") || has("meadow") || has("forest") || has("urban") || has("cities") || has("city")) return "Temperate Terrestrial World";
    if (has("ring"))                                                                return "Ringed Oceanic World";
    return "";
}

inline const char* techName(int level) {   // 0-10 tech ladder (see docs/EDEN_Reference.md)
    switch (level) {
        case 0:  return "Primitive";
        case 1:  return "Ancient";
        case 2:  return "Classical";
        case 3:  return "Medieval";
        case 4:  return "Industrial";
        case 5:  return "Atomic";
        case 6:  return "Early Interstellar";
        case 7:  return "Advanced Interstellar";
        case 8:  return "Energy Manipulation";
        case 9:  return "Matter Conversion";
        case 10: return "Reality Engineering";
        default: return "Unknown";
    }
}

// Population range per tech level (from EDEN_Reference.md), returned as a seeded value.
inline long long techPopulation(Rng& rng, int level) {
    long long lo, hi;
    switch (level) {
        case 1:  lo = 50;        hi = 500;          break;
        case 2:  lo = 500;       hi = 5000;         break;
        case 3:  lo = 500;       hi = 5000;         break;
        case 4:  lo = 5000;      hi = 100000;       break;
        case 5:  lo = 5000;      hi = 100000;       break;
        case 6:  lo = 100000;    hi = 10000000;     break;
        case 7:  lo = 100000;    hi = 10000000;     break;
        default: lo = 10000000;  hi = 1000000000;   break;   // 8-10
    }
    return lo + (long long)(rng.d() * (double)(hi - lo));
}

// ── Generation ───────────────────────────────────────────────────
inline std::string genStarName(Rng& rng) {
    static const std::vector<std::string> prefixes = {
        "Alpha","Beta","Gamma","Delta","Epsilon","Zeta","Eta","Theta","Iota","Kappa","Lambda","Sigma","Tau","Omega",
        "Nova","Proxima","Ultima","Stellar","Cosmic","Radiant","Distant","Ancient","Phantom","Silent","Echo",
        "Quantum","Spectral","Orbital","Nebula","Plasma","Fusion","Hyper","Ultra","Mega"};
    static const std::vector<std::string> cores = {
        "Centauri","Draconis","Orionis","Cassiopeiae","Cephei","Lyrae","Scorpii","Aquilae","Sagittarii","Virginis",
        "Hydrae","Carina","Ursae","Bootis","Pegasi","Andromedae","Tauri","Eridani","Canis","Phoenicis","Serpentis",
        "Quasar","Pulsar","Magnetar","Nexus","Core","Horizon","Meridian","Vertex","Pinnacle"};
    static const std::vector<std::string> suffixes = {
        "","Prime","Major","Minor","A","B","X","Z","One","Two","Central","Edge","Outer","Inner","Deep","Far"};
    double roll = rng.d();
    std::string name;
    if (roll < 0.3)      name = rng.pick(prefixes) + " " + rng.pick(cores) + " " + rng.pick(suffixes);
    else if (roll < 0.9) name = rng.pick(prefixes) + " " + rng.pick(cores);
    else                 name = rng.pick(cores);
    // trim any trailing space from an empty suffix
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name;
}

// Base human-habitability per planet type (0-100). >=50 comfortably habitable, 20-49 marginal
// (survivable with tech), <20 hostile.
inline int baseHabitability(const std::string& type) {
    if (type == "Lush Jungle World")            return 85;
    if (type == "Temperate Terrestrial World")  return 80;
    if (type == "Deep Oceanic Planet")          return 65;
    if (type == "Aquatic Transitional World")   return 55;
    if (type == "Super-Earth")                  return 48;
    if (type == "Ringed Oceanic World")         return 45;
    if (type == "Arid Desert Planet")           return 30;
    if (type == "Rocky Foundational World")     return 28;
    if (type == "Crystalline Ice World")        return 16;
    if (type == "Ammonia World")                return 8;
    if (type == "Extreme Volcanic World")       return 5;
    return 0;   // gas/iron/toxic/carbon/chthonian/unstable-ringed: uninhabitable
}

inline int weightedPick(Rng& rng, const std::vector<int>& weights) {
    int total = 0; for (int w : weights) total += w;
    int r = rng.range(1, total);
    for (size_t i = 0; i < weights.size(); ++i) { r -= weights[i]; if (r <= 0) return (int)i; }
    return 0;
}

inline Planet genPlanet(Rng& rng, const std::string& starName, int orbitIndex, int orbitCount, char letter) {
    const auto& types = planetTypes();
    std::vector<int> w; w.reserve(types.size());
    for (auto& t : types) w.push_back(t.weight);
    const PlanetType& pt = types[weightedPick(rng, w)];

    Planet p;
    p.type = pt.name;
    p.name = starName + " " + std::string(1, letter);
    p.resources = pt.resources;
    p.habitability = std::max(0, std::min(100, baseHabitability(pt.name) + rng.range(-10, 12)));
    if (rng.chance(0.10)) p.resources.push_back(rng.pick(rareResources()));   // rare bonus seed

    // Orbital zone: inner third hot, middle habitable, outer cold -> drives moon flavour.
    int zone = 0;
    if (orbitCount > 1) {
        float frac = (float)orbitIndex / (float)(orbitCount - 1);
        zone = frac < 0.34f ? 0 : (frac < 0.67f ? 1 : 2);
    } else zone = 1;

    int moonCount = rng.range(0, 3);
    const auto& mtypes = moonTypes(zone);
    for (int m = 0; m < moonCount; ++m) {
        const MoonType& mt = rng.pick(mtypes);
        p.moons.push_back({mt.name, mt.resources});
    }
    return p;
}

inline StarSystem genStar(Rng& rng, glm::vec3 pos, float controlProb) {
    StarSystem s;
    s.pos = pos;

    // Weighted star type.
    const auto& types = starTypes();
    std::vector<int> w; w.reserve(types.size());
    for (auto& t : types) w.push_back(t.weight);
    s.typeIndex = weightedPick(rng, w);
    const StarType& st = types[s.typeIndex];
    s.typeName = st.name;
    s.color    = st.color;
    s.size     = rng.f(st.minSize, st.maxSize);
    s.name     = genStarName(rng);

    // Planets (0-6, richer around bigger/normal stars).
    int planetCount = rng.range(0, 6);
    for (int i = 0; i < planetCount; ++i)
        s.planets.push_back(genPlanet(rng, s.name, i, planetCount, (char)('b' + i)));

    // Civilization: pick a species from the 130-entry catalog, weighted by ENVIRONMENT so its
    // homeworld biome matches a planet in the system. Each species carries its own GOVERNMENT
    // and TECH LEVEL (from its civ-id), so both correlate automatically. Higher tech is a little
    // rarer than low tech, but pre-spaceflight worlds (tech 1-5) do appear as scannable finds.
    if (rng.chance(controlProb)) {
        s.controlled = true;
        const auto& cat = speciesCatalog();
        std::vector<int> w; w.reserve(cat.size());
        for (const auto& sp : cat) {
            std::string biome = homeworldToPlanetType(sp.homeworld);
            bool match = false;
            if (!biome.empty())
                for (const auto& pl : s.planets) if (pl.type == biome) { match = true; break; }
            int tierBias = (sp.techLevel >= 6) ? 3 : 2;   // spacefaring civs a touch more common
            w.push_back((match ? 8 : 1) * tierBias);
        }
        const SpeciesEntry& sp = cat[weightedPick(rng, w)];
        s.species     = sp.name;
        s.speciesDesc = sp.physical;
        s.culture     = sp.culture;
        s.homeworld   = sp.homeworld;
        s.government  = sp.government;
        s.techLevel   = sp.techLevel;
        s.techName    = techName(sp.techLevel);
        s.population  = techPopulation(rng, sp.techLevel);

        // Habitation reflects reach. Pre-spaceflight (tech <=5) settles ONLY its cradle world
        // (its biome-match, else the most habitable). Spacefaring civs colonize their system's
        // habitable worlds; higher tech reaches down to marginal worlds too.
        if (sp.techLevel <= 5) {
            std::string biome = homeworldToPlanetType(sp.homeworld);
            int home = -1;
            if (!biome.empty())
                for (size_t i = 0; i < s.planets.size(); ++i)
                    if (s.planets[i].type == biome) { home = (int)i; break; }
            if (home < 0) {
                int bestH = -1;
                for (size_t i = 0; i < s.planets.size(); ++i)
                    if (s.planets[i].habitability > bestH) { bestH = s.planets[i].habitability; home = (int)i; }
            }
            if (home >= 0) s.planets[(size_t)home].inhabited = true;
        } else {
            int reach = (sp.techLevel >= 8) ? 15 : 40;   // higher tech colonizes marginal worlds
            for (auto& p : s.planets) if (p.habitability >= reach) p.inhabited = true;
        }
    }
    return s;
}

// Generate `count` stars spread through a cube of half-size `extent`, fully populated.
// `controlProb` is the chance a star hosts a civilization (region density; ~0.4 default).
// `clearRadius` keeps a clear bubble around the origin so the ship spawns in open space.
inline std::vector<StarSystem> generateField(uint64_t seed, int count, float extent,
                                             float controlProb = 0.4f, float clearRadius = 0.0f) {
    Rng rng(seed);
    std::vector<StarSystem> field;
    field.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        glm::vec3 pos;
        for (int tries = 0; tries < 8; ++tries) {   // keep the origin clear so the start view is open
            pos = glm::vec3(rng.f(-extent, extent), rng.f(-extent, extent), rng.f(-extent, extent));
            if (glm::length(pos) >= clearRadius) break;
        }
        field.push_back(genStar(rng, pos, controlProb));
    }
    return field;
}

} // namespace galaxy
