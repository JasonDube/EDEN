#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace eden {

/**
 * Renders comic-style dialogue bubbles above characters.
 * Uses ImGui draw list for 2D overlay rendering.
 */
class DialogueBubbleRenderer {
public:
    struct Bubble {
        glm::vec3 worldPos;       // 3D position (character head)
        std::string text;         // Dialogue text
        float timeRemaining;      // Auto-hide timer
        glm::vec4 bgColor;        // Background color
        glm::vec4 borderColor;    // Border color
        glm::vec4 textColor;      // Text color
        bool isThought;           // Thought bubble (cloud style) vs speech
    };

    DialogueBubbleRenderer() = default;

    // Add a bubble to render
    void addBubble(const glm::vec3& worldPos, const std::string& text, 
                   float duration = 3.0f, bool isThought = false);

    // Update timers, remove expired bubbles
    void update(float deltaTime);

    // Render all active bubbles
    // viewProj: combined view-projection matrix
    // screenWidth/Height: viewport dimensions
    void render(const glm::mat4& viewProj, float screenWidth, float screenHeight);

    // Clear all bubbles
    void clear();

    // Settings
    void setDefaultDuration(float duration) { m_defaultDuration = duration; }
    void setPadding(float padding) { m_padding = padding; }
    void setTailHeight(float height) { m_tailHeight = height; }
    void setMaxWidth(float width) { m_maxWidth = width; }
    void setFontScale(float scale) { m_fontScale = scale; }

private:
    // Project 3D world position to 2D screen position
    // Returns false if behind camera
    bool worldToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj,
                       float screenWidth, float screenHeight, glm::vec2& screenPos);

    // Draw a speech bubble (rounded rect with pointed tail)
    void drawSpeechBubble(const glm::vec2& pos, const std::string& text,
                          const glm::vec4& bgColor, const glm::vec4& borderColor,
                          const glm::vec4& textColor);

    // Draw a thought bubble (cloud style with circular tail)
    void drawThoughtBubble(const glm::vec2& pos, const std::string& text,
                           const glm::vec4& bgColor, const glm::vec4& borderColor,
                           const glm::vec4& textColor);

    std::vector<Bubble> m_bubbles;

    // Settings
    float m_defaultDuration = 3.0f;
    float m_padding = 12.0f;
    float m_tailHeight = 20.0f;
    float m_maxWidth = 300.0f;
    float m_fontScale = 1.2f;
    float m_cornerRadius = 10.0f;
};

} // namespace eden
