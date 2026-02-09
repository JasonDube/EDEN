#pragma once

#include "Eve.hpp"
#include <string>
#include <functional>

namespace eve {

/**
 * @brief ImGui-based chat interface for communicating with Eve
 */
class ChatInterface {
public:
    ChatInterface();
    ~ChatInterface() = default;
    
    /**
     * @brief Set the Eve instance to communicate with
     */
    void setEve(Eve* eve) { m_eve = eve; }
    
    /**
     * @brief Render the chat interface
     * @param width Window width for layout
     * @param height Window height for layout
     */
    void render(float width, float height);
    
    /**
     * @brief Focus the input field
     */
    void focusInput() { m_focusInput = true; }
    
    /**
     * @brief Check if chat has focus
     */
    bool hasFocus() const { return m_hasFocus; }
    
private:
    void renderMessageHistory(float width, float height);
    void renderInputArea(float width);
    void sendMessage();
    
    Eve* m_eve = nullptr;
    
    char m_inputBuffer[1024] = {0};
    bool m_focusInput = false;
    bool m_scrollToBottom = false;
    bool m_hasFocus = false;
    
    // Visual settings
    float m_messageSpacing = 10.0f;
    float m_avatarSize = 40.0f;
};

} // namespace eve
