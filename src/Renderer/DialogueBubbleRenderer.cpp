#include "DialogueBubbleRenderer.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace eden {

void DialogueBubbleRenderer::addBubble(const glm::vec3& worldPos, const std::string& text,
                                        float duration, bool isThought) {
    Bubble bubble;
    bubble.worldPos = worldPos;
    bubble.text = text;
    bubble.timeRemaining = duration > 0 ? duration : m_defaultDuration;
    bubble.bgColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.95f);      // White background
    bubble.borderColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);   // Dark border
    bubble.textColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);     // Black text
    bubble.isThought = isThought;
    m_bubbles.push_back(bubble);
}

void DialogueBubbleRenderer::update(float deltaTime) {
    // Update timers and remove expired bubbles
    m_bubbles.erase(
        std::remove_if(m_bubbles.begin(), m_bubbles.end(),
            [deltaTime](Bubble& b) {
                b.timeRemaining -= deltaTime;
                return b.timeRemaining <= 0;
            }),
        m_bubbles.end()
    );
}

bool DialogueBubbleRenderer::worldToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj,
                                            float screenWidth, float screenHeight, glm::vec2& screenPos) {
    // Transform to clip space
    glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
    
    // Behind camera check
    if (clipPos.w <= 0.0f) {
        return false;
    }
    
    // Perspective divide to NDC
    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
    
    // Check if in view frustum
    if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < 0.0f || ndc.z > 1.0f) {
        return false;
    }
    
    // Convert to screen coordinates (flip Y for screen space)
    screenPos.x = (ndc.x + 1.0f) * 0.5f * screenWidth;
    screenPos.y = (1.0f - ndc.y) * 0.5f * screenHeight;
    
    return true;
}

void DialogueBubbleRenderer::render(const glm::mat4& viewProj, float screenWidth, float screenHeight) {
    if (m_bubbles.empty()) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    
    for (const auto& bubble : m_bubbles) {
        glm::vec2 screenPos;
        if (!worldToScreen(bubble.worldPos, viewProj, screenWidth, screenHeight, screenPos)) {
            continue;  // Behind camera or out of view
        }
        
        if (bubble.isThought) {
            drawThoughtBubble(screenPos, bubble.text, bubble.bgColor, bubble.borderColor, bubble.textColor);
        } else {
            drawSpeechBubble(screenPos, bubble.text, bubble.bgColor, bubble.borderColor, bubble.textColor);
        }
    }
}

void DialogueBubbleRenderer::drawSpeechBubble(const glm::vec2& pos, const std::string& text,
                                               const glm::vec4& bgColor, const glm::vec4& borderColor,
                                               const glm::vec4& textColor) {
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    
    // Calculate text size with wrapping
    float fontSize = ImGui::GetFontSize() * m_fontScale;
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, m_maxWidth, text.c_str());
    
    // Bubble dimensions
    float bubbleWidth = textSize.x + m_padding * 2;
    float bubbleHeight = textSize.y + m_padding * 2;
    
    // Position bubble above the character (pos is at character's head)
    float bubbleX = pos.x - bubbleWidth * 0.5f;
    float bubbleY = pos.y - bubbleHeight - m_tailHeight - 10.0f;  // 10px extra offset
    
    // Clamp to screen edges
    bubbleX = std::max(10.0f, bubbleX);
    bubbleY = std::max(10.0f, bubbleY);
    
    ImVec2 bubbleMin(bubbleX, bubbleY);
    ImVec2 bubbleMax(bubbleX + bubbleWidth, bubbleY + bubbleHeight);
    
    // Colors
    ImU32 bgCol = IM_COL32(
        (int)(bgColor.r * 255), (int)(bgColor.g * 255),
        (int)(bgColor.b * 255), (int)(bgColor.a * 255));
    ImU32 borderCol = IM_COL32(
        (int)(borderColor.r * 255), (int)(borderColor.g * 255),
        (int)(borderColor.b * 255), (int)(borderColor.a * 255));
    ImU32 textCol = IM_COL32(
        (int)(textColor.r * 255), (int)(textColor.g * 255),
        (int)(textColor.b * 255), (int)(textColor.a * 255));
    
    // Draw rounded rectangle background
    drawList->AddRectFilled(bubbleMin, bubbleMax, bgCol, m_cornerRadius);
    
    // Draw tail (triangle pointing down to character)
    float tailCenterX = pos.x;
    // Clamp tail to bubble width
    tailCenterX = std::max(bubbleX + 20.0f, std::min(tailCenterX, bubbleX + bubbleWidth - 20.0f));
    
    ImVec2 tailPt1(tailCenterX - 10.0f, bubbleY + bubbleHeight);  // Left base
    ImVec2 tailPt2(tailCenterX + 10.0f, bubbleY + bubbleHeight);  // Right base
    ImVec2 tailPt3(pos.x, pos.y - 5.0f);                          // Point (near character)
    
    drawList->AddTriangleFilled(tailPt1, tailPt2, tailPt3, bgCol);
    
    // Draw border
    drawList->AddRect(bubbleMin, bubbleMax, borderCol, m_cornerRadius, 0, 2.0f);
    
    // Draw tail border (just the two outer edges)
    drawList->AddLine(tailPt1, tailPt3, borderCol, 2.0f);
    drawList->AddLine(tailPt2, tailPt3, borderCol, 2.0f);
    
    // Draw text
    ImVec2 textPos(bubbleX + m_padding, bubbleY + m_padding);
    drawList->AddText(font, fontSize, textPos, textCol, text.c_str(), nullptr, m_maxWidth);
}

void DialogueBubbleRenderer::drawThoughtBubble(const glm::vec2& pos, const std::string& text,
                                                const glm::vec4& bgColor, const glm::vec4& borderColor,
                                                const glm::vec4& textColor) {
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    
    // Calculate text size with wrapping
    float fontSize = ImGui::GetFontSize() * m_fontScale;
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, m_maxWidth, text.c_str());
    
    // Bubble dimensions
    float bubbleWidth = textSize.x + m_padding * 2;
    float bubbleHeight = textSize.y + m_padding * 2;
    
    // Position bubble above the character
    float bubbleX = pos.x - bubbleWidth * 0.5f;
    float bubbleY = pos.y - bubbleHeight - m_tailHeight - 30.0f;
    
    // Clamp to screen edges
    bubbleX = std::max(10.0f, bubbleX);
    bubbleY = std::max(10.0f, bubbleY);
    
    ImVec2 bubbleCenter(bubbleX + bubbleWidth * 0.5f, bubbleY + bubbleHeight * 0.5f);
    
    // Colors
    ImU32 bgCol = IM_COL32(
        (int)(bgColor.r * 255), (int)(bgColor.g * 255),
        (int)(bgColor.b * 255), (int)(bgColor.a * 255));
    ImU32 borderCol = IM_COL32(
        (int)(borderColor.r * 255), (int)(borderColor.g * 255),
        (int)(borderColor.b * 255), (int)(borderColor.a * 255));
    ImU32 textCol = IM_COL32(
        (int)(textColor.r * 255), (int)(textColor.g * 255),
        (int)(textColor.b * 255), (int)(textColor.a * 255));
    
    // Draw cloud-like bubble (ellipse)
    float radiusX = bubbleWidth * 0.5f + 5.0f;
    float radiusY = bubbleHeight * 0.5f + 5.0f;
    ImVec2 radius(radiusX, radiusY);
    drawList->AddEllipseFilled(bubbleCenter, radius, bgCol, 0.0f, 32);
    drawList->AddEllipse(bubbleCenter, radius, borderCol, 0.0f, 32, 2.0f);
    
    // Draw thought trail (three decreasing circles leading to character)
    float trailX = pos.x;
    float trailY = bubbleY + bubbleHeight + 5.0f;
    
    for (int i = 0; i < 3; i++) {
        float t = (float)i / 3.0f;
        float radius = 8.0f - i * 2.0f;
        float cx = trailX + (bubbleCenter.x - trailX) * (1.0f - t * 0.7f);
        float cy = trailY + (pos.y - trailY) * t * 0.8f;
        
        drawList->AddCircleFilled(ImVec2(cx, cy), radius, bgCol, 16);
        drawList->AddCircle(ImVec2(cx, cy), radius, borderCol, 16, 2.0f);
    }
    
    // Draw text
    ImVec2 textPos(bubbleX + m_padding, bubbleY + m_padding);
    drawList->AddText(font, fontSize, textPos, textCol, text.c_str(), nullptr, m_maxWidth);
}

void DialogueBubbleRenderer::clear() {
    m_bubbles.clear();
}

} // namespace eden
