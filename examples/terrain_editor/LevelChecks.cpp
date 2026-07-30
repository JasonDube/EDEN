#include "LevelChecks.hpp"

#include <cstdio>
#include <vector>

namespace {

int  g_failed = 0;
bool g_verbose = false;

void report(const char* name, bool ok, const std::string& detail) {
    if (!ok) ++g_failed;
    if (!ok || g_verbose) {
        std::printf("  %-38s %-4s %s\n", name, ok ? "ok" : "FAIL", detail.c_str());
    }
}

std::string count(const char* what, std::size_t n) {
    return std::string(what) + " = " + std::to_string(n);
}

// One channel of state a level can carry: how to dirty it, how to see it, and
// what to call it. Written as a table so that adding a new leaky system is one
// row rather than three edits in three places.
struct Channel {
    const char* name;
    std::function<void()> dirty;                        // may be null if unhookable
    std::function<bool(const LevelStateReport&)> present; // true = still carrying it
    std::function<std::string(const LevelStateReport&)> describe;
};

std::vector<Channel> channelsFor(const LevelCheckHooks& h) {
    return {
        {"game module",
         h.loadAGameModule,
         [](const LevelStateReport& r) { return !r.gameModuleName.empty(); },
         [](const LevelStateReport& r) {
             return r.gameModuleName.empty() ? std::string("none")
                                             : "module = " + r.gameModuleName;
         }},
        {"tribe sim",
         h.enableTribeSim,
         [](const LevelStateReport& r) { return r.tribeSimEnabled; },
         [](const LevelStateReport& r) {
             return std::string("enabled = ") + (r.tribeSimEnabled ? "true" : "false");
         }},
        {"grass",
         h.enableGrass,
         [](const LevelStateReport& r) { return r.grassEnabled || r.grassBlades > 0; },
         [](const LevelStateReport& r) {
             return std::string("enabled = ") + (r.grassEnabled ? "true" : "false") +
                    ", " + count("blades", r.grassBlades);
         }},
        {"hotbar",
         h.occupyAHotbarSlot,
         [](const LevelStateReport& r) { return r.occupiedSlots > 0; },
         [](const LevelStateReport& r) {
             return count("occupied slots", static_cast<std::size_t>(r.occupiedSlots));
         }},
        {"scene objects",
         h.addASceneObject,
         [](const LevelStateReport& r) { return r.sceneObjects > 0; },
         [](const LevelStateReport& r) { return count("objects", r.sceneObjects); }},
        {"spawn point",
         h.setASpawnPoint,
         [](const LevelStateReport& r) { return r.hasSpawnPoint; },
         [](const LevelStateReport& r) {
             return std::string("hasSpawnPoint = ") + (r.hasSpawnPoint ? "true" : "false");
         }},
    };
}

// Fill the level with junk, and INSIST it took.
//
// This half matters more than the wipe half. If a dirty hook quietly does
// nothing -- a renderer not ready, a model file gone missing, a system renamed
// out from under it -- then the wipe below has nothing to clear and the whole
// check passes while testing air. So every channel that fails to dirty is a
// failure in its own right, reported by name.
void dirtyEverything(const std::vector<Channel>& channels, const LevelCheckHooks& h) {
    for (const auto& c : channels) {
        if (!c.dirty) {
            report((std::string("dirty: ") + c.name).c_str(), false,
                   "no hook -- this channel is NOT being checked");
            continue;
        }
        c.dirty();
        const LevelStateReport after = h.snapshot();
        report((std::string("dirty: ") + c.name).c_str(), c.present(after),
               c.describe(after));
    }
}

// Wipe, then look. `what` names the template so a failure says which one.
void expectEmptyAfter(const char* what, const std::vector<Channel>& channels,
                      const LevelCheckHooks& h, const std::function<void()>& wipe) {
    if (!wipe) {
        report(what, false, "no hook");
        return;
    }
    wipe();
    const LevelStateReport r = h.snapshot();
    for (const auto& c : channels) {
        // A template is allowed to place its OWN props and its own spawn point --
        // that is what a template is. Anything else it inherited is not.
        const bool templatesMayHaveThese =
            (std::string(c.name) == "scene objects" || std::string(c.name) == "spawn point");
        if (templatesMayHaveThese && std::string(what) != "newLevel") continue;

        report((std::string(what) + ": " + c.name).c_str(), !c.present(r), c.describe(r));
    }
}

} // namespace

int runEmptyLevelChecks(const LevelCheckHooks& hooks, bool verbose) {
    g_failed = 0;
    g_verbose = verbose;

    std::printf("\n=== empty-level checks ===\n");

    if (!hooks.snapshot) {
        std::printf("  no snapshot hook -- nothing can be checked\n");
        return 1;
    }

    const std::vector<Channel> channels = channelsFor(hooks);

    // A plain New Level must forget everything.
    dirtyEverything(channels, hooks);
    expectEmptyAfter("newLevel", channels, hooks, hooks.newLevel);

    // And so must the templates, which route through newLevel() and then place
    // their own props. Grass is the reason this half exists: the Terrain Cell
    // template used to switch it on itself, so newLevel() was clean and the
    // thing you actually pressed was not.
    dirtyEverything(channels, hooks);
    expectEmptyAfter("Foundation", channels, hooks, hooks.newFoundationLevel);

    dirtyEverything(channels, hooks);
    expectEmptyAfter("Terrain Cell", channels, hooks, hooks.newTerrainCellLevel);

    // Leave the editor on a clean level rather than on the last template's, so
    // running the checks does not decide what you are looking at afterwards.
    if (hooks.newLevel) hooks.newLevel();

    std::printf("=== %d failed ===\n\n", g_failed);
    return g_failed;
}
