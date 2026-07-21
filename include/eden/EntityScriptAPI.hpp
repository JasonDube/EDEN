#pragma once

// The entity-script API: the extern "C" surface compiled HEIDIC entity scripts
// (heidic_v2 --script) call into. "self" is implicit — the dispatcher points
// the API at the scripted thing's Transform before invoking a script function,
// and every self_* call operates on it. Works for anything with an
// eden::Transform (ActionSystem entities, editor SceneObjects). Keep this list
// in sync with ENTITY_SCRIPT_PRELUDE in the HEIDIC compiler (HEIDIC/src/main.rs).
//
// The functions are exported from the host executable (ENABLE_EXPORTS) so a
// dlopen'd script .so resolves them at load time.

namespace eden {

class Entity;
class Transform;

// Point the self_* API at a transform (nullptr = no target; calls become no-ops).
// The dispatcher wraps every script call:
//   setCurrentScriptTransform(&thing.getTransform()); fn(dt); setCurrentScriptTransform(nullptr);
void setCurrentScriptTransform(Transform* t);
Transform* currentScriptTransform();

// Convenience for ActionSystem entities.
void setCurrentScriptEntity(Entity* e);

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
