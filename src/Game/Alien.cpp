#include "Alien.hpp"
#include "../Renderer/ModelRenderer.hpp"

namespace eden {

Alien::Alien() : SentientBeing() {
    setName("Alien");
}

Alien::Alien(const std::string& name) : SentientBeing(name) {
}

} // namespace eden
