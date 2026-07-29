#pragma once

// Talking to the biped.
//
// Deliberately dialogue ONLY for now: the request sets allow_actions false, so
// the model is never offered the motor-action block that EDEN OS's NPCs use.
// The creature already has move_to, pickup, place and stop implemented properly
// as its own task machine, and hooking a model to them is worth doing carefully
// rather than as a side effect of getting him to speak.

#include "Network/AsyncHttpClient.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tessara {

class Conversation {
public:
    struct Line {
        std::string who;
        std::string text;
        bool fromPlayer = false;
    };

    void start(const std::string& backendUrl = "http://localhost:8080");
    void shutdown();

    // Drains anything the worker thread delivered. Call once a frame, from the
    // frame thread -- the HTTP callbacks land on a background thread and ImGui
    // state must never be touched from there.
    void update();

    void open(const std::string& npcName);
    void close();

    bool isOpen() const { return m_open; }
    bool isConnected() const { return m_client && m_client->isConnected(); }
    bool isThinking() const { return m_inFlight; }
    const std::string& provider() const { return m_provider; }

    // Returns true if the player asked to close it (Escape inside the window).
    bool draw();

private:
    void send(const std::string& text);

    std::unique_ptr<eden::AsyncHttpClient> m_client;

    std::string m_npcName;
    std::string m_sessionId;
    std::string m_provider;

    std::vector<Line> m_lines;
    char m_input[512] = {0};

    bool m_open = false;
    bool m_inFlight = false;
    bool m_scrollToBottom = false;
    bool m_focusInput = false;

    // Filled by the HTTP worker, drained by update().
    std::mutex m_pendingMutex;
    std::vector<Line> m_pending;
    std::string m_pendingSession;
    std::string m_pendingProvider;
};

} // namespace tessara
