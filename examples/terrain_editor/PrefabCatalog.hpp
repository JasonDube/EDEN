#pragma once

// The shop. What a player can buy for their ship.
//
// BUYING PUTS THE COMPONENT IN THE HOTBAR -- the first free slot, exactly like
// an item picked up off the ground. Placing it is the game's ordinary
// right-click placement, the same flow every akelba item uses, with the same
// R-rotation afterwards. The shop does not aim, place, or know where decks
// are. It briefly did, and the aiming fought the UI: you cannot point at a
// deck and click a Buy button with one mouse. The hotbar flow the game
// already had was what was asked for in the first place.
//
// A CATALOGUE ITEM IS A FILE, NOT A LEVEL OBJECT. Every entry is a model file
// in assets/models/prefabs, and everything the shop knows about it -- title,
// price, shelf, summary -- is read out of metadata. A .lime carries `meta`
// lines in-file; a .glb cannot (it is somebody else's binary format), so its
// metadata rides in a sidecar: helm_model.glb + helm_model.meta with the same
// lines. Nothing in this code knows what a helm costs. Drop a file in the
// folder and it appears on the shelf; edit the price in a text editor and
// Rescan charges the new one.

#include <functional>
#include <string>
#include <vector>

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

struct PrefabCatalogHooks {
    std::function<float()>      credits;
    std::function<void(float)>  spend;

    // Put the file in the first free hotbar slot; returns the slot index, or
    // -1 when the hotbar is full. The host owns slot mechanics -- how geometry
    // loads, what a thumbnail is -- the shop only asks.
    std::function<int(const PrefabCatalogEntry& entry)> giveToPlayer;
};

class PrefabCatalog {
public:
    // Reads every .lime (in-file meta) and .glb (sidecar .meta) in `dir` and
    // keeps the ones that say `meta prefab: 1`. Safe to call again; it
    // rescans, so a prefab edited while the game runs shows its new price
    // without a restart.
    void load(const std::string& dir);
    void setHooks(const PrefabCatalogHooks& hooks) { m_hooks = hooks; }

    const std::vector<PrefabCatalogEntry>& entries() const { return m_entries; }
    const std::string& lastMessage() const { return m_message; }

    // The shop window. `open` is the host's toggle.
    void render(bool& open);

    // Buy one into the hotbar. Returns the slot index, or -1 with
    // lastMessage() saying why not. Exposed so a check can drive the purchase
    // without a mouse.
    int buy(const PrefabCatalogEntry& entry);

private:
    PrefabCatalogHooks m_hooks;
    std::vector<PrefabCatalogEntry> m_entries;
    std::string m_message;
    std::string m_dir;
};
