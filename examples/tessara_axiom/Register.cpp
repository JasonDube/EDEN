#include "Register.hpp"

#include "TessaraModule.hpp"
#include "GameModules/GameModule.hpp"

#include <memory>

namespace tessara {

void registerGameModule() {
    eden::GameModuleFactory::registerModule("tessara", [] {
        return std::make_unique<TessaraModule>();
    });
}

} // namespace tessara
