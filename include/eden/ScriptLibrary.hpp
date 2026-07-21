#pragma once

#include <string>

namespace eden {

// A loaded HEIDIC entity-script shared library (<script>.so built by the
// Script Editor's compile pipeline). Owns the dlopen handle; resolves @entity
// functions by their (extern "C") names.
//
// IMPORTANT: function pointers resolved from this library dangle after
// unload()/load() — the host must clear every bound script before reloading
// (the editor unbinds all SceneObject tick scripts, then load(), then rebinds).
class ScriptLibrary {
public:
    ScriptLibrary() = default;
    ~ScriptLibrary();
    ScriptLibrary(const ScriptLibrary&) = delete;
    ScriptLibrary& operator=(const ScriptLibrary&) = delete;

    // Load (or reload) the library. Closes any previous handle first so a
    // recompiled .so at the same path is picked up fresh. Returns false and
    // fills `error` on failure (missing file, unresolved symbols, ...).
    bool load(const std::string& soPath, std::string& error);
    void unload();
    bool isLoaded() const { return m_handle != nullptr; }
    const std::string& path() const { return m_path; }

    // An @entity per-tick function: extern "C" void <name>(float dt)
    using TickFn = void (*)(float);
    // Resolve by name; nullptr if the symbol isn't in the library.
    TickFn resolveTick(const std::string& name) const;

private:
    void* m_handle = nullptr;
    std::string m_path;
};

} // namespace eden
