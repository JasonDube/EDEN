#pragma once
// AgentConsole — the EDEN OS agent chat, reimagined as a lightweight CLI.
//
// Old model (removed): a tabbed "Agents" ImGui window on the right edge, pick a
// tab, type, read a scrolling transcript. Too many clicks, hogs the screen.
//
// New model: you AIM at a robot and click it. A transparent, cheat-code-style
// prompt appears at the bottom-left of the screen (to the left of the hotbar).
// Type, Enter to send; the reply prints inline above the prompt. Click a
// different robot to switch; Enter on an empty line closes it.
//
// This lives OUTSIDE the god file on purpose. It has ZERO engine coupling — the
// host wires ONE send callback (setSendFn) that does the actual /chat call, and
// calls select()/render() from the play loop. Nothing else leaks in.
#include <imgui.h>
#include <string>
#include <vector>
#include <functional>
#include <cstring>

namespace eden {

class AgentConsole {
public:
    // What the host's send callback returns once the model answers. `provider`
    // and `model` are the ACTUAL brain that replied (may differ from the
    // requested provider on a silent fallback) — we stamp it so you can tell.
    struct Reply {
        std::string sessionId;
        std::string text;
        std::string provider;
        std::string model;
        bool ok = false;
    };
    using ReplyCb = std::function<void(const Reply&)>;
    // (agentName, provider, sessionId, message, onReply)
    using SendFn  = std::function<void(const std::string&, const std::string&,
                                       const std::string&, const std::string&, ReplyCb)>;
    // Real-time command (e.g. activate_bot). Host runs it (via Grove), fills `out`
    // with a result line, and returns true if the input WAS a command (so it's
    // not also sent as chat). (selectedBotName, rawInput, out) -> handled?
    using CommandFn = std::function<bool(const std::string&, const std::string&, std::string&)>;
    // Is the named bot currently switched on? Gates chat + colors the status line.
    using IsActiveFn = std::function<bool(const std::string&)>;
    // Show text as a speech bubble over the named bot (host draws it in-world).
    // Used for the "…" thinking state and the reply, so the chat panel can close
    // and the player can move while the answer floats over the robot's head.
    using BubbleFn = std::function<void(const std::string&, const std::string&)>;

    void setSendFn(SendFn fn) { m_send = std::move(fn); }
    void setCommandFn(CommandFn fn) { m_cmd = std::move(fn); }
    void setIsActiveFn(IsActiveFn fn) { m_isActive = std::move(fn); }
    void setBubbleFn(BubbleFn fn) { m_bubble = std::move(fn); }

    // Provider -> accent color, shared with the floating name tags.
    static ImU32 providerColor(const std::string& p) {
        return p == "claude"   ? IM_COL32(233, 143,  74, 255)
             : p == "grok"     ? IM_COL32( 90, 168, 235, 255)
             : p == "deepseek" ? IM_COL32(150, 118, 224, 255)
             : p == "gemma"    ? IM_COL32(226, 110, 180, 255)
             : p == "heretic"  ? IM_COL32(190,  60,  70, 255)
             : p == "ollama"   ? IM_COL32(104, 202, 130, 255)
                               : IM_COL32(220, 220, 220, 255);
    }

    // Player clicked a robot. Switch the console to it (fresh transcript+session
    // when it's a different agent; re-selecting the same one just refocuses).
    void select(const std::string& name, const std::string& provider) {
        if (name != m_name) {
            m_history.clear();
            m_sessionId.clear();
            m_input[0] = '\0';
        }
        m_name = name;
        m_provider = provider;
        m_open = true;
        m_focusInput = true;
        // Immediately tell the player whether this bot is on, and how to switch it.
        bool on = m_isActive ? m_isActive(name) : true;
        m_history.push_back({on ? (name + " is online.")
                                : (name + " is offline — type  activate_bot  to switch it on."), false});
        m_scrollBottom = true;
        trim();
    }

    void close() { m_open = false; }
    bool isOpen() const { return m_open; }
    const std::string& selectedName() const { return m_name; }

    // Draw the bottom-left CLI. `hotbarLeftX` is the screen X where the hotbar
    // begins — the console's right edge stays left of it so they never overlap.
    void render(float screenW, float screenH, float hotbarLeftX) {
        if (!m_open) return;

        const float pad = 12.0f;
        float right = (hotbarLeftX > 200.0f ? hotbarLeftX : screenW * 0.5f) - pad;
        float x = pad;
        float w = right - x;
        if (w < 260.0f) w = 260.0f;
        const float h = 168.0f;
        float y = screenH - h - pad;

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        // Faint dark backdrop so text stays legible without hogging the view.
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.03f, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,  ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
        if (ImGui::Begin("##agentconsole", nullptr, flags)) {
            ImU32 accent = providerColor(m_provider);

            // Transcript (last few exchanges), oldest-of-the-window at top.
            float inputH = ImGui::GetFrameHeightWithSpacing();
            float logH = ImGui::GetContentRegionAvail().y - inputH;
            if (logH < 20.0f) logH = 20.0f;
            ImGui::BeginChild("##log", ImVec2(0, logH), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            for (const auto& ln : m_history) {
                ImGui::PushStyleColor(ImGuiCol_Text, ln.isPlayer
                    ? ImVec4(0.72f, 0.90f, 0.72f, 0.95f)
                    : ImGui::ColorConvertU32ToFloat4(accent));
                ImGui::TextWrapped("%s", ln.text.c_str());
                ImGui::PopStyleColor();
            }
            if (m_waiting) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 0.9f));
                ImGui::TextUnformatted("...");
                ImGui::PopStyleColor();
            }
            if (m_scrollBottom) { ImGui::SetScrollHereY(1.0f); m_scrollBottom = false; }
            ImGui::EndChild();

            // Prompt line:  name>  ______
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(accent));
            ImGui::TextUnformatted((m_name + ">").c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (m_focusInput) { ImGui::SetKeyboardFocusHere(); m_focusInput = false; }
            ImGui::SetNextItemWidth(-1.0f);
            bool enter = ImGui::InputText("##agentcli", m_input, sizeof(m_input),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter) {
                if (m_input[0] == '\0') {
                    m_open = false; // Enter on empty line closes the console.
                } else {
                    submit();
                    m_focusInput = true; // keep typing without re-clicking
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(2);
    }

private:
    struct Line { std::string text; bool isPlayer; };

    void submit() {
        std::string msg = m_input;
        m_input[0] = '\0';
        if (msg.empty()) { m_open = false; return; }

        // Real-time command? (activate_bot, …) Run it and KEEP the console open,
        // so you can flip a bot on and then chat in the same session.
        if (m_cmd) {
            std::string out;
            if (m_cmd(m_name, msg, out)) {
                m_history.push_back({"you> " + msg, true});
                if (!out.empty()) m_history.push_back({out, false});
                m_scrollBottom = true; m_focusInput = true; trim();
                return;
            }
        }

        // Chatting an off bot: tell them, stay open so they can type activate_bot.
        if (m_isActive && !m_isActive(m_name)) {
            m_history.push_back({"you> " + msg, true});
            m_history.push_back({m_name + " is offline — type  activate_bot  to switch it on.", false});
            m_scrollBottom = true; m_focusInput = true; trim();
            return;
        }
        if (!m_send) { m_open = false; return; }

        // Real chat: fire ONE message, float a "…" over the bot, then CLOSE the
        // input so the player can move. The reply arrives as a bubble over the
        // bot (routed by name, so it shows on the right robot even if the player
        // has since clicked another). Click the bot again to say more.
        if (m_bubble) m_bubble(m_name, "...");
        std::string forName = m_name;
        m_send(m_name, m_provider, m_sessionId, msg,
               [this, forName](const Reply& r) {
                   if (r.ok) {
                       if (!r.sessionId.empty() && forName == m_name) m_sessionId = r.sessionId;
                       if (m_bubble) m_bubble(forName, r.text.empty() ? "..." : r.text);
                   } else if (m_bubble) {
                       m_bubble(forName, "(no response)");
                   }
               });
        m_open = false;
    }

    // Keep the transcript short — this is a glance-CLI, not a scrollback buffer.
    void trim() {
        const size_t kMax = 12;
        if (m_history.size() > kMax)
            m_history.erase(m_history.begin(), m_history.end() - kMax);
    }

    std::string m_name, m_provider, m_sessionId;
    std::vector<Line> m_history;
    char m_input[512] = {0};
    bool m_open = false;
    bool m_waiting = false;
    bool m_focusInput = false;
    bool m_scrollBottom = false;
    SendFn m_send;
    CommandFn m_cmd;
    IsActiveFn m_isActive;
    BubbleFn m_bubble;
};

} // namespace eden
