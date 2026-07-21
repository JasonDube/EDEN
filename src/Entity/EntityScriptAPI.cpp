#include <eden/EntityScriptAPI.hpp>
#include <eden/Entity.hpp>
#include <eden/Transform.hpp>

namespace eden {

// The transform the self_* API acts on. thread_local so a future threaded
// script dispatcher can't cross-talk; today everything runs on the main thread.
static thread_local Transform* t_currentScriptTransform = nullptr;

void setCurrentScriptTransform(Transform* t) { t_currentScriptTransform = t; }
Transform* currentScriptTransform() { return t_currentScriptTransform; }

void setCurrentScriptEntity(Entity* e) {
    t_currentScriptTransform = e ? &e->getTransform() : nullptr;
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

} // extern "C"
