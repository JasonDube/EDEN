#pragma once

// The Shipwright -- a 2D deck-plan grid, drawn with the mouse.
//
// THE DESIGN DECISION THIS SERVES (2026-07-31): freeform 3D ship-building is
// out. The player states INTENT on a 2D grid -- walls, doors, and what each
// room IS -- and a generator raises the ship: hull shell, sockets implied by
// the room types (bridge -> helm socket, engine room -> engine sockets),
// procedural dressing. Plan as seed, not blueprint. A 2D grid has no camera,
// no modes, no reach, no drag planes: none of the malarkey that sank the 3D
// build system, because the malarkey WAS the 3D interaction.
//
// This file is the drafting table only. It draws, paints, mirrors, and
// serializes plans -- to the clipboard, to the console, to a file -- so plans
// can travel through prompts while the generator's rules are being argued
// into shape. The generator itself is the next piece, and it consumes exactly
// the text this emits.
//
// Legend (shared with the generator, keep in sync):
//   _  outside hull        .  floor / corridor
//   #  wall                D  door
//   B  bridge              C  cargo hold
//   E  engine room         R  robot station
//   W  window (translucent wall -- glass to fly by)
// Top of the grid is the BOW.

#include <functional>
#include <string>
#include <vector>

class Shipwright {
public:
    Shipwright();

    // The window. `open` is the host's toggle (Tab in play mode).
    void render(bool& open);

    // The host raises a ship from plan text: writes the plan, runs the
    // generator, queues the level load. Returns the level path, or empty on
    // failure. The Shipwright stays a drafting table; shipbuilding is the
    // yard's business.
    void setBuildShipHook(std::function<std::string(const std::string&, int)> h) { m_buildShip = std::move(h); }

    // The yard's chandlery: opens the parts catalog beside the drafting table,
    // because Tab no longer passes through the old build panel that held it.
    void setCatalogHook(std::function<void()> h) { m_openCatalog = std::move(h); }

    // The yard's SURVEY: Finalize hands the plan to the generator's
    // segmentation (survey mode -- nothing is built) and gets back which cell
    // belongs to which named room, so the designer sees the colour-coded
    // verdict -- the very plate names the ship will carry -- before any
    // credits change hands. Any edit voids the survey.
    struct RoomPatch {
        std::string name;
        std::vector<std::pair<int, int>> cells;
    };
    void setFinalizeHook(std::function<bool(const std::string&, std::vector<RoomPatch>&)> h) {
        m_finalize = std::move(h);
    }

    // The plan as text, one row per line, exactly the legend above.
    std::string serialize() const;
    bool deserialize(const std::string& text);

    static constexpr int kW = 48;
    static constexpr int kH = 48;

private:
    void clear();
    void paint(int x, int y, char c);

    std::function<std::string(const std::string&, int)> m_buildShip;
    std::function<bool(const std::string&, std::vector<RoomPatch>&)> m_finalize;
    std::vector<RoomPatch> m_overlay;   // the survey's verdict, if current
    std::vector<int>       m_overlayIdx; // per-cell room index, -1 = none
    int m_material = 1;             // hull material tier, 1..6
    std::function<void()> m_openCatalog;
    std::vector<char> m_cells;      // kW * kH, row-major
    char m_tool = '#';
    bool m_sideView = false;        // the profile window toggle
    std::vector<float> m_loft;      // wall-top height per station (plan row)
    bool m_mirrorX = true;          // ships are symmetric; paint both halves
    std::string m_status;
};
