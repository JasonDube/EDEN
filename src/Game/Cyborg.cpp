#include "Cyborg.hpp"
#include "../Renderer/ModelRenderer.hpp"

namespace eden {

Cyborg::Cyborg() : SentientBeing() {
    setName("Cyborg");
}

Cyborg::Cyborg(const std::string& name) : SentientBeing(name) {
}

} // namespace eden
