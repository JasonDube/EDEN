// relations.hpp — the core of the relationship-dynamics system.
//
// Given two characters' personalities (the 14 temperament traits in
// character.hpp), derive how they regard one another and what shape their bond
// takes. Two forces drive it:
//   - SIMILARITY: shared values (honor, faith, kindness, appetites, curiosity...)
//     build affinity; clashing values breed friction.
//   - COMPLEMENTARITY: the dominance axis (Will to Power, Self-Regard, Temper) -
//     a strong will and a compliant one lock into a stable leader/follower pair,
//     while two strong wills collide.
// On top sits the realm's prejudice: house-races (human, half-blood) regard the
// non-human "outsider" races coolly, softened by curiosity/compassion and
// hardened by piety.
//
// Opinion is ASYMMETRIC (A may adore B while B stays cold). This is design data,
// not yet wired to gameplay - a prototype to feel the model out.
#pragma once

#include "character.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace rpgr {

inline bool halfBlood(const std::string& race) { return race.find("Half-") != std::string::npos; }
inline bool fullHuman(const std::string& race) { return race == "Human"; }
inline bool houseRace(const std::string& race) { return fullHuman(race) || halfBlood(race); }

// A character's raw dominance: mostly Will to Power, lifted by ego and hot temper.
inline double dominance(const rpgc::Character& c) {
    return c.willToPower + (c.narcissism - 10) * 0.5 + (c.temper - 10) * 0.25;
}

// A's opinion of B, roughly -100..+100.
inline int opinion(const rpgc::Character& a, const rpgc::Character& b) {
    auto sim = [&](int av, int bv, double w) { return w * (5 - std::abs(av - bv)); };
    double s = 0.0;
    // shared values (similarity): close = warmth, far = friction
    s += sim(a.honor,       b.honor,       2.0);
    s += sim(a.compassion,  b.compassion,  1.3);
    s += sim(a.piety,       b.piety,       1.0);
    s += sim(a.greed,       b.greed,       1.0);
    s += sim(a.temper,      b.temper,      0.9);
    s += sim(a.curiosity,   b.curiosity,   0.8);
    s += sim(a.diligence,   b.diligence,   0.7);
    s += sim(a.sociability, b.sociability, 0.7);
    s += sim(a.carnality,   b.carnality,   0.6);
    // A's trust disposition: the gullible warm to everyone, the cynical to no one
    s += (10 - a.skepticism) * 1.2;
    // A admires virtue in B, weighted by A's own virtue (the honorable prize honor)
    s += (b.honor - 10)      * (a.honor > 10 ? 0.8 : 0.3);
    s += (b.compassion - 10) * (a.compassion > 10 ? 0.7 : 0.3);
    // the kind-hearted recoil from cruelty
    if (a.compassion >= 11) s -= std::max(0, b.cruelty - 11) * 1.2;
    // abrasive extremes wear on others
    s -= std::max(0, b.sociability - 15) * (a.sociability < 9 ? 1.2 : 0.6);
    s -= std::max(0, b.temper - 15) * 0.8;
    // the realm's prejudice: a house-race looking on outsiders (and half-bloods)
    if (houseRace(a.race)) {
        if (!houseRace(b.race)) {
            double x = -14.0;
            x -= std::max(0, a.piety - 10) * 0.8;       // the pious are less welcoming
            x += std::max(0, a.curiosity - 10) * 0.6;   // the curious, more
            x += std::max(0, a.compassion - 10) * 0.5;
            s += std::max(x, -30.0);
        } else if (fullHuman(a.race) && halfBlood(b.race)) {
            s -= 5.0;   // half-bloods sit a rung below in a full human's eyes
        }
    }
    long r = std::lround(s);
    return (int)std::max(-100L, std::min(100L, r));
}

struct Relationship {
    int opinionAB = 0, opinionBA = 0;   // A->B and B->A (asymmetric)
    int domA = 0, domB = 0;
    std::string dynamic;                // Feud / Rivalry / Dominance / Oathbond / ...
    std::string leader;                 // name of who leads (hierarchical bonds), else ""
    bool toxic = false;
    bool romancePossible = false;
    std::string note;                   // one-line human read
};

inline Relationship assess(const rpgc::Character& a, const rpgc::Character& b) {
    Relationship r;
    r.opinionAB = opinion(a, b);
    r.opinionBA = opinion(b, a);
    r.domA = (int)std::lround(dominance(a));
    r.domB = (int)std::lround(dominance(b));
    int mutual = (r.opinionAB + r.opinionBA) / 2;
    int gap = std::abs(r.domA - r.domB);
    const rpgc::Character& hi = (r.domA >= r.domB) ? a : b;   // the more dominant
    const rpgc::Character& lo = (r.domA >= r.domB) ? b : a;
    bool bothHighDom = std::min(r.domA, r.domB) >= 15;

    // A predator lacks scruples AND has the ego/cruelty to use others.
    bool predator = (hi.narcissism >= 14 || hi.cruelty >= 14) && (hi.honor < 11 || hi.compassion <= 7);
    // Prey is easily dominated: weak-willed, self-effacing, gullible, or masochistic.
    bool prey = (lo.willToPower <= 8 || lo.narcissism <= 6 || lo.skepticism <= 6 || lo.cruelty <= 6);

    if (gap >= 4 && predator && prey && mutual > -22) {
        // ensnared, not at war: the prey tolerates the predator rather than hating them
        r.dynamic = "Exploitation"; r.toxic = true; r.leader = hi.name;
        r.note = hi.name + " dominates and uses " + lo.name + ".";
    } else if (mutual <= -25) {
        r.dynamic = "Feud";
        r.note = a.name + " and " + b.name + " are set against one another.";
    } else if (bothHighDom && gap <= 3 && mutual < 30) {
        r.dynamic = "Rivalry";
        r.note = "Two strong wills; expect a contest for the lead.";
    } else if (mutual >= 35 && std::min(a.honor, b.honor) >= 12) {
        r.dynamic = "Oathbond";
        r.note = "A deep, honor-bound loyalty - the stuff of sworn oaths.";
    } else if (gap >= 5 && mutual >= 5) {
        // willing followership needs at least mild regard; resentful gaps fall through
        r.dynamic = "Dominance"; r.leader = hi.name;
        r.note = hi.name + " leads; " + lo.name + " falls in behind.";
    } else if (mutual >= 25) {
        r.dynamic = "Friendship"; r.note = "Genuine mutual regard.";
    } else if (mutual >= 8) {
        r.dynamic = "Amicable"; r.note = "Cordial enough.";
    } else if (mutual <= -8) {
        r.dynamic = "Cold"; r.note = "Wary and distant.";
    } else {
        r.dynamic = "Neutral"; r.note = "Little between them yet.";
    }

    // Romance is a later layer - it needs gender/orientation data we don't model yet.
    return r;
}

}  // namespace rpgr
