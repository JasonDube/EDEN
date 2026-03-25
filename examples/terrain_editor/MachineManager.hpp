#pragma once
/**
 * MachineManager.hpp — Owns all machine type instances.
 *
 * TerrainEditor creates one MachineManager and delegates machine-related
 * events to it.  Adding a new machine type:
 *   1. Create machines/FooMachine.hpp
 *   2. Add a FooMachine member here
 *   3. Wire it into the methods below
 */

#include "MachineHost.hpp"
#include "Machine.hpp"
#include "machines/GeneratorMachine.hpp"
#include "machines/HingeMachine.hpp"
#include "machines/CompressorMachine.hpp"

class MachineManager {
public:
    explicit MachineManager(MachineHost& host) : m_host(host) {}

    // Per-frame update — spin fans, animate hinges, etc.
    void update(float deltaTime) {
        m_generator.update(deltaTime, m_host);
        m_hinge.update(deltaTime, m_host);
        m_compressor.update(deltaTime, m_host);
    }

    // Try to handle an E-key interaction. Returns true if handled.
    bool tryInteract(eden::SceneObject* obj) {
        if (m_hinge.onInteract(obj, m_host)) return true;
        if (m_compressor.onInteract(obj, m_host)) return true;
        if (m_generator.onInteract(obj, m_host)) return true;
        return false;
    }

    // Notify all machine types that an object is being picked up.
    void notifyPickup(eden::SceneObject* obj) {
        m_generator.onPickup(obj, m_host);
        m_hinge.onPickup(obj, m_host);
        m_compressor.onPickup(obj, m_host);
    }

    // Notify all machine types that an object is being deleted.
    void notifyDelete(eden::SceneObject* obj) {
        m_generator.onDelete(obj);
        m_hinge.onDelete(obj);
        m_compressor.onDelete(obj);
    }

    // Shut down everything (level exit / cleanup).
    void shutdownAll() {
        m_generator.shutdown(m_host);
        m_hinge.shutdown(m_host);
        m_compressor.shutdown(m_host);
    }

    // Query: is this object a running machine of any type?
    bool isRunning(eden::SceneObject* obj) const {
        if (m_generator.isRunning(obj)) return true;
        if (m_hinge.isRunning(obj)) return true;
        if (m_compressor.isRunning(obj)) return true;
        return false;
    }

    // Direct access for special cases
    GeneratorMachine& generator() { return m_generator; }
    HingeMachine& hinge() { return m_hinge; }
    CompressorMachine& compressor() { return m_compressor; }

private:
    MachineHost& m_host;
    GeneratorMachine m_generator;
    HingeMachine m_hinge;
    CompressorMachine m_compressor;
};
