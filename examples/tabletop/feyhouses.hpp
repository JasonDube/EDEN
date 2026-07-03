// feyhouses.hpp — Evermere, the fey Bright Court that welcomes gnomes and halflings.
//
// A realm not of this world: a plane of eternal dawn, talking beasts, and unicorn-
// knights, ruled (in all but name) by the Dawn-Lion Aurelior, and forever holding
// back the Long Dusk. The great houses are courts of wondrous creatures; the small
// folk - halflings and gnomes - are the beloved of the Bright Court and dwell in
// its lesser houses. Gnomes and halflings in the mortal world came by a fairy-road.
#pragma once

#include "houses.hpp"   // for rpgw::RGB

#include <random>
#include <string>
#include <vector>

namespace rpgw {

inline bool isFey(const std::string& race) {
    return race.find("Gnome") != std::string::npos || race.find("Halfling") != std::string::npos;
}

enum FeyCourt { GREAT_FEY, SMALL_FEY };
enum FeyKin   { KIN_BOTH, KIN_HALFLING, KIN_GNOME };

struct FeyHouse {
    const char* name;
    const char* words;
    const char* seat;        // "" for Wanderloom (the fairy-road wanderers)
    FeyCourt    court;
    FeyKin      kindred;     // which small folk it welcomes
    const char* art;         // the court's nature
    const char* gift;        // mechanical seed
    const char* sigil;       // lion/unicorn/leaf/wing/harp/mushroom/cog/star
    RGB         color;
    const char* stance;      // against the Long Dusk
    const char* rep;
    bool royal;
};

struct FeyRealm {
    const char* name;
    const char* sovereign;
    const char* dawn;
    const char* dusk;
    const char* tension;
};
inline const FeyRealm& evermere() {
    static const FeyRealm r = {
        "Evermere, the Bright Court",
        "the Dawn-Lion Aurelior, who wears no crown yet is the heart of all Evermere",
        "The Dawn: the living light of the realm - warmth, growth, song, and mercy. To stand in the "
        "Dawn is to be brave, kind, and true.",
        "The Long Dusk: a creeping shadow that would still every song and bring an endless winter. "
        "Every rider and every hearth of the Bright Court stands against it.",
        "The Silverhorn would ride out and meet the Dusk head-on; Greenmantle counsels patience and "
        "deep roots; and the small folk keep the hearths warm so the realm has something to fight for.",
    };
    return r;
}

// ── the houses ─────────────────────────────────────────────────────────────
inline const std::vector<FeyHouse>& feyHouses() {
    static const std::vector<FeyHouse> v = {
        // The Dawn Throne
        {"The Dawncourt", "Toward the Dawn", "the Sunhall of Aureline", GREAT_FEY, KIN_BOTH,
         "The Dawn-Lion & the Light",
         "The court of Aurelior himself: radiance made law and mercy made might - the heart from which all Evermere takes its courage.",
         "lion", {192, 124, 42}, "Keep the light, and shelter every creature that turns toward it.",
         "No throne, no crown - only a great golden Lion, wise and terrible and kind.", true},

        // The Great Houses (the noble creatures)
        {"House Silverhorn", "By Horn and Light", "the Shining Meadow", GREAT_FEY, KIN_BOTH,
         "The Unicorn-Knights",
         "The shining cavalry of Evermere: riders upon unicorns, valor and purity given a lance, first into the Dusk and last to fall back.",
         "unicorn", {43, 76, 140}, "Ride against the Dusk wherever it creeps.",
         "Gleaming and gallant; a Silverhorn charge has turned back the shadow a hundred times.", false},
        {"House Greenmantle", "The Wood Abides", "the Wildwood Deep", GREAT_FEY, KIN_BOTH,
         "Dryads & the Living Wood",
         "Dryads, treants, and wardens of the greenwood: growth, healing, and the deep patient strength of root and bough.",
         "leaf", {46, 93, 58}, "Nurture the green, and let no frost take the wildwood.",
         "Ancient and gentle; where Greenmantle walks, the flowers do not fear the winter.", false},
        {"House Skyborne", "On High We Watch", "the Cloudspire", GREAT_FEY, KIN_BOTH,
         "Griffons & the Winged",
         "Griffons, pegasi, and the winged fey: the sky-wardens of Evermere, swift messengers and swifter talons.",
         "wing", {111, 163, 196}, "Guard the skies and carry word between the far greens.",
         "Proud and far-seeing; nothing crosses Evermere's borders unseen from on high.", false},

        // The Houses of the Small Folk
        {"House Merrivale", "A Song for Every Sorrow", "the Piping Green", SMALL_FEY, KIN_HALFLING,
         "Fauns, Feast & Revelry",
         "Fauns, satyrs, and merry-makers: the music, feasting, and good cheer that keep a besieged realm laughing.",
         "harp", {176, 86, 122}, "Keep the hearth warm and the pipes playing; joy is its own defiance of the Dusk.",
         "Warm and welcoming; a halfling is never more at home than at a Merrivale feast.", false},
        {"House Thistledown", "Small Roots, Deep", "the Burrow-Shires", SMALL_FEY, KIN_HALFLING,
         "The Burrow-Folk & the Harvest",
         "The halfling homesteads of Evermere: hearth, harvest, and the quiet courage of small hands doing what must be done.",
         "mushroom", {122, 106, 58}, "Tend the shires; the realm marches on full bellies and stout hearts.",
         "Homely and steadfast; the last to boast and the last to break.", false},
        {"House Cogwhistle", "A Wonder for Every Woe", "the Glimmerworks", SMALL_FEY, KIN_GNOME,
         "Tinker-Fey & Glamour",
         "The gnome-fey and their wonders: clockwork marvels, glamours, and lantern-light to hold back the dark - invention as a kind of magic.",
         "cog", {156, 118, 32}, "Build the lanterns and marvels that keep the Dusk at bay.",
         "Clever and irrepressible; a Cogwhistle gnome can make a miracle of a spring and a wish.", false},
        {"House Wanderloom", "Over the Hills and Far Away", "", SMALL_FEY, KIN_BOTH,
         "Wayfarers of the Fairy-Roads",
         "The small folk who slip through the fairy-rings into the mortal world and back: worldliness, luck, and an uncanny knack for the road.",
         "star", {200, 153, 47}, "Wander the roads between the worlds, and carry Evermere's light abroad.",
         "Seatless and lucky; most halflings and gnomes in Aldermarch came by a Wanderloom road.", false},
    };
    return v;
}

inline std::vector<int> feyHousesForRace(bool gnome, bool halfling) {
    std::vector<int> out;
    const auto& v = feyHouses();
    for (int i = 0; i < (int)v.size(); ++i) {
        if (v[i].royal) continue;
        if (v[i].kindred == KIN_BOTH ||
            (halfling && v[i].kindred == KIN_HALFLING) ||
            (gnome && v[i].kindred == KIN_GNOME))
            out.push_back(i);
    }
    return out;
}

}  // namespace rpgw
