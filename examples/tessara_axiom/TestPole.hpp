#pragma once

// A LADDER WITH NOTHING ELSE ATTACHED TO IT.
//
// The ship's ladder has been fixed three times and still flips people back down,
// and every fix has been a guess about which of its parts was lying: a two-way
// toggle, a key edge, a cooldown, a latch, a platform patch, a hatch, a pull onto
// the rungs, a host that takes the max of two ground heights. Too many moving
// pieces to reason about, and none of them individually provable.
//
// So this is the control experiment. Four metres of pole standing next to the
// ship, rungs up one side, and:
//
//   * NO platform. Nothing to arrive on, nothing to step off onto.
//   * NO two-way toggle. Z goes up, X goes down. There is no "which end am I at"
//     question to get wrong, because the player answers it with the key.
//   * NO edge detection, no cooldown, no latch. Held key means moving; released
//     means stopped. A held key cannot loop something that has no other state.
//   * NO hatch, no door, no deck, no ship geometry involved at all.
//
// If climbing THIS works, the fault is in one of the parts listed above and we
// can add them back one at a time until it breaks. If climbing this ALSO flips
// you back down, then the fault is in the one thing both ladders share -- the
// way a module hands the player a height and the controller snaps to it -- and
// every fix so far has been aimed at the wrong half of the problem.

#include "SceneVertex.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace tessara {

class TestPole {
public:
    // Where it stands and how tall. Height is the climbable rise above the
    // ground at its base.
    void placeAt(const glm::vec3& base, float height = 4.0f) {
        m_base = base;
        m_height = height;
        m_placed = true;
    }

    bool placed() const { return m_placed; }
    glm::vec3 base() const { return m_base; }
    float height() const { return m_height; }
    float topY() const { return m_base.y + m_height; }

    // Close enough to have hold of the rungs. Generous, because reaching a
    // ladder you can see is not the thing being tested here.
    static constexpr float kReach = 1.8f;

    bool withinReach(const glm::vec3& feet) const {
        if (!m_placed) return false;
        const float d = glm::length(glm::vec2(feet.x - m_base.x, feet.z - m_base.z));
        return d < kReach;
    }

    // THE WHOLE OF THE CLIMB.
    //
    // Two keys, a clamp, and the height he is at. `up` and `down` are the keys as
    // they are right now -- not edges, not presses, just held or not. Returns
    // whether he is on the ladder at all; if he is, `outY` is the height his feet
    // should be at, and the host's snap-to-ground does the carrying exactly as it
    // does for a ship's deck.
    //
    // Stepping OFF is leaving: walk out of reach and m_on goes false, and he is
    // back on whatever the terrain says. There is no dismount to get wrong.
    bool update(float dt, const glm::vec3& feet, float groundY, bool up, bool down,
                float& outY) {
        if (!m_placed || !withinReach(feet)) { m_on = false; return false; }

        const float floorY = groundY;
        const float ceilY  = m_base.y + m_height;

        if (!m_on) {
            // Joining the ladder: only a request to go UP does it, from wherever
            // his feet are. Otherwise standing next to a pole would capture him.
            if (!up) return false;
            m_on = true;
            m_y = glm::clamp(feet.y, floorY, ceilY);
        }

        constexpr float kRate = 2.0f;         // metres per second, a deliberate pace
        if (up)   m_y += kRate * dt;
        if (down) m_y -= kRate * dt;
        m_y = glm::clamp(m_y, floorY, ceilY);

        // At the very bottom with nothing asking to go up, he has got off.
        if (m_y <= floorY + 0.001f && !up) { m_on = false; return false; }

        outY = m_y;
        return true;
    }

    bool on() const { return m_on; }
    float climbY() const { return m_y; }

    void buildMesh(std::vector<SceneVertex>& verts, std::vector<uint32_t>& indices) const;

private:
    glm::vec3 m_base{0.0f};
    float m_height = 4.0f;
    bool  m_placed = false;

    bool  m_on = false;
    float m_y = 0.0f;
};

} // namespace tessara
