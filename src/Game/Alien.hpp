#pragma once

#include "SentientBeing.hpp"

namespace eden {

/**
 * Alien - extraterrestrial sentient being.
 * May have different communication patterns, values, physiology.
 */
class Alien : public SentientBeing {
public:
    Alien();
    Alien(const std::string& name);

    const char* getTypeName() const override { return "Alien"; }

    // Future: species, home world, translation accuracy, cultural traits
};

} // namespace eden
