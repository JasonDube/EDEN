#pragma once

// The terrain editor's answer to eden::ModuleHost.
//
// In its own translation unit rather than in main.cpp, and reaching the editor
// through a struct of borrowed pointers, for the same reason TribeSim is --
// main.cpp is a 33k-line god file and nothing new goes in it. The wiring in
// main is a dozen lines of "here is where each of these lives".

#include "GameModules/ModuleHost.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eden {
class Camera;
class ModelRenderer;
class SceneObject;
class SkinnedModelRenderer;
class Terrain;
class Window;
}

// Where the editor keeps the things this needs.
struct EditorModuleHostDeps {
    std::vector<std::unique_ptr<eden::SceneObject>>* sceneObjects = nullptr;
    eden::Terrain*              terrain = nullptr;
    eden::ModelRenderer*        models  = nullptr;
    eden::SkinnedModelRenderer* skinned = nullptr;
    eden::Camera*               camera  = nullptr;   // projection only
    eden::Window*               window  = nullptr;   // screen size for that projection

    // The world clock. The host owns it; a module may ask for it to turn.
    const float* gameTimeMinutes = nullptr;
    float*       gameTimeScale   = nullptr;

    // False in levels with no ground -- EDEN OS, space, test levels.
    std::function<bool()> hasTerrain;

    // Importing a model file is real host policy -- .lime and .glb take
    // different paths, skinned and static take different paths again, and all of
    // it already exists in the editor. Rather than reimplement any of that here,
    // ask for it and find out what appeared: the callback imports `path` and the
    // objects it added are whatever is on the end of the scene list afterwards.
    std::function<void(const std::string& path)> importModelFile;

    // Destroying one is host policy too -- physics bodies, selection indices,
    // spawn-point references and the editor's object list all care.
    std::function<void(const std::string& name)> destroyNamed;

    // The scene list changed; the editor's panels should notice.
    std::function<void()> objectsChanged;
};

class EditorModuleHost final : public eden::ModuleHost {
public:
    void setDeps(const EditorModuleHostDeps& deps) { m_deps = deps; }

    std::string spawnModel(const std::string& path, const glm::vec3& position,
                           float yawDegrees, const std::string& nameHint) override;
    std::string spawnBox(const glm::vec3& position, const glm::vec3& size,
                         const glm::vec4& color, const std::string& nameHint) override;
    void destroyObject(const std::string& name) override;
    void destroyAllOwned() override;

    bool objectPosition(const std::string& name, glm::vec3& outPosition) const override;
    bool setObjectPosition(const std::string& name, const glm::vec3& position) override;
    bool setObjectYaw(const std::string& name, float yawDegrees) override;
    bool setObjectScale(const std::string& name, const glm::vec3& scale) override;
    bool setObjectTag(const std::string& name, const std::string& tag) override;
    void ownedObjects(std::vector<eden::ModuleObjectInfo>& out) const override;
    bool playAnimation(const std::string& name, const std::string& clip, bool loop) override;

    float terrainHeight(float x, float z) const override;
    bool  hasTerrain() const override;
    bool  worldToScreen(const glm::vec3& world, glm::vec2& outPixel) const override;
    bool  screenRay(const glm::vec2& pixel, glm::vec3& outOrigin,
                    glm::vec3& outDirection) const override;
    float gameTimeMinutes() const override;
    void  requestTimeScale(float minutesPerRealSecond) override;
    std::size_t ownedCount() const override { return m_owned.size(); }

    // Forget the ownership list without destroying anything. For when the host
    // has already wiped the scene by other means (New Level clears every object
    // it has), so the names in here refer to nothing and destroying them would
    // be a pointless walk over a list that cannot match.
    void forgetOwned() { m_owned.clear(); }

private:
    eden::SceneObject* find(const std::string& name) const;
    std::string uniqueName(const std::string& hint) const;

    EditorModuleHostDeps m_deps;
    std::vector<std::string> m_owned;   // spawned through here, in spawn order
    int m_counter = 0;
};
