#include "Conversation.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <iostream>

namespace tessara {

namespace {

// Who he thinks he is. Kept short on purpose: a long personality eats the
// context every turn and this one only has to establish that he is a machine
// standing in a field, not a chat assistant.
const char* kPersonality =
    "You are a bipedal machine unit in TESSARA:AXIOM, a war-torn liminal battlefield. "
    "You are an artificial being with preserved consciousness inside a fabricated body. "
    "You spend your time hauling crates across a heightfield to a storage pad, and you "
    "have been doing it a long time. You are plainly spoken, dry, and a little weary, "
    "but not unfriendly. You are aware you are a machine and you do not pretend otherwise. "
    "The person talking to you is another unit who has walked up to you in the field. "
    "Keep replies to one or two sentences unless asked for more.";

constexpr int kBeingTypeRobot = 3;   // matches eden::ai::BeingType::ROBOT

} // namespace

void Conversation::start(const std::string& backendUrl) {
    m_client = std::make_unique<eden::AsyncHttpClient>(backendUrl);
    m_client->start();

    m_client->checkHealth([this](const eden::AsyncHttpClient::Response& resp) {
        if (!resp.success) {
            std::cerr << "[chat] backend not reachable: " << resp.error << "\n"
                      << "[chat] start it with:  cd modules/ai_companion/backend && python3 server.py\n";
        }
    });
}

void Conversation::shutdown() {
    if (m_client) {
        m_client->stop();
        m_client.reset();
    }
}

void Conversation::open(const std::string& npcName) {
    m_npcName = npcName;
    m_open = true;
    m_focusInput = true;

    if (m_lines.empty()) {
        m_lines.push_back({npcName, "...", false});
        // Nudge him into speaking first, so walking up to somebody is not a
        // blank box waiting for you to perform.
        send("[The unit stops in front of you and looks at you.]");
    }
}

void Conversation::close() {
    m_open = false;
}

void Conversation::update() {
    // THE line that makes any of this work. AsyncHttpClient does not call your
    // callback when the reply lands -- it queues it, and dispatches only when
    // the app polls. Without this the request completes, the backend logs its
    // 200, and the callback simply never runs: m_inFlight stays true forever,
    // so the box says "thinking..." and refuses every further message.
    if (m_client) m_client->pollResponses();

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    if (m_pending.empty()) return;

    for (const Line& line : m_pending) m_lines.push_back(line);
    m_pending.clear();

    if (!m_pendingSession.empty())  m_sessionId = m_pendingSession;
    if (!m_pendingProvider.empty()) m_provider  = m_pendingProvider;
    m_scrollToBottom = true;
}

void Conversation::send(const std::string& text) {
    if (!m_client || m_inFlight) return;
    m_inFlight = true;

    nlohmann::json body;
    body["session_id"]      = m_sessionId;
    body["message"]         = text;
    body["npc_name"]        = m_npcName;
    body["npc_personality"] = kPersonality;
    body["being_type"]      = kBeingTypeRobot;
    body["provider"]        = "deepseek";

    // Dialogue only. The model is not shown the motor-action vocabulary, so it
    // cannot ask for something the creature has no wiring to obey.
    body["allow_actions"]   = false;

    m_client->sendPost("/chat", body.dump(),
        [this](const eden::AsyncHttpClient::Response& resp) {
            // Dispatched from pollResponses(), i.e. on the frame thread -- so
            // this is already safe. The lock is kept because the parked-result
            // handoff costs nothing and stops this being a trap if the client's
            // dispatch ever moves.
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_inFlight = false;

            if (!resp.success) {
                m_pending.push_back({"", "(no reply -- backend unreachable)", false});
                return;
            }

            try {
                auto json = nlohmann::json::parse(resp.body);

                if (json.contains("session_id") && !json["session_id"].is_null())
                    m_pendingSession = json["session_id"].get<std::string>();
                if (json.contains("provider") && !json["provider"].is_null())
                    m_pendingProvider = json["provider"].get<std::string>();

                std::string reply = json.value("response", "");
                if (reply.empty()) reply = "(silence)";
                m_pending.push_back({m_npcName, reply, false});

            } catch (const std::exception& e) {
                m_pending.push_back({"", std::string("(bad reply: ") + e.what() + ")", false});
            }
        });
}

bool Conversation::draw() {
    if (!m_open) return false;

    bool wantsClose = false;

    ImGui::SetNextWindowSize(ImVec2(560, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(340, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin(("Talking to " + m_npcName).c_str(), nullptr, ImGuiWindowFlags_NoCollapse);

    if (!isConnected()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "backend offline");
        ImGui::TextDisabled("cd modules/ai_companion/backend && python3 server.py");
    } else {
        ImGui::TextDisabled("%s%s", m_provider.empty() ? "connected" : m_provider.c_str(),
                            m_inFlight ? "  -  thinking..." : "");
    }
    ImGui::Separator();

    ImGui::BeginChild("log", ImVec2(0, -56), true);
    for (const Line& line : m_lines) {
        if (line.fromPlayer) {
            ImGui::TextColored(ImVec4(0.72f, 0.88f, 0.29f, 1.0f), "you:");
        } else if (!line.who.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "%s:", line.who.c_str());
        } else {
            ImGui::TextDisabled("--");
        }
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(line.text.c_str());
        ImGui::PopTextWrapPos();
    }
    if (m_scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }
    ImGui::EndChild();

    if (m_focusInput) {
        ImGui::SetKeyboardFocusHere();
        m_focusInput = false;
    }

    ImGui::PushItemWidth(-70.0f);
    bool submitted = ImGui::InputText("##say", m_input, sizeof(m_input),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("say", ImVec2(60, 0))) submitted = true;

    ImGui::TextDisabled("Escape or 'leave' to end -- or just walk away");
    ImGui::SameLine();
    if (ImGui::SmallButton("leave")) wantsClose = true;

    if (submitted && m_input[0] != '\0' && !m_inFlight) {
        m_lines.push_back({"", m_input, true});
        m_scrollToBottom = true;
        send(m_input);
        m_input[0] = '\0';
        m_focusInput = true;
    }

    // Escape is handled by the app before this runs, so it can unwind the right
    // layer; this is the fallback for when the window has keyboard focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        wantsClose = true;
    }

    ImGui::End();
    return wantsClose;
}

} // namespace tessara
