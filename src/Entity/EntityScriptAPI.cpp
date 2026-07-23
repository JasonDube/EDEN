#include <eden/EntityScriptAPI.hpp>
#include <eden/Entity.hpp>
#include <eden/Transform.hpp>
#include "Editor/SceneObject.hpp"   // getLocalBounds() — feet-aware ground snap
#include <cmath>

namespace eden {

// The transform the self_* API acts on. thread_local so a future threaded
// script dispatcher can't cross-talk; today everything runs on the main thread.
static thread_local Transform* t_currentScriptTransform = nullptr;

void setCurrentScriptTransform(Transform* t) { t_currentScriptTransform = t; }
Transform* currentScriptTransform() { return t_currentScriptTransform; }

void setCurrentScriptEntity(Entity* e) {
    t_currentScriptTransform = e ? &e->getTransform() : nullptr;
}

static thread_local SceneObject* t_currentScriptObject = nullptr;
static std::function<void(SceneObject&, const char*)> s_playAnimHook;
static std::function<float(float, float)> s_groundHeightFn;   // host: terrain height at (x,z)
static glm::vec3 s_playerPos{0.0f};   // set by the host each frame

void setCurrentScriptObject(SceneObject* o) { t_currentScriptObject = o; }
SceneObject* currentScriptObject() { return t_currentScriptObject; }
void setScriptPlayAnimHook(std::function<void(SceneObject&, const char*)> hook) {
    s_playAnimHook = std::move(hook);
}
void setScriptGroundHeightHook(std::function<float(float, float)> hook) {
    s_groundHeightFn = std::move(hook);
}
void setScriptPlayerPosition(float x, float y, float z) { s_playerPos = {x, y, z}; }

// Whether the shared bot server (AI Backend) is actually up. The host refreshes
// this each frame; agent_online() ANDs it with the per-bot switch so a robot
// only "comes alive" once its server is genuinely ready, not the instant you
// flip it on (there's a boot delay). One server powers all bots.
static bool s_backendOnline = false;
void setScriptBackendOnline(bool online) { s_backendOnline = online; }

// Player input, refreshed by the host each play-mode frame.
static struct { float moveX, moveZ, jump, run, mouseDx, mouseDy; } s_input{};
void setScriptInput(float mx, float mz, float j, float r, float dx, float dy) {
    s_input = {mx, mz, j, r, dx, dy};
}

} // namespace eden

using eden::currentScriptTransform;

extern "C" {

void self_translate(float dx, float dy, float dz) {
    if (auto* t = currentScriptTransform()) t->translate(dx, dy, dz);
}

void self_set_position(float x, float y, float z) {
    if (auto* t = currentScriptTransform()) t->setPosition(x, y, z);
}

float self_get_x() {
    auto* t = currentScriptTransform();
    return t ? t->getPosition().x : 0.0f;
}

float self_get_y() {
    auto* t = currentScriptTransform();
    return t ? t->getPosition().y : 0.0f;
}

float self_get_z() {
    auto* t = currentScriptTransform();
    return t ? t->getPosition().z : 0.0f;
}

void self_rotate_y(float degrees) {
    if (auto* t = currentScriptTransform()) t->rotate(degrees, {0.0f, 1.0f, 0.0f});
}

void self_set_rotation(float rx, float ry, float rz) {
    if (auto* t = currentScriptTransform()) t->setRotation(glm::vec3(rx, ry, rz));
}

void self_play_anim(const char* name) {
    if (!name) return;
    auto* o = eden::currentScriptObject();
    // The host-installed hook owns renderer access and already-playing dedup.
    if (o && eden::s_playAnimHook) eden::s_playAnimHook(*o, name);
}

// --- Agent/world state verbs ---

// 1.0 when THIS agent is switched on AND its server is actually up, else 0.0.
// The tick wrapper sets the current script object before each call, so this
// reads the bound robot's own switch (activate_bot) and ANDs it with the shared
// server status. A robot that isn't facing you is either off or still booting.
float agent_online() {
    auto* o = eden::currentScriptObject();
    return (eden::s_backendOnline && o && o->isAiActivated()) ? 1.0f : 0.0f;
}

// --- Player-relative verbs (horizontal XZ plane, feet) ---

float self_dist_to_player() {
    auto* t = currentScriptTransform();
    if (!t) return 1.0e9f;   // no self -> "infinitely far" so idle branches win
    glm::vec3 s = t->getPosition();
    float dx = eden::s_playerPos.x - s.x;
    float dz = eden::s_playerPos.z - s.z;
    return std::sqrt(dx * dx + dz * dz);
}

void self_face_player() {
    auto* t = currentScriptTransform();
    if (!t) return;
    glm::vec3 s = t->getPosition();
    float dx = eden::s_playerPos.x - s.x;
    float dz = eden::s_playerPos.z - s.z;
    if (dx * dx + dz * dz < 1.0e-6f) return;  // player is right on top — keep facing
    // Same convention as the engine's turn_to: yaw aligns local forward with (dx,dz).
    float yaw = std::atan2(dx, dz) * 180.0f / 3.14159265f;
    t->setRotation(glm::vec3(0.0f, yaw, 0.0f));  // yaw only (ground creature)
}

void self_move_toward_player(float step) {
    auto* t = currentScriptTransform();
    if (!t || step == 0.0f) return;
    glm::vec3 s = t->getPosition();
    float dx = eden::s_playerPos.x - s.x;
    float dz = eden::s_playerPos.z - s.z;
    float d = std::sqrt(dx * dx + dz * dz);
    if (d < 1.0e-4f) return;
    float m = (step < d) ? step : d;             // don't overshoot past the player
    t->translate(dx / d * m, 0.0f, dz / d * m);  // horizontal step
}

// --- Facing-relative movement (horizontal plane) ---
// Direction comes from the entity's own rotation (quat * axis), so it inherits
// self_face_player / self_rotate_y's convention automatically. Y is flattened so
// pitch never drives the creature into or off the ground.

void self_move_forward(float dist) {
    auto* t = currentScriptTransform();
    if (!t) return;
    glm::vec3 f = t->getRotation() * glm::vec3(0.0f, 0.0f, 1.0f);
    f.y = 0.0f;
    float len = glm::length(f);
    if (len < 1e-6f) return;
    f /= len;
    t->translate(f.x * dist, 0.0f, f.z * dist);
}

void self_move_right(float dist) {
    auto* t = currentScriptTransform();
    if (!t) return;
    // Forward is +Z, up is +Y, so the player's RIGHT is local -X (matches the
    // engine camera's getRight()); +X would strafe left.
    glm::vec3 r = t->getRotation() * glm::vec3(-1.0f, 0.0f, 0.0f);
    r.y = 0.0f;
    float len = glm::length(r);
    if (len < 1e-6f) return;
    r /= len;
    t->translate(r.x * dist, 0.0f, r.z * dist);
}

// --- Terrain ground verbs ---

float self_ground_y() {
    auto* t = currentScriptTransform();
    if (!t) return 0.0f;
    glm::vec3 s = t->getPosition();
    if (eden::s_groundHeightFn) return eden::s_groundHeightFn(s.x, s.z);
    return s.y;   // no terrain hook -> leave height as-is
}

float input_move_x() { return eden::s_input.moveX; }
float input_move_z() { return eden::s_input.moveZ; }
float input_jump()   { return eden::s_input.jump; }
float input_run()    { return eden::s_input.run; }
float mouse_dx()     { return eden::s_input.mouseDx; }
float mouse_dy()     { return eden::s_input.mouseDy; }

void self_snap_to_ground() {
    auto* t = currentScriptTransform();
    if (!t || !eden::s_groundHeightFn) return;
    glm::vec3 s = t->getPosition();
    float ground = eden::s_groundHeightFn(s.x, s.z);
    // Place the model's BOTTOM on the ground, not its origin — a centered-origin
    // model (feet at local -Y) would otherwise sink. Uses the object's local
    // bounds + scale when we have the SceneObject; falls back to origin-on-ground.
    float feetOffset = 0.0f;
    if (auto* o = eden::currentScriptObject())
        feetOffset = -o->getLocalBounds().min.y * t->getScale().y;
    t->setPosition(s.x, ground + feetOffset, s.z);
}

} // extern "C"
