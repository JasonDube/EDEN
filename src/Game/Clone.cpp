#include "Clone.hpp"
#include "../Renderer/ModelRenderer.hpp"

namespace eden {

Clone::Clone() : SentientBeing() {
    setName("Clone");
}

Clone::Clone(const std::string& name) : SentientBeing(name) {
}

} // namespace eden
