#pragma once

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <filesystem>
#include <chrono>

namespace eve {

/**
 * @brief Terminal entry (command or response)
 */
struct TerminalEntry {
    enum class Type { Command, Response, System, Error };
    Type type;
    std::string text;
    std::string timestamp;
};

/**
 * @brief Xenk's command terminal interface
 * 
 * Communicates with Xenk (Claude via OpenClaw) through file-based IPC:
 * - Commands written to ~/.openclaw/eve_inbox/
 * - Responses read from ~/.openclaw/eve_outbox/
 */
class XenkTerminal {
public:
    XenkTerminal();
    ~XenkTerminal() = default;
    
    /**
     * @brief Initialize the terminal and IPC directories
     */
    bool initialize();
    
    /**
     * @brief Update - check for responses from Xenk
     */
    void update();
    
    /**
     * @brief Render the terminal window
     */
    void render();
    
    /**
     * @brief Check if terminal is visible
     */
    bool isVisible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; }
    void toggleVisible() { m_visible = !m_visible; }
    
    /**
     * @brief Check if terminal has input focus
     */
    bool hasFocus() const { return m_hasFocus; }
    
    /**
     * @brief Add a system message to the terminal
     */
    void addSystemMessage(const std::string& msg);
    
private:
    void sendCommand(const std::string& command);
    void checkForResponses();
    std::string getCurrentTimestamp();
    void renderTerminalContent();
    void renderInputLine();
    
    // IPC paths
    std::filesystem::path m_inboxPath;   // We write here (to Xenk)
    std::filesystem::path m_outboxPath;  // We read from here (from Xenk)
    
    // Terminal state
    std::deque<TerminalEntry> m_history;
    static constexpr size_t MAX_HISTORY = 500;
    
    char m_inputBuffer[1024] = {0};
    bool m_visible = true;
    bool m_hasFocus = false;
    bool m_scrollToBottom = false;
    bool m_focusInput = false;
    
    // Response polling
    std::chrono::steady_clock::time_point m_lastPoll;
    int m_pendingCommandId = 0;
    
    // Visual settings
    bool m_autoScroll = true;
};

} // namespace eve
