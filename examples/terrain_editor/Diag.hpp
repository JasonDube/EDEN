#pragma once

// One switch for the investigation prints.
//
// [Place] [Ground] [BodyReg] [Vessel] [FlightGlitch] all check this before
// speaking. They earned their keep -- between them they convicted the 20 m
// placement bug, the slab-standing mystery, and the flight hiccup -- but they
// chatter, and an instrument you cannot silence gets deleted the day it
// annoys someone, which is how the next bug takes three days instead of one.
//
// Toggled from the M panel ("Diagnostics"), next to the sim checkboxes.
// DELIBERATELY not persisted: it resets to quiet every launch, because turning
// it on is a debugging act, and a forgotten switch left on becomes wallpaper.
inline bool g_diagnostics = false;
