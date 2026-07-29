// Headless gait sweep.
//
// The heading rule and the slope rule are the two things in this example that
// cannot be judged by eye: a walker that looks purposeful for ten seconds can
// still be covering 11% of the field and circling in a rut. LIME's numbers came
// from running the gait without a renderer, and the sliders' defaults are only
// defensible for as long as that keeps being true.
//
// It also runs the ship checks in Checks.cpp, for the same reason: whether a
// creature can walk up the ramp is not something looking at the code will tell
// you either, and that particular question has already been got wrong five ways.
//
// Run it after touching Walker.cpp, Biped.cpp, Ground.cpp or Ship.cpp:
//   ./build/examples/tessara_axiom/tessara_gait_sim [ticks]
//   ./build/examples/tessara_axiom/tessara_gait_sim --check    (checks only, quick)
//   ./build/examples/tessara_axiom/tessara_gait_sim --check -v (and say what passed)
//
// Exits non-zero if a check fails, so it can be run as one.

#include "Checks.hpp"
#include "Ground.hpp"
#include "Heightfield.hpp"
#include "Walker.hpp"

#include <cstring>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <vector>

using namespace tessara;

namespace {

struct Result {
    float coverage;
    float interior;   // coverage ignoring a margin of nodes at the field edge
    float border;     // coverage of that margin alone
    float turnRate;
    int   stuck;
};

constexpr int kMargin = 4;

Result run(int gridN, float spacing, float relief,
           float slopeDeg, int lookahead, int bias, int ticks,
           bool tieBreak = true, uint32_t seed = 1u, glm::ivec2 start = {-1, -1})
{
    Heightfield terrain(gridN, spacing, relief);
    // No ship in the sim: a bare Ground over the terrain is the same world the
    // walker had before there was one to stand on.
    Ground field(terrain);

    Walker walker;
    walker.params.maxSlopeDeg     = slopeDeg;
    walker.params.lookahead       = lookahead;
    walker.params.straightBias    = bias;
    walker.params.randomTieBreak  = tieBreak;
    walker.params.stepsPerSecond  = 1.0f;      // one tick per update below

    walker.setSeed(seed);
    if (start.x < 0) start = glm::ivec2(gridN / 2, gridN / 3);
    walker.reset(field, start, 0);

    for (int i = 0; i < ticks; ++i) walker.update(field, 1.0f);

    // Split the count, because "not covered" has two very different causes:
    // ground the terrain walls off, and ground the heading rule never aims at.
    const auto& visited = walker.visitedMap();
    int interiorHit = 0, interiorAll = 0, borderHit = 0, borderAll = 0;
    for (int y = 0; y < gridN; ++y) {
        for (int x = 0; x < gridN; ++x) {
            bool isBorder = x < kMargin || y < kMargin ||
                            x >= gridN - kMargin || y >= gridN - kMargin;
            bool hit = visited[static_cast<size_t>(y) * gridN + x] != 0;
            if (isBorder) { ++borderAll; borderHit += hit; }
            else          { ++interiorAll; interiorHit += hit; }
        }
    }

    int total = walker.steps() + walker.turns() + walker.stuckTicks();
    return {
        walker.coverage() * 100.0f,
        interiorAll ? 100.0f * interiorHit / interiorAll : 0.0f,
        borderAll   ? 100.0f * borderHit   / borderAll   : 0.0f,
        total > 0 ? 100.0f * walker.turns() / total : 0.0f,
        walker.stuckTicks(),
    };
}

} // namespace

int main(int argc, char** argv) {
    bool checkOnly = false, verbose = false;
    int ticks = 100000;

    for (int i = 1; i < argc; ++i) {
        if      (std::strcmp(argv[i], "--check") == 0) checkOnly = true;
        else if (std::strcmp(argv[i], "-v") == 0)      verbose = true;
        else                                           ticks = std::atoi(argv[i]);
    }

    // Checks first, and always. They take a couple of seconds against the sweep's
    // couple of minutes, and a gait number measured in a world whose ramp is
    // broken is a number about nothing.
    const int failed = tessara::runShipChecks(verbose);
    if (checkOnly) return failed ? 1 : 0;

    const int   kGridN   = 128;
    const float kSpacing = 2.0f;
    const float kRelief  = 11.0f;   // must track kDefaultRelief in main.cpp

    std::printf("%dx%d nodes, spacing %.1f, relief %.1f, %d ticks\n\n",
                kGridN, kGridN, kSpacing, kRelief, ticks);

    // Eight drop points, so a number is a property of the GAIT and not of one
    // lucky trajectory. Worst-case matters more than the mean here: a colony is
    // only as good as its unluckiest member.
    const glm::ivec2 starts[] = {
        {kGridN / 2, kGridN / 3}, {12, 12},   {kGridN - 14, 12},
        {12, kGridN - 14},        {kGridN - 14, kGridN - 14},
        {kGridN / 4, kGridN / 2}, {kGridN * 3 / 4, kGridN / 2}, {kGridN / 2, kGridN - 20},
    };
    constexpr int kStartCount = static_cast<int>(std::size(starts));

    auto sweep = [&](const char* label, bool tieBreak) {
        std::printf("%s\n", label);
        std::printf("  %-10s %-6s %10s %10s %10s %8s\n",
                    "lookahead", "bias", "worst", "mean", "best", "turns");

        const int lookaheads[] = {0, 1, 2, 3, 4, 6, 8};
        const int biases[]     = {0, 1, 2, 3};

        for (int lookahead : lookaheads) {
            for (int bias : biases) {
                float worst = 1e9f, best = -1.0f, sum = 0.0f, turnSum = 0.0f;
                for (int s = 0; s < kStartCount; ++s) {
                    Result r = run(kGridN, kSpacing, kRelief, 45.0f, lookahead, bias,
                                   ticks, tieBreak, 1u + s * 7919u, starts[s]);
                    worst = std::min(worst, r.coverage);
                    best  = std::max(best,  r.coverage);
                    sum   += r.coverage;
                    turnSum += r.turnRate;
                }
                std::printf("  %-10d %-6d %9.1f%% %9.1f%% %9.1f%% %7.1f%%\n",
                            lookahead, bias, worst, sum / kStartCount, best,
                            turnSum / kStartCount);
            }
        }
        std::printf("\n");
    };

    sweep("heading rule, DETERMINISTIC tie-break (the LIME behaviour)", false);
    sweep("heading rule, RANDOM tie-break", true);

    // Held at the knobs chosen from the sweep above.
    constexpr int kLookahead = 3;
    constexpr int kBias      = 2;

    std::printf("slope rule (lookahead %d, bias %d, random tie-break)   border margin = %d\n",
                kLookahead, kBias, kMargin);
    std::printf("  %-8s %10s %10s %10s %9s %8s\n",
                "slope", "worst", "mean", "interior", "border", "refusing");
    const float slopes[] = {25.0f, 28.0f, 30.0f, 32.0f, 34.0f, 36.0f, 38.0f,
                            40.0f, 42.0f, 45.0f, 55.0f, 70.0f};
    for (float slope : slopes) {
        float worst = 1e9f, sum = 0.0f, interiorSum = 0.0f, borderSum = 0.0f;
        for (int s = 0; s < kStartCount; ++s) {
            Result r = run(kGridN, kSpacing, kRelief, slope, kLookahead, kBias,
                           ticks, true, 1u + s * 7919u, starts[s]);
            worst = std::min(worst, r.coverage);
            sum += r.coverage;
            interiorSum += r.interior;
            borderSum   += r.border;
        }

        // How much of the field the rule actually walls off. If this is zero the
        // limit never binds, the probe rays are always green, and the creature
        // never looks like it is refusing anything -- which is most of what
        // makes it read as alive.
        Heightfield terrain(kGridN, kSpacing, kRelief);
        // No ship in the sim: a bare Ground over the terrain is the same world the
        // walker had before there was one to stand on.
        Ground field(terrain);
        Walker probe;
        probe.params.maxSlopeDeg = slope;
        probe.reset(field, glm::ivec2(2, 2), 0);
        int blocked = 0, tested = 0;
        for (int y = 1; y + 1 < kGridN; ++y) {
            for (int x = 1; x + 1 < kGridN; ++x) {
                for (int d = 0; d < 4; ++d) {
                    glm::ivec2 from(x, y);
                    ++tested;
                    if (!probe.goodStep(field, from, from + Walker::dirVec(d), terrain.heightAt(from))) ++blocked;
                }
            }
        }

        std::printf("  %6.0f   %9.1f%% %9.1f%% %9.1f%% %8.1f%% %7.1f%%\n",
                    slope, worst, sum / kStartCount,
                    interiorSum / kStartCount, borderSum / kStartCount,
                    tested ? 100.0f * blocked / tested : 0.0f);
    }

    // The slope limit only means something relative to how steep the ground
    // actually is. At relief 5 on spacing 2 the field has almost no walls in it,
    // so the rule never fires and the creature never refuses anything. This is
    // the dial that puts the walls back.
    std::printf("\nterrain relief (slope 45 deg, lookahead %d, bias %d)\n",
                kLookahead, kBias);
    std::printf("  %-8s %10s %10s %9s %9s\n",
                "relief", "worst", "mean", "refusing", "unreachable");
    const float reliefs[] = {5.0f, 7.0f, 9.0f, 11.0f, 13.0f, 15.0f, 18.0f, 22.0f};
    for (float relief : reliefs) {
        float worst = 1e9f, sum = 0.0f;
        for (int s = 0; s < kStartCount; ++s) {
            Result r = run(kGridN, kSpacing, relief, 45.0f, kLookahead, kBias,
                           ticks, true, 1u + s * 7919u, starts[s]);
            worst = std::min(worst, r.coverage);
            sum += r.coverage;
        }

        Heightfield terrain(kGridN, kSpacing, relief);
        // No ship in the sim: a bare Ground over the terrain is the same world the
        // walker had before there was one to stand on.
        Ground field(terrain);
        Walker probe;
        probe.params.maxSlopeDeg = 45.0f;
        probe.reset(field, glm::ivec2(2, 2), 0);

        int blocked = 0, tested = 0, isolated = 0;
        for (int y = 1; y + 1 < kGridN; ++y) {
            for (int x = 1; x + 1 < kGridN; ++x) {
                glm::ivec2 from(x, y);
                int open = 0;
                for (int d = 0; d < 4; ++d) {
                    ++tested;
                    if (probe.goodStep(field, from, from + Walker::dirVec(d), terrain.heightAt(from))) ++open;
                    else ++blocked;
                }
                if (open == 0) ++isolated;   // a node no single step can leave
            }
        }

        std::printf("  %6.1f   %9.1f%% %9.1f%% %8.1f%% %8.1f%%\n",
                    relief, worst, sum / kStartCount,
                    100.0f * blocked / tested,
                    100.0f * isolated / ((kGridN - 2) * (kGridN - 2)));
    }

    return failed ? 1 : 0;
}
