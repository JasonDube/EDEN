#include "Walker.hpp"

#include <algorithm>
#include <cmath>

namespace tessara {

namespace {

constexpr int kDirCount = 4;

// How many ticks of a refused route-step to sit through before throwing the
// route away. Long enough that a ramp finishing its travel is waited out rather
// than replanned around; short enough that a genuinely dead route costs him
// under a second.
constexpr int kStallTicks = 6;
const int kDirX[kDirCount] = { 1, 0, -1, 0 };
const int kDirY[kDirCount] = { 0, 1,  0, -1 };

constexpr float kPi = 3.14159265f;

} // namespace

// xorshift32. Deliberately not <random>: this has to give the same sequence in
// the sim and in the game, on any standard library.
uint32_t Walker::nextRandom() {
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return m_rngState;
}

glm::ivec2 Walker::dirVec(int dir) {
    int d = ((dir % kDirCount) + kDirCount) % kDirCount;
    return glm::ivec2(kDirX[d], kDirY[d]);
}

// The 2x2 footprint's corners, counter-clockwise from the min corner. Turning is
// a rotation of role over this ring, which is why it costs no ground.
glm::ivec2 Walker::corner(int index) const {
    static const glm::ivec2 kOffsets[4] = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1}
    };
    return m_block + kOffsets[((index % 4) + 4) % 4];
}

// How far up or down a single step may go, from the slope limit and the spacing.
// The step rule and the reach rule are deliberately the SAME number: a surface
// he could not climb to is a surface he cannot be standing on, so asking what is
// under a foot and asking whether he may put it there is one question.
float Walker::stepReach(const Ground& hf) const {
    return hf.spacing() * std::tan(glm::radians(params.maxSlopeDeg));
}

// The height of whatever the foot on this node is standing on. All four corners
// are occupied in phase 0, which is the only phase that asks about a corner.
float Walker::surfaceUnder(const Ground& hf, const glm::ivec2& node) const {
    for (int i = 0; i < 4; ++i) {
        if (m_feet[i] == node) return m_footY[i];
    }
    return hf.terrain().heightAt(node);
}

void Walker::recomputeFeet(const Ground& hf) {
    glm::ivec2 was[4];
    float wasY[4];
    for (int i = 0; i < 4; ++i) { was[i] = m_feet[i]; wasY[i] = m_footY[i]; }

    // frontLeft, frontRight, backRight, backLeft -- one step apart around the
    // ring, offset by the heading. For heading +x this puts the front pair on
    // the +x side; rotating the heading rotates the whole assignment.
    m_feet[0] = corner(m_dir + 1);   // front left
    m_feet[1] = corner(m_dir + 2);   // front right
    m_feet[2] = corner(m_dir + 3);   // back right
    m_feet[3] = corner(m_dir + 0);   // back left

    if (m_phase == 1) {
        glm::ivec2 d = dirVec(m_dir);
        m_feet[0] += d;
        m_feet[1] += d;
    }

    // Then work out what each foot is now standing ON, measured from where a
    // foot already was rather than from the terrain.
    //
    // Which foot to measure from is the whole trick, and it falls out of the two
    // moves this creature has. A TURN lands every foot on a node one of the
    // others was just standing on, so each inherits that foot's surface exactly
    // and a pivot on the cargo deck stays on the cargo deck. A STEP moves only
    // the front pair onto new ground, and they reach from their own last height
    // -- which is what makes the ramp climbable one node at a time and the side
    // of the hull, two units up in one go, not.
    for (int i = 0; i < 4; ++i) {
        float reference = wasY[i];
        for (int j = 0; j < 4; ++j) {
            if (was[j] == m_feet[i]) { reference = wasY[j]; break; }
        }
        m_footY[i] = hf.heightAt(m_feet[i], reference, stepReach(hf));
    }
}

void Walker::reset(const Ground& hf, glm::ivec2 blockMin, int heading) {
    m_gridN = hf.n();
    m_visited.assign(static_cast<size_t>(m_gridN) * m_gridN, 0);

    m_block = glm::clamp(blockMin, glm::ivec2(0), glm::ivec2(m_gridN - 2));
    m_dir   = ((heading % kDirCount) + kDirCount) % kDirCount;
    m_phase = 0;
    m_accum = 0.0f;
    m_steps = m_turns = m_stuck = 0;
    m_routeStall = 0;
    m_stationStalled = false;

    // Dropped onto the terrain, always. Being set down inside the ship is not a
    // thing anyone asks for, and starting on the dirt means the first step out
    // of here is measured from somewhere real.
    for (int i = 0; i < 4; ++i) {
        m_feet[i] = corner(i);
        m_footY[i] = hf.terrain().heightAt(m_feet[i]);
    }

    recomputeFeet(hf);
    for (int i = 0; i < 4; ++i) {
        m_prevFeet[i]  = m_feet[i];
        m_prevFootY[i] = m_footY[i];
    }
    markVisited();
}

void Walker::markVisited() {
    for (int i = 0; i < 4; ++i) {
        const glm::ivec2& n = m_feet[i];
        if (n.x >= 0 && n.y >= 0 && n.x < m_gridN && n.y < m_gridN) {
            m_visited[static_cast<size_t>(n.y) * m_gridN + n.x] = 1;
        }
    }
}

bool Walker::visited(const glm::ivec2& node) const {
    if (node.x < 0 || node.y < 0 || node.x >= m_gridN || node.y >= m_gridN) {
        return true;   // off the field counts as seen, so it never draws him outward
    }
    return m_visited[static_cast<size_t>(node.y) * m_gridN + node.x] != 0;
}

float Walker::coverage() const {
    if (m_visited.empty()) return 0.0f;
    size_t hit = 0;
    for (uint8_t v : m_visited) hit += v ? 1 : 0;
    return static_cast<float>(hit) / static_cast<float>(m_visited.size());
}

// A step is good if the target exists, the surface does not rise or fall more
// steeply than the limit, and there is nothing solid standing where the foot
// would go. At 45 degrees on unit spacing the slope rule is a height change of
// one spacing -- so he walks slopes and refuses walls.
//
// `fromY` is the height the stepping foot is at NOW, which is not the same as
// the terrain under it once there is a ship in the world. Everything about
// walking onto the deck and refusing to climb the hull comes out of passing the
// right value here.
bool Walker::goodStep(const Ground& hf, const glm::ivec2& from, const glm::ivec2& to,
                      float fromY) const {
    if (!hf.inBounds(to)) return false;

    float run = hf.spacing() * static_cast<float>(std::abs(to.x - from.x) + std::abs(to.y - from.y));
    if (run < 1e-6f) return false;

    float maxRise = run * std::tan(glm::radians(params.maxSlopeDeg));

    // Reached for with exactly the step's own limit, so a surface too high to
    // climb to is never offered as a candidate in the first place.
    glm::vec3 landing = hf.worldAt(to, fromY, maxRise);

    if (std::fabs(landing.y - fromY) > maxRise) return false;

    // And nowhere to put a foot inside something solid. No radius: the other
    // three corners are tested on their own, and between them they are his width.
    if (hf.blocked(landing.x, landing.z, landing.y, params.bodyRise)) return false;

    // Nor anything solid BETWEEN here and there.
    //
    // His feet are points and his stride is two units, so testing only where a
    // foot lands means anything narrower than a step is something he walks over
    // without ever touching. The ramp's kerb is exactly that -- and a kerb wide
    // enough that a point could not miss it would be a kerb wider than the ramp.
    //
    // One sample at the midpoint is enough here because a step is a single node
    // and there is only one gap to fall in. It is the same reasoning that says
    // the bulkhead had to be seven units deep rather than half of one, arrived at
    // from the other end: make the obstacle bigger, or look more often.
    const glm::vec3 origin = hf.worldAt(from, fromY, 0.01f);
    const glm::vec3 half   = (origin + landing) * 0.5f;

    // Stood on whatever is under the MIDPOINT, not at the height of either end.
    //
    // Both ends used to be candidates and the higher one was taken, on the
    // reasoning that it is the more forgiving. It is not forgiving enough on a
    // ramp: the ramp is a patch with a solid underside whose top face is set a
    // sixteenth of a unit proud of the deck, so halfway through the last step --
    // ramp to deck -- the floor under him is higher than either end of the step,
    // and measuring from an end puts his soles inside the slab he is walking on.
    //
    // That refused the ramp-to-deck step outright wherever the ramp lay oblique
    // to the lattice, because only then are his two front feet at different
    // heights on it and only then does the geometry get that close. Twelve of
    // seventy-two ship headings could not be boarded, and the ramp was innocent.
    //
    // Only ever a RAISE, never a drop. Taking the midpoint's floor outright is
    // too generous the other way: measured up there, a body stepping onto the
    // ramp's flank from the dirt beside it is standing on the ramp by the time it
    // is halfway, so the kerb it is climbing over stops colliding with it and the
    // flank becomes a way in. Eight of sixty-four ship placements leaked one.
    //
    // The higher of the two ends stays the floor, then; the midpoint only gets a
    // say when the ground between them is higher than both, which is the one case
    // the old rule could not see.
    float midFloor = std::max(fromY, landing.y);

    // ...and only for a foot already standing on one of the ship's own surfaces.
    //
    // Lifting the body onto the floor between the two ends is right for somebody
    // walking along the ramp and wrong for somebody standing on the dirt
    // UNDERNEATH it: from down there the same lift puts him on top of the slab
    // halfway through the step, so the underside stops colliding with him and he
    // mounts the ramp from below, in the middle, in one step. Thirty of those
    // appeared the moment the lift was unconditional.
    //
    // Asked with a hair's step-up, so the question is what he is standing on now
    // rather than what he could reach.
    if (hf.onPatch(origin.x, origin.z, fromY, 0.01f)) {
        midFloor = std::max(midFloor, hf.heightAt(half.x, half.z, fromY, maxRise));
    }

    return !hf.blocked(half.x, half.z, midFloor, params.bodyRise);
}

// Both front feet must be able to step, or the whole move is refused. Half a
// step forward would tear the body.
//
// Only ever asked in phase 0, when all four corners are underfoot -- so the
// heights the two front feet reach from are ones he is actually standing at,
// even for a heading he has not turned to yet.
bool Walker::canAdvance(const Ground& hf, int dir) const {
    glm::ivec2 d = dirVec(dir);
    glm::ivec2 fl = corner(dir + 1);
    glm::ivec2 fr = corner(dir + 2);
    return goodStep(hf, fl, fl + d, surfaceUnder(hf, fl))
        && goodStep(hf, fr, fr + d, surfaceUnder(hf, fr));
}

// The 2x2 block whose CENTRE lands nearest a world point.
//
// Not the same as the block offset from the nearest node, which is what the
// hauling code uses and is fine there -- a crate only has to be reached. A
// station has to be STOOD ON, and he stands at the middle of his block, so the
// question is which block puts its middle closest. The difference is a whole
// node: sent to a mark he settled two and a half units off it, and looked like
// he was near his spot rather than on it, because he was.
static glm::ivec2 blockNearest(const Ground& hf, const glm::vec3& world) {
    const float half = hf.n() * 0.5f * hf.spacing();
    return glm::ivec2(
        static_cast<int>(std::round((world.x + half) / hf.spacing() - 0.5f)),
        static_cast<int>(std::round((world.z + half) / hf.spacing() - 0.5f)));
}

// Nearest lattice node to a world point, which is how anything out in continuous
// space gets expressed to a creature that only understands nodes.
static glm::ivec2 nodeNear(const Ground& hf, const glm::vec3& world) {
    float half = hf.n() * 0.5f * hf.spacing();
    int x = static_cast<int>(std::round((world.x + half) / hf.spacing()));
    int y = static_cast<int>(std::round((world.z + half) / hf.spacing()));
    return glm::clamp(glm::ivec2(x, y), glm::ivec2(0), glm::ivec2(hf.n() - 1));
}

// Can a 2x2 block at `block`, standing at height `fromY`, step one node along
// `dir`? Same rule canAdvance applies to the live creature, asked about a
// hypothetical position -- which is what makes the terrain searchable rather than
// only walkable. `outY` comes back as the height the block would then be at.
//
// The height has to be carried, and this is why.
//
// It was planned on the terrain once, on the grounds that a search visits
// sixteen thousand block positions with no creature standing at any of them, so
// there is no foot to say which of two surfaces a node means. That is true and it
// cost him the ship: the cargo deck is two units up, and at terrain level the
// whole bay is inside a solid, so every route to the pile came back NO ROUTE. He
// would fetch a crate perfectly well and then have nowhere to take it.
//
// Carrying one height per block through the search is the fix and it is barely
// more program: the frontier already stores where it came from, and now stores
// how high it was when it got there. It is an approximation -- a block has four
// corners and they are not all at one height -- but the exact version is the live
// gait's job, and the gait is what actually walks the route.
bool Walker::blockCanStep(const Ground& hf, const glm::ivec2& block, int dir,
                          float fromY, float& outY) const {
    static const glm::ivec2 kOffsets[4] = { {0,0}, {1,0}, {1,1}, {0,1} };
    glm::ivec2 d  = dirVec(dir);
    glm::ivec2 fl = block + kOffsets[(dir + 1) % 4];
    glm::ivec2 fr = block + kOffsets[(dir + 2) % 4];

    // Each front foot measured from the surface under THAT foot, which is what
    // canAdvance does when he actually takes the step. It used to measure both
    // from the block's one height, and the two rules disagreed in exactly one
    // place: the edge of the ramp, where one front foot is up on the slope and
    // the other is still on the dirt beside it. Averaged, both look reachable.
    // Taken separately, the foot on the dirt cannot make the rise.
    //
    // So the planner handed him routes along the lip of the ramp that he could
    // never walk, and he stood at the foot of it straddling the kerb until the
    // rally gave up. A planner that lies about one step in a hundred is worse
    // than one that refuses: a refusal routes him round to the ramp's foot,
    // which is the way in the kerbs exist to force.
    const float reach = stepReach(hf);
    const float flY = hf.heightAt(fl, fromY, reach);
    const float frY = hf.heightAt(fr, fromY, reach);

    if (!goodStep(hf, fl, fl + d, flY) || !goodStep(hf, fr, fr + d, frY)) {
        return false;
    }

    outY = 0.5f * (hf.heightAt(fl + d, flY, reach) + hf.heightAt(fr + d, frY, reach));
    return true;
}

// Breadth-first over block positions. The field is 128x128, so this is 16k
// nodes and finishes instantly; there is nothing here that needs A*'s heuristic
// and BFS has the advantage of being obviously correct.
//
// It also answers a question greedy never could: whether the crate is reachable
// AT ALL. Past a certain relief the ridge genuinely cuts the field in two, and
// knowing that up front is the difference between giving up immediately and
// grinding at a wall for half a minute.
bool Walker::planPath(const Ground& hf, const glm::ivec2& goalBlock) {
    m_path.clear();
    m_pathNodes.clear();
    m_pathIndex = 0;
    m_pathFailed = false;

    const int span = hf.n() - 1;            // valid block positions per axis

    const glm::ivec2 start = m_block;
    const glm::ivec2 goal  = glm::clamp(goalBlock, glm::ivec2(0), glm::ivec2(span - 1));
    if (start.x < 0 || start.y < 0 || start.x >= span || start.y >= span) {
        m_pathFailed = true;
        return false;
    }

    // Windowed for the same reason Ground::findRoute is: these arrays are one
    // entry per block position, and a level four thousand units across has four
    // million of them. See the note there.
    constexpr int kMargin = 48;
    const glm::ivec2 lo = glm::max(glm::min(start, goal) - kMargin, glm::ivec2(0));
    const glm::ivec2 hi = glm::min(glm::max(start, goal) + kMargin, glm::ivec2(span - 1));
    const int w = hi.x - lo.x + 1;

    auto index = [lo, w](const glm::ivec2& b) { return (b.y - lo.y) * w + (b.x - lo.x); };
    auto valid = [lo, hi](const glm::ivec2& b) {
        return b.x >= lo.x && b.y >= lo.y && b.x <= hi.x && b.y <= hi.y;
    };

    const size_t cells = static_cast<size_t>(w) * (hi.y - lo.y + 1);

    // The search state is a block AND a height, not a block.
    //
    // It used to be a block, with one recorded arrival height apiece -- "how high
    // it was when the search first got there" -- and a block was closed the
    // moment it was reached. That is exactly wrong at the foot of a ramp, which
    // is the one place in this world where a single 2x2 of nodes is legitimately
    // standable at two different heights: on the dirt, and on the slab a few
    // inches above it. Whichever the search happened to reach first won, and the
    // other was lost for good.
    //
    // Which one it reached first depended on how the ramp lay across the lattice,
    // so the ship could be landed at a heading from which nothing could ever
    // route aboard. Twenty of seventy-two headings, measured -- and never the one
    // it happens to land at, which is why it went unseen until the ship could fly
    // and come down facing anywhere.
    //
    // Heights within kSameSurface of each other count as the same state, so this
    // stays a handful of states per block rather than one per float.
    constexpr float kSameSurface = 0.35f;

    struct State {
        glm::ivec2 block;
        float y;
        int parent;    // index into `states`, -1 at the start
        int dir;       // the heading that arrived here
    };
    std::vector<State> states;
    std::vector<std::vector<int>> atCell(cells);

    // A ceiling on the whole thing, so a pathological surface cannot turn this
    // into a search over every float. Four standing heights per block is already
    // more than any geometry here produces.
    const size_t kMaxStates = cells * 4;

    auto known = [&](const glm::ivec2& b, float y) {
        for (int i : atCell[index(b)]) {
            if (std::fabs(states[static_cast<size_t>(i)].y - y) < kSameSurface) return true;
        }
        return false;
    };
    auto push = [&](const glm::ivec2& b, float y, int parent, int dir) {
        states.push_back(State{b, y, parent, dir});
        atCell[index(b)].push_back(static_cast<int>(states.size()) - 1);
        return static_cast<int>(states.size()) - 1;
    };

    // Seeded from where he is actually standing, not from the terrain -- so a
    // route planned while he is already on the deck starts on the deck.
    const float startY = 0.25f * (m_footY[0] + m_footY[1] + m_footY[2] + m_footY[3]);
    push(start, startY, -1, -1);

    int goalState = (start == goal) ? 0 : -1;
    for (size_t head = 0; goalState < 0 && head < states.size(); ++head) {
        const glm::ivec2 b = states[head].block;
        const float here   = states[head].y;

        for (int dir = 0; dir < kDirCount; ++dir) {
            const glm::ivec2 to = b + dirVec(dir);
            if (!valid(to)) continue;

            float landed;
            if (!blockCanStep(hf, b, dir, here, landed)) continue;
            if (known(to, landed)) continue;
            if (states.size() >= kMaxStates) break;

            const int made = push(to, landed, static_cast<int>(head), dir);
            if (to == goal) { goalState = made; break; }
        }
    }

    if (goalState < 0) { m_pathFailed = true; return false; }

    // Walk the parents back, then reverse.
    for (int at = goalState; states[static_cast<size_t>(at)].parent >= 0;
         at = states[static_cast<size_t>(at)].parent) {
        m_path.push_back(states[static_cast<size_t>(at)].dir);
        m_pathNodes.push_back(states[static_cast<size_t>(at)].block);
    }
    std::reverse(m_path.begin(), m_path.end());
    std::reverse(m_pathNodes.begin(), m_pathNodes.end());
    return true;
}

void Walker::assignFetch(const Ground& hf, const glm::vec3& crate,
                         const glm::vec3& storage) {
    m_target = nodeNear(hf, crate);
    m_storageWorld = storage;
    m_carrying = false;
    m_leaving = false;      // a job outranks getting out of the way
    m_waiting = false;
    m_goingHome = false;
    m_taskTicks = 0;

    // No route, no job. Better to say so at once than to grind at a ridge.
    m_hasTask = planPath(hf, m_target - glm::ivec2(1));
    m_activity = m_hasTask ? Activity::Approach : Activity::Wander;
}

void Walker::orderTo(const Ground& hf, const glm::vec3& spot) {
    abandonTask();

    m_stationed = true;
    m_atStation = false;
    m_stationTries = 0;
    m_stationStalled = false;
    m_stationWorld = spot;
    m_activity = Activity::Stationed;

    // Planned, like every trip he makes. If there is no route he stays ordered
    // and keeps trying -- a ramp that is still coming down is the commonest
    // reason, and it will not be a reason for long.
    m_target = nodeNear(hf, spot);
    planPath(hf, blockNearest(hf, spot));
}

void Walker::standDown() {
    m_stationed = false;
    m_atStation = false;
    m_stationStalled = false;
    m_activity = Activity::Wander;
    m_path.clear();
    m_pathNodes.clear();
    m_pathIndex = 0;
}

void Walker::abandonTask() {
    m_path.clear();
    m_pathNodes.clear();
    m_pathIndex = 0;
    m_hasTask = m_carrying = m_leaving = m_waiting = m_goingHome = false;
    m_activity = Activity::Wander;
}

const char* Walker::activityName() const {
    switch (m_activity) {
        case Activity::Approach: return "driving to the crate";
        case Activity::Carry:    return "hauling it to the pile";
        case Activity::Leaving:  return "heading back outside";
        case Activity::Waiting:  return "waiting for the ramp";
        case Activity::Stationed: return "answering the rally";
        default:                 return "wandering";
    }
}

glm::vec3 Walker::cargoPosition(const Ground& hf) const {
    // Riding on his back, so it tilts with the shell -- which means on a slope
    // you can see the crate lean before you notice the machine has.
    return bodyCentre(hf) + bodyUp(hf) * (params.cargoRise * hf.spacing());
}

void Walker::tick(const Ground& hf) {
    for (int i = 0; i < 4; ++i) {
        m_prevFeet[i]  = m_feet[i];
        m_prevFootY[i] = m_footY[i];

        // Asked again even though the foot has not moved, because the surface
        // may have: the ramp swings, and a foot planted on it is standing on a
        // floor that is going somewhere. Without this he keeps the height he had
        // when he put the foot down and sinks through a ramp on its way up.
        m_footY[i] = hf.heightAt(m_feet[i], m_footY[i], stepReach(hf));
    }

    if (m_phase == 0) {
        // ---- choose a heading, then the front pair reaches ------------------
        //
        // He reconsiders EVERY step, not only when blocked. Only turning at
        // walls looks sensible and is useless: simulated over 100k steps it
        // covered 5-11% of the field, because walking straight until something
        // stops you traces lines rather than area, and with every direction
        // equally stale he just circles in a rut.
        //
        // Scoring how much unwalked ground each heading opens over the next few
        // nodes took that to 100%, and still only turns on about 1.6% of steps,
        // so he reads as purposeful rather than twitchy.
        // Hauling: follow the planned route. Each entry is the heading for one
        // step, so "am I going the right way" is an equality test rather than a
        // search, and he cannot wedge in a local minimum because the route was
        // proved to exist before he set off.
        if ((m_hasTask || m_leaving || m_stationed) && m_pathIndex < m_path.size()) {
            int want = m_path[m_pathIndex];

            if (want != m_dir) {
                m_dir = want;
                ++m_turns;
                recomputeFeet(hf);
                markVisited();
                return;
            }
            if (!canAdvance(hf, m_dir)) {
                ++m_stuck;

                // A route is proved when it is planned, and the world does not
                // hold still: the ramp is still coming down, the ship has been
                // republished, the ground under a foot is not what it was. A step
                // that has become impossible has to be ANSWERED. Counting it and
                // returning is what left him at the foot of the ramp until the
                // rally gave up on him.
                //
                // Give it a moment first -- most of these clear on their own,
                // because the thing in the way is a door finishing its travel.
                if (++m_routeStall >= kStallTicks) {
                    m_routeStall = 0;
                    m_path.clear();
                    m_pathNodes.clear();
                    m_pathIndex = 0;
                }
                return;
            }
            m_routeStall = 0;

            m_phase = 1;
            recomputeFeet(hf);
            markVisited();
            return;
        }

        int bestDir = -1;
        int bestScore = -1000;
        int tied = 0;

        for (int k = 0; k < kDirCount; ++k) {
            int cand = (m_dir + k) % kDirCount;
            if (!canAdvance(hf, cand)) continue;

            glm::ivec2 d = dirVec(cand);
            glm::ivec2 fl = corner(cand + 1);
            glm::ivec2 fr = corner(cand + 2);

            int score = 0;
            for (int look = 1; look <= params.lookahead; ++look) {
                if (!visited(fl + d * look)) score += (params.lookahead + 1 - look);
                if (!visited(fr + d * look)) score += (params.lookahead + 1 - look);
            }
            if (cand == m_dir) score += params.straightBias;

            if (score > bestScore) {
                bestScore = score;
                bestDir = cand;
                tied = 1;
            } else if (score == bestScore && params.randomTieBreak) {
                // Reservoir sample, so every tied heading is equally likely
                // however many there are.
                ++tied;
                if (nextRandom() % static_cast<uint32_t>(tied) == 0) bestDir = cand;
            }
        }

        if (bestDir < 0) {
            ++m_stuck;              // boxed in on all four sides
            return;
        }
        if (bestDir != m_dir) {
            m_dir = bestDir;
            ++m_turns;
            recomputeFeet(hf);        // the pivot: same four nodes, roles rotated
            markVisited();
            return;                 // turning costs a tick, which reads as hesitation
        }

        m_phase = 1;
        recomputeFeet(hf);
        markVisited();
        return;
    }

    // ---- phase 2: the back pair closes up ---------------------------------
    // Sliding the block one node along the heading puts the back feet exactly
    // where the front feet were standing a moment ago. Those nodes are known
    // good, so this phase can never fail, and the body ends the cycle the same
    // shape it started.
    m_block += dirVec(m_dir);
    m_phase = 0;
    ++m_steps;
    if (m_pathIndex < m_path.size()) ++m_pathIndex;

    recomputeFeet(hf);
    markVisited();
}

// Is he underneath something, rather than standing on it?
//
// Tested with slack, because the ramp's top face sits a hand's width proud of the
// deck where the two overlap, and a foot on the deck at the doorway is fractionally
// below the ramp beside it without being in any trouble at all.
bool Walker::buried(const Ground& hf) const {
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 w = hf.terrain().worldAt(m_feet[i]);
        if (hf.blocked(w.x, w.z, m_footY[i] + 0.35f, params.bodyRise)) return true;
    }
    return false;
}

// Something arrived on top of him. Get out from under it.
//
// The ramp is the only thing in this world that moves, and it sweeps down through
// ground a creature may perfectly reasonably be standing on. The biped is shoved
// clear; the walker cannot be, because he has no position to nudge -- he has four
// feet on lattice nodes two units apart, and being moved by half a node is not a
// thing that can happen to him. So nothing happened to him at all: he stood there
// while the slab came down, every step refused, and stayed.
//
// He steps out a whole block at a time instead, to the nearest placing that is
// clear. It is a lurch rather than a walk, and it should be -- being under a
// descending ramp is not a situation with a graceful exit.
void Walker::escapeIfBuried(const Ground& hf) {
    if (!buried(hf)) return;

    const glm::ivec2 was = m_block;
    const float reach = stepReach(hf);

    for (int distance = 1; distance <= 4; ++distance) {
        for (int d = 0; d < kDirCount; ++d) {
            const glm::ivec2 candidate = was + dirVec(d) * distance;
            if (candidate.x < 0 || candidate.y < 0 ||
                candidate.x > m_gridN - 2 || candidate.y > m_gridN - 2) continue;

            m_block = candidate;
            recomputeFeet(hf);

            // Reached for from the terrain, because a creature climbing out from
            // under something is not standing on anything worth measuring from.
            for (int i = 0; i < 4; ++i) {
                m_footY[i] = hf.heightAt(m_feet[i], hf.terrain().heightAt(m_feet[i]), reach);
            }

            if (!buried(hf)) {
                for (int i = 0; i < 4; ++i) {
                    m_prevFeet[i]  = m_feet[i];       // no interpolating across a lurch
                    m_prevFootY[i] = m_footY[i];
                }
                m_accum = 0.0f;
                markVisited();

                // And the route he was following is now nonsense.
                //
                // A route here is a list of HEADINGS, not of places: "north,
                // north, east". Followed from the block it was planned at it
                // arrives; followed from a block two nodes away it arrives two
                // nodes away, which near a ship means inside the hull. He would
                // turn onto the first heading, find it refused, and stand at the
                // foot of the ramp for the rest of the rally.
                //
                // So a lurch throws the plan away. Whoever wanted him somewhere
                // will plan again from where he actually is, which is the only
                // place a heading list can be honestly counted from.
                m_path.clear();
                m_pathNodes.clear();
                m_pathIndex = 0;
                return;
            }
        }
    }

    // Nowhere clear within two blocks. Put him back and let him wait it out; the
    // ramp is going somewhere, and when it gets there this will be asked again.
    m_block = was;
    recomputeFeet(hf);
}

void Walker::update(const Ground& hf, float dt) {
    if (m_visited.empty()) return;

    // Parked: the floor is moving and he has no way to move with it. See park().
    if (m_parked) return;

    escapeIfBuried(hf);

    // Called in. Ahead of the leash, the leaving trip and any job, because all
    // three of those are things he does when nobody has told him otherwise.
    if (m_stationed) {
        // Arrival LATCHES. Once he is on his spot he stays on it, and nothing
        // measured per-frame is allowed to take it back.
        //
        // Without the latch he shook. He cannot stand on an arbitrary point --
        // his feet are lattice nodes, so the nearest he can get to a spot is the
        // block containing it, which is up to a node and a half away. Asking him
        // every frame whether he was within some radius of the exact spot, and
        // replanning whenever the route ran out, meant he arrived as close as he
        // could get, was told he had not arrived, replanned, took a step, and did
        // that sixty times a second forever. From outside it reads as a seizure.
        if (m_atStation) {
            m_accum = 0.0f;
            return;
        }

        // The block his station falls in. This -- not the spot -- is the thing he
        // can actually stand on, so it is the thing he is asked about.
        const glm::ivec2 want = blockNearest(hf, m_stationWorld);
        const glm::vec3 at = bodyCentre(hf);
        const float away = glm::length(glm::vec2(at.x - m_stationWorld.x,
                                                 at.z - m_stationWorld.z));

        if (m_block == want || away < 1.6f) {
            m_atStation = true;
            m_accum = 0.0f;
            return;
        }

        if (m_pathIndex >= m_path.size()) {
            // Route spent and still not there. Try again a couple of times -- a
            // ramp still coming down is the usual reason and it passes -- but
            // count the attempts, because a station he cannot quite reach must
            // end with him standing still near it rather than trying forever.
            // Giving up is allowed. Claiming to have arrived is not.
            //
            // This used to latch m_atStation whatever the distance, so a walker
            // who could not plan a route at all stood nine units out on the dirt
            // reporting himself on station, and the rally waited on a hold he was
            // never going to be inside. Only the branch above -- the one that
            // checks he is actually on the block -- may say he arrived. Out here
            // he keeps trying, more slowly, and says he is stalled so that
            // something can show it, and tries again every few seconds in case
            // the thing in his way was a door.
            if (++m_stationTries > 3) {
                m_stationStalled = true;
                if (m_stationTries < 240) { m_accum = 0.0f; return; }
                m_stationTries = 4;       // and round again, every few seconds
            }
            m_target = nodeNear(hf, m_stationWorld);
            planPath(hf, blockNearest(hf, m_stationWorld));
        }

        m_accum += dt * std::max(0.0f, params.stepsPerSecond);
        int guard = 0;
        while (m_accum >= 1.0f && guard < 64) { m_accum -= 1.0f; tick(hf); ++guard; }
        if (guard >= 64) m_accum = 0.0f;
        return;
    }

    // Strayed too far: come back.
    //
    // Checked before the wander rule gets a say, because the wander rule is what
    // took him out there -- it scores a heading by how much unwalked ground it
    // opens, and the unwalked ground is always further from home. Left to it he
    // walks in a straight line for as long as you watch.
    //
    // Planned rather than steered, like every other trip he makes: he has a
    // pathfinder and coming home is exactly the sort of thing it is for. Not a
    // task, so a crate coming up on the way back simply replaces it.
    if (m_homeRadius > 0.0f && !m_hasTask && !m_waiting) {
        const glm::vec3 at = bodyCentre(hf);
        const float out = glm::length(glm::vec2(at.x - m_home.x, at.z - m_home.z));

        if (!m_goingHome && out > m_homeRadius) {
            m_target = nodeNear(hf, m_home);
            if (planPath(hf, m_target - glm::ivec2(1))) {
                m_goingHome = true;
                m_leaving = true;             // rides the same "under my own steam" path
                m_activity = Activity::Leaving;
                m_taskTicks = 0;
            }
        } else if (m_goingHome && out < m_homeRadius * 0.6f) {
            // Well back inside before letting go, or he turns round on the line
            // and spends the rest of the day crossing it.
            m_goingHome = false;
            m_leaving = false;
            m_activity = Activity::Wander;
            m_path.clear();
            m_pathNodes.clear();
            m_pathIndex = 0;
        }
    }

    // Nothing to do and indoors: go outside.
    //
    // He is a field machine, and the hold is a corridor a few nodes wide that he
    // has just walked every node of. His heading rule scores a direction by how
    // much UNWALKED ground it opens, so in there every direction scores nothing,
    // the tie-break picks at random, and he paces the bay for as long as you care
    // to watch -- measured at 153 steps in a minute without once finding the
    // door. He is not stuck; the stuck counter never moves. That is what makes it
    // worse than being stuck, because it reads as deliberation.
    //
    // The route out is planned, not steered toward. He has a pathfinder and the
    // way out is exactly the kind of thing it is for.
    if (!m_hasTask && !m_leaving) {
        glm::vec3 out;
        bool shut = false;
        if (hf.wayOut(bodyCentre(hf), out, shut) && !shut) {
            m_target = nodeNear(hf, out);
            if (planPath(hf, m_target - glm::ivec2(1))) {
                m_leaving = true;
                m_activity = Activity::Leaving;
                m_taskTicks = 0;
            }
        }
    }

    if (m_leaving) {
        const glm::vec2 here = glm::vec2(m_block) + 0.5f;
        const bool arrived = (m_pathIndex >= m_path.size())
                          || glm::length(glm::vec2(m_target) - here) <= params.pickupNodes;

        // Out, or out of patience. Either way he goes back to wandering, and if he
        // is somehow still indoors the next tick will simply plan the trip again.
        if (arrived || ++m_taskTicks > params.giveUpTicks) {
            m_leaving = false;
            m_activity = Activity::Wander;
            m_path.clear();
            m_pathNodes.clear();
            m_pathIndex = 0;
        }
    }

    // Waiting on a door. Re-asked every tick, because the whole point of waiting
    // is that the thing waited for changes.
    if (m_waiting) {
        const Enclosure* door = hf.between(bodyCentre(hf), m_storageWorld);

        // He stops waiting when there is a ROUTE, not when the door reports
        // itself open. Those are not the same instant: the ramp calls itself open
        // as soon as its tip is within stepping height of the ground, which is
        // about four fifths of the way through its travel and a good half second
        // before the far end of it is anywhere a search will cross.
        //
        // Asking once, at that moment, and giving up on a failure -- which is
        // what the carry leg does everywhere else -- meant he waited a patient
        // minute for the door, watched it open, and dropped the crate in the dirt
        // half a second before the way through appeared. So he simply keeps
        // asking; a door that is opening will answer shortly.
        if (!door || door->open) {
            m_target = nodeNear(hf, m_storageWorld);
            if (planPath(hf, m_target - glm::ivec2(1))) {
                m_waiting = false;
                m_activity = Activity::Carry;
            } else if (++m_taskTicks > params.giveUpTicks) {
                // Open, and still no way through after a long time. Something
                // other than the door is wrong; put it down and find other work.
                m_hasTask = false;
                m_carrying = false;
                m_waiting = false;
                m_activity = Activity::Wander;
            }
            m_accum = 0.0f;
            return;
        }

        {
            // Still shut. Walk to the waiting spot if he is not there yet; once
            // he is, stand STILL. Not wander -- wandering would drift him back
            // under the very door he is waiting for, which is how he got hit by
            // it in the first place.
            // Arrived when the route runs out OR he is near enough -- the same
            // pair of tests every other arrival uses, and both are needed. The
            // radius alone leaves him short: the route ends at a block corner
            // that quantises to four and a half units from the mark, so he never
            // satisfies it, never settles, and spends the wait shuffling on the
            // spot. Which reads exactly like being frozen, while being the
            // opposite of it -- measured at a hundred and sixty units of walking
            // per ten seconds without going anywhere.
            const glm::vec3 at = bodyCentre(hf);
            const bool there = (m_pathIndex >= m_path.size())
                            || glm::length(glm::vec2(at.x - m_waitWorld.x,
                                                     at.z - m_waitWorld.z)) < 4.0f;
            if (there) {
                m_accum = 0.0f;
                return;
            }
            m_accum += dt * std::max(0.0f, params.stepsPerSecond);
            int guard = 0;
            while (m_accum >= 1.0f && guard < 64) { m_accum -= 1.0f; tick(hf); ++guard; }
            if (guard >= 64) m_accum = 0.0f;
            return;
        }
    }

    // A door can shut while he is already on his way to it, and until now that
    // was a different situation entirely from finding it shut when he set off:
    // the route was planned and valid when he got it, so he followed it to the
    // ramp, was refused, and ground there with his stuck counter climbing. The
    // wait was only ever entered at the moment of picking a crate up.
    //
    // It is the same predicament either way, so it gets the same answer.
    if (m_hasTask && m_carrying && !m_waiting) {
        const Enclosure* door = hf.between(bodyCentre(hf), m_storageWorld);
        if (door && !door->open) {
            m_waitWorld = door->outside;
            m_waiting = true;
            m_activity = Activity::Waiting;
            m_taskTicks = 0;
            planPath(hf, nodeNear(hf, m_waitWorld) - glm::ivec2(1));
        }
    }

    if (m_hasTask) {
        glm::vec2 here = glm::vec2(m_block) + 0.5f;
        float distance = glm::length(glm::vec2(m_target) - here);

        // Arrived when the route is spent, or near enough by distance.
        bool arrived = (m_pathIndex >= m_path.size()) || distance <= params.pickupNodes;
        if (arrived) {
            if (!m_carrying) {
                // Over the crate: it clamps on, and the goal becomes the pile.
                m_carrying = true;
                m_activity = Activity::Carry;
                m_target = nodeNear(hf, m_storageWorld);
                if (!planPath(hf, m_target - glm::ivec2(1))) {
                    // No route. There are two very different reasons for that and
                    // they want opposite answers.
                    //
                    // A SHUT DOOR is temporary and somebody else can open it, so
                    // standing about holding the crate is exactly right -- he has
                    // no hands to work a control with and nothing better to do.
                    // WHERE he waits is not free, though: the ramp swings down
                    // through the ground in front of its own doorway, so he waits
                    // at the muster point, which is sited clear of that on purpose.
                    //
                    // Anything ELSE -- a ridge, a pocket the search cannot cross --
                    // is not going to change, and he should put the crate down and
                    // go find something he can actually do.
                    const Enclosure* door = hf.between(bodyCentre(hf), m_storageWorld);
                    if (door && !door->open) {
                        m_waitWorld = door->outside;
                        m_waiting = true;
                        m_activity = Activity::Waiting;
                        m_taskTicks = 0;
                        planPath(hf, nodeNear(hf, m_waitWorld) - glm::ivec2(1));
                    } else {
                        m_hasTask = false;
                        m_carrying = false;
                        m_activity = Activity::Wander;
                    }
                }
            } else {
                // Delivered. Letting go matters as much as picking up: without
                // clearing this he ends the task, goes back to wandering, and
                // carries the crate around on his back forever.
                m_carrying = false;
                m_hasTask = false;
                m_activity = Activity::Wander;
            }
        }

        if (++m_taskTicks > params.giveUpTicks) abandonTask();
    }

    m_accum += dt * std::max(0.0f, params.stepsPerSecond);

    int guard = 0;
    while (m_accum >= 1.0f && guard < 64) {
        m_accum -= 1.0f;
        tick(hf);
        ++guard;
    }
    if (guard >= 64) m_accum = 0.0f;   // fell far behind; do not spiral
}

void Walker::parkIn(const Ground& hf, const glm::vec3& origin,
                    const glm::vec3& right, const glm::vec3& fwd) {
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 d = footWorld(hf, i) - origin;
        m_parkFoot[i] = glm::vec3(glm::dot(d, right), d.y, glm::dot(d, fwd));
    }
    m_parked = true;
    m_accum = 0.0f;
    parkFollow(origin, right, fwd);
}

void Walker::parkFollow(const glm::vec3& origin, const glm::vec3& right,
                        const glm::vec3& fwd) {
    m_parkOrigin = origin;
    m_parkRight = right;
    m_parkFwd = fwd;
}

void Walker::unpark(const Ground& hf, const glm::vec3& origin,
                    const glm::vec3& right, const glm::vec3& fwd) {
    if (!m_parked) return;

    // Where the carried feet actually are now, in the frame he arrived in.
    parkFollow(origin, right, fwd);

    glm::vec3 centre(0.0f);
    float held[4];
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 at = footWorld(hf, i);   // still the parked branch
        centre += at;
        held[i] = at.y;
    }
    centre *= 0.25f;

    // The block whose middle is nearest that, which is the nearest thing to where
    // he was standing that he is able to stand on.
    m_parked = false;
    m_block = glm::clamp(blockNearest(hf, centre), glm::ivec2(0),
                         glm::ivec2(m_gridN - 2));
    m_phase = 0;
    m_accum = 0.0f;

    // Seeded from the heights he was carried at, so recomputeFeet measures the
    // step down onto the deck from the deck rather than from the dirt below it --
    // he is standing in a hold, and the hold's floor is what he should find.
    for (int i = 0; i < 4; ++i) {
        m_feet[i] = corner(i);
        m_footY[i] = held[i];
    }
    recomputeFeet(hf);
    for (int i = 0; i < 4; ++i) {
        m_prevFeet[i]  = m_feet[i];      // no interpolation across the handover
        m_prevFootY[i] = m_footY[i];
    }
    markVisited();

    // And whatever route he had is counted from a block on another continent.
    m_path.clear();
    m_pathNodes.clear();
    m_pathIndex = 0;
    m_routeStall = 0;
}

glm::vec3 Walker::footWorld(const Ground& hf, int i) const {
    // Parked: drawn in the carrying frame rather than on the ground. His feet are
    // lattice indices and a moving deck is not on the lattice, so the drawing is
    // what moves.
    if (m_parked) {
        const glm::vec3& L = m_parkFoot[i];
        return m_parkOrigin + m_parkRight * L.x + glm::vec3(0.0f, L.y, 0.0f) + m_parkFwd * L.z;
    }

    const glm::ivec2& from = m_prevFeet[i];
    const glm::ivec2& to   = m_feet[i];

    // The node gives x and z; the height comes from what the foot was decided to
    // be standing on, not from asking the world again. Re-asking here would
    // answer for a foot at rest instead of one halfway through a step, and would
    // put a foot swinging from the deck to the ground on whichever surface the
    // node happened to prefer for the whole swing.
    glm::vec3 a = hf.terrain().worldAt(from); a.y = m_prevFootY[i];
    glm::vec3 b = hf.terrain().worldAt(to);   b.y = m_footY[i];

    if (from == to) return a;

    float t = std::clamp(m_accum, 0.0f, 1.0f);
    glm::vec3 p = a + (b - a) * t;
    p.y += std::sin(t * kPi) * params.legLiftFrac * hf.spacing();
    return p;
}

glm::vec3 Walker::bodyCentre(const Ground& hf) const {
    glm::vec3 sum(0.0f);
    for (int i = 0; i < 4; ++i) sum += footWorld(hf, i);
    glm::vec3 c = sum * 0.25f;
    c += bodyUp(hf) * (params.bodyHoverFrac * hf.spacing());
    return c;
}

// Taken from the feet rather than from the terrain, so the body tilts with what
// he is actually standing on -- including mid-stretch, when the front pair is a
// node further up the slope than the back pair.
glm::vec3 Walker::bodyUp(const Ground& hf) const {
    glm::vec3 fl = footWorld(hf, 0), fr = footWorld(hf, 1);
    glm::vec3 br = footWorld(hf, 2), bl = footWorld(hf, 3);

    glm::vec3 n = glm::cross(fr - fl, bl - fl) + glm::cross(bl - br, fr - br);
    if (glm::dot(n, n) < 1e-8f) return glm::vec3(0, 1, 0);

    n = glm::normalize(n);
    return n.y < 0.0f ? -n : n;
}

glm::vec3 Walker::bodyForward(const Ground& hf) const {
    glm::vec3 front = (footWorld(hf, 0) + footWorld(hf, 1)) * 0.5f;
    glm::vec3 back  = (footWorld(hf, 2) + footWorld(hf, 3)) * 0.5f;

    glm::vec3 f = front - back;
    if (glm::dot(f, f) < 1e-8f) {
        glm::ivec2 d = dirVec(m_dir);
        return glm::vec3(d.x, 0.0f, d.y);
    }
    return glm::normalize(f);
}

} // namespace tessara
