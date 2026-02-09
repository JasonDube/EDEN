#pragma once

#include "SentientBeing.hpp"

namespace eden {

/**
 * Human - baseline sentient being.
 * Standard emotional responses, natural dialogue patterns.
 */
class Human : public SentientBeing {
public:
    Human();
    Human(const std::string& name);

    const char* getTypeName() const override { return "Human"; }

    // Future: mood, relationships, needs, etc.
};

} // namespace eden
