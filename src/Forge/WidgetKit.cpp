#include "WidgetKit.hpp"
#include "../Editor/PrimitiveMeshBuilder.hpp"
#include "../Editor/LimeLoader.hpp"
#include <iostream>
#include <filesystem>

namespace eden {

void WidgetKit::spawnFromLime(const std::string& limePath, const glm::vec3& position,
                               float baseY, std::vector<std::unique_ptr<SceneObject>>* scene,
                               ModelRenderer* renderer) {
    if (!scene || !renderer) return;
    if (limePath.empty() || !std::filesystem::exists(limePath)) return;

    auto result = LimeLoader::load(limePath);
    if (!result.success || result.mesh.vertices.empty()) {
        std::cerr << "[WidgetKit] Failed to load .lime: " << result.error << std::endl;
        return;
    }

    // Spawn the visual model
    auto visual = LimeLoader::createSceneObject(result.mesh, *renderer);
    if (!visual) return;

    visual->setBuildingType("machine_visual");
    visual->setDescription("Widget Machine");
    visual->getTransform().setPosition({position.x, baseY, position.z});
    m_visual = visual.get();
    scene->push_back(std::move(visual));

    // Scan control points for widget_<type>_<name> pattern
    for (auto& cp : result.mesh.controlPoints) {
        if (cp.name.rfind("widget_", 0) != 0) continue;

        // Parse: "widget_<type>_<name>"
        std::string rest = cp.name.substr(7);  // strip "widget_"
        auto underscorePos = rest.find('_');
        if (underscorePos == std::string::npos) continue;

        std::string widgetType = rest.substr(0, underscorePos);
        std::string widgetName = rest.substr(underscorePos + 1);

        if (widgetType != "button" && widgetType != "checkbox" && widgetType != "slot") {
            std::cerr << "[WidgetKit] Unknown widget type: " << widgetType << std::endl;
            continue;
        }

        // Get control point world position
        glm::vec3 cpPos{0.0f};
        if (cp.vertexIndex < result.mesh.vertices.size()) {
            cpPos = result.mesh.vertices[cp.vertexIndex].position;
        }
        cpPos += glm::vec3(position.x, baseY, position.z);

        // Create invisible hitbox cube
        glm::vec4 hitboxColor{0.0f, 0.0f, 0.0f, 0.0f};
        auto mesh = PrimitiveMeshBuilder::createCube(1.0f, hitboxColor);
        uint32_t handle = renderer->createModel(
            mesh.vertices, mesh.indices, nullptr, 0, 0);

        auto obj = std::make_unique<SceneObject>("Widget_" + cp.name);
        obj->setBufferHandle(handle);
        obj->setIndexCount(static_cast<uint32_t>(mesh.indices.size()));
        obj->setVertexCount(static_cast<uint32_t>(mesh.vertices.size()));
        obj->setLocalBounds(mesh.bounds);
        obj->setMeshData(mesh.vertices, mesh.indices);
        obj->setPrimitiveType(PrimitiveType::Cube);
        obj->setBuildingType("widget");
        obj->setDescription(widgetType + ":" + widgetName);
        obj->setVisible(false);
        obj->getTransform().setPosition(cpPos);
        obj->getTransform().setScale({1.5f, 1.5f, 1.5f});

        WidgetInstance wi;
        wi.widgetType = widgetType;
        wi.name = widgetName;
        wi.hitbox = obj.get();
        m_widgets.push_back(std::move(wi));

        scene->push_back(std::move(obj));
    }

    if (!m_widgets.empty()) {
        std::cout << "[WidgetKit] Spawned " << m_widgets.size()
                  << " widgets from " << limePath << std::endl;
    }
}

bool WidgetKit::handleCrosshairInteract(SceneObject* obj) {
    WidgetInstance* wi = getWidgetFor(obj);
    if (!wi) return false;

    if (wi->widgetType == "button") {
        // Momentary button — flash visual and emit signal
        if (m_visual) {
            m_visual->triggerHitFlash(0.15f);
        }
        emitSignal("button_" + wi->name, 1.0f);
        std::cout << "[WidgetKit] Button pressed: " << wi->name << std::endl;
        return true;
    }

    if (wi->widgetType == "checkbox") {
        // Toggle state
        wi->toggleState = !wi->toggleState;
        float value = wi->toggleState ? 1.0f : 0.0f;

        // Visual feedback: make hitbox visible with green (on) or red (off)
        if (wi->hitbox) {
            wi->hitbox->setVisible(true);
            if (wi->toggleState) {
                wi->hitbox->setHueShift(120.0f);  // green-ish
                wi->hitbox->setBrightness(1.5f);
            } else {
                wi->hitbox->setHueShift(0.0f);    // red-ish
                wi->hitbox->setBrightness(1.0f);
            }
        }

        emitSignal("checkbox_" + wi->name, value);
        std::cout << "[WidgetKit] Checkbox toggled: " << wi->name
                  << " = " << (wi->toggleState ? "ON" : "OFF") << std::endl;
        return true;
    }

    return false;
}

bool WidgetKit::handleSlotDrop(SceneObject* obj, const std::string& filePath) {
    WidgetInstance* wi = getWidgetFor(obj);
    if (!wi || wi->widgetType != "slot") return false;

    wi->slotValue = filePath;

    // Visual feedback: make hitbox visible as white indicator
    if (wi->hitbox) {
        wi->hitbox->setVisible(true);
        wi->hitbox->setBrightness(2.0f);
        wi->hitbox->setHueShift(0.0f);
    }

    emitSignal("slot_" + wi->name, 1.0f);
    std::cout << "[WidgetKit] Slot filled: " << wi->name
              << " = " << filePath << std::endl;
    return true;
}

int WidgetKit::getSlotIndex(SceneObject* obj) const {
    for (size_t i = 0; i < m_widgets.size(); i++) {
        if (m_widgets[i].hitbox == obj && m_widgets[i].widgetType == "slot") {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool WidgetKit::isWidgetHitbox(SceneObject* obj) const {
    return getWidgetFor(obj) != nullptr;
}

bool WidgetKit::isWidgetButton(SceneObject* obj) const {
    auto* wi = getWidgetFor(obj);
    return wi && wi->widgetType == "button";
}

bool WidgetKit::isWidgetCheckbox(SceneObject* obj) const {
    auto* wi = getWidgetFor(obj);
    return wi && wi->widgetType == "checkbox";
}

bool WidgetKit::isWidgetSlot(SceneObject* obj) const {
    auto* wi = getWidgetFor(obj);
    return wi && wi->widgetType == "slot";
}

WidgetInstance* WidgetKit::getWidgetFor(SceneObject* obj) {
    if (!obj) return nullptr;
    for (auto& wi : m_widgets) {
        if (wi.hitbox == obj) return &wi;
    }
    return nullptr;
}

const WidgetInstance* WidgetKit::getWidgetFor(SceneObject* obj) const {
    if (!obj) return nullptr;
    for (auto& wi : m_widgets) {
        if (wi.hitbox == obj) return &wi;
    }
    return nullptr;
}

WidgetInstance* WidgetKit::findByName(const std::string& name) {
    for (auto& wi : m_widgets) {
        if (wi.name == name) return &wi;
    }
    return nullptr;
}

const WidgetInstance* WidgetKit::findByName(const std::string& name) const {
    for (auto& wi : m_widgets) {
        if (wi.name == name) return &wi;
    }
    return nullptr;
}

void WidgetKit::despawn(std::vector<std::unique_ptr<SceneObject>>* scene,
                         ModelRenderer* renderer) {
    if (!scene || !renderer) return;

    // Remove widget hitboxes
    for (auto& wi : m_widgets) {
        if (!wi.hitbox) continue;
        auto it = scene->begin();
        while (it != scene->end()) {
            if (it->get() == wi.hitbox) {
                uint32_t h = (*it)->getBufferHandle();
                if (h != 0) renderer->destroyModel(h);
                it = scene->erase(it);
                break;
            } else {
                ++it;
            }
        }
    }

    // Remove visual
    if (m_visual) {
        auto it = scene->begin();
        while (it != scene->end()) {
            if (it->get() == m_visual) {
                uint32_t h = (*it)->getBufferHandle();
                if (h != 0) renderer->destroyModel(h);
                it = scene->erase(it);
                break;
            } else {
                ++it;
            }
        }
    }

    m_widgets.clear();
    m_visual = nullptr;
}

void WidgetKit::emitSignal(const std::string& name, float value) {
    if (m_signalCallback) {
        m_signalCallback(name, value);
    }

    // Also broadcast through the SceneObject signal system
    // so ON_SIGNAL behaviors on other objects can react
    if (SceneObject::s_signalCallback) {
        SceneObject::s_signalCallback(name, "", nullptr);
    }
}

} // namespace eden
