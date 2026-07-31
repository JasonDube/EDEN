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
// Top of the grid is the BOW.

#include <string>
#include <vector>

class Shipwright {
public:
    Shipwright();

    // The window. `open` is the host's toggle (Tab in play mode).
    void render(bool& open);

    // The plan as text, one row per line, exactly the legend above.
    std::string serialize() const;
    bool deserialize(const std::string& text);

    static constexpr int kW = 48;
    static constexpr int kH = 48;

private:
    void clear();
    void paint(int x, int y, char c);

    std::vector<char> m_cells;      // kW * kH, row-major
    char m_tool = '#';
    bool m_mirrorX = true;          // ships are symmetric; paint both halves
    std::string m_status;
};
