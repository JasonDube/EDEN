#pragma once

// Does a new level actually come up empty?
//
// WHY THIS EXISTS. Four separate things were leaking into fresh levels and not
// one of them was visible in the code:
//
//   - the game module survived New Level, so a fresh terrain arrived with a
//     cargo ship, a ladder, a walker and a crate loop on it;
//   - the tribe sim's state was never cleared ANYWHERE in the binary, so a new
//     level held records naming bodies the wipe had just deleted;
//   - grass switched itself on with the Terrain Cell template;
//   - the hotbar came up holding EDEN OS's inventory.
//
// Each was found by looking at the screen and going "what is that doing there".
// Each would come straight back the next time something new grew a piece of
// global state, because nothing anywhere asserts that a new level is new.
//
// WHAT MAKES THIS A CHECK AND NOT A REASSURANCE. Asking a freshly-booted editor
// whether it is empty proves nothing -- it was already empty. So the check
// DIRTIES every channel first, verifies the dirt actually took, and only then
// wipes and looks. A channel that cannot be dirtied is reported as a failure
// too, because a check that silently tests nothing is worse than no check: it
// reports "ok" forever.
//
// The dirt is applied through hooks rather than reached for directly, so the
// sequence and the assertions live here -- out of main.cpp -- and the host only
// says HOW each individual poke is performed.

#include <cstddef>
#include <functional>
#include <string>

// Everything a level can be carrying, in one snapshot.
//
// Add a field here the moment a system grows state that survives a wipe. The
// point is that the list is in one place and something reads it, rather than
// being four hand-written "and forget this too" lines in newLevel() that nobody
// counts.
struct LevelStateReport {
    std::string gameModuleName;      // empty = no module loaded
    bool        tribeSimEnabled = false;
    bool        grassEnabled    = false;
    std::size_t grassBlades     = 0;
    int         occupiedSlots   = 0;   // hotbar
    std::size_t sceneObjects    = 0;
    std::size_t aiNodes         = 0;
    bool        hasSpawnPoint   = false;
    bool        waterVisible    = false;
    bool        testLevel       = false;
    bool        spaceLevel      = false;
    bool        edenOSLevel     = false;
};

// How the check pokes the editor. Each is one small thing the host knows how to
// do and this file does not.
struct LevelCheckHooks {
    std::function<LevelStateReport()> snapshot;

    // Dirtying. Each must make its own field of the snapshot non-empty.
    std::function<void()> loadAGameModule;
    std::function<void()> enableTribeSim;
    std::function<void()> enableGrass;
    std::function<void()> occupyAHotbarSlot;
    std::function<void()> addASceneObject;
    std::function<void()> setASpawnPoint;

    // The wipes under test.
    std::function<void()> newLevel;
    std::function<void()> newFoundationLevel;
    std::function<void()> newTerrainCellLevel;
};

// Returns the number of failed checks. 0 means a new level is genuinely new.
int runEmptyLevelChecks(const LevelCheckHooks& hooks, bool verbose);
