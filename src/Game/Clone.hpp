#pragma once

#include "SentientBeing.hpp"

namespace eden {

/**
 * Clone - genetically created human.
 * May have identity issues, shorter lifespan concerns.
 */
class Clone : public SentientBeing {
public:
    Clone();
    Clone(const std::string& name);

    const char* getTypeName() const override { return "Clone"; }

    // Future: generation number, genetic stability, origin template
};

} // namespace eden
