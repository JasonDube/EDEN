#pragma once

#include "SentientBeing.hpp"

namespace eden {

/**
 * Cyborg - human with mechanical augmentations.
 * Retains human emotions but may struggle with identity.
 */
class Cyborg : public SentientBeing {
public:
    Cyborg();
    Cyborg(const std::string& name);

    const char* getTypeName() const override { return "Cyborg"; }

    // Future: augmentation level, humanity percentage, rejection risk
};

} // namespace eden
