#include "GameModule.hpp"

namespace eden {

std::vector<std::string> GameModuleFactory::getAvailableModules() {
    return {};
}

std::unique_ptr<GameModule> GameModuleFactory::create(const std::string& /*moduleName*/) {
    return nullptr;
}

} // namespace eden
