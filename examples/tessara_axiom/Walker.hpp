#pragma once

#include "Ground.hpp"

#include <glm/glm.hpp>
#include <vector>

namespace tessara {

// A four-legged machine that crawls the heightfield, one foot per corner, feet
// only ever standing on lattice NODES.
//
// The gait, two phases:
//   1. Both front feet step one node along the heading. The body stretches.
//   2. Both back feet drop into the nodes the front feet just left.
//
// Because phase 2 lands on positions phase 1 vacated, the body's shape is
// RESTORED every cycle rather than drifting: no step inherits the previous
// step's error, because the targets are exact and already known rather than
// estimated.
//
// A step is refused unless the ground it lands on is within the slope limit of
// the foot's current node. Refusing is what produces the searching look -- he
// probes, rejects, and turns, with no seeking behaviour written anywhere.
//
//
// ONE CHANGE FROM THE LIME VERSION, AND WHY
//
// In LIME a turn changed the heading but left the feet where they were, so the
// front pair ended up separated ALONG the direction of travel instead of across
// it and the body sheared into a degenerate strip. It never showed, because that
// version drew the body as a flat diagram seen from above.
//
// Here the footprint is held as a 2x2 block of nodes plus a heading, and turning
// rotates the ROLES of the four corners by one. The four occupied nodes are
// unchanged -- a 2x2 block maps onto itself under a 90-degree rotation -- so a
// turn costs no new ground and can never be refused, while each leg swings to
// the adjacent corner. It reads as a pivot in place, which is what it is.
class Walker {
public:
    // Hauling, for a creature with no arms: he drives onto the crate, it clamps
    // to his back, he drives to the pile and lets go. The interesting part is
    // not the carrying -- it is that the heading rule has to change. Wandering
    // scores a direction by how much UNWALKED ground it opens; hauling scores it
    // by how much closer it gets him. Same loop, opposite objective.
    enum class Activity { Wander, Approach, Carry, Leaving };

    struct Params {
        // What counts as a wall. Whether this bites at all depends on the
        // terrain's relief, not on the angle alone -- see the relief sweep in
        // gait_sim.cpp. At the example's default relief it refuses about 3.6% of
        // candidate steps, which is often enough to watch him do it.
        float maxSlopeDeg = 45.0f;

        // Settled by running the gait headless from eight drop points on the
        // example's default terrain, not by taste, and neither is guessable.
        // With no lookahead at all he covers about 10% of the field however the
        // bias is set: walking straight until something stops you traces lines
        // rather than area, and with every direction equally stale he circles in
        // a rut. This pair has the best worst case of any smooth setting --
        // 92.4%, against 83.7% for LIME's 4-and-1.
        //
        // Those LIME values were measured on a 64-node field at unit spacing and
        // did NOT transfer here, which is the argument for keeping gait_sim.cpp
        // rather than the argument for any particular pair. Re-run it whenever
        // the terrain, the spacing or the relief changes.
        int lookahead    = 3;
        int straightBias = 2;

        // Choose uniformly at random between headings that score EXACTLY equal,
        // rather than always taking the first. Not a seeking behaviour and not a
        // random walk -- it only fires on a genuine tie, and the turn rate stays
        // near 1% either way.
        //
        // Without it the walk is fully deterministic and drops into limit
        // cycles, and the good settings become knife-edge. At the defaults it
        // costs 13 points of worst-case coverage outright (79.2% against 92.4%),
        // and across the whole lookahead 2-4 by bias 1-3 block the deterministic
        // worst case falls as low as 19% where the random one never drops below
        // 75%. Since both knobs are on sliders someone will drag, a broad
        // plateau is worth more than a taller peak.
        bool randomTieBreak = true;

        float stepsPerSecond = 7.0f;
        float legLiftFrac    = 0.35f;   // of a node spacing
        float bodyHoverFrac  = 0.55f;   // of a node spacing, above the foot plane

        // ---- hauling ----
        float pickupNodes  = 1.6f;    // how near a crate counts as over it
        float cargoRise    = 0.55f;   // how high the crate rides above his shell
        int   giveUpTicks  = 4000;    // patience before abandoning a crate

        // ---- how much room he needs ----
        // Headroom above whatever a foot is standing on. He gets no radius to go
        // with it, and does not need one: his footprint is four separate nodes
        // and every one of them is tested on its own, so the 2x2 IS the
        // collision volume. A wall he could not fit through is a wall one of the
        // four corners cannot be put down in.
        float bodyRise = 1.6f;
    };

    // ---- hauling ----------------------------------------------------------
    void assignFetch(const Ground& hf, const glm::vec3& crate, const glm::vec3& storage);
    void abandonTask();

    Activity  activity() const { return m_activity; }
    bool      hasTask()  const { return m_hasTask; }
    bool      hasCargo() const { return m_carrying; }
    glm::vec3 cargoPosition(const Ground& hf) const;
    const char* activityName() const;

    // The route he is following, as block positions -- exposed so the diagram
    // view can draw it.
    const std::vector<glm::ivec2>& path() const { return m_pathNodes; }
    bool pathFailed() const { return m_pathFailed; }

    // Fixed by default, so a run is still repeatable exactly.
    void setSeed(uint32_t seed) { m_rngState = seed ? seed : 1u; }

    Params params;

    void reset(const Ground& hf, glm::ivec2 blockMin, int heading = 0);
    void update(const Ground& hf, float dt);

    // ---- discrete state ---------------------------------------------------
    // Foot order is frontLeft, frontRight, backRight, backLeft, so 0..3 winds
    // the body quad without reordering.
    const glm::ivec2& footNode(int i) const { return m_feet[i]; }
    int  heading() const { return m_dir; }
    int  phase()   const { return m_phase; }

    // The surface this foot is standing on. A node no longer answers that on its
    // own once anything is stacked over the terrain, so anyone asking a question
    // about a step -- the diagram view, most of all -- has to be told where the
    // foot doing the stepping already is.
    float footHeight(int i) const { return m_footY[i]; }

    // ---- animated state ---------------------------------------------------
    // Feet interpolate from where they were to where they are, with a lifting
    // arc, so a step is a step rather than a teleport.
    glm::vec3 footWorld(const Ground& hf, int i) const;
    glm::vec3 bodyCentre(const Ground& hf) const;
    glm::vec3 bodyUp(const Ground& hf) const;
    glm::vec3 bodyForward(const Ground& hf) const;

    // ---- the rules, exposed so the diagnostic view can draw them ----------
    bool goodStep(const Ground& hf, const glm::ivec2& from, const glm::ivec2& to,
                  float fromY) const;
    bool canAdvance(const Ground& hf, int dir) const;
    static glm::ivec2 dirVec(int dir);

    // ---- where he has been ------------------------------------------------
    bool visited(const glm::ivec2& node) const;
    const std::vector<uint8_t>& visitedMap() const { return m_visited; }
    float coverage() const;   // 0..1 of reachable-or-not nodes touched

    int steps()    const { return m_steps; }
    int turns()    const { return m_turns; }
    int stuckTicks() const { return m_stuck; }

private:
    void tick(const Ground& hf);
    void recomputeFeet(const Ground& hf);
    float stepReach(const Ground& hf) const;
    float surfaceUnder(const Ground& hf, const glm::ivec2& node) const;
    void markVisited();
    glm::ivec2 corner(int index) const;   // 0..3 counter-clockwise from blockMin
    uint32_t nextRandom();
    bool blockCanStep(const Ground& hf, const glm::ivec2& block, int dir,
                      float fromY, float& outY) const;
    bool planPath(const Ground& hf, const glm::ivec2& goalBlock);

    glm::ivec2 m_block{0, 0};    // min corner of the 2x2 footprint
    int m_dir   = 0;
    int m_phase = 0;             // 0 = settled, 1 = stretched

    glm::ivec2 m_feet[4]{};
    glm::ivec2 m_prevFeet[4]{};

    // The height of the surface each foot is actually standing on, and was
    // standing on before the last step.
    //
    // A node used to be enough to say where a foot was, because a node had one
    // height. With a ship in the world it has two -- the dirt and the cargo deck
    // over it -- and which one a foot is on is not a property of the node, it is
    // a property of the foot. Remembering it per foot is what lets him climb a
    // ramp: each step is measured from where that foot already is, so reaching
    // the deck is a short step up from the ramp and an impossible one from the
    // ground beside the hull.
    float m_footY[4]{};
    float m_prevFootY[4]{};

    float m_accum = 0.0f;        // fraction of the way to the next tick
    uint32_t m_rngState = 0x9E3779B9u;

    // Hauling state.
    Activity   m_activity = Activity::Wander;
    bool       m_hasTask  = false;
    bool       m_carrying = false;

    // On his way out of somewhere, under his own steam rather than under orders.
    // Deliberately NOT m_hasTask: he stays assignable the whole time, so a crate
    // coming up while he is halfway down the ramp simply replaces the trip.
    bool       m_leaving  = false;
    glm::ivec2 m_target{0, 0};
    glm::vec3  m_storageWorld{0.0f};
    // A planned route, not a greedy heading. Greedy scored 7 of 24 deliveries;
    // the terrain wedges it in local minima and no amount of local escaping gets
    // it out. The whole reason this creature stands on lattice nodes is that its
    // movement IS a graph -- so the graph can simply be searched.
    std::vector<int>        m_path;        // one heading per step
    std::vector<glm::ivec2> m_pathNodes;   // the same route as block positions
    size_t     m_pathIndex = 0;
    bool       m_pathFailed = false;
    int        m_taskTicks = 0;

    std::vector<uint8_t> m_visited;
    int m_gridN  = 0;
    int m_steps  = 0;
    int m_turns  = 0;
    int m_stuck  = 0;
};

} // namespace tessara
