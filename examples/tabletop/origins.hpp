// origins.hpp — where the non-human races hail from, and why they're in Aldermarch.
//
// The Aldermarch houses are for humans and half-bloods. Everyone else is an
// OUTSIDER in the realm - so instead of a House they get an Origin: a homeland,
// a people, and a reason they've come to a human feudal kingdom. These are
// placeholders; race-specific homelands (and, later, their own houses for
// dwarves, dragonborn, and elves) will be fleshed out.
#pragma once

#include "houses.hpp"

#include <random>
#include <string>
#include <vector>

namespace rpgw {

struct Origin {
    std::string homeland;   // where the people dwell
    std::string people;     // what they call themselves
    std::string reason;     // why this one is in Aldermarch
    std::string blurb;      // one composed line
};

namespace odetail {
    inline const std::vector<std::string>& reasons() {
        static const std::vector<std::string> v = {
            "driven into exile", "selling their sword for coin", "seeking fortune in a strange land",
            "a refugee from war and ruin", "walking a long pilgrimage", "chasing a debt they are owed",
            "fleeing a crime, real or invented", "an envoy far from home", "cast out by their own kind",
            "driven by restless wanderlust", "hunting someone who wronged them", "in search of lost kin",
        };
        return v;
    }
}

inline Origin originFor(const std::string& race, std::mt19937& rng) {
    Origin o;
    auto has = [&](const char* s) { return race.find(s) != std::string::npos; };
    if (has("Drow")) { o.homeland = "the sunless deeps beneath Aelvarin"; o.people = "the Drow"; }
    else if (has("Elf"))        { o.homeland = "the Verdant Reaches of Aelvarin"; o.people = "the Aelvar"; }
    else if (has("Dwarf"))      { o.homeland = "the deep holds of Kadmurn";       o.people = "the dwarrow"; }
    else if (has("Gnome"))      { o.homeland = "the tinker-warrens of Whistledeep"; o.people = "the gnomefolk"; }
    else if (has("Halfling"))   { o.homeland = "the river-shires of the Ambling Downs"; o.people = "the halflingfolk"; }
    else if (has("Tiefling"))   { o.homeland = "no realm that will claim them";   o.people = "the Marked"; }
    else if (has("Dragonborn")) { o.homeland = "the ember-clans of Vharok";        o.people = "the dragon-born"; }
    else { o.homeland = "distant lands beyond the map"; o.people = "foreigners"; }

    std::uniform_int_distribution<int> d(0, (int)odetail::reasons().size() - 1);
    o.reason = odetail::reasons()[d(rng)];
    o.blurb = "Of " + o.people + ", out of " + o.homeland + " - " + o.reason +
              ". In Aldermarch you are an outsider: no house will claim you, and the "
              "realm eyes your kind with suspicion.";
    return o;
}

}  // namespace rpgw
