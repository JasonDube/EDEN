// classfit.hpp — experiment: roll the scores, let the data pick the class.
//
// Roll the six ability scores straight down (old-school), roll the temperament,
// then rank the twelve classes by fit. ABILITY match is primary (does the class
// want the stats you rolled high?); TEMPERAMENT is the tie-breaker - crucial for
// classes that share an ability profile (Barbarian vs Fighter, Sorcerer vs
// Warlock), where personality is what actually distinguishes them.
#pragma once

#include "character.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace rpgcf {

// How well the rolled abilities suit a class: weight each ability by its rank in
// the class's priority order (primary counts most).
inline double abilityFit(const std::string& cls, const std::array<int, 6>& ab) {
    static const double w[6] = {3.0, 1.6, 0.8, 0.3, 0.1, 0.0};
    auto pri = rpgc::classAbilityPriority(cls);
    double s = 0.0;
    for (int k = 0; k < 6; ++k) s += ab[pri[k]] * w[k];
    return s;
}

// Temperament signature: which traits a class wants high, low, or centered.
inline double personalityFit(const std::string& cls, const rpgc::Character& c) {
    auto hi  = [](int v) { return double(v - 10); };            // high is fitting
    auto lo  = [](int v) { return double(10 - v); };            // low is fitting
    auto mid = [](int v) { return double(5 - std::abs(v - 10)); }; // centered is fitting
    double s = 0.0;
    if (cls == "Barbarian") s = hi(c.bravery)*1.2 + hi(c.temper)*1.0 + lo(c.diligence)*0.6 + lo(c.skepticism)*0.3;
    else if (cls == "Fighter")  s = hi(c.bravery)*1.0 + hi(c.diligence)*0.9 + mid(c.temper)*0.6 + hi(c.honor)*0.4;
    else if (cls == "Paladin")  s = hi(c.honor)*1.4 + hi(c.piety)*1.0 + hi(c.bravery)*0.7 + hi(c.compassion)*0.5;
    else if (cls == "Cleric")   s = hi(c.piety)*1.5 + hi(c.compassion)*0.8 + hi(c.honor)*0.4;
    else if (cls == "Druid")    s = hi(c.curiosity)*1.0 + lo(c.sociability)*0.6 + lo(c.greed)*0.4 + lo(c.piety)*0.3;
    else if (cls == "Monk")     s = hi(c.diligence)*1.2 + mid(c.temper)*0.8 + lo(c.carnality)*0.6 + lo(c.greed)*0.4;
    else if (cls == "Rogue")    s = lo(c.honor)*1.2 + hi(c.skepticism)*0.8 + hi(c.greed)*0.7 + hi(c.willToPower)*0.4;
    else if (cls == "Ranger")   s = hi(c.curiosity)*0.9 + lo(c.sociability)*0.7 + hi(c.bravery)*0.4;
    else if (cls == "Bard")     s = hi(c.sociability)*1.2 + hi(c.carnality)*0.8 + hi(c.curiosity)*0.5 + hi(c.willToPower)*0.3;
    else if (cls == "Sorcerer") s = hi(c.narcissism)*0.9 + hi(c.willToPower)*0.6 + hi(c.bravery)*0.4;
    else if (cls == "Warlock")  s = lo(c.honor)*1.0 + hi(c.curiosity)*0.9 + hi(c.greed)*0.5 + lo(c.piety)*0.5;
    else if (cls == "Wizard")   s = hi(c.curiosity)*1.2 + hi(c.diligence)*0.9 + lo(c.sociability)*0.5 + hi(c.skepticism)*0.4;
    return s;
}

struct ClassScore { std::string cls; double total, ability, personality; };

// Rank all classes for a rolled character. abilities are straight down in
// STR,DEX,CON,INT,WIS,CHA order; personality carries the temperament traits.
inline std::vector<ClassScore> rankClasses(const std::array<int, 6>& ab, const rpgc::Character& pers) {
    std::vector<ClassScore> out;
    for (const char* cls : rpgc::classOptions()) {
        double a = abilityFit(cls, ab);
        double p = personalityFit(cls, pers);
        out.push_back({cls, a + p, a, p});   // ability primary, personality tie-breaker
    }
    std::sort(out.begin(), out.end(), [](const ClassScore& x, const ClassScore& y) { return x.total > y.total; });
    return out;
}

inline std::string bestFitClass(const std::array<int, 6>& ab, const rpgc::Character& pers) {
    return rankClasses(ab, pers).front().cls;
}

}  // namespace rpgcf
