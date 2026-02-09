#pragma once

#include "SentientBeing.hpp"

namespace eden {

/**
 * Robot - fully mechanical, no biological components.
 * Logical responses, no emotions (or simulated ones).
 */
class Robot : public SentientBeing {
public:
    Robot();
    Robot(const std::string& name);

    const char* getTypeName() const override { return "Robot"; }

    // Future: power level, directives, manufacturer
};

} // namespace eden
