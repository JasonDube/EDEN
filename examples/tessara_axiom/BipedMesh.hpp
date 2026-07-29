#pragma once

#include "Biped.hpp"
#include "SceneVertex.hpp"

#include "Renderer/ModelRenderer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tessara {

// APPENDS the biped's geometry to the buffers. Both creatures share one vertex
// buffer and one draw, which also means the ghost pass's depth sort covers them
// together rather than having to order two objects against each other.
// A head loaded from a file, instead of the box he was born with. Stored in a
// normalised local frame -- centred on its own bounds and scaled so its height
// is exactly 1 -- so the creature's head size stays the thing that decides how
// big it looks, and swapping in a different model changes nothing else.
struct HeadModel {
    // Kept as ModelVertex, texture bytes and all, because this one goes to
    // eden::ModelRenderer rather than into the example's own untextured
    // pipeline. Baking the map into vertex colours was the wrong call: the
    // sentinel's camo, panel lines and lit eye slots are per-pixel detail, and
    // no triangle density carries that through vertex interpolation.
    std::vector<eden::ModelVertex> vertices;
    std::vector<uint32_t>          indices;

    std::vector<unsigned char> texture;   // RGBA8
    int   texWidth  = 0;
    int   texHeight = 0;

    bool  loaded = false;

    // 1.0 is the height of the box head it replaced. Judged by eye against the
    // torso once the frame was right: 1.0 too small, 2.0 too big, 1.5 lands.
    float scale     = 1.5f;

    // Exporters disagree about which way is up and which way is forward, and
    // there is no reliable way to tell from the file. Three angles can reach any
    // orientation, so rather than guess, turn it until it looks right.
    // All zero, and they should stay that way for a model authored Y-up facing
    // +Z. The 180 roll that used to live here was compensating for an inverted
    // head frame in Biped, not for anything in the file -- and because a roll
    // flips left-for-right as well as top-for-bottom, it was quietly mirroring
    // the head too.
    float yawOffset   = 0.0f;
    float pitchOffset = 0.0f;
    float rollOffset  = 0.0f;

    // Height of the CHIN above the collar, not of the centre above anything.
    // Anchoring the centre means every change of scale also moves the chin --
    // grow the head and it eats the torso, shrink it and it floats -- so the
    // size and the placement can never be settled independently. Anchored at the
    // chin, scale grows the head upward and this stays put.
    float rise      = 0.10f;
    bool  textured  = false;   // whether the file brought a base-colour map

    // Set once the mesh is on the GPU.
    uint32_t handle = UINT32_MAX;

    // The frame the head should be drawn in, given the creature's own head frame.
    glm::mat4 transform(const Biped& biped) const;
};

// Returns false and fills `error` rather than throwing: a missing model should
// cost you the head, not the run.
bool loadHeadModel(const std::string& path, HeadModel& out, std::string& error);

void appendBipedMesh(const Biped& biped,
                     const HeadModel& head,
                     std::vector<SceneVertex>& outVertices,
                     std::vector<uint32_t>& outIndices);

// The crate he fetches, and the pad he takes it to. Drawn here so the whole
// scene stays in one vertex buffer and one draw.
void appendCrate(const glm::vec3& centre, float size, const glm::vec3& right,
                 const glm::vec3& up, const glm::vec3& forward,
                 std::vector<SceneVertex>& outVertices,
                 std::vector<uint32_t>& outIndices);

void appendStoragePad(const glm::vec3& centre, float radius, bool occupied,
                      std::vector<SceneVertex>& outVertices,
                      std::vector<uint32_t>& outIndices);

// The muster point at the foot of the ramp: where anything meaning to go aboard
// has to get to first, because the rails make it the only way on. `claimed` is
// whether somebody is currently heading for it.
void appendApproachMark(const glm::vec3& centre, float radius, bool claimed,
                        std::vector<SceneVertex>& outVertices,
                        std::vector<uint32_t>& outIndices);

} // namespace tessara
