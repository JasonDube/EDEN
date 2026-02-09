#include "Robot.hpp"
#include "../Renderer/ModelRenderer.hpp"

namespace eden {

Robot::Robot() : SentientBeing() {
    setName("Robot");
}

Robot::Robot(const std::string& name) : SentientBeing(name) {
}

} // namespace eden
