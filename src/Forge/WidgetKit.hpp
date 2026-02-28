#pragma once

#include "../Editor/SceneObject.hpp"
#include "../Renderer/ModelRenderer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace eden {

struct WidgetInstance {
    std::string widgetType;    // "button", "checkbox", "slot"
    std::string name;          // user-defined name from control point
    SceneObject* hitbox = nullptr;  // invisible interaction cube
    bool toggleState = false;  // for checkbox
    std::string slotValue;     // for slot (file path)
};

class WidgetKit {
public:
    WidgetKit() = default;

    // Spawn widgets from a .lime model's control points
    // Scans for control points named "widget_<type>_<name>" and creates
    // invisible hitbox cubes at each location.
    void spawnFromLime(const std::string& limePath, const glm::vec3& position,
                       float baseY, std::vector<std::unique_ptr<SceneObject>>* scene,
                       ModelRenderer* renderer);

    // Interaction handlers (called from main.cpp)
    bool handleCrosshairInteract(SceneObject* obj);  // button/checkbox click
    bool handleSlotDrop(SceneObject* obj, const std::string& filePath);  // slot drop

    // Query
    int getSlotIndex(SceneObject* obj) const;
    bool isWidgetHitbox(SceneObject* obj) const;
    bool isWidgetButton(SceneObject* obj) const;
    bool isWidgetCheckbox(SceneObject* obj) const;
    bool isWidgetSlot(SceneObject* obj) const;

    // Get widget instance for a hitbox object (nullptr if not found)
    WidgetInstance* getWidgetFor(SceneObject* obj);
    const WidgetInstance* getWidgetFor(SceneObject* obj) const;

    // Find widget by name
    WidgetInstance* findByName(const std::string& name);
    const WidgetInstance* findByName(const std::string& name) const;

    // Cleanup
    void despawn(std::vector<std::unique_ptr<SceneObject>>* scene,
                 ModelRenderer* renderer);

    bool hasWidgets() const { return !m_widgets.empty(); }

    // Signal callback — set by main.cpp to route to Grove signal system
    void setSignalCallback(std::function<void(const std::string&, float)> cb) {
        m_signalCallback = std::move(cb);
    }

private:
    std::vector<WidgetInstance> m_widgets;
    SceneObject* m_visual = nullptr;  // the parent .lime model visual
    std::function<void(const std::string&, float)> m_signalCallback;

    void emitSignal(const std::string& name, float value = 0.0f);
};

} // namespace eden
