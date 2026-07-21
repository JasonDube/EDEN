#include <eden/EntityScriptAPI.hpp>
#include <eden/Entity.hpp>

namespace eden {

// The entity the self_* API acts on. thread_local so a future threaded script
// dispatcher can't cross-talk; today everything runs on the main thread.
static thread_local Entity* t_currentScriptEntity = nullptr;

void setCurrentScriptEntity(Entity* e) { t_currentScriptEntity = e; }
Entity* currentScriptEntity() { return t_currentScriptEntity; }

} // namespace eden

using eden::currentScriptEntity;

extern "C" {

void self_translate(float dx, float dy, float dz) {
    if (auto* e = currentScriptEntity()) e->getTransform().translate(dx, dy, dz);
}

void self_set_position(float x, float y, float z) {
    if (auto* e = currentScriptEntity()) e->getTransform().setPosition(x, y, z);
}

float self_get_x() {
    auto* e = currentScriptEntity();
    return e ? e->getTransform().getPosition().x : 0.0f;
}

float self_get_y() {
    auto* e = currentScriptEntity();
    return e ? e->getTransform().getPosition().y : 0.0f;
}

float self_get_z() {
    auto* e = currentScriptEntity();
    return e ? e->getTransform().getPosition().z : 0.0f;
}

void self_rotate_y(float degrees) {
    if (auto* e = currentScriptEntity()) e->getTransform().rotate(degrees, {0.0f, 1.0f, 0.0f});
}

void self_set_rotation(float rx, float ry, float rz) {
    if (auto* e = currentScriptEntity()) e->getTransform().setRotation(glm::vec3(rx, ry, rz));
}

} // extern "C"
