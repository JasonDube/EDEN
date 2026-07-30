#pragma once

// Registering TESSARA:AXIOM with the host, without the host seeing any of it.
//
// This exists so main.cpp does not #include TessaraModule.hpp. That include
// pulled in Ship, Biped, Walker and Ground, so touching any one of them
// recompiled the editor's whole 33k-line translation unit -- fifty-one seconds,
// paid at the next launch by whoever pressed the desktop icon. Declaring one
// function costs a relink instead.
//
// Call once at startup, before a level asks for the module by name.

namespace tessara {
void registerGameModule();
}
