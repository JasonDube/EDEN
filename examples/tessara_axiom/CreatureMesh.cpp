#include "CreatureMesh.hpp"
#include "MeshBuild.hpp"

#include <cmath>

namespace tessara {

namespace {

// Tessara palette. The lime is the walker's colour from the LIME experiment,
// kept deliberately -- it is the thing that read well at a glance.
const glm::vec3 kShell   {0.16f, 0.19f, 0.21f};
const glm::vec3 kPanel   {0.717f, 0.878f, 0.290f};
const glm::vec3 kStrut   {0.34f, 0.38f, 0.40f};
const glm::vec3 kFootLead{1.000f, 0.941f, 0.470f};
const glm::vec3 kFootRear{1.000f, 0.588f, 0.353f};
const glm::vec3 kEye     {0.55f, 0.95f, 1.00f};

} // namespace

void buildCreatureMesh(const Ground& hf,
                       const Walker& walker,
                       std::vector<SceneVertex>& outVertices,
                       std::vector<uint32_t>& outIndices)
{
    outVertices.clear();
    outIndices.clear();

    const float s = hf.spacing();

    const glm::vec3 feet[4] = {
        walker.footWorld(hf, 0),   // front left
        walker.footWorld(hf, 1),   // front right
        walker.footWorld(hf, 2),   // back right
        walker.footWorld(hf, 3),   // back left
    };

    const glm::vec3 centre = walker.bodyCentre(hf);
    const glm::vec3 up     = walker.bodyUp(hf);
    glm::vec3 fwd          = walker.bodyForward(hf);

    // Re-orthogonalise: forward comes from the feet, which are not square to the
    // body plane mid-stretch.
    glm::vec3 right = glm::cross(fwd, up);
    if (glm::dot(right, right) < 1e-8f) right = glm::vec3(1, 0, 0);
    right = glm::normalize(right);
    fwd   = glm::normalize(glm::cross(up, right));

    // ---- chassis ----------------------------------------------------------
    appendBox(outVertices, outIndices, centre, right, up, fwd,
              glm::vec3(0.34f * s, 0.13f * s, 0.52f * s), kShell);

    // Dorsal panel, the one piece of colour, sitting proud of the shell.
    appendBox(outVertices, outIndices, centre + up * (0.15f * s), right, up, fwd,
              glm::vec3(0.24f * s, 0.045f * s, 0.38f * s), kPanel);

    // ---- prow -------------------------------------------------------------
    // Which end is leading is never in doubt. That mattered in the diagram and
    // it matters more once he is a solid object seen from behind.
    const glm::vec3 prow = centre + fwd * (0.60f * s);
    appendBox(outVertices, outIndices, prow, right, up, fwd,
              glm::vec3(0.20f * s, 0.09f * s, 0.14f * s), kShell);
    appendBox(outVertices, outIndices, prow + fwd * (0.10f * s) + up * (0.05f * s),
              right, up, fwd,
              glm::vec3(0.10f * s, 0.035f * s, 0.05f * s), kEye);

    // ---- legs -------------------------------------------------------------
    // Hips sit at the chassis corners, so a leg's slant tells you how far that
    // foot has reached -- the stretch is legible from outside.
    const glm::vec3 hipOffset[4] = {
        (fwd * 0.42f - right * 0.30f) * s,
        (fwd * 0.42f + right * 0.30f) * s,
        (-fwd * 0.42f + right * 0.30f) * s,
        (-fwd * 0.42f - right * 0.30f) * s,
    };

    for (int i = 0; i < 4; ++i) {
        const glm::vec3 hip  = centre + hipOffset[i] - up * (0.04f * s);
        const glm::vec3 foot = feet[i];

        // A knee pushed outward and up, so the leg bends rather than being a
        // spoke, and the lifted foot's arc is visible in the joint.
        const glm::vec3 outward = glm::normalize(hip - centre + up * (0.05f * s));
        const glm::vec3 knee    = (hip + foot) * 0.5f + outward * (0.16f * s) + up * (0.10f * s);

        appendStrut(outVertices, outIndices, hip,  knee, 0.045f * s, kStrut);
        appendStrut(outVertices, outIndices, knee, foot, 0.036f * s, kStrut);

        const glm::vec3 padColor = (i == 0 || i == 1) ? kFootLead : kFootRear;
        appendBox(outVertices, outIndices, foot + up * (0.03f * s), right, up, fwd,
                  glm::vec3(0.09f * s, 0.028f * s, 0.09f * s), padColor);
    }
}

} // namespace tessara
