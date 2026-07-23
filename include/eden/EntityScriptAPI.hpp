#pragma once

#include <functional>

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
class SceneObject;

// Point the self_* API at a transform (nullptr = no target; calls become no-ops).
// The dispatcher wraps every script call:
//   setCurrentScriptTransform(&thing.getTransform()); fn(dt); setCurrentScriptTransform(nullptr);
void setCurrentScriptTransform(Transform* t);
Transform* currentScriptTransform();

// Convenience for ActionSystem entities.
void setCurrentScriptEntity(Entity* e);

// For SceneObject-level API calls (animation etc.), the dispatcher also points
// the API at the object itself. Optional — transform-only targets leave it null.
void setCurrentScriptObject(SceneObject* o);
SceneObject* currentScriptObject();

// The player's world position, for the self_*_player verbs. The host sets this
// once per frame before ticking scripts (in play mode it's the camera).
void setScriptPlayerPosition(float x, float y, float z);

// Whether the shared bot server (AI Backend) is up. Host refreshes each frame;
// agent_online() ANDs it with the per-bot switch so a robot only comes alive
// once its server is genuinely ready.
void setScriptBackendOnline(bool online);

// Player input, set by the host once per frame (play mode) so scripted
// controllers can read WASD / jump / run / mouse-look. moveX/moveZ are -1..1
// (strafe / forward), buttons are 0 or 1, mouse deltas are pixels this frame.
void setScriptInput(float moveX, float moveZ, float jump, float run,
                    float mouseDx, float mouseDy);

// Host hook for self_play_anim: the app owns the skinned-model renderer, so it
// installs how "play this animation on this object" actually happens. The hook
// should no-op when the requested animation is already playing (scripts call
// self_play_anim every tick).
void setScriptPlayAnimHook(std::function<void(SceneObject&, const char*)> hook);

// Host hook for the ground-height verbs: the app owns the terrain, so it supplies
// "surface height at (x, z)". Lets a walking script (e.g. a companion following
// over hills) stay on the terrain instead of floating/sinking. No hook set =
// ground verbs leave Y unchanged.
void setScriptGroundHeightHook(std::function<float(float x, float z)> hook);

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
    // Animation: play the named clip (looped) on this object's skinned model.
    // Safe to call every tick — switching only happens when the name changes.
    void self_play_anim(const char* name);
    // World/agent state (not tied to "self").
    float agent_online();                      // 1.0 if THIS agent is switched on AND its server is up
    // Player-relative verbs (horizontal / XZ plane, in feet). "self" is this entity.
    float self_dist_to_player();               // feet to the player
    void  self_face_player();                  // turn (yaw) to look at the player
    void  self_move_toward_player(float step); // step `step` feet toward the player
    // Facing-relative movement (horizontal): move along where this entity faces,
    // for a player/creature controller. self_rotate_y turns the facing first.
    void  self_move_forward(float dist);       // + = forward, - = back (own facing)
    void  self_move_right(float dist);         // + = right strafe, - = left
    // Terrain: keep a ground creature on the surface as it moves over hills.
    float self_ground_y();                     // terrain height at this entity's (x,z)
    void  self_snap_to_ground();               // set this entity's Y to the ground
    // Player input (for scripted controllers). Valid in play mode; 0 otherwise.
    float input_move_x();                      // -1 (A/left) .. +1 (D/right)
    float input_move_z();                      // -1 (S/back) .. +1 (W/forward)
    float input_jump();                        // 1 while jump (space) held, else 0
    float input_run();                         // 1 while run (ctrl) held, else 0
    float mouse_dx();                          // look delta X this frame (pixels)
    float mouse_dy();                          // look delta Y this frame (pixels)
}
