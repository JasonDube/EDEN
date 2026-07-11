#pragma once

// In-game server control for Chronicles of the Iron Temple.
//
// A trimmed version of the terrain editor's ServerManager: manages just the two
// processes NPC dialogue needs — Ollama (local models) and the ai_companion
// Python backend (localhost:8080) — plus the provider/model switcher. Lives in
// the game's Options menu so players never need the terrain editor to talk to
// NPCs. Servers are left running on exit (like the editor) so a game restart
// reconnects instantly.

#include <string>
#include <vector>
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

class GameServers {
public:
    static constexpr int IDX_OLLAMA  = 0;
    static constexpr int IDX_BACKEND = 1;

    struct Entry {
        std::string name, command, workingDir;
        int port = 0;
        pid_t pid = 0;
        int pipefd = -1;
        std::vector<std::string> log;
        enum Status { Stopped, Starting, Running, Error } status = Stopped;
        int dependsOn = -1;
        bool isRunning() const { return status == Running || status == Starting; }
    };

    // projectRoot is the EDEN_GAME_WORK source tree (compiled in via CMAKE_SOURCE_DIR)
    // so the Python backend is found regardless of the game's working directory.
    explicit GameServers(const std::string& projectRoot) {
        Entry ollama;
        ollama.name = "Ollama (local models)";
        ollama.command = "ollama serve";
        ollama.port = 11434;
        m_servers.push_back(std::move(ollama));

        Entry ai;
        ai.name = "Dialogue AI Backend";
        ai.command = "python3 server.py";
        ai.workingDir = projectRoot + "/modules/ai_companion/backend";
        ai.port = 8080;
        ai.dependsOn = IDX_OLLAMA;
        m_servers.push_back(std::move(ai));
    }

    ~GameServers() {
        for (auto& s : m_servers)
            if (s.pipefd >= 0) { close(s.pipefd); s.pipefd = -1; }
    }

    bool backendReady() const { return m_servers[IDX_BACKEND].status == Entry::Running; }

    void startAll() {
        for (size_t i = 0; i < m_servers.size(); ++i)
            if (m_servers[i].dependsOn < 0 && !m_servers[i].isRunning()) start(i);
        // dependents kick off from poll() once their dependency is Running
        for (size_t i = 0; i < m_servers.size(); ++i)
            if (m_servers[i].dependsOn >= 0 && !m_servers[i].isRunning())
                m_pending.push_back(i);
    }

    void stopAll() { for (size_t i = 0; i < m_servers.size(); ++i) if (m_servers[i].pid > 0) stop(i); }

    void start(size_t index) {
        if (index >= m_servers.size()) return;
        Entry& srv = m_servers[index];
        if (srv.pid > 0) return;

        if (srv.dependsOn >= 0) {
            Entry& dep = m_servers[srv.dependsOn];
            if (dep.status != Entry::Running) {
                if (!dep.isRunning()) start(srv.dependsOn);
                m_pending.push_back(index);
                return;
            }
        }
        if (isPortOpen(srv.port)) {              // already running externally
            srv.status = Entry::Running;
            srv.log.push_back("[servers] Port " + std::to_string(srv.port) + " already in use.");
            return;
        }

        int pe[2];
        if (pipe(pe) < 0) { srv.status = Entry::Error; return; }
        pid_t pid = fork();
        if (pid < 0) { close(pe[0]); close(pe[1]); srv.status = Entry::Error; return; }
        if (pid == 0) {
            close(pe[0]);
            dup2(pe[1], STDOUT_FILENO); dup2(pe[1], STDERR_FILENO); close(pe[1]);
            if (!srv.workingDir.empty() && chdir(srv.workingDir.c_str()) != 0) { perror("chdir"); _exit(1); }
            setpgid(0, 0);
            execl("/bin/sh", "sh", "-c", srv.command.c_str(), nullptr);
            perror("exec"); _exit(1);
        }
        close(pe[1]);
        int fl = fcntl(pe[0], F_GETFL, 0); fcntl(pe[0], F_SETFL, fl | O_NONBLOCK);
        srv.pid = pid; srv.pipefd = pe[0]; srv.status = Entry::Starting;
        srv.log.push_back("[servers] Started '" + srv.command + "' (PID " + std::to_string(pid) + ")");
    }

    void stop(size_t index) {
        if (index >= m_servers.size()) return;
        Entry& srv = m_servers[index];
        if (srv.pid <= 0) { srv.status = Entry::Stopped; return; }
        kill(-srv.pid, SIGTERM);
        int st; if (waitpid(srv.pid, &st, WNOHANG) == 0) {
            usleep(500000);
            if (waitpid(srv.pid, &st, WNOHANG) == 0) { kill(-srv.pid, SIGKILL); waitpid(srv.pid, &st, 0); }
        }
        if (srv.pipefd >= 0) { close(srv.pipefd); srv.pipefd = -1; }
        srv.pid = 0; srv.status = Entry::Stopped;
    }

    // Call once per frame from the game loop.
    void poll() {
        char buf[4096];
        for (auto& srv : m_servers) {
            if (srv.status == Entry::Starting && isPortOpen(srv.port)) srv.status = Entry::Running;
            if (srv.pipefd < 0) continue;
            if (srv.pid > 0) {
                int st; if (waitpid(srv.pid, &st, WNOHANG) > 0) {
                    srv.log.push_back("[servers] exited (status " + std::to_string(WEXITSTATUS(st)) + ")");
                    srv.pid = 0; srv.status = Entry::Stopped;
                }
            }
            ssize_t n;
            while ((n = read(srv.pipefd, buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                std::string chunk(buf, n), line;
                for (char c : chunk) { if (c == '\n') { srv.log.push_back(line); line.clear(); } else line += c; }
                if (!line.empty()) srv.log.push_back(line);
                if (srv.status == Entry::Starting && isPortOpen(srv.port)) srv.status = Entry::Running;
            }
            if (srv.log.size() > 500) srv.log.erase(srv.log.begin(), srv.log.begin() + 200);
        }
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            Entry& srv = m_servers[*it];
            if (srv.dependsOn >= 0 && m_servers[srv.dependsOn].status == Entry::Running) {
                size_t idx = *it; it = m_pending.erase(it); start(idx);
            } else ++it;
        }
    }

    // Compact panel for the Options menu.
    void renderPanel() {
        ImGui::TextUnformatted("NPC dialogue needs these running:");
        ImGui::Spacing();
        if (ImGui::Button("Start All")) startAll();
        ImGui::SameLine();
        if (ImGui::Button("Stop All")) stopAll();
        ImGui::Spacing();

        if (ImGui::BeginTable("##gsrv", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Service", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            for (size_t i = 0; i < m_servers.size(); ++i) {
                Entry& srv = m_servers[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImVec4 dot = srv.status == Entry::Running  ? ImVec4(0.2f, 0.9f, 0.2f, 1)
                           : srv.status == Entry::Starting ? ImVec4(0.9f, 0.9f, 0.2f, 1)
                           : srv.status == Entry::Error    ? ImVec4(0.9f, 0.2f, 0.2f, 1)
                                                           : ImVec4(0.5f, 0.5f, 0.5f, 1);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(p.x + 9, p.y + ImGui::GetTextLineHeight() * 0.5f), 5.0f,
                    ImGui::ColorConvertFloat4ToU32(dot));
                ImGui::Dummy(ImVec2(18, 0));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(srv.name.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);
                if (srv.isRunning()) { if (ImGui::SmallButton("Stop")) stop(i); }
                else                 { if (ImGui::SmallButton("Start")) start(i); }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        // Provider / model switcher (only meaningful once the backend answers).
        if (backendReady()) {
            if (!m_modelsFetched && !m_fetching) refreshModels();
            if (m_modelsFetched) {
                ImGui::Separator();
                ImGui::TextUnformatted("Talk using:");
                const char* provs[] = {"ollama", "grok", "claude", "deepseek"};
                const char* labels[] = {"Ollama (free/local)", "Grok", "Claude", "DeepSeek"};
                for (int i = 0; i < 4; ++i) {
                    if (i) ImGui::SameLine();
                    if (ImGui::RadioButton(labels[i], m_provider == provs[i]) && m_provider != provs[i])
                        switchProvider(provs[i]);
                }
                if (m_provider == "ollama" && !m_ollamaModels.empty()) {
                    ImGui::SetNextItemWidth(280.0f);
                    if (ImGui::BeginCombo("Model", m_ollamaModel.c_str())) {
                        for (auto& m : m_ollamaModels)
                            if (ImGui::Selectable(m.c_str(), m == m_ollamaModel) && m != m_ollamaModel)
                                switchModel(m);
                        ImGui::EndCombo();
                    }
                } else if (m_provider != "ollama") {
                    ImGui::Text("Model: %s", m_provider == "grok" ? m_grokModel.c_str()
                                          : m_provider == "claude" ? m_claudeModel.c_str()
                                                                   : m_deepseekModel.c_str());
                    ImGui::TextDisabled("(cloud providers need an API key set in the backend's environment)");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Refresh")) m_modelsFetched = false;
            }
        } else {
            ImGui::TextDisabled("Start the backend to choose a provider/model.");
        }
    }

    void refreshModels() {
        if (m_fetching) return;
        m_fetching = true;
        std::thread([this]() {
            try {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(2); cli.set_read_timeout(2);
                if (auto r = cli.Get("/models"); r && r->status == 200) {
                    auto j = nlohmann::json::parse(r->body);
                    if (j.contains("ollama") && j["ollama"].is_array()) {
                        std::vector<std::string> v;
                        for (auto& m : j["ollama"]) v.push_back(m.get<std::string>());
                        m_ollamaModels = std::move(v);
                    }
                }
                if (auto r = cli.Get("/model/current"); r && r->status == 200) {
                    auto j = nlohmann::json::parse(r->body);
                    m_ollamaModel   = j.value("ollama_model", m_ollamaModel);
                    m_grokModel     = j.value("grok_model", m_grokModel);
                    m_claudeModel   = j.value("claude_model", m_claudeModel);
                    m_deepseekModel = j.value("deepseek_model", m_deepseekModel);
                    m_provider      = j.value("provider", m_provider);
                }
                m_modelsFetched = true;
            } catch (...) {}
            m_fetching = false;
        }).detach();
    }

    void switchModel(const std::string& model) {
        m_ollamaModel = model;   // optimistic; backend confirms
        std::thread([model]() {
            try { httplib::Client cli("localhost", 8080); cli.set_read_timeout(5);
                  nlohmann::json b; b["model"] = model; cli.Post("/model/switch", b.dump(), "application/json"); }
            catch (...) {}
        }).detach();
    }

    void switchProvider(const std::string& provider) {
        m_provider = provider;
        std::thread([provider]() {
            try { httplib::Client cli("localhost", 8080); cli.set_read_timeout(5);
                  nlohmann::json b; b["provider"] = provider; cli.Post("/provider/switch", b.dump(), "application/json"); }
            catch (...) {}
        }).detach();
    }

private:
    static bool isPortOpen(int port) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return false;
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        timeval tv{0, 200000}; setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        bool open = connect(s, (sockaddr*)&a, sizeof(a)) == 0;
        close(s);
        return open;
    }

    std::vector<Entry> m_servers;
    std::vector<size_t> m_pending;
    std::vector<std::string> m_ollamaModels;
    std::string m_ollamaModel, m_grokModel, m_claudeModel, m_deepseekModel, m_provider = "ollama";
    bool m_modelsFetched = false, m_fetching = false;
};
