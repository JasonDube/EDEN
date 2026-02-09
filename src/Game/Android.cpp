#include "Android.hpp"
#include "../Renderer/ModelRenderer.hpp"

namespace eden {

Android::Android() : SentientBeing() {
    setName("Android");
}

Android::Android(const std::string& name) : SentientBeing(name) {
}

} // namespace eden
