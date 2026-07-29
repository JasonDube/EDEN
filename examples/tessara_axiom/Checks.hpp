#pragma once

namespace tessara {

// Headless checks on the ship: can things get aboard it, and can they be stopped
// by it. Companion to the gait sweep in gait_sim.cpp and run from the same
// binary, for the same reason -- neither can be judged by looking.
//
// Returns the number that failed.
int runShipChecks(bool verbose);

} // namespace tessara
