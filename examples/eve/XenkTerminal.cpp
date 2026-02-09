#include "XenkTerminal.hpp"
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <ctime>
#include <iomanip>

namespace eve {

XenkTerminal::XenkTerminal() {
    m_lastPoll = std::chrono::steady_clock::now();
}

bool XenkTerminal::initialize() {
    // Setup IPC directories in OpenClaw workspace
    const char* home = std::getenv("HOME");
    if (!home) {
        addSystemMessage("ERROR: Could not determine HOME directory");
        return false;
    }
    
    std::filesystem::path openclawPath = std::filesystem::path(home) / ".openclaw";
    m_inboxPath = openclawPath / "eve_inbox";
    m_outboxPath = openclawPath / "eve_outbox";
    
    // Create directories if they don't exist
    try {
        std::filesystem::create_directories(m_inboxPath);
        std::filesystem::create_directories(m_outboxPath);
    } catch (const std::exception& e) {
        addSystemMessage(std::string("ERROR: Failed to create IPC directories: ") + e.what());
        return false;
    }
    
    addSystemMessage("XENK TERMINAL v1.0");
    addSystemMessage("IPC initialized at ~/.openclaw/eve_inbox|outbox");
    addSystemMessage("Type 'help' for available commands");
    addSystemMessage("─────────────────────────────────────────");
    
    return true;
}

void XenkTerminal::update() {
    // Poll for responses every 500ms
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPoll);
    
    if (elapsed.count() >= 500) {
        checkForResponses();
        m_lastPoll = now;
    }
}

void XenkTerminal::checkForResponses() {
    if (!std::filesystem::exists(m_outboxPath)) return;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(m_outboxPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".txt") {
                // Read response
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string response = buffer.str();
                    
                    if (!response.empty()) {
                        TerminalEntry respEntry;
                        respEntry.type = TerminalEntry::Type::Response;
                        respEntry.text = response;
                        respEntry.timestamp = getCurrentTimestamp();
                        m_history.push_back(respEntry);
                        
                        if (m_history.size() > MAX_HISTORY) {
                            m_history.pop_front();
                        }
                        
                        m_scrollToBottom = true;
                    }
                    
                    file.close();
                }
                
                // Delete processed file
                std::filesystem::remove(entry.path());
            }
        }
    } catch (const std::exception& e) {
        // Silently ignore errors during polling
    }
}

void XenkTerminal::sendCommand(const std::string& command) {
    if (command.empty()) return;
    
    // Add command to history
    TerminalEntry cmdEntry;
    cmdEntry.type = TerminalEntry::Type::Command;
    cmdEntry.text = command;
    cmdEntry.timestamp = getCurrentTimestamp();
    m_history.push_back(cmdEntry);
    
    if (m_history.size() > MAX_HISTORY) {
        m_history.pop_front();
    }
    
    // Handle local commands
    if (command == "help") {
        addSystemMessage("Available commands:");
        addSystemMessage("  help       - Show this help");
        addSystemMessage("  clear      - Clear terminal");
        addSystemMessage("  status     - Show system status");
        addSystemMessage("  ping       - Test Xenk connection");
        addSystemMessage("  <anything> - Send to Xenk");
        return;
    }
    
    if (command == "clear") {
        m_history.clear();
        addSystemMessage("Terminal cleared");
        return;
    }
    
    if (command == "status") {
        addSystemMessage("XENK TERMINAL STATUS");
        addSystemMessage("  Inbox:  " + m_inboxPath.string());
        addSystemMessage("  Outbox: " + m_outboxPath.string());
        addSystemMessage("  History entries: " + std::to_string(m_history.size()));
        return;
    }
    
    // Write command to inbox for Xenk to pick up
    try {
        m_pendingCommandId++;
        std::string filename = "cmd_" + std::to_string(m_pendingCommandId) + "_" + 
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt";
        
        std::ofstream file(m_inboxPath / filename);
        if (file.is_open()) {
            file << command;
            file.close();
            
            // Show waiting indicator
            TerminalEntry waitEntry;
            waitEntry.type = TerminalEntry::Type::System;
            waitEntry.text = "[Transmitting to Xenk...]";
            waitEntry.timestamp = getCurrentTimestamp();
            m_history.push_back(waitEntry);
        } else {
            TerminalEntry errEntry;
            errEntry.type = TerminalEntry::Type::Error;
            errEntry.text = "ERROR: Failed to write to inbox";
            errEntry.timestamp = getCurrentTimestamp();
            m_history.push_back(errEntry);
        }
    } catch (const std::exception& e) {
        TerminalEntry errEntry;
        errEntry.type = TerminalEntry::Type::Error;
        errEntry.text = std::string("ERROR: ") + e.what();
        errEntry.timestamp = getCurrentTimestamp();
        m_history.push_back(errEntry);
    }
    
    m_scrollToBottom = true;
}

std::string XenkTerminal::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&time);
    
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << tm->tm_hour << ":"
       << std::setfill('0') << std::setw(2) << tm->tm_min << ":"
       << std::setfill('0') << std::setw(2) << tm->tm_sec;
    return ss.str();
}

void XenkTerminal::addSystemMessage(const std::string& msg) {
    TerminalEntry entry;
    entry.type = TerminalEntry::Type::System;
    entry.text = msg;
    entry.timestamp = getCurrentTimestamp();
    m_history.push_back(entry);
    
    if (m_history.size() > MAX_HISTORY) {
        m_history.pop_front();
    }
    
    m_scrollToBottom = true;
}

void XenkTerminal::render() {
    if (!m_visible) return;
    
    ImGui::SetNextWindowPos(ImVec2(10, 320), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    
    // Terminal styling - dark, monospace feel
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.15f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    
    if (ImGui::Begin("XENK /// ARCHITECT TERMINAL", &m_visible, flags)) {
        m_hasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        
        float footerHeight = ImGui::GetFrameHeightWithSpacing() + 4;
        
        // Terminal content area
        ImGui::BeginChild("TerminalScroll", ImVec2(0, -footerHeight), true);
        renderTerminalContent();
        ImGui::EndChild();
        
        // Input line
        renderInputLine();
    }
    ImGui::End();
    
    ImGui::PopStyleColor(4);
}

void XenkTerminal::renderTerminalContent() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
    
    for (const auto& entry : m_history) {
        ImVec4 color;
        std::string prefix;
        
        switch (entry.type) {
            case TerminalEntry::Type::Command:
                color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);  // Green
                prefix = "> ";
                break;
            case TerminalEntry::Type::Response:
                color = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);  // Light blue
                prefix = "XENK: ";
                break;
            case TerminalEntry::Type::System:
                color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);  // Gray
                prefix = "// ";
                break;
            case TerminalEntry::Type::Error:
                color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  // Red
                prefix = "!! ";
                break;
        }
        
        // Timestamp
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("[%s] ", entry.timestamp.c_str());
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        
        // Content
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s%s", prefix.c_str(), entry.text.c_str());
        ImGui::PopStyleColor();
    }
    
    ImGui::PopStyleVar();
    
    // Auto-scroll
    if (m_scrollToBottom || (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10)) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }
}

void XenkTerminal::renderInputLine() {
    ImGui::Separator();
    
    // Focus input if requested
    if (m_focusInput) {
        ImGui::SetKeyboardFocusHere();
        m_focusInput = false;
    }
    
    // Prompt
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
    ImGui::Text(">");
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    
    // Input field
    ImGui::PushItemWidth(-1);
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    
    if (ImGui::InputText("##TerminalInput", m_inputBuffer, sizeof(m_inputBuffer), inputFlags)) {
        std::string cmd(m_inputBuffer);
        m_inputBuffer[0] = '\0';
        
        if (!cmd.empty()) {
            sendCommand(cmd);
        }
        
        m_focusInput = true;  // Keep focus after sending
    }
    ImGui::PopItemWidth();
}

} // namespace eve
