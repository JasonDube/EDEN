#include "GameModule.hpp"

#include <map>

namespace eden {

namespace {

// Ordered, so the picker in the editor lists them the same way twice running.
std::map<std::string, GameModuleFactory::Maker>& registry() {
    static std::map<std::string, GameModuleFactory::Maker> instance;
    return instance;
}

} // namespace

void GameModuleFactory::registerModule(const std::string& name, Maker maker) {
    if (name.empty() || !maker) return;
    registry()[name] = std::move(maker);
}

std::vector<std::string> GameModuleFactory::getAvailableModules() {
    std::vector<std::string> names;
    names.reserve(registry().size());
    for (const auto& entry : registry()) names.push_back(entry.first);
    return names;
}

std::unique_ptr<GameModule> GameModuleFactory::create(const std::string& moduleName) {
    auto it = registry().find(moduleName);
    return it == registry().end() ? nullptr : it->second();
}

} // namespace eden
