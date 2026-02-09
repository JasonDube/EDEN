#include "Human.hpp"
#include "../Renderer/ModelRenderer.hpp"

namespace eden {

Human::Human() : SentientBeing() {
    setName("Human");
}

Human::Human(const std::string& name) : SentientBeing(name) {
}

} // namespace eden
