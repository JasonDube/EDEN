#pragma once

// A LADDER. THE PLAYER CLIMBS IT; IT DOES NOT CARRY HIM.
//
// This started as a control experiment -- a bare pole beside the ship, to find
// out which part of the ship's ladder was lying. It turned out to be the premise.
//
// The ship's ladder used to GRAB you: you pressed a key, it decided which end you
// wanted, took ownership of your position, and animated you there. That one
// decision is where every bug came from. Which end do I send him to. What if the
// key is still held when he arrives. What if he arrives off-centre and there is
// no floor. How long must he wait before he may ask again. Four fixes, an edge
// detector, a cooldown and a latch, all of them answering questions that only
// exist because something else was doing the moving.
//
// Diagnosed by the person playing it: "the stairs sort of grab you and pull you
// up step by step, whereas the pole is completely dependent on the player's
// movement. You grab the player and he loses control, and I think that's probably
// the source of your bug."
//
// So the ladder owns nothing. It answers one question -- what height are this
// player's feet at -- and only while he is asking to move:
//
//   * W goes up, S goes down, held -- the same keys every game uses for a ladder.
//     There is no "which end am I at" to get wrong, because the player answers it
//     with the key. Those keys also walk, so a ladder holds the keyboard while you
//     are on it; that is the ONLY thing it takes, and it gives it back the moment
//     there is somewhere to stand.
//   * No edge detection, no cooldown, no latch, no destination. A held key cannot
//     loop something that has no other state to loop through.
//   * Stepping off is walking out of reach. There is no dismount to get wrong.
//
// The height goes back through the same channel a deck or a ramp uses, so the
// controller's own snap-to-ground does the carrying and nothing fights over where
// the player is.

#include "SceneVertex.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace tessara {

class Ladder {
public:
    // Where it stands and how tall. Height is the climbable rise above the
    // ground at its base. Cheap enough to call every frame, which the ship's
    // ladder does -- it moves when the ship does.
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
            // Joining: asking to go up from anywhere, or asking to go DOWN from
            // somewhere above the bottom -- that is how you get on at the top.
            // Standing beside it pressing nothing must never capture you.
            const bool wantsUp   = up && feet.y < ceilY - 0.02f;
            const bool wantsDown = down && feet.y > floorY + 0.20f;
            if (!wantsUp && !wantsDown) return false;
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

    // Let go, because the caller has found the player somewhere to stand. The
    // ladder never decides this itself: knowing whether there is a floor at the
    // top is the module's business, and a ladder that guesses is a ladder that
    // drops people.
    void release() { m_on = false; }

    bool atTop() const { return m_on && m_y >= m_base.y + m_height - 0.02f; }
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
