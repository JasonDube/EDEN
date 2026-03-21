#pragma once
/**
 * Machine.hpp — Base class for all machine types.
 *
 * Machines are game objects with behaviors triggered by player interaction.
 * Each concrete Machine subclass (GeneratorMachine, etc.) owns its runtime
 * state and implements the virtual interface below.
 *
 * MachineManager holds one instance of each machine type and delegates calls.
 */

#include <string>

namespace eden { class SceneObject; }
struct MachineHost;

class Machine {
public:
    virtual ~Machine() = default;

    // Called when player presses E on a SceneObject.
    // Returns true if this machine type handled the interaction.
    virtual bool onInteract(eden::SceneObject* obj, MachineHost& host) = 0;

    // Per-frame update for all running instances of this machine type.
    virtual void update(float deltaTime, MachineHost& host) = 0;

    // Called when a SceneObject is being picked up / salvaged.
    // Must shut down any running behavior (audio, particles, animation).
    virtual void onPickup(eden::SceneObject* obj, MachineHost& host) = 0;

    // Called when a SceneObject is being deleted from the scene.
    // Must remove any references to this pointer.
    virtual void onDelete(eden::SceneObject* obj) = 0;

    // Shut down all running instances (level exit / cleanup).
    virtual void shutdown(MachineHost& host) = 0;

    // Query: is this SceneObject currently a running instance of this machine type?
    virtual bool isRunning(eden::SceneObject* obj) const = 0;
};
