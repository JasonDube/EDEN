#pragma once

// The shop. What a player can buy and put on their ship.
//
// THE LOOP THIS SERVES. The player lays down an h-slab in play-mode build --
// that slab is the hull, and in v1 everything standing on it is the vessel.
// Then they open this, spend credits on a component, and it lands on the deck
// they are looking at. Helm first, then engine, pallet, robot station, struts,
// doors.
//
// A CATALOGUE ITEM IS A FILE, NOT A LEVEL OBJECT. Every entry here is a .lime
// in assets/models/prefabs, and everything the shop knows about it -- what it
// is called, what it costs, which shelf it sits on, what it does -- is read out
// of that file's own `meta` lines. Nothing in this code knows a helm costs
// 2500. Add a .lime to that folder and it appears in the shop; change the price
// in a text editor and the shop charges the new one. That is the whole point of
// having authored prefabs rather than hardcoded items.
//
// Only the METADATA is parsed here. The geometry is loaded by the host's own
// .lime path when something is actually bought, because that is the code that
// already knows how to turn a file into an object the editor can select, move
// and save.

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// One thing on a shelf, as its file describes itself.
struct PrefabCatalogEntry {
    std::string filePath;
    std::string title    = "(untitled)";
    std::string role;                  // helm, engine, pallet...
    std::string shelf    = "misc";     // `meta catalog:` -- which tab it sits on
    std::string summary;
    std::string mountPort;             // port whose origin sits on the deck
    std::string stationPort;           // where a body stands to use it
    float       price = 0.0f;
    float       mass  = 0.0f;
};

// A deck the player has built: an h-slab, in world space.
struct CatalogDeck {
    std::string name;
    glm::vec3   min{0.0f};
    glm::vec3   max{0.0f};
};

struct PrefabCatalogHooks {
    std::function<float()>      credits;
    std::function<void(float)>  spend;

    // Where the player is looking, and what they have built.
    std::function<void(glm::vec3& outOrigin, glm::vec3& outDirection)> aimRay;
    std::function<std::vector<CatalogDeck>()> decks;
    std::function<float()> viewYawDegrees;

    // Put the file in the world. Returns the object's name, empty on failure.
    std::function<std::string(const std::string& path,
                              const glm::vec3& position,
                              float yawDegrees)> place;
};

class PrefabCatalog {
public:
    // Reads every .lime in `dir` and keeps the ones that say `meta prefab: 1`.
    // Safe to call again; it rescans, so a prefab edited while the game runs
    // shows its new price without a restart.
    void load(const std::string& dir);
    void setHooks(const PrefabCatalogHooks& hooks) { m_hooks = hooks; }

    const std::vector<PrefabCatalogEntry>& entries() const { return m_entries; }
    const std::string& lastMessage() const { return m_message; }

    // The shop window. `open` is the host's toggle.
    void render(bool& open);

    // Buy one and put it down, without the UI. Returns the object's name, or
    // empty with lastMessage() saying why not. Exposed so a check can drive the
    // whole loop -- buy, place, confirm the credits went -- without a mouse.
    std::string buyAndPlace(const PrefabCatalogEntry& entry);

private:
    // Where the player is aiming, if that is a deck they have built. The top
    // FACE, not the centre: a helm sits on a floor, it does not sink into it.
    bool aimedDeckPoint(glm::vec3& outPoint, std::string& outDeck) const;

    PrefabCatalogHooks m_hooks;
    // The last deck the mouse actually pointed at, HELD while the mouse crosses
    // the shop window to reach the Buy button -- at click time the live aim is
    // the button itself, which is never a deck.
    bool        m_haveTarget = false;
    glm::vec3   m_targetPoint{0.0f};
    std::string m_targetDeck;
    std::vector<PrefabCatalogEntry> m_entries;
    std::string m_message;
    std::string m_dir;
};
