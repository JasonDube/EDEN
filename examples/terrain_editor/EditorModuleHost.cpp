#include "EditorModuleHost.hpp"

#include "Editor/SceneObject.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/SkinnedModelRenderer.hpp"

#include <eden/Camera.hpp>
#include <eden/Terrain.hpp>
#include <eden/Window.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace eden;

SceneObject* EditorModuleHost::find(const std::string& name) const {
    if (!m_deps.sceneObjects) return nullptr;
    for (const auto& o : *m_deps.sceneObjects) {
        if (o && o->getName() == name) return o.get();
    }
    return nullptr;
}

// A name nothing else is using.
//
// The host promises the name it RETURNS is unique, and this is where that
// promise is kept. Without it a module spawning two of anything gets two objects
// with one name, and every later lookup finds whichever comes first in the list
// -- a bug that looks like the second one silently not working.
std::string EditorModuleHost::uniqueName(const std::string& hint) const {
    const std::string base = hint.empty() ? "ModuleObject" : hint;
    if (!find(base)) return base;
    for (int n = 1; n < 100000; ++n) {
        const std::string candidate = base + "_" + std::to_string(n);
        if (!find(candidate)) return candidate;
    }
    return base;   // 100k of one name is somebody else's problem
}

std::string EditorModuleHost::spawnModel(const std::string& path, const glm::vec3& position,
                                         float yawDegrees, const std::string& nameHint) {
    if (!m_deps.sceneObjects || !m_deps.importModelFile) return {};

    // What the import added is whatever is on the end of the list afterwards.
    // The editor's importers do not report what they created -- they push and
    // select -- so this is how the tribe sim found its inhabitants too.
    const std::size_t before = m_deps.sceneObjects->size();
    m_deps.importModelFile(path);
    if (m_deps.sceneObjects->size() <= before) return {};   // nothing loaded

    // An import can produce several objects (a GLB with more than one mesh). The
    // first is the handle the module gets; the rest are owned and cleaned up but
    // are not separately addressable, which is honest -- a module that needs to
    // address them wants a scene format, not a spawn call.
    std::string firstName;
    for (std::size_t i = before; i < m_deps.sceneObjects->size(); ++i) {
        auto* o = (*m_deps.sceneObjects)[i].get();
        if (!o) continue;
        const std::string name = uniqueName(firstName.empty() ? nameHint
                                                              : o->getName());
        o->setName(name);
        if (firstName.empty()) {
            firstName = name;
            o->getTransform().setPosition(position);
            o->getTransform().setRotation(glm::vec3(0.0f, yawDegrees, 0.0f));
        }
        m_owned.push_back(name);
    }

    if (m_deps.objectsChanged) m_deps.objectsChanged();
    return firstName;
}

std::string EditorModuleHost::spawnBox(const glm::vec3& position, const glm::vec3& size,
                                       const glm::vec4& color, const std::string& nameHint) {
    if (!m_deps.sceneObjects || !m_deps.models) return {};

    auto mesh = PrimitiveMeshBuilder::createCube(1.0f, color);
    auto obj = std::make_unique<SceneObject>(uniqueName(nameHint.empty() ? "ModuleBox" : nameHint));
    obj->setBufferHandle(m_deps.models->createModel(mesh.vertices, mesh.indices));
    obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
    obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
    obj->setLocalBounds(mesh.bounds);
    obj->setMeshData(mesh.vertices, mesh.indices);
    obj->setPrimitiveType(PrimitiveType::Cube);
    obj->setPrimitiveSize(1.0f);
    obj->setPrimitiveColor(color);
    obj->setAABBCollision(true);
    obj->getTransform().setPosition(position);
    obj->getTransform().setScale(size);

    const std::string name = obj->getName();
    m_deps.sceneObjects->push_back(std::move(obj));
    m_owned.push_back(name);
    if (m_deps.objectsChanged) m_deps.objectsChanged();
    return name;
}

void EditorModuleHost::destroyObject(const std::string& name) {
    if (m_deps.destroyNamed) m_deps.destroyNamed(name);
    for (std::size_t i = 0; i < m_owned.size(); ++i) {
        if (m_owned[i] == name) { m_owned.erase(m_owned.begin() + static_cast<long>(i)); break; }
    }
    if (m_deps.objectsChanged) m_deps.objectsChanged();
}

// Backwards, and off a copy.
//
// Backwards because the host's destroy renumbers everything after the object it
// removes, and off a copy because destroyObject() edits m_owned as it goes --
// iterating the list you are erasing from is how the respawnFrameFiles bug
// worked, and it is not worth learning twice.
void EditorModuleHost::destroyAllOwned() {
    const std::vector<std::string> names = m_owned;
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        if (m_deps.destroyNamed) m_deps.destroyNamed(*it);
    }
    m_owned.clear();
    if (m_deps.objectsChanged) m_deps.objectsChanged();
}

bool EditorModuleHost::objectPosition(const std::string& name, glm::vec3& outPosition) const {
    if (auto* o = find(name)) { outPosition = o->getTransform().getPosition(); return true; }
    return false;
}

bool EditorModuleHost::setObjectPosition(const std::string& name, const glm::vec3& position) {
    if (auto* o = find(name)) { o->getTransform().setPosition(position); return true; }
    return false;
}

bool EditorModuleHost::setObjectYaw(const std::string& name, float yawDegrees) {
    if (auto* o = find(name)) {
        o->getTransform().setRotation(glm::vec3(0.0f, yawDegrees, 0.0f));
        return true;
    }
    return false;
}

bool EditorModuleHost::playAnimation(const std::string& name, const std::string& clip, bool loop) {
    auto* o = find(name);
    if (!o || !o->isSkinned() || !m_deps.skinned) return false;
    m_deps.skinned->playAnimation(o->getSkinnedModelHandle(), clip, loop);
    o->setCurrentAnimation(clip);
    return true;
}

bool EditorModuleHost::setObjectScale(const std::string& name, const glm::vec3& scale) {
    if (auto* o = find(name)) { o->getTransform().setScale(scale); return true; }
    return false;
}

// Stored in the object's buildingType, which is already the editor's free-form
// "what sort of thing is this" string. Reusing it rather than adding a field
// keeps a module's tag saved with the level for free.
bool EditorModuleHost::setObjectTag(const std::string& name, const std::string& tag) {
    if (auto* o = find(name)) { o->setBuildingType(tag); return true; }
    return false;
}

void EditorModuleHost::ownedObjects(std::vector<ModuleObjectInfo>& out) const {
    out.clear();
    if (!m_deps.sceneObjects) return;
    out.reserve(m_owned.size());
    for (const std::string& name : m_owned) {
        auto* o = find(name);
        if (!o) continue;                 // destroyed by something else; skip quietly
        ModuleObjectInfo info;
        info.name     = name;
        info.tag      = o->getBuildingType();
        info.position = o->getTransform().getPosition();
        info.scale    = o->getTransform().getScale();
        info.skinned  = o->isSkinned();
        out.push_back(std::move(info));
    }
}

bool EditorModuleHost::hasTerrain() const {
    return m_deps.hasTerrain ? m_deps.hasTerrain() : (m_deps.terrain != nullptr);
}

// The same projection the host renders with, so a label lands on the thing it
// names rather than near it.
bool EditorModuleHost::worldToScreen(const glm::vec3& world, glm::vec2& outPixel) const {
    if (!m_deps.camera || !m_deps.window) return false;
    const float w = static_cast<float>(m_deps.window->getWidth());
    const float h = static_cast<float>(m_deps.window->getHeight());
    if (w <= 0.0f || h <= 0.0f) return false;

    const glm::mat4 vp = m_deps.camera->getProjectionMatrix(w / h, 0.1f, 5000.0f) *
                         m_deps.camera->getViewMatrix();
    const glm::vec4 clip = vp * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f) return false;          // behind the eye, not merely off-screen
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outPixel.x = (ndc.x * 0.5f + 0.5f) * w;
    outPixel.y = (ndc.y * 0.5f + 0.5f) * h;
    return true;
}

bool EditorModuleHost::screenRay(const glm::vec2& pixel, glm::vec3& outOrigin,
                                 glm::vec3& outDirection) const {
    if (!m_deps.camera || !m_deps.window) return false;
    const float w = static_cast<float>(m_deps.window->getWidth());
    const float h = static_cast<float>(m_deps.window->getHeight());
    if (w <= 0.0f || h <= 0.0f) return false;

    const glm::mat4 invVP = glm::inverse(
        m_deps.camera->getProjectionMatrix(w / h, 0.1f, 5000.0f) *
        m_deps.camera->getViewMatrix());
    const float ndcX = (pixel.x / w) * 2.0f - 1.0f;
    const float ndcY = (pixel.y / h) * 2.0f - 1.0f;
    glm::vec4 near4 = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 far4  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::fabs(near4.w) < 1e-6f || std::fabs(far4.w) < 1e-6f) return false;
    outOrigin    = glm::vec3(near4) / near4.w;
    outDirection = glm::normalize(glm::vec3(far4) / far4.w - outOrigin);
    return true;
}

float EditorModuleHost::gameTimeMinutes() const {
    return m_deps.gameTimeMinutes ? *m_deps.gameTimeMinutes : 0.0f;
}

void EditorModuleHost::requestTimeScale(float minutesPerRealSecond) {
    if (m_deps.gameTimeScale) *m_deps.gameTimeScale = minutesPerRealSecond;
}

float EditorModuleHost::terrainHeight(float x, float z) const {
    if (!m_deps.terrain) return 0.0f;
    const float h = m_deps.terrain->getHeightAt(x, z);
    return (h < -1000.0f) ? 0.0f : h;   // the hole/unloaded sentinel, not a height
}
