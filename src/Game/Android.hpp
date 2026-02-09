#pragma once

#include "SentientBeing.hpp"

namespace eden {

/**
 * Android - synthetic human, designed to pass as human.
 * Advanced AI, simulated emotions, human-like responses.
 */
class Android : public SentientBeing {
public:
    Android();
    Android(const std::string& name);

    const char* getTypeName() const override { return "Android"; }

    // Future: model series, awakening status, empathy simulation level
};

} // namespace eden
