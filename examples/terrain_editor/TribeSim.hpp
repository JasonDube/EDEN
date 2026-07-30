#pragma once

// The tribe sim -- inhabitants, tribes, camps, resources, predators, the day's
// reckoning and the news feed -- as a thing you can put down.
//
// WHY THIS FILE EXISTS. All of it used to live directly in terrain_editor's
// main.cpp, and the cost was not stylistic. It ran in EVERY level, editor and
// play alike; it bound L/U/O/Y on top of keys the editor already used (U was
// both "drop water" and "toggle the 5-ft grid", L was both "spawn inhabitant"
// and the grass brush, so one press did two unrelated things); its state was
// never cleared anywhere in the binary, so it leaked across New Level; and it
// wound the world clock forward in levels that had deliberately parked it. None
// of those are bugs you can have in a system that has to be asked for.
//
// So it is asked for. The host owns the world; this owns the sim, and reaches
// the world through Host below rather than through the editor's members. That
// list of pointers IS the coupling, all of it, in one place you can read -- and
// it is what a GameModule seam has to grow to carry this, when it does.

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace eden {
class Camera;
class ModelRenderer;
class SceneObject;
class SkinnedModelRenderer;
class Terrain;
class Window;
}

// Everything the sim needs from whoever is hosting it.
//
// Raw pointers because the host outlives the sim by construction (it owns it),
// and callbacks for the three things that are host POLICY rather than host data:
// how a skinned model gets imported, how an object gets destroyed, and how the
// clock is written. A sim that reached in and did those itself would be making
// decisions that belong to whatever is running the level.
struct TribeSimHost {
    std::vector<std::unique_ptr<eden::SceneObject>>* sceneObjects = nullptr;
    eden::Terrain*              terrain      = nullptr;
    eden::Camera*               camera       = nullptr;
    eden::ModelRenderer*        models       = nullptr;
    eden::SkinnedModelRenderer* skinned      = nullptr;
    eden::Window*               window       = nullptr;

    // The world clock, read for dusk and written when the sim needs the day to
    // turn. Pointers rather than copies: the host keeps owning them.
    const float* gameTimeMinutes = nullptr;
    float*       gameTimeScale   = nullptr;

    // True in EDEN OS levels, which have no terrain to live on.
    const bool*  isEdenOSLevel   = nullptr;

    std::function<void(const std::string&)> importSkinnedModel;
    std::function<void(const std::string&)> destroyObjectNamed;
    std::function<std::string(float)>       formatGameTime;

    // Screen x of the right-hand edge of the host's hotbar. The inhabitant panel
    // sits off it, and used to compute it from the host's own TOOLBAR_SLOT_COUNT
    // -- which is the sim knowing the editor's toolbar layout. It needs the edge,
    // not the slot count, so that is what it asks for.
    std::function<float()> hotbarRightEdge;
};

class TribeSim {
public:
    void setHost(const TribeSimHost& host) { m_host = host; }

    // Off unless a level asks for it. This is the whole point of the file.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    // Forget every inhabitant, tribe, camp, source and predator, and hand the
    // clock back. Called on New Level -- the leak that made a fresh level come
    // up remembering a tribe whose bodies had just been deleted.
    void reset();

    // The spawn keys. The host says whether it is in play mode; the sim decides
    // whether it cares. Returns nothing -- these are commands, not queries.
    void handleInput(bool isPlayMode, bool guiWantsKeys, bool guiWantsMouse);

    // One frame of sim. Winds the clock while inhabitants exist and puts it back
    // when the level stops playing.
    void update(float deltaTime, bool isPlayMode);

    // Play-mode overlays, in the order the host drew them.
    void renderTags();
    void renderPanel();
    void renderNewsFeed();

private:
    // ---- the world, borrowed ------------------------------------------------
    TribeSimHost m_host;
    bool m_enabled = false;

    std::vector<std::unique_ptr<eden::SceneObject>>& sceneObjects() { return *m_host.sceneObjects; }

    // ---- creatures ----------------------------------------------------------
    // A creature that lives on a terrain, wanders a patch, and stays stuck to the
    // ground as it moves.
    struct InhabitantState {
        bool init = false;          // lazily seeded from the object's current pose
        float footLift = 0.0f;      // origin-to-feet offset, so feet stay on the terrain
        glm::vec3 home{0.0f};       // (kept for reference; wander now centres on the tribe)
        glm::vec3 target{0.0f};     // current walk goal
        bool walking = false;
        float timer = 0.0f;         // countdown to the next state change
        std::string clip;           // current anim clip (avoid re-triggering every frame)
        bool greeting = false;      // paused, facing a neighbour it just noticed
        float greetCd = 0.0f;       // cooldown before it will greet again
        int   tribe = 0;            // which tribe it belongs to (index into m_tribeCenters)
        // Needs (the deterministic state-sim).
        float thirst = 0.0f;        // 0 = sated → 1 = parched
        float thirstRate = 0.02f;   // per-second rise, randomised per creature
        bool  seekingWater = false; // heading for water (hysteresis: on at THIRSTY, off at SATED)
        bool  drinking = false;
        float hunger = 0.0f;        // 0 = fed → 1 = starving
        float hungerRate = 0.015f;
        bool  seekingFood = false;
        bool  eating = false;
        // Economy: anyone not personally needy works — fill up at a source, carry it
        // home, drop it in the tribe's store. That store is what they live on once
        // the pond runs dry, which is what makes hauling survival and not busywork.
        bool  hauling = false;      // committed to a gather-and-return trip
        bool  gathering = false;    // stood at the source, filling up
        int   carryType = -1;       // 0 = water, 1 = food, -1 = empty handed
        float carry = 0.0f;         // units in hand
        std::string targetSrc;      // the source we SET OUT for — stuck with till it dies
        bool  panicked = false;     // running from a predator (sticky, see kSafeR)
        // Temperament. Work costs legs, and legs need sitting down again — but how
        // soon and how long is a fixed trait, so the tribe sorts itself into grafters
        // and layabouts without anyone authoring either.
        float laziness = 0.5f;      // 0 = tireless grafter … 1 = born layabout
        float fatigue = 0.0f;       // 0 = fresh → 1 = spent
        bool  resting = false;      // sat down until recovered (hysteresis)
        // ── Skills ──────────────────────────────────────────────────────────
        // Aptitude, fixed for life, and some of them are genuinely hopeless at it.
        // One skill for now; it splits into buried / fruit / mushroom gathering
        // later, at which point this becomes a small table rather than a float.
        float skillForage = 0.5f;   // 0 = can't find a berry … 1 = gifted
        float skillSocial = 0.5f;   // 0 = charmless … 1 = magnetic
        float greetOutcome = 0.0f;  // how the last contest went, for the floating tag
        std::string displayName;    // what the news feed calls her
        // What you forage is YOURS. Water is communal and goes to the camp crate;
        // food is not, which is what makes going hungry a thing you have to ask
        // someone else to fix.
        float foodStock = 0.0f;     // personal larder, in units
        bool  foraging = false;     // out working a forage ground
        float foragedToday = 0.0f;  // found since the last reckoning (for the feed)
        float forageTimeToday = 0.0f; // seconds spent out there today
        std::string stateTag = "#content"; // dominant state, shown above the head
    };
    std::unordered_map<std::string, InhabitantState> m_inhabitants; // keyed by object name
    std::string m_selectedInhabitant;   // the one being inspected (empty = nobody)

    // ── Bonds ───────────────────────────────────────────────────────────────
    // One symmetric number per pair, -1 (hostile) … +1 (close). Keyed by the two
    // names in sorted order. Small tribes, so N-squared is nothing.
    std::unordered_map<std::string, float> m_bonds;

    // ── The day, and the reckoning at the end of it ─────────────────────────
    // The engine clock sits frozen at noon by default; the tribe needs it turning to
    // have a dusk to be reckoned at, so the sim winds it and puts it back after.
    static constexpr float kSimDayScale = 4.8f;   // game minutes per real second → 5 min/day
    static constexpr float kDuskMinutes = 1140.0f; // 19:00
    bool  m_simDroveClock = false;
    float m_prevSimMinutes = -1.0f;
    int   m_dayNumber = 0;
    std::vector<std::pair<bool, std::string>> m_newsFeed;  // {isDayHeader, line}
    int m_inhabitantCounter = 0;
    int m_waterCounter = 0;
    int m_foodCounter = 0;

    // Each tribe clusters around its own centre. A new inhabitant joins the nearest
    // tribe if close enough, else founds a new one — so you make a second tribe just
    // by spawning somewhere else on the map.
    std::vector<glm::vec3> m_tribeCenters;
    std::vector<int>       m_tribeCounts;   // running counts for each centre's average

    // ── The tribe economy ───────────────────────────────────────────────────
    // What a tribe has hauled home. Per-tribe, so two tribes drawing on one pond
    // are genuinely competing for the same finite water.
    struct TribeStore { float water = 0.0f; float food = 0.0f; };
    std::vector<TribeStore>  m_tribeStores;
    std::vector<std::string> m_tribeCamps;   // stockpile object name, per tribe

    // Sources are FINITE. Every drink and every armful comes out of the pond/bush,
    // which visibly shrinks as it's drawn down and trickles back over time. Without
    // depletion there's no economy — just errands.
    struct ResourceState {
        bool  init = false;
        float amount = 0.0f;
        float capacity = 0.0f;
        float regen = 0.0f;             // units per second trickling back
        glm::vec3 fullScale{1.0f};      // scale when brimming, to shrink from
        // Drained sources need to actually refill a bit before they're worth walking
        // to again. Without this gap, regen and the last sip trade the source across
        // the threshold every frame and the whole tribe buzzes at the water's edge.
        bool exhausted = false;
    };
    std::unordered_map<std::string, ResourceState> m_resources; // keyed by object name

    // ── Predators (the lion) ────────────────────────────────────────────────
    // A hunter that stalks inhabitants and turns the watering hole into a risk —
    // also a population valve for the rat cage. Inhabitants flee it (fear beats
    // thirst/hunger).
    struct PredatorState {
        bool init = false;
        float footLift = 0.0f;
        glm::vec3 home{0.0f};
        glm::vec3 target{0.0f};
        bool prowling = false;   // has a wander target
        float timer = 0.0f;
        float fedCd = 0.0f;      // resting/digesting after a kill — won't hunt
    };
    std::unordered_map<std::string, PredatorState> m_predators; // keyed by object name
    int m_lionCounter = 0;
    std::vector<std::string> m_pendingKills; // inhabitants caught this frame, removed after the loops

    // ---- the sim itself, unchanged from where it was ------------------------
    void spawnInhabitant();
    void spawnStockpile(int tribe, const glm::vec3& at);
    static std::string bondKey(const std::string& a, const std::string& b);
    float bondValue(const std::string& a, const std::string& b) const;
    static const char* bondWord(float v);
    bool objectPos(const std::string& name, glm::vec3& out);
    bool tribeCamp(int tribe, glm::vec3& out);
    void updateResources(float deltaTime);
    void updateInhabitants(float deltaTime);
    void spawnWater();
    void spawnFood();
    void spawnLion();
    void updatePredators(float deltaTime);
    static std::string nameFor(const std::string& objName);
    void updateReckoning(float deltaTime);
    void runReckoning();
    void runSharing();
    void pickInhabitantAtCursor();
};
