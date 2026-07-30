#include "EditorModuleHost.hpp"

#include "Editor/SceneObject.hpp"
#include "Editor/PrimitiveMeshBuilder.hpp"
#include "Renderer/ModelRenderer.hpp"
#include "Renderer/SkinnedModelRenderer.hpp"

#include <eden/Terrain.hpp>

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

float EditorModuleHost::terrainHeight(float x, float z) const {
    if (!m_deps.terrain) return 0.0f;
    const float h = m_deps.terrain->getHeightAt(x, z);
    return (h < -1000.0f) ? 0.0f : h;   // the hole/unloaded sentinel, not a height
}
