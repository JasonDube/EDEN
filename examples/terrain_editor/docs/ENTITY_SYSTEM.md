# EDEN Entity & Action System

## Overview

The Entity/Action system provides data-driven entity behaviors that can be saved and loaded with levels. Actions can trigger other actions via signals, enabling complex interaction chains.

## Core Concepts

### Entity
An object in the world with:
- **Transform**: Position, rotation, scale
- **Flags**: VISIBLE, ACTIVE, INTERACTABLE, COLLIDABLE, STATIC
- **Behaviors**: List of action sequences with triggers
- **Properties**: Key-value storage for game logic
- **Tags**: For grouping/filtering

### Behavior
A sequence of actions with a trigger condition:
```cpp
Behavior doorOpen;
doorOpen.name = "OpenDoor";
doorOpen.trigger = TriggerType::ON_INTERACT;
doorOpen.actions = {
    Action::RotateTo({0, 90, 0}, 0.5f, Action::Easing::EASE_OUT)
};
entity->addBehavior(doorOpen);
```

### Action Types
| Type | Description | Parameters |
|------|-------------|------------|
| ROTATE | Rotate by delta | vec3Param = delta, duration |
| ROTATE_TO | Rotate to absolute | vec3Param = target, duration |
| MOVE | Move by delta | vec3Param = delta, duration |
| MOVE_TO | Move to absolute | vec3Param = target, duration |
| SCALE | Scale by factor | vec3Param = factor, duration |
| WAIT | Pause | duration |
| SEND_SIGNAL | Trigger another entity | stringParam = "signal:target" |
| SPAWN_ENTITY | Create entity from template | stringParam = template name |
| DESTROY_SELF | Remove this entity | - |
| SET_VISIBLE | Show/hide | boolParam |
| SET_PROPERTY | Set property value | stringParam = key, floatParam = value |
| PLAY_SOUND | Play audio (future) | stringParam = filename |
| CUSTOM | Custom callback | stringParam = callback name |

### Trigger Types
| Trigger | When it fires |
|---------|---------------|
| ON_START | When level loads |
| ON_INTERACT | Player presses interact key nearby |
| ON_PROXIMITY | Player enters radius |
| ON_SIGNAL | Receives named signal |
| ON_COLLISION | Collides with something |
| MANUAL | Only via code |

## Action Chaining Example

Gun → Bullet → Explosion → Damage chain:

```cpp
// Register templates
EntityTemplate bulletTemplate;
bulletTemplate.name = "Bullet";
bulletTemplate.behaviors = {{
    .name = "fly",
    .trigger = TriggerType::ON_START,
    .actions = {
        Action::Move({0, 0, 100}, 2.0f),  // Fly forward
        Action::SendSignal("EXPLODE:Explosion"),
        Action::DestroySelf()
    }
}};
actionSystem.registerTemplate(bulletTemplate);

EntityTemplate explosionTemplate;
explosionTemplate.name = "Explosion";
explosionTemplate.behaviors = {{
    .name = "explode",
    .trigger = TriggerType::ON_SIGNAL,
    .triggerParam = "EXPLODE",
    .actions = {
        Action::Scale({5, 5, 5}, 0.2f),
        // Custom action to damage nearby
        {ActionType::CUSTOM, .stringParam = "DamageNearby", .floatParam = 50.0f},
        Action::Wait(0.5f),
        Action::DestroySelf()
    }
}};
actionSystem.registerTemplate(explosionTemplate);

// Register custom damage action
actionSystem.registerCustomAction("DamageNearby", [](Entity& e, const Action& a, ActionSystem& sys) {
    sys.broadcastSignal("DAMAGE", e.getTransform().position, 10.0f, a.floatParam);
});

// Enemy receives damage
Behavior hurtBehavior;
hurtBehavior.trigger = TriggerType::ON_SIGNAL;
hurtBehavior.triggerParam = "DAMAGE";
hurtBehavior.actions = {
    // Flash red, play hurt animation, etc.
    Action::SetProperty("health", -50.0f)  // Would need custom logic
};
enemy->addBehavior(hurtBehavior);
```

## Integration with main.cpp

```cpp
#include <eden/ActionSystem.hpp>

class TerrainEditor {
    ActionSystem m_actionSystem;

    void init() {
        // ... existing init ...

        // Register templates
        setupEntityTemplates();
    }

    void update(float dt) {
        // ... existing update ...

        // Update entities
        m_actionSystem.update(dt, m_camera.getPosition());

        // Check for player interaction (e.g., E key)
        if (Input::isKeyPressed(Key::E)) {
            m_actionSystem.playerInteract(m_camera.getPosition(), 3.0f);
        }
    }

    void recordCommandBuffer() {
        // ... existing rendering ...

        // Render entity models
        for (auto& entity : m_actionSystem.getEntities()) {
            if (!entity->hasFlag(EntityFlags::VISIBLE)) continue;
            uint32_t modelHandle = entity->getModelHandle();
            if (modelHandle != UINT32_MAX) {
                m_modelRenderer->render(cmd, vp, modelHandle,
                    entity->getTransform().getMatrix());
            }
        }
    }
};
```

## Creating Interactive Objects

### Door
```cpp
Entity* door = actionSystem.createEntity("Door");
door->setModelHandle(doorModelHandle);
door->addFlag(EntityFlags::INTERACTABLE);

Behavior open;
open.name = "Open";
open.trigger = TriggerType::ON_INTERACT;
open.actions = {
    Action::RotateTo({0, 90, 0}, 0.5f, Action::Easing::EASE_OUT)
};
door->addBehavior(open);
```

### Light Switch
```cpp
Entity* lightSwitch = actionSystem.createEntity("LightSwitch");
lightSwitch->addFlag(EntityFlags::INTERACTABLE);
lightSwitch->setProperty("isOn", 0.0f);

Behavior toggle;
toggle.name = "Toggle";
toggle.trigger = TriggerType::ON_INTERACT;
toggle.actions = {
    Action::SendSignal("TOGGLE:CeilingLight"),
    // Could toggle own property with custom action
};
lightSwitch->addBehavior(toggle);

// Light responds to signal
Entity* light = actionSystem.createEntity("CeilingLight");
Behavior lightToggle;
lightToggle.trigger = TriggerType::ON_SIGNAL;
lightToggle.triggerParam = "TOGGLE";
lightToggle.actions = {
    // Custom action to toggle light state
    {ActionType::CUSTOM, .stringParam = "ToggleLight"}
};
light->addBehavior(lightToggle);
```

### Rotating Platform
```cpp
Entity* platform = actionSystem.createEntity("RotatingPlatform");
platform->setModelHandle(platformModelHandle);

Behavior spin;
spin.name = "Spin";
spin.trigger = TriggerType::ON_START;
spin.loop = true;
spin.actions = {
    Action::Rotate({0, 360, 0}, 10.0f)  // Full rotation in 10 seconds
};
platform->addBehavior(spin);
```

### Elevator
```cpp
Entity* elevator = actionSystem.createEntity("Elevator");
elevator->addFlag(EntityFlags::INTERACTABLE);

Behavior goUp;
goUp.name = "GoUp";
goUp.trigger = TriggerType::ON_INTERACT;
goUp.actions = {
    Action::MoveTo({0, 20, 0}, 3.0f, Action::Easing::EASE_IN_OUT),
    Action::Wait(2.0f),
    Action::MoveTo({0, 0, 0}, 3.0f, Action::Easing::EASE_IN_OUT)
};
elevator->addBehavior(goUp);
```

## Saving/Loading

```cpp
// Save
ActionSystem::SaveData saveData = actionSystem.getSaveData();
// Serialize saveData to file (JSON, binary, etc.)

// Load
ActionSystem::SaveData loadedData = /* deserialize from file */;
actionSystem.loadSaveData(loadedData);
```

## Future Extensions

- **Behavior Trees**: For complex AI decision making
- **Animation Integration**: Trigger skeletal animations
- **Physics Integration**: Apply forces, detect collisions
- **Audio System**: Play sounds from actions
- **Visual Scripting**: Node-based editor for behaviors
