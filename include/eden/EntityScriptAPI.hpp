#pragma once

// The entity-script API: the extern "C" surface compiled HEIDIC entity scripts
// (heidic_v2 --script) call into. "self" is implicit — the engine sets the
// current entity before invoking a script function, and every self_* call
// operates on it. Keep this list in sync with ENTITY_SCRIPT_PRELUDE in the
// HEIDIC compiler (HEIDIC/src/main.rs).
//
// The functions are exported from the host executable (ENABLE_EXPORTS) so a
// dlopen'd script .so resolves them at load time.

namespace eden {

class Entity;

// Set/get the entity the self_* API operates on. The dispatcher wraps every
// script call: setCurrentScriptEntity(e); fn(...); setCurrentScriptEntity(nullptr);
void setCurrentScriptEntity(Entity* e);
Entity* currentScriptEntity();

} // namespace eden

extern "C" {
    // Movement
    void self_translate(float dx, float dy, float dz);
    void self_set_position(float x, float y, float z);
    float self_get_x();
    float self_get_y();
    float self_get_z();
    // Rotation (degrees)
    void self_rotate_y(float degrees);
    void self_set_rotation(float rx, float ry, float rz);
}
