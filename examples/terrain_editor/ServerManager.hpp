#pragma once

#include <string>
#include <vector>
#include <array>
#include <functional>
#include <cstring>
#include <imgui.h>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>

struct ServerEntry {
    std::string name;
    std::string command;
    std::string workingDir;
    int port;
    pid_t pid = 0;
    int pipefd = -1;  // read end of stdout/stderr pipe
    std::vector<std::string> log;
    enum Status { Stopped, Starting, Running, Error } status = Stopped;
    int dependsOn = -1;  // index of server that must be Running before this one starts

    bool isRunning() const { return status == Running || status == Starting; }
};

class ServerManager {
public:
    // Server indices (for dependency references and external access)
    static constexpr int IDX_OLLAMA   = 0;
    static constexpr int IDX_BACKEND  = 1;
    static constexpr int IDX_HUNYUAN  = 2;

    explicit ServerManager(const std::string& projectRoot = "") {
        m_projectRoot = projectRoot;

        // Ollama (index 0 — no dependencies, must start first)
        ServerEntry ollama;
        ollama.name = "Ollama";
        ollama.command = "ollama serve";
        ollama.workingDir = "";  // system command
        ollama.port = 11434;
        m_servers.push_back(std::move(ollama));

        // AI Backend (index 1 — depends on Ollama)
        ServerEntry ai;
        ai.name = "AI Backend";
        ai.command = "python3 server.py";
        ai.workingDir = projectRoot.empty() ? "modules/ai_companion/backend"
                                            : projectRoot + "/modules/ai_companion/backend";
        ai.port = 8080;
        ai.dependsOn = IDX_OLLAMA;
        m_servers.push_back(std::move(ai));

        // Hunyuan3D (index 2 — no dependencies)
        ServerEntry hunyuan;
        hunyuan.name = "Hunyuan3D";
        hunyuan.command = "bash launch_hunyuan.sh";
        hunyuan.workingDir = "";  // configurable
        hunyuan.port = 8081;
        m_servers.push_back(std::move(hunyuan));
    }

    ~ServerManager() {
        // Don't kill servers on exit — let them persist across game restarts.
        // User manages server lifetime via the Servers window.
        // Just close our pipe file descriptors.
        for (auto& srv : m_servers) {
            if (srv.pipefd >= 0) {
                close(srv.pipefd);
                srv.pipefd = -1;
            }
        }
    }

    void stopAll() {
        for (size_t i = 0; i < m_servers.size(); i++) {
            if (m_servers[i].pid > 0) {
                stop(i);
            }
        }
    }

    // Restart all: stop everything, then start once all are dead
    void restartAll() {
        stopAll();
        m_restartPending = true;
    }

    // Start all servers in dependency order
    void startAll() {
        m_startAllPending = true;
        m_startAllQueue.clear();
        // Build ordered list: dependencies first
        for (size_t i = 0; i < m_servers.size(); i++) {
            if (m_servers[i].dependsOn < 0 && !m_servers[i].isRunning())
                m_startAllQueue.push_back(i);
        }
        for (size_t i = 0; i < m_servers.size(); i++) {
            if (m_servers[i].dependsOn >= 0 && !m_servers[i].isRunning())
                m_startAllQueue.push_back(i);
        }
        // Kick off the first one
        processStartQueue();
    }

    void start(size_t index) {
        if (index >= m_servers.size()) return;
        auto& srv = m_servers[index];

        // Already running?
        if (srv.pid > 0) return;

        // Check dependency
        if (srv.dependsOn >= 0 && srv.dependsOn < (int)m_servers.size()) {
            auto& dep = m_servers[srv.dependsOn];
            if (dep.status != ServerEntry::Running) {
                srv.log.push_back("[ServerManager] Waiting for " + dep.name + " to be ready...");
                // Auto-start the dependency if it's not even started
                if (!dep.isRunning()) {
                    start(srv.dependsOn);
                }
                // Queue this server to start once dependency is ready
                m_pendingStarts.push_back(index);
                return;
            }
        }

        // Check if something is already listening on the port
        if (isPortOpen(srv.port)) {
            srv.status = ServerEntry::Running;
            srv.log.push_back("[ServerManager] Port " + std::to_string(srv.port) +
                              " already in use - server appears to be running externally.");
            return;
        }

        // Create pipe for stdout/stderr capture
        int pipeEnds[2];
        if (pipe(pipeEnds) < 0) {
            srv.log.push_back("[ServerManager] Failed to create pipe: " + std::string(strerror(errno)));
            srv.status = ServerEntry::Error;
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            srv.log.push_back("[ServerManager] Fork failed: " + std::string(strerror(errno)));
            close(pipeEnds[0]);
            close(pipeEnds[1]);
            srv.status = ServerEntry::Error;
            return;
        }

        if (pid == 0) {
            // ── Child process ──
            close(pipeEnds[0]);  // close read end
            dup2(pipeEnds[1], STDOUT_FILENO);
            dup2(pipeEnds[1], STDERR_FILENO);
            close(pipeEnds[1]);

            // Change working directory if specified
            if (!srv.workingDir.empty()) {
                if (chdir(srv.workingDir.c_str()) != 0) {
                    perror("chdir");
                    _exit(1);
                }
            }

            // Create new process group so we can kill the whole tree
            setpgid(0, 0);

            execl("/bin/sh", "sh", "-c", srv.command.c_str(), nullptr);
            perror("exec");
            _exit(1);
        }

        // ── Parent process ──
        close(pipeEnds[1]);  // close write end

        // Make read end non-blocking
        int flags = fcntl(pipeEnds[0], F_GETFL, 0);
        fcntl(pipeEnds[0], F_SETFL, flags | O_NONBLOCK);

        srv.pid = pid;
        srv.pipefd = pipeEnds[0];
        srv.status = ServerEntry::Starting;
        srv.log.push_back("[ServerManager] Started '" + srv.command + "' (PID " + std::to_string(pid) + ")");
    }

    void stop(size_t index) {
        if (index >= m_servers.size()) return;
        auto& srv = m_servers[index];

        if (srv.pid <= 0) {
            srv.status = ServerEntry::Stopped;
            return;
        }

        // Kill the process group (child + all its children)
        kill(-srv.pid, SIGTERM);

        // Wait briefly, then force kill if needed
        int status;
        pid_t result = waitpid(srv.pid, &status, WNOHANG);
        if (result == 0) {
            // Not exited yet, give it 500ms then SIGKILL
            usleep(500000);
            result = waitpid(srv.pid, &status, WNOHANG);
            if (result == 0) {
                kill(-srv.pid, SIGKILL);
                waitpid(srv.pid, &status, 0);
            }
        }

        srv.log.push_back("[ServerManager] Stopped (PID " + std::to_string(srv.pid) + ")");

        if (srv.pipefd >= 0) {
            close(srv.pipefd);
            srv.pipefd = -1;
        }

        srv.pid = 0;
        srv.status = ServerEntry::Stopped;
    }

    void pollOutput(float dt = 0.016f) {
        // Tick model switch status fade
        if (m_modelSwitchStatusTimer > 0.0f)
            m_modelSwitchStatusTimer -= dt;

        char buf[4096];
        for (auto& srv : m_servers) {
            // For Starting servers, periodically check if port is open (even without pipe output)
            if (srv.status == ServerEntry::Starting && isPortOpen(srv.port)) {
                srv.status = ServerEntry::Running;
                srv.log.push_back("[ServerManager] " + srv.name + " is ready (port " +
                                  std::to_string(srv.port) + " open).");
            }

            if (srv.pipefd < 0) continue;

            // Check if child exited
            if (srv.pid > 0) {
                int status;
                pid_t result = waitpid(srv.pid, &status, WNOHANG);
                if (result > 0) {
                    // Child exited
                    srv.log.push_back("[ServerManager] Process exited with status " +
                                      std::to_string(WEXITSTATUS(status)));
                    srv.pid = 0;
                    srv.status = ServerEntry::Stopped;
                }
            }

            // Read available output
            ssize_t n;
            while ((n = read(srv.pipefd, buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                // Split into lines
                std::string chunk(buf, n);
                size_t pos = 0;
                while (pos < chunk.size()) {
                    size_t nl = chunk.find('\n', pos);
                    if (nl == std::string::npos) {
                        // Partial line - append to last line or create new
                        if (srv.log.empty() || (!srv.log.empty() && srv.log.back().back() == '\n'))
                            srv.log.push_back(chunk.substr(pos));
                        else
                            srv.log.back() += chunk.substr(pos);
                        break;
                    }
                    std::string line = chunk.substr(pos, nl - pos);
                    if (!srv.log.empty() && !srv.log.back().empty() && srv.log.back().back() != '\n'
                        && srv.log.back()[0] != '[') {
                        srv.log.back() += line;
                    } else {
                        srv.log.push_back(line);
                    }
                    pos = nl + 1;
                }

                // Update status: if we see output and status is Starting, check port
                if (srv.status == ServerEntry::Starting && isPortOpen(srv.port)) {
                    srv.status = ServerEntry::Running;
                }
            }

            // Cap log size
            if (srv.log.size() > 2000) {
                srv.log.erase(srv.log.begin(), srv.log.begin() + 500);
            }
        }

        // Process pending dependency starts: start servers whose dependencies are now Running
        for (auto it = m_pendingStarts.begin(); it != m_pendingStarts.end(); ) {
            size_t idx = *it;
            if (idx < m_servers.size()) {
                auto& srv = m_servers[idx];
                if (srv.dependsOn >= 0 && m_servers[srv.dependsOn].status == ServerEntry::Running) {
                    it = m_pendingStarts.erase(it);
                    start(idx);  // dependency is ready, start it now
                    continue;
                }
            }
            ++it;
        }

        // Process "Start All" queue
        if (m_startAllPending) {
            processStartQueue();
        }

        // Restart: once all servers are stopped, kick off startAll
        if (m_restartPending) {
            bool allStopped = true;
            for (auto& s : m_servers) {
                if (s.pid > 0) { allStopped = false; break; }
            }
            if (allStopped) {
                m_restartPending = false;
                startAll();
            }
        }
    }

    void renderImGui(bool* open) {
        ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Server Manager", open)) {
            ImGui::End();
            return;
        }

        // ── Start All / Stop All buttons ──
        if (ImGui::Button("Start All")) { startAll(); }
        ImGui::SameLine();
        if (ImGui::Button("Stop All")) { stopAll(); }
        ImGui::SameLine();
        if (ImGui::Button("Restart All")) { restartAll(); }
        ImGui::SameLine();
        // Show pending status
        if (m_startAllPending || !m_pendingStarts.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "Starting services...");
        } else if (m_restartPending) {
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "Restarting...");
        }
        ImGui::Spacing();

        // ── Status table ──
        if (ImGui::BeginTable("##servers", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 24.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < m_servers.size(); i++) {
                auto& srv = m_servers[i];
                ImGui::TableNextRow();

                // Status dot
                ImGui::TableNextColumn();
                ImVec4 dotColor;
                switch (srv.status) {
                    case ServerEntry::Running:  dotColor = ImVec4(0.2f, 0.9f, 0.2f, 1.0f); break;
                    case ServerEntry::Starting: dotColor = ImVec4(0.9f, 0.9f, 0.2f, 1.0f); break;
                    case ServerEntry::Error:    dotColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); break;
                    default:                    dotColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
                }
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(p.x + 10, p.y + ImGui::GetTextLineHeight() * 0.5f), 5.0f,
                    ImGui::ColorConvertFloat4ToU32(dotColor));
                ImGui::Dummy(ImVec2(20, 0));

                // Name
                ImGui::TableNextColumn();
                ImGui::Text("%s", srv.name.c_str());

                // Port
                ImGui::TableNextColumn();
                ImGui::Text("%d", srv.port);

                // PID
                ImGui::TableNextColumn();
                if (srv.pid > 0)
                    ImGui::Text("%d", srv.pid);
                else
                    ImGui::TextDisabled("--");

                // Action button
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                if (srv.isRunning()) {
                    if (ImGui::SmallButton("Stop")) stop(i);
                } else {
                    if (ImGui::SmallButton("Start")) start(i);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();

        // ── Provider & Model Switcher ──
        if (isServerRunning(IDX_BACKEND)) {
            // Auto-fetch models on first render
            if (!m_modelsFetched && !m_fetchingModels) {
                refreshModels();
            }

            if (m_modelsFetched) {
                ImGui::Separator();

                // Provider toggle
                ImGui::Text("Provider:");
                ImGui::SameLine();
                bool isOllama = (m_currentProvider == "ollama");
                bool isGrok = (m_currentProvider == "grok");
                bool isClaude = (m_currentProvider == "claude");
                bool isDeepSeek = (m_currentProvider == "deepseek");
                if (ImGui::RadioButton("Ollama", isOllama)) {
                    if (!isOllama) switchProvider("ollama");
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Grok", isGrok)) {
                    if (!isGrok) switchProvider("grok");
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Claude", isClaude)) {
                    if (!isClaude) switchProvider("claude");
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("DeepSeek", isDeepSeek)) {
                    if (!isDeepSeek) switchProvider("deepseek");
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(active: %s)", m_currentProvider.c_str());

                // Model display
                if (isOllama && !m_ollamaModels.empty()) {
                    ImGui::Text("Model:");
                    ImGui::SameLine();

                    int currentIdx = 0;
                    for (int i = 0; i < (int)m_ollamaModels.size(); i++) {
                        if (m_ollamaModels[i] == m_currentOllamaModel) {
                            currentIdx = i;
                            break;
                        }
                    }

                    ImGui::SetNextItemWidth(250.0f);
                    if (ImGui::BeginCombo("##ollama_model", m_currentOllamaModel.c_str())) {
                        for (int i = 0; i < (int)m_ollamaModels.size(); i++) {
                            bool selected = (i == currentIdx);
                            if (ImGui::Selectable(m_ollamaModels[i].c_str(), selected)) {
                                if (m_ollamaModels[i] != m_currentOllamaModel) {
                                    switchModel(m_ollamaModels[i]);
                                }
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else if (isGrok) {
                    ImGui::Text("Model:");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", m_currentGrokModel.c_str());
                } else if (isClaude) {
                    ImGui::Text("Model:");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "%s", m_currentClaudeModel.c_str());
                } else if (isDeepSeek) {
                    ImGui::Text("Model:");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "%s", m_currentDeepSeekModel.c_str());
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("Refresh")) {
                    m_modelsFetched = false;
                }

                // Status message
                if (m_modelSwitchStatusTimer > 0.0f) {
                    bool isError = m_modelSwitchStatus.find("Error") != std::string::npos ||
                                   m_modelSwitchStatus.find("Failed") != std::string::npos;
                    ImVec4 col = isError ? ImVec4(0.9f, 0.3f, 0.3f, m_modelSwitchStatusTimer / 4.0f)
                                         : ImVec4(0.2f, 0.9f, 0.2f, m_modelSwitchStatusTimer / 4.0f);
                    ImGui::TextColored(col, "%s", m_modelSwitchStatus.c_str());
                }

                ImGui::Separator();
            }
        }

        ImGui::Spacing();

        // ── Console tabs ──
        if (ImGui::BeginTabBar("##server_logs")) {
            for (size_t i = 0; i < m_servers.size(); i++) {
                auto& srv = m_servers[i];
                if (ImGui::BeginTabItem(srv.name.c_str())) {
                    float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
                    ImGui::BeginChild("##log", ImVec2(0, -footerHeight), true);

                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
                    for (const auto& line : srv.log) {
                        // Color error lines red
                        if (line.find("ERROR") != std::string::npos ||
                            line.find("error") != std::string::npos ||
                            line.find("Traceback") != std::string::npos) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                            ImGui::TextUnformatted(line.c_str());
                            ImGui::PopStyleColor();
                        } else {
                            ImGui::TextUnformatted(line.c_str());
                        }
                    }
                    ImGui::PopStyleVar();

                    // Auto-scroll to bottom
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
                        ImGui::SetScrollHereY(1.0f);

                    ImGui::EndChild();

                    // Clear button
                    if (ImGui::Button("Clear Log")) {
                        srv.log.clear();
                    }
                    ImGui::SameLine();
                    ImGui::Text("%zu lines", srv.log.size());

                    ImGui::EndTabItem();
                }
            }

            // ── AI Context tab ──
            if (ImGui::BeginTabItem("AI Context")) {
                if (m_contextSessions.empty()) {
                    ImGui::TextDisabled("No active AI sessions");
                } else {
                    if (ImGui::BeginTable("##ctx", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("NPC", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Msgs", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                        ImGui::TableSetupColumn("~Context", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                        ImGui::TableSetupColumn("Session Total", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableSetupColumn("Provider", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableHeadersRow();

                        for (const auto& s : m_contextSessions) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", s.npcName.c_str());
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", s.messages);
                            ImGui::TableNextColumn();
                            // Color context tokens: green < 2k, yellow < 4k, red >= 4k
                            ImVec4 col = s.estTokens < 2000 ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f) :
                                         s.estTokens < 4000 ? ImVec4(0.9f, 0.9f, 0.2f, 1.0f) :
                                                              ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                            ImGui::TextColored(col, "%d", s.estTokens);
                            ImGui::TableNextColumn();
                            // Session aggregate: in + out, formatted as "12.3k" for readability
                            int totalTok = s.totalInputTokens + s.totalOutputTokens;
                            if (totalTok >= 1000)
                                ImGui::Text("%.1fk (i:%d o:%d)", totalTok / 1000.0f, s.totalInputTokens, s.totalOutputTokens);
                            else
                                ImGui::Text("%d (i:%d o:%d)", totalTok, s.totalInputTokens, s.totalOutputTokens);
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", s.provider.c_str());
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    // Access server info (for health check integration)
    bool isServerRunning(size_t index) const {
        if (index >= m_servers.size()) return false;
        return m_servers[index].status == ServerEntry::Running;
    }

    const std::vector<ServerEntry>& servers() const { return m_servers; }

    // AI context session info (updated by main app from /sessions/context endpoint)
    struct ContextSession {
        std::string npcName;
        int messages = 0;
        int estTokens = 0;
        std::string provider;
        int totalInputTokens = 0;
        int totalOutputTokens = 0;
    };

    void updateContextSessions(const std::vector<ContextSession>& sessions) {
        m_contextSessions = sessions;
    }

    // Fetch available models + current model from backend (non-blocking)
    void refreshModels() {
        if (m_fetchingModels) return;
        m_fetchingModels = true;

        std::thread([this]() {
            try {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(2);
                cli.set_read_timeout(2);

                // Get available Ollama models
                if (auto res = cli.Get("/models")) {
                    if (res->status == 200) {
                        auto j = nlohmann::json::parse(res->body);
                        std::vector<std::string> models;
                        if (j.contains("ollama") && j["ollama"].is_array()) {
                            for (auto& m : j["ollama"]) {
                                models.push_back(m.get<std::string>());
                            }
                        }
                        m_ollamaModels = std::move(models);
                    }
                }

                // Get current model
                if (auto res = cli.Get("/model/current")) {
                    if (res->status == 200) {
                        auto j = nlohmann::json::parse(res->body);
                        if (j.contains("ollama_model"))
                            m_currentOllamaModel = j["ollama_model"].get<std::string>();
                        if (j.contains("grok_model"))
                            m_currentGrokModel = j["grok_model"].get<std::string>();
                        if (j.contains("claude_model"))
                            m_currentClaudeModel = j["claude_model"].get<std::string>();
                        if (j.contains("deepseek_model"))
                            m_currentDeepSeekModel = j["deepseek_model"].get<std::string>();
                        if (j.contains("provider"))
                            m_currentProvider = j["provider"].get<std::string>();
                    }
                }

                m_modelsFetched = true;
            } catch (...) {}
            m_fetchingModels = false;
        }).detach();
    }

    void switchModel(const std::string& model) {
        std::thread([this, model]() {
            try {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(2);
                cli.set_read_timeout(5);

                nlohmann::json body;
                body["model"] = model;
                auto res = cli.Post("/model/switch", body.dump(), "application/json");

                if (res && res->status == 200) {
                    auto j = nlohmann::json::parse(res->body);
                    if (j.value("status", "") == "ok") {
                        m_currentOllamaModel = model;
                        int updated = j.value("sessions_updated", 0);
                        m_modelSwitchStatus = "Switched to " + model +
                            " (" + std::to_string(updated) + " sessions updated)";
                    } else {
                        m_modelSwitchStatus = "Error: " + j.value("message", "unknown");
                    }
                } else {
                    m_modelSwitchStatus = "Failed to connect to backend";
                }
            } catch (...) {
                m_modelSwitchStatus = "Error switching model";
            }
            m_modelSwitchStatusTimer = 4.0f;
        }).detach();
    }

    void switchProvider(const std::string& provider) {
        std::thread([this, provider]() {
            try {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(2);
                cli.set_read_timeout(5);

                nlohmann::json body;
                body["provider"] = provider;
                auto res = cli.Post("/provider/switch", body.dump(), "application/json");

                if (res && res->status == 200) {
                    auto j = nlohmann::json::parse(res->body);
                    if (j.value("status", "") == "ok") {
                        m_currentProvider = provider;
                        int updated = j.value("sessions_updated", 0);
                        std::string model = j.value("model", "?");
                        m_modelSwitchStatus = "Switched to " + provider + " (" + model +
                            ", " + std::to_string(updated) + " sessions updated)";
                    } else {
                        m_modelSwitchStatus = "Error: " + j.value("message", "unknown");
                    }
                } else {
                    m_modelSwitchStatus = "Failed to connect to backend";
                }
            } catch (...) {
                m_modelSwitchStatus = "Error switching provider";
            }
            m_modelSwitchStatusTimer = 4.0f;
        }).detach();
    }

private:
    static bool isPortOpen(int port) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        // Set short timeout
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;  // 200ms
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        bool open = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
        close(sock);
        return open;
    }

    void processStartQueue() {
        while (!m_startAllQueue.empty()) {
            size_t idx = m_startAllQueue.front();
            auto& srv = m_servers[idx];

            // Skip if already running
            if (srv.isRunning() || srv.status == ServerEntry::Running) {
                m_startAllQueue.erase(m_startAllQueue.begin());
                continue;
            }

            // If this server has a dependency that isn't Running yet, wait
            if (srv.dependsOn >= 0 && m_servers[srv.dependsOn].status != ServerEntry::Running) {
                break;  // will retry on next pollOutput()
            }

            m_startAllQueue.erase(m_startAllQueue.begin());
            start(idx);
        }

        if (m_startAllQueue.empty()) {
            m_startAllPending = false;
        }
    }

    std::string m_projectRoot;
    std::vector<ServerEntry> m_servers;
    std::vector<size_t> m_pendingStarts;     // servers waiting on dependency
    std::vector<size_t> m_startAllQueue;     // ordered queue for "Start All"
    bool m_startAllPending = false;
    bool m_restartPending = false;
    std::vector<ContextSession> m_contextSessions;

    // Model switcher state
    std::vector<std::string> m_ollamaModels;
    std::string m_currentOllamaModel;
    std::string m_currentGrokModel;
    std::string m_currentClaudeModel;
    std::string m_currentDeepSeekModel;
    std::string m_currentProvider;
    std::string m_modelSwitchStatus;
    float m_modelSwitchStatusTimer = 0.0f;
    float m_modelRefreshTimer = 0.0f;
    bool m_modelsFetched = false;
    bool m_fetchingModels = false;
};
