#include "BipedMesh.hpp"
#include "MeshBuild.hpp"

#include "Editor/GLBLoader.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace tessara {

namespace {

constexpr float kPi = 3.14159265f;

// Amber rather than the walker's lime, so at a glance you can tell which of them
// you are looking at from across the field.
const glm::vec3 kShell {0.19f, 0.20f, 0.23f};
const glm::vec3 kPanel {1.000f, 0.749f, 0.000f};
const glm::vec3 kLimb  {0.36f, 0.38f, 0.41f};
const glm::vec3 kJoint {0.52f, 0.54f, 0.58f};
const glm::vec3 kFoot  {0.26f, 0.27f, 0.30f};
const glm::vec3 kVisor    {0.30f, 0.52f, 0.62f};
const glm::vec3 kVisorLit {0.62f, 1.00f, 1.00f};

} // namespace

bool loadHeadModel(const std::string& path, HeadModel& out, std::string& error) {
    out = HeadModel{};

    eden::LoadResult result = eden::GLBLoader::load(path);
    if (!result.success || result.meshes.empty()) {
        error = result.error.empty() ? "no meshes in file" : result.error;
        return false;
    }

    // Bounds over every primitive, so a multi-part head normalises as one object
    // rather than each piece being centred on itself.
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const eden::LoadedMesh& mesh : result.meshes) {
        for (const eden::ModelVertex& v : mesh.vertices) {
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
    }
    if (lo.x > hi.x) { error = "mesh has no vertices"; return false; }

    const glm::vec3 centre = (lo + hi) * 0.5f;
    const float inv = 1.0f / std::max(1e-4f, hi.y - lo.y);

    for (const eden::LoadedMesh& mesh : result.meshes) {
        uint32_t base = static_cast<uint32_t>(out.vertices.size());

        for (eden::ModelVertex v : mesh.vertices) {
            // Normalised in place: centred on its own bounds and scaled to unit
            // height, so the creature's head size decides how big it looks and
            // swapping in another model changes nothing else.
            v.position = (v.position - centre) * inv;
            out.vertices.push_back(v);
        }
        for (uint32_t i : mesh.indices) out.indices.push_back(base + i);

        if (mesh.hasTexture && out.texture.empty()) {
            out.texture   = mesh.texture.data;
            out.texWidth  = mesh.texture.width;
            out.texHeight = mesh.texture.height;
            out.textured  = out.texWidth > 0 && out.texHeight > 0;
        }
    }

    out.loaded = !out.vertices.empty() && !out.indices.empty();
    if (!out.loaded) error = "mesh loaded but had no triangles";
    return out.loaded;
}

// Where the head sits, as a matrix -- the model goes to the GPU once and only
// this changes, so nothing re-uploads 6833 vertices every frame.
glm::mat4 HeadModel::transform(const Biped& biped) const {
    // Rodrigues rather than a matrix chain: three lines, and it keeps the axes
    // obviously orthonormal.
    auto spin = [](const glm::vec3& v, const glm::vec3& axis, float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return v * c + glm::cross(axis, v) * s + axis * glm::dot(axis, v) * (1.0f - c);
    };

    glm::vec3 r = biped.headRight(), u = biped.headUp(), f = biped.headForward();

    float yw = glm::radians(yawOffset);
    r = spin(r, u, yw);  f = spin(f, u, yw);
    float pt = glm::radians(pitchOffset);
    u = spin(u, r, pt);  f = spin(f, r, pt);
    float rl = glm::radians(rollOffset);
    r = spin(r, f, rl);  u = spin(u, f, rl);

    // 0.52 is the box head's height, so scale 1 is the size of what it replaced.
    const float size = 0.52f * scale;

    // The model is unit height and centred on itself, so half of it hangs below
    // its own origin. Offsetting by that (less the height it had at scale 1)
    // keeps the chin where `rise` puts it however big the head gets.
    const float half = 0.5f * size;
    const glm::vec3 at = biped.headCentre() + biped.headUp() * (rise + half - 0.26f);

    glm::mat4 m(1.0f);
    m[0] = glm::vec4(r * size, 0.0f);
    m[1] = glm::vec4(u * size, 0.0f);
    m[2] = glm::vec4(f * size, 0.0f);
    m[3] = glm::vec4(at, 1.0f);
    return m;
}

void appendBipedMesh(const Biped& biped,
                     const HeadModel& head,
                     std::vector<SceneVertex>& verts,
                     std::vector<uint32_t>& indices)
{
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 fwd   = biped.forward();
    const glm::vec3 right = biped.right();
    const glm::vec3 hips  = biped.hipCentre();

    // The pelvis rides the legs and stays square to the world; everything above
    // it hangs off the torso frame, which is what makes a lean move the chest,
    // shoulders, arms and head together as one piece.
    const glm::vec3 tUp    = biped.torsoUp();
    const glm::vec3 tFwd   = biped.torsoForward();
    const glm::vec3 tRight = biped.torsoRight();

    const Biped::Params& p = biped.params;

    // ---- pelvis, torso, head ---------------------------------------------
    appendBox(verts, indices, hips, right, up, fwd,
              glm::vec3(p.hipWidth * 0.52f, 0.20f, 0.26f), kShell);

    const glm::vec3 chest = biped.chestCentre();
    appendBox(verts, indices, chest, tRight, tUp, tFwd,
              glm::vec3(p.hipWidth * 0.58f, 0.52f, 0.30f), kShell);

    // The one piece of colour, on the chest, so his facing is readable.
    appendBox(verts, indices, chest + tFwd * 0.28f, tRight, tUp, tFwd,
              glm::vec3(p.hipWidth * 0.34f, 0.30f, 0.05f), kPanel);

    // The head has its own frame, so it turns without the body knowing. A short
    // neck stub between them keeps the join from looking like a floating box
    // when he is watching something well off to one side.
    const glm::vec3 headAt = biped.headCentre();
    const glm::vec3 hFwd  = biped.headForward();
    const glm::vec3 hRight = biped.headRight();
    const glm::vec3 hUp   = biped.headUp();

    appendBox(verts, indices, (chest + headAt) * 0.5f, tRight, tUp, tFwd,
              glm::vec3(0.13f, 0.20f, 0.13f), kLimb);

    // The head is only drawn here when there is no model to draw instead --
    // a loaded one goes through eden::ModelRenderer, which has the samplers.
    if (!head.loaded) {
        appendBox(verts, indices, headAt, hRight, hUp, hFwd,
                  glm::vec3(0.28f, 0.26f, 0.28f), kShell);

        // Brighter when he has actually got you, so you can tell being LOOKED at
        // from merely being faced.
        const glm::vec3 visor = biped.isWatching() ? kVisorLit : kVisor;
        appendBox(verts, indices, headAt + hFwd * 0.26f, hRight, hUp, hFwd,
                  glm::vec3(0.19f, 0.08f, 0.04f), visor);
    }

    // ---- legs -------------------------------------------------------------
    // Nothing here decides where the knee goes. The solver does, from the hip
    // and the planted foot, which is why this survives being on a slope.
    for (int i = 0; i < 2; ++i) {
        glm::vec3 knee, ankle;
        biped.solveLeg(i, knee, ankle);

        const glm::vec3 hip = biped.hipJoint(i);

        appendTaperedStrut(verts, indices, hip, knee, 0.15f, 0.11f, kLimb);
        appendBox(verts, indices, knee, right, up, fwd,
                  glm::vec3(0.13f, 0.12f, 0.13f), kJoint);
        appendTaperedStrut(verts, indices, knee, ankle, 0.11f, 0.08f, kLimb);

        // Foot, in its OWN frame -- the ankle servo has tilted it to sit flat
        // on whatever is under it. The offsets go along the foot's axes too, or
        // a tilted foot would swing its sole off the ground while its centre
        // stayed put.
        const glm::vec3 fUp    = biped.footUp(i);
        const glm::vec3 fFwd   = biped.footForward(i);
        const glm::vec3 fRight = biped.footRight(i);

        appendBox(verts, indices, ankle + fUp * 0.06f + fFwd * 0.07f,
                  fRight, fUp, fFwd,
                  glm::vec3(0.13f, 0.06f, 0.24f), kFoot);
    }

    // ---- arms -------------------------------------------------------------
    // Same two-bone chain as the legs, so the elbow is solved rather than posed
    // and the bone lengths stay honest whatever the swing is doing.
    for (int i = 0; i < 2; ++i) {
        glm::vec3 elbow, hand;
        biped.solveArm(i, elbow, hand);

        const glm::vec3 shoulder = biped.shoulderJoint(i);

        appendBox(verts, indices, shoulder, tRight, tUp, tFwd,
                  glm::vec3(0.12f, 0.12f, 0.12f), kJoint);
        appendTaperedStrut(verts, indices, shoulder, elbow, 0.115f, 0.09f, kLimb);

        // The elbow cube, matching the knee, so both joints read the same way.
        appendBox(verts, indices, elbow, right, up, fwd,
                  glm::vec3(0.105f, 0.10f, 0.105f), kJoint);

        appendTaperedStrut(verts, indices, elbow, hand, 0.09f, 0.07f, kLimb);
        appendBox(verts, indices, hand, right, up, fwd,
                  glm::vec3(0.095f, 0.095f, 0.10f), kJoint);
    }
}

void appendCrate(const glm::vec3& centre, float size, const glm::vec3& right,
                 const glm::vec3& up, const glm::vec3& forward,
                 std::vector<SceneVertex>& verts, std::vector<uint32_t>& indices)
{
    const glm::vec3 kCrate {0.72f, 0.13f, 0.11f};
    const glm::vec3 kBand  {0.95f, 0.42f, 0.20f};

    const float h = size * 0.5f;
    appendBox(verts, indices, centre, right, up, forward, glm::vec3(h), kCrate);

    // A band round the middle, so you can see it rotate in his hands rather than
    // it reading as a featureless cube.
    appendBox(verts, indices, centre, right, up, forward,
              glm::vec3(h * 1.04f, h * 0.22f, h * 1.04f), kBand);
}

void appendStoragePad(const glm::vec3& centre, float radius, bool occupied,
                      std::vector<SceneVertex>& verts, std::vector<uint32_t>& indices)
{
    const glm::vec3 idle {0.20f, 0.34f, 0.44f};
    const glm::vec3 full {0.24f, 0.52f, 0.34f};
    const glm::vec3 c = occupied ? full : idle;

    const glm::vec3 right(1, 0, 0), up(0, 1, 0), fwd(0, 0, 1);

    // A low slab plus four corner posts -- enough to read as a place from across
    // the field without being a building.
    appendBox(verts, indices, centre + up * 0.06f, right, up, fwd,
              glm::vec3(radius, 0.06f, radius), c);

    for (int i = 0; i < 4; ++i) {
        float sx = (i & 1) ? 1.0f : -1.0f;
        float sz = (i & 2) ? 1.0f : -1.0f;
        appendBox(verts, indices,
                  centre + glm::vec3(sx * radius * 0.9f, 0.32f, sz * radius * 0.9f),
                  right, up, fwd, glm::vec3(0.08f, 0.32f, 0.08f), c * 1.5f);
    }
}

void appendApproachMark(const glm::vec3& centre, float radius, bool claimed,
                        std::vector<SceneVertex>& verts, std::vector<uint32_t>& indices)
{
    // Blue, because nothing else out here is. This is the muster point at the
    // foot of the ramp -- the one place on the ground from which the way up is
    // open -- and being able to SEE it is the difference between watching a
    // creature navigate and watching it mill about.
    const glm::vec3 idle {0.16f, 0.42f, 0.82f};
    const glm::vec3 live {0.35f, 0.72f, 1.00f};
    const glm::vec3 c = claimed ? live : idle;

    const glm::vec3 right(1, 0, 0), up(0, 1, 0), fwd(0, 0, 1);

    // A ring rather than a disc, so it reads as somewhere to stand rather than
    // as something to stand on -- and so it does not fight the ground for depth
    // across its whole area.
    const int kSegments = 16;
    for (int i = 0; i < kSegments; ++i) {
        const float a = (6.2831853f * i) / kSegments;
        const glm::vec3 at = centre + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * radius;

        appendBox(verts, indices, at + up * 0.07f, right, up, fwd,
                  glm::vec3(0.17f, 0.07f, 0.17f), c);
    }

    // A short post in the middle, so it is findable across the field and from
    // above, where a flat ring on flat ground disappears.
    appendBox(verts, indices, centre + up * 0.55f, right, up, fwd,
              glm::vec3(0.09f, 0.55f, 0.09f), c * 1.4f);
}

} // namespace tessara
