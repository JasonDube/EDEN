#pragma once

#include "Ground.hpp"
#include "Heightfield.hpp"
#include "SceneVertex.hpp"

#include <cstdint>
#include <vector>

namespace tessara {

// A landed cargo hauler: a bay to put crates in, a bridge to fly it from, and a
// ramp at the back that opens.
//
// Everything is in the ship's own frame -- origin on the ground under its
// centre, +Z out the nose -- and placed once. The only thing that moves is the
// ramp, so that is the only thing that needs animating.
class Ship {
public:
    struct Params {
        // Sized from the creature, not by eye. He measures 4.90 from sole to
        // crown and about 1.8 across the shoulders; the first pass gave him a
        // 4.40 bay, so he had HALF A UNIT OF HEAD too much and simply could not
        // get in -- he walked to the door and timed out. A hold wants headroom
        // enough to carry something through, and width enough for two.
        float length     = 38.0f;
        float width      = 17.0f;
        float hullHeight = 8.6f;

        float deckHeight = 2.2f;    // cargo floor above the ground
        float bayWidth   = 11.0f;   // the doorway
        float bayHeight  = 6.6f;

        // The ramp is exactly as long as the doorway is tall, so shut it seals
        // the opening instead of standing up out of the hull like a fin. That
        // fixes its length, which in turn fixes how shallow it can lie: the open
        // angle is whatever reaches the ground, not a number chosen separately.
        float rampSeconds  = 2.4f;  // full open or close

        float controlRise  = 2.3f;  // the button, at about chest height on a unit

        // How far anything meant to touch the ground actually sinks into it.
        //
        // Resting a face exactly ON the terrain is the one thing guaranteed to
        // z-fight, and the landing pad being perfectly level makes it certain
        // rather than likely -- two coplanar surfaces, no depth between them to
        // resolve. Burying the contact slightly means the faces intersect rather
        // than coincide, and there is always a winner.
        float groundBite   = 0.14f;
    };

    Params params;

    // Finds the flattest ground near `near`, LEVELS it, and sets down.
    void place(Heightfield& hf, glm::vec2 near, float yawDegrees);

    float openAngleDegrees() const { return m_openAngle; }

    void update(float dt);
    void toggleRamp() { m_opening = !m_opening; }

    bool  isOpening() const { return m_opening; }
    bool  rampSettled() const { return m_ramp <= 0.0f || m_ramp >= 1.0f; }
    float rampProgress() const { return m_ramp; }   // 0 shut, 1 down

    // ---- frame -------------------------------------------------------------
    glm::vec3 origin() const { return m_origin; }       // on the ground, ship centre
    glm::vec3 forward() const;
    glm::vec3 right() const;
    static glm::vec3 up() { return glm::vec3(0, 1, 0); }

    // Where the ramp control sits, and where a unit stands to press it.
    glm::vec3 controlPosition() const;

    // The far end of the ramp, so the scene can tell how far it has swung.
    glm::vec3 rampFootPosition() const;

    // What can be stood on. Published rather than acted on: the ship does not
    // know who walks into it, and nothing that walks knows it is a ship.
    SurfacePatch deckPatch() const;
    SurfacePatch rampPatch() const;

    // Where cargo goes, and the spot on the ground you walk to before you can
    // walk UP. Published for the same reason as the patches.
    glm::vec3 bayStoragePoint() const;
    glm::vec3 rampApproachPoint() const;
    bool      isAboard(const glm::vec3& p) const;

private:
    glm::vec3 m_origin{0.0f};
    float m_yaw = 0.0f;
    float m_ramp = 0.0f;       // 0 shut, 1 down
    bool  m_opening = false;
    float m_rampLength = 4.4f; // the doorway height
    float m_openAngle  = 27.0f; // worked out at placement, from the two of them
};

void appendShipMesh(const Ship& ship,
                    std::vector<SceneVertex>& outVertices,
                    std::vector<uint32_t>& outIndices);

} // namespace tessara
