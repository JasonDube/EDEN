#pragma once

#include <string>
#include <cstdint>

namespace eden {
namespace ai {

/**
 * Type of being for dialogue/interaction purposes.
 * Determines personality template and conversation style.
 */
enum class BeingType : uint8_t {
    STATIC = 0,       // Non-interactive object (default)
    HUMAN = 1,        // Human character
    CLONE = 2,        // Cloned human
    ROBOT = 3,        // Mechanical robot
    ANDROID = 4,      // Human-like robot
    CYBORG = 5,       // Part human, part machine
    ALIEN = 6,        // Extraterrestrial being
    EVE = 7,          // Eve companion android (special)
    AI_ARCHITECT = 8, // AI world architect (Xenk)
    ALGOBOT = 9,      // Algorithmic bot — executes Grove scripts, no chat
    EDEN_COMPANION = 10, // EDEN companion — tabula rasa AI partner (Liora etc.)

    COUNT
};

/**
 * Get display name for being type.
 */
inline const char* getBeingTypeName(BeingType type) {
    switch (type) {
        case BeingType::STATIC:       return "Static";
        case BeingType::HUMAN:        return "Human";
        case BeingType::CLONE:        return "Clone";
        case BeingType::ROBOT:        return "Robot";
        case BeingType::ANDROID:      return "Android";
        case BeingType::CYBORG:       return "Cyborg";
        case BeingType::ALIEN:        return "Alien";
        case BeingType::EVE:          return "Eve";
        case BeingType::AI_ARCHITECT: return "AI Architect";
        case BeingType::ALGOBOT:      return "AlgoBot";
        case BeingType::EDEN_COMPANION: return "EDEN Companion";
        default: return "Unknown";
    }
}

/**
 * Check if being type can engage in conversation.
 */
inline bool isSentient(BeingType type) {
    return type != BeingType::STATIC;
}

/**
 * Check if being type is an AI (non-biological).
 */
inline bool isArtificial(BeingType type) {
    return type == BeingType::ROBOT ||
           type == BeingType::ANDROID ||
           type == BeingType::EVE ||
           type == BeingType::AI_ARCHITECT ||
           type == BeingType::ALGOBOT ||
           type == BeingType::EDEN_COMPANION;
}

} // namespace ai
} // namespace eden
