#include <eden/ScriptLibrary.hpp>

#include <dlfcn.h>

namespace eden {

ScriptLibrary::~ScriptLibrary() { unload(); }

bool ScriptLibrary::load(const std::string& soPath, std::string& error) {
    unload();  // drop any previous handle so the same path reloads fresh
    // RTLD_NOW: resolve everything at load time — an unresolved self_* symbol
    // (host built without ENABLE_EXPORTS, or prelude/API drift) fails HERE with
    // a clear message instead of crashing at first call.
    m_handle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle) {
        const char* e = dlerror();
        error = e ? e : "dlopen failed";
        return false;
    }
    m_path = soPath;
    return true;
}

void ScriptLibrary::unload() {
    if (m_handle) {
        dlclose(m_handle);
        m_handle = nullptr;
        m_path.clear();
    }
}

ScriptLibrary::TickFn ScriptLibrary::resolveTick(const std::string& name) const {
    if (!m_handle) return nullptr;
    dlerror();  // clear
    void* sym = dlsym(m_handle, name.c_str());
    return reinterpret_cast<TickFn>(sym);
}

} // namespace eden
