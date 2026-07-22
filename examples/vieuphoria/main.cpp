// VIEUPHORIA v0.2 — a 3D shell for EDEN OS, with an IN-WORLD REPL.
//
// You type directly into the 3D window: a console is rendered on a HUD quad in
// front of the camera, keystrokes come from GLFW, and spatial verbs reshape the
// world live. No terminal. `ask` is stubbed to mark where AI-native primitives
// ("pipe through a mind") will live.

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ModelRenderer.hpp"

#include <eden/Camera.hpp>
#include <eden/Window.hpp>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "hud.h"

#include <httplib.h>          // talk to the ai_companion backend (localhost:8080)
#include <nlohmann/json.hpp>

#include <climits>
#include <csignal>
#include <cctype>
#include <fstream>
#include <libgen.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace eden;

namespace {

// ── Mesh builders (double-winded, so face winding never matters) ─────────────
void addFace(std::vector<ModelVertex>& v, std::vector<uint32_t>& idx,
             glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, glm::vec4 col) {
    uint32_t base = (uint32_t)v.size();
    v.push_back({a, n, {0, 0}, col});
    v.push_back({b, n, {1, 0}, col});
    v.push_back({c, n, {1, 1}, col});
    v.push_back({d, n, {0, 1}, col});
    idx.insert(idx.end(), {base, base + 1, base + 2, base + 2, base + 3, base,
                           base, base + 2, base + 1, base, base + 3, base + 2});
}
void addTri(std::vector<ModelVertex>& v, std::vector<uint32_t>& idx,
            glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 col) {
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    uint32_t base = (uint32_t)v.size();
    v.push_back({a, n, {0, 0}, col});
    v.push_back({b, n, {1, 0}, col});
    v.push_back({c, n, {0.5f, 1}, col});
    idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 1});
}
void buildBox(glm::vec3 h, glm::vec4 c, std::vector<ModelVertex>& v, std::vector<uint32_t>& idx) {
    addFace(v, idx, {h.x, -h.y, -h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, -h.z}, {1, 0, 0}, c);
    addFace(v, idx, {-h.x, -h.y, h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {-h.x, h.y, h.z}, {-1, 0, 0}, c);
    addFace(v, idx, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z}, {0, 1, 0}, c);
    addFace(v, idx, {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {0, -1, 0}, c);
    addFace(v, idx, {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z}, {0, 0, 1}, c);
    addFace(v, idx, {h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}, {0, 0, -1}, c);
}
void buildPyramid(glm::vec3 h, glm::vec4 c, std::vector<ModelVertex>& v, std::vector<uint32_t>& idx) {
    glm::vec3 A(0, h.y, 0);
    glm::vec3 b0(-h.x, -h.y, -h.z), b1(h.x, -h.y, -h.z), b2(h.x, -h.y, h.z), b3(-h.x, -h.y, h.z);
    addTri(v, idx, b0, b1, A, c);
    addTri(v, idx, b1, b2, A, c);
    addTri(v, idx, b2, b3, A, c);
    addTri(v, idx, b3, b0, A, c);
    addFace(v, idx, b0, b1, b2, b3, {0, -1, 0}, c);
}
bool parseColor(const std::string& s, glm::vec4& out) {
    const char* p = s.c_str();
    if (*p == '#') p++;
    unsigned r, g, b;
    if (sscanf(p, "%2x%2x%2x", &r, &g, &b) != 3) return false;
    out = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    return true;
}
const glm::vec4 kPalette[] = {
    {0.42f, 0.58f, 0.71f, 1}, {0.85f, 0.40f, 0.44f, 1}, {0.45f, 0.72f, 0.50f, 1},
    {0.90f, 0.72f, 0.35f, 1}, {0.62f, 0.48f, 0.78f, 1}, {0.40f, 0.75f, 0.78f, 1},
};

constexpr const char* kPersonality =
    "You are the mind inside VIEUPHORIA, a 3D operating-system shell. "
    "Answer concisely (2-4 sentences), plain text.";

} // namespace

struct Obj {
    uint32_t handle = 0;
    glm::vec3 pos{0};
    glm::vec3 scale{0.5f};
    float angle = 0.0f, spin = 0.0f;
    glm::vec4 color{1};
    std::string shape = "cube";
};

class Vieuphoria : public VulkanApplicationBase {
public:
    Vieuphoria() : VulkanApplicationBase(1280, 800, "VIEUPHORIA — command the world") {}

protected:
    void onInit() override {
        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());
        m_modelRenderer->setDayNight(1.0f, 1.0f);
        m_modelRenderer->setLights({});

        m_camera.setNoClip(true);

        // A small living scene so it's obviously alive on launch.
        spawn("cube", kPalette[0]);
        spawn("pyramid", kPalette[1]);
        spawn("box", kPalette[2]);
        for (auto& o : m_objs) o.spin = 30.0f;

        say("VIEUPHORIA v0 - a 3D shell for EDEN");
        say("type here, or HOLD TAB to speak. try:  make a solar system  |  help");
        say("");

        // In-world console: a HUD quad billboarded in front of the camera.
        hud_init("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
        rebuildConsole();
        {
            std::vector<ModelVertex> v;
            std::vector<uint32_t> idx;
            // Unit quad, UV v flipped (top vertex v1) so console row 0 is on top.
            glm::vec4 w{1, 1, 1, 1};
            v = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}, w}, {{1, -1, 0}, {0, 0, 1}, {1, 0}, w},
                 {{1, 1, 0}, {0, 0, 1}, {1, 1}, w},   {{-1, 1, 0}, {0, 0, 1}, {0, 1}, w}};
            idx = {0, 1, 2, 2, 3, 0, 0, 2, 1, 0, 3, 2};
            m_hud = m_modelRenderer->createModel(v, idx, m_hudPix.data(), m_hudW, m_hudH);
        }

        // Input: keystrokes from GLFW. (Do NOT touch the window user pointer —
        // EDEN's Window owns it; use a static instance pointer.)
        s_instance = this;
        GLFWwindow* win = getWindow().getHandle();
        glfwSetCharCallback(win, &Vieuphoria::charCb);
        glfwSetKeyCallback(win, &Vieuphoria::keyCb);

        glfwMaximizeWindow(win); // fill the desktop

        if (getenv("VIEUPHORIA_SELFTEST"))
            exec("make clear the world, then spawn five pyramids of different bright "
                 "colors, ring them, and spin them all");
    }

    void onCleanup() override {
        vkDeviceWaitIdle(getContext().getDevice());
        m_modelRenderer.reset();
    }

    void update(float dt) override {
        // Run any commands the mind (or another thread) queued — ON the main
        // thread, because they create/destroy Vulkan resources.
        for (;;) {
            std::string cmd;
            {
                std::lock_guard<std::mutex> lk(m_cmdMtx);
                if (m_cmdQueue.empty()) break;
                cmd = m_cmdQueue.front();
                m_cmdQueue.pop();
            }
            exec(cmd);
        }

        for (auto& o : m_objs)
            if (o.spin != 0.0f) o.angle = std::fmod(o.angle + o.spin * dt, 360.0f);
        if (m_hudDirty) {
            rebuildConsole();
            m_modelRenderer->updateTexture(m_hud, m_hudPix.data(), m_hudW, m_hudH);
            m_hudDirty = false;
        }
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &bi);

        auto& sc = getSwapchain();
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = sc.getRenderPass();
        rp.framebuffer = sc.getFramebuffers()[imageIndex];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = sc.getExtent();
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{0.05f, 0.06f, 0.08f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        rp.clearValueCount = (uint32_t)clears.size();
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        auto ext = sc.getExtent();
        VkViewport vp{0, 0, (float)ext.width, (float)ext.height, 0, 1};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D scissor{{0, 0}, ext};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        float aspect = (float)ext.width / ext.height;
        frameCamera();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 200.0f);
        glm::mat4 viewProj = proj * m_camera.getViewMatrix();

        m_modelRenderer->setDayNight(1.0f, 1.0f);
        m_modelRenderer->setLights({});
        for (auto& o : m_objs) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), o.pos) *
                              glm::rotate(glm::mat4(1.0f), glm::radians(o.angle), glm::vec3(0, 1, 0)) *
                              glm::scale(glm::mat4(1.0f), o.scale);
            m_modelRenderer->render(cmd, viewProj, o.handle, model);
        }

        // HUD console: a quad pinned to the bottom of the view, closest to the
        // camera so it draws in front of the world.
        m_modelRenderer->render(cmd, viewProj, m_hud, hudModel(aspect));

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

private:
    // ── Input callbacks ──────────────────────────────────────────────────────
    static inline Vieuphoria* s_instance = nullptr;
    static void charCb(GLFWwindow*, unsigned int cp) {
        if (s_instance && cp >= 0x20 && cp < 0x7f) {
            s_instance->m_input += (char)cp;
            s_instance->m_hudDirty = true;
        }
    }
    static void keyCb(GLFWwindow*, int key, int, int action, int) {
        if (!s_instance) return;
        auto* s = s_instance;
        // Push-to-talk: hold Tab to speak (Tab emits no char, so it's safe).
        if (key == GLFW_KEY_TAB) {
            if (action == GLFW_PRESS) s->startVoice();
            else if (action == GLFW_RELEASE) s->stopVoice();
            return;
        }
        if (action == GLFW_RELEASE) return;
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) s->submit();
        else if (key == GLFW_KEY_BACKSPACE) { if (!s->m_input.empty()) s->m_input.pop_back(); s->m_hudDirty = true; }
        else if (key == GLFW_KEY_ESCAPE) { s->m_input.clear(); s->m_hudDirty = true; }
    }
    void submit() {
        say("vieuphoria> " + m_input);
        std::string cmd = m_input;
        m_input.clear();
        exec(cmd);
        m_hudDirty = true;
    }

    // ── Console I/O ──────────────────────────────────────────────────────────
    void say(const std::string& s) {
        std::lock_guard<std::mutex> lk(m_scrollMtx);
        std::istringstream ss(s);
        std::string ln;
        bool any = false;
        while (std::getline(ss, ln)) { m_scroll.push_back(ln); any = true; }
        if (!any) m_scroll.push_back("");
        if (m_scroll.size() > 500) m_scroll.erase(m_scroll.begin(), m_scroll.begin() + 100);
        m_hudDirty = true;
    }
    void rebuildConsole() {
        std::vector<std::string> disp;
        {
            std::lock_guard<std::mutex> lk(m_scrollMtx);
            disp = m_scroll;
        }
        disp.push_back("vieuphoria> " + m_input);
        hud_render(m_hudPix, m_hudW, m_hudH, 20, disp);
    }

    // ── World geometry / camera ──────────────────────────────────────────────
    float ringRadius() const { return std::max(2.2f, 0.55f * (float)std::max<size_t>(m_objs.size(), 1)); }
    void reRing() {
        int n = (int)m_objs.size();
        if (!n) return;
        float r = ringRadius();
        for (int i = 0; i < n; i++) {
            float a = (float)i / n * 6.2831853f;
            m_objs[i].pos = glm::vec3(r * std::cos(a), 0.0f, r * std::sin(a));
        }
    }
    void frameCamera() {
        float R = ringRadius();
        float D = R * 1.7f + 4.0f, H = R * 0.8f + 2.6f;
        m_camera.setPosition(glm::vec3(0.0f, H, D));
        m_camera.setYaw(-90.0f);
        m_camera.setPitch(-glm::degrees(std::atan2(H, D)));
    }
    // Billboard the console quad as a full-height panel on the RIGHT of the view.
    glm::mat4 hudModel(float aspect) {
        glm::vec3 P = m_camera.getPosition();
        glm::vec3 F = m_camera.getFront();
        glm::vec3 Rt = glm::normalize(glm::cross(F, glm::vec3(0, 1, 0)));
        glm::vec3 Up = glm::normalize(glm::cross(Rt, F));
        float d = 1.0f;
        float hh = d * std::tan(glm::radians(m_camera.getFov() * 0.5f));
        float hw = hh * aspect;
        float panelHH = hh;                                  // full height
        float panelHW = panelHH * ((float)m_hudW / m_hudH);  // narrow (match texture aspect)
        // +Rt pins it to the screen RIGHT (screen X isn't flipped, only Y is).
        glm::vec3 center = P + F * d + Rt * (hw - panelHW);
        glm::mat4 M(1.0f);
        M[0] = glm::vec4(Rt * panelHW, 0);
        M[1] = glm::vec4(Up * panelHH, 0);
        M[2] = glm::vec4(-F, 0);
        M[3] = glm::vec4(center, 1);
        return M;
    }

    uint32_t makeMesh(const std::string& shape, glm::vec4 color) {
        std::vector<ModelVertex> v;
        std::vector<uint32_t> idx;
        if (shape == "pyramid")   buildPyramid({0.9f, 1.0f, 0.9f}, color, v, idx);
        else if (shape == "box")  buildBox({0.6f, 1.4f, 0.6f}, color, v, idx);
        else if (shape == "card") buildBox({1.2f, 0.8f, 0.05f}, color, v, idx);
        else                      buildBox({0.9f, 0.9f, 0.9f}, color, v, idx);
        return m_modelRenderer->createModel(v, idx, nullptr, 0, 0);
    }
    void spawn(const std::string& shape, glm::vec4 color) {
        Obj o;
        o.shape = shape;
        o.color = color;
        o.handle = makeMesh(shape, color);
        m_objs.push_back(o);
        reRing();
        say("  + " + shape + " #" + std::to_string(m_objs.size() - 1));
    }

    // ── The mind: `ask` pipes through the ai_companion backend (:8080) ───────
    // Runs on a detached thread. Carries a session_id for conversation memory
    // and honours the current provider (ollama/grok/claude/deepseek).
    void doAsk(const std::string& prompt) {
        std::string session, provider;
        { std::lock_guard<std::mutex> lk(m_sessMtx); session = m_session; provider = m_provider; }

        nlohmann::json body;
        body["message"] = prompt;
        body["npc_personality"] = kPersonality;
        body["raw"] = true;          // system prompt verbatim, no EDEN framing
        body["allow_actions"] = false;
        if (!provider.empty()) body["provider"] = provider;
        if (!session.empty()) body["session_id"] = session;

        httplib::Client cli("localhost", 8080);
        cli.set_connection_timeout(3);
        cli.set_read_timeout(120);
        auto res = cli.Post("/chat", body.dump(), "application/json");

        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                std::string text = j.value("response", "...");
                std::string prov = j.value("provider", provider);
                std::string model = j.value("model", "");
                { std::lock_guard<std::mutex> lk(m_sessMtx); m_session = j.value("session_id", session); }
                say("  > [" + prov + (model.empty() ? "" : "/" + model) + "] " + text);
            } catch (...) {
                say("  > (couldn't parse the backend's reply)");
            }
        } else {
            say("  > (ai_companion not answering on :8080 — is the backend running?)");
        }
    }

    void enqueue(const std::string& cmd) {
        std::lock_guard<std::mutex> lk(m_cmdMtx);
        m_cmdQueue.push(cmd);
    }

    // ── Voice: hold Tab to talk (record → /stt → route) ──────────────────────
    static constexpr const char* kWav = "/tmp/vieuphoria_ptt.wav";
    void startVoice() {
        if (m_recPid > 0) return;
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            freopen("/dev/null", "w", stderr);
            execlp("parecord", "parecord", "--file-format=wav", "--rate=16000",
                   "--channels=1", kWav, (char*)nullptr);
            _exit(127);
        }
        m_recPid = pid;
        say("  [mic] listening... (release Tab to send)");
    }
    void stopVoice() {
        if (m_recPid <= 0) return;
        kill(m_recPid, SIGTERM);            // finalizes the WAV
        waitpid(m_recPid, nullptr, 0);
        m_recPid = 0;
        say("  [mic] transcribing...");
        std::thread([this] { transcribeVoice(); }).detach();
    }
    void transcribeVoice() {
        std::ifstream f(kWav, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(f)), {});
        if (bytes.size() < 200) { say("  [mic] (no audio captured)"); return; }

        httplib::Client cli("localhost", 8080);
        cli.set_read_timeout(90);
        httplib::MultipartFormDataItems items = {{"audio", bytes, "ptt.wav", "audio/wav"}};
        auto res = cli.Post("/stt", items);
        if (!res || res->status != 200) { say("  [mic] STT failed (:8080)"); return; }

        std::string text;
        try { text = nlohmann::json::parse(res->body).value("text", ""); } catch (...) {}
        // trim
        size_t a = text.find_first_not_of(" \t\r\n");
        size_t b = text.find_last_not_of(" \t\r\n");
        text = (a == std::string::npos) ? "" : text.substr(a, b - a + 1);
        if (text.empty()) { say("  [mic] (nothing heard)"); return; }

        say("  [mic] heard: \"" + text + "\"");
        // Voice defaults to `make`; say "ask ..." to ask a question instead.
        std::string low;
        for (char c : text) low += (char)std::tolower((unsigned char)c);
        if (low.rfind("ask", 0) == 0 && (low.size() == 3 || low[3] == ' '))
            enqueue("ask " + (text.size() > 4 ? text.substr(4) : ""));
        else
            enqueue("make " + text);
    }

    // `make` — the mind ACTS: it emits VIEUPHORIA commands, which we queue back
    // to the main thread to execute. The tools ARE the spatial verbs.
    void doMake(const std::string& request) {
        std::string provider;
        { std::lock_guard<std::mutex> lk(m_sessMtx); provider = m_provider; }

        const std::string sys =
            "You are the command engine of VIEUPHORIA, a 3D world. Translate the request "
            "into VIEUPHORIA commands, ONE PER LINE, using ONLY these verbs:\n"
            "spawn <cube|box|pyramid|card> [#rrggbb]\n"
            "color <id> <#rrggbb>\n"
            "move <id> <dx> <dy> <dz>\n"
            "spin <id|all> [deg]\n"
            "stop <id|all>\n"
            "ring\n"
            "clear\n"
            "Object ids are 0-based in the order spawned. Output ONLY commands - no prose, "
            "no explanations, no code fences, no numbering.";

        nlohmann::json body;
        body["message"] = request;
        body["npc_personality"] = sys;
        body["raw"] = true;
        body["allow_actions"] = false;
        if (!provider.empty()) body["provider"] = provider; // stateless: no session_id

        httplib::Client cli("localhost", 8080);
        cli.set_connection_timeout(3);
        cli.set_read_timeout(120);
        auto res = cli.Post("/chat", body.dump(), "application/json");
        if (!res || res->status != 200) { say("  ! the mind didn't answer (:8080)"); return; }

        std::string text;
        try { text = nlohmann::json::parse(res->body).value("response", ""); } catch (...) {}

        static const std::vector<std::string> verbs = {
            "spawn", "color", "move", "spin", "stop", "ring", "clear"};
        std::istringstream ss(text);
        std::string ln;
        int n = 0;
        say("  ~ the mind acts:");
        while (std::getline(ss, ln)) {
            size_t a = ln.find_first_not_of(" \t`-*0123456789.)");
            if (a == std::string::npos) continue;
            ln = ln.substr(a);
            std::string w = ln.substr(0, ln.find(' '));
            if (std::find(verbs.begin(), verbs.end(), w) == verbs.end()) continue;
            say("    " + ln);
            enqueue(ln);
            n++;
        }
        if (n == 0) say("    (the mind proposed no actions)");
    }

    // ── The language ─────────────────────────────────────────────────────────
    void exec(const std::string& line) {
        std::istringstream ss(line);
        std::string verb;
        ss >> verb;
        if (verb.empty()) return;

        auto badId = [&](int id) { say("  ? no thing #" + std::to_string(id)); };

        if (verb == "help") {
            say("  spawn [cube|box|pyramid|card] [#rrggbb]   make a thing");
            say("  move <id> <dx> <dy> <dz>  |  color <id> <#rrggbb>");
            say("  spin <id|all> [deg/s]     |  stop <id|all>");
            say("  ring | clear | list | quit");
            say("  ask <text...>            the mind ANSWERS (talk)");
            say("  make <text...>           the mind ACTS - spawns & arranges (do)");
            say("  provider [ollama|grok|claude|deepseek]   switch the mind");
            say("  [hold TAB]               speak - voice goes to make (or ask)");
        } else if (verb == "spawn") {
            std::string shape = "cube", colStr;
            ss >> shape;
            glm::vec4 col = kPalette[m_objs.size() % (sizeof(kPalette) / sizeof(kPalette[0]))];
            if (ss >> colStr) parseColor(colStr, col);
            spawn(shape, col);
        } else if (verb == "list") {
            if (m_objs.empty()) say("  (empty world)");
            for (size_t i = 0; i < m_objs.size(); i++)
                say("  #" + std::to_string(i) + "  " + m_objs[i].shape + (m_objs[i].spin != 0 ? "  (spinning)" : ""));
        } else if (verb == "move") {
            int id; float dx, dy, dz;
            if (!(ss >> id) || id < 0 || id >= (int)m_objs.size()) { badId(id); }
            else if (ss >> dx >> dy >> dz) { m_objs[id].pos += glm::vec3(dx, dy, dz); say("  moved #" + std::to_string(id)); }
        } else if (verb == "color") {
            int id; std::string c;
            glm::vec4 col;
            if (!(ss >> id) || id < 0 || id >= (int)m_objs.size()) { badId(id); }
            else if ((ss >> c) && parseColor(c, col)) {
                m_modelRenderer->destroyModel(m_objs[id].handle);
                m_objs[id].color = col;
                m_objs[id].handle = makeMesh(m_objs[id].shape, col);
                say("  recolored #" + std::to_string(id));
            }
        } else if (verb == "spin" || verb == "stop") {
            std::string who;
            ss >> who;
            float rate = 0.0f;
            if (verb == "spin" && !(ss >> rate)) rate = 45.0f;
            if (who == "all") { for (auto& o : m_objs) o.spin = rate; say("  " + verb + " all"); }
            else {
                std::istringstream w(who);
                int id;
                if (w >> id && id >= 0 && id < (int)m_objs.size()) { m_objs[id].spin = rate; say("  " + verb + " #" + std::to_string(id)); }
                else say("  ? which thing?");
            }
        } else if (verb == "ring") {
            reRing();
            say("  ringed " + std::to_string(m_objs.size()) + " things");
        } else if (verb == "clear") {
            for (auto& o : m_objs) m_modelRenderer->destroyModel(o.handle);
            m_objs.clear();
            say("  world cleared");
        } else if (verb == "ask") {
            std::string rest;
            std::getline(ss, rest);
            while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            if (rest.empty()) { say("  ? ask what?"); }
            else {
                say("  ~ thinking (" + m_provider + ") ...");
                std::thread([this, rest] { doAsk(rest); }).detach(); // off the render thread
            }
        } else if (verb == "make") {
            std::string rest;
            std::getline(ss, rest);
            while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            if (rest.empty()) say("  ? make what?");
            else {
                say("  ~ the mind is shaping the world (" + m_provider + ") ...");
                std::thread([this, rest] { doMake(rest); }).detach();
            }
        } else if (verb == "provider") {
            std::string p;
            ss >> p;
            if (p.empty()) say("  provider = " + m_provider + " (try: ollama|grok|claude|deepseek)");
            else {
                std::lock_guard<std::mutex> lk(m_sessMtx);
                m_provider = p;
                m_session.clear(); // fresh conversation on a new mind
                say("  provider -> " + p + " (new conversation)");
            }
        } else if (verb == "quit" || verb == "exit") {
            glfwSetWindowShouldClose(getWindow().getHandle(), 1);
        } else {
            say("  ? unknown verb '" + verb + "' (try help)");
        }
    }

    std::unique_ptr<ModelRenderer> m_modelRenderer;
    Camera m_camera;
    std::vector<Obj> m_objs;

    // The mind: conversation session + selected provider.
    std::string m_session;
    std::string m_provider = "ollama"; // local-first default
    std::mutex m_sessMtx;

    // Commands queued (by the mind, on another thread) for main-thread execution.
    std::queue<std::string> m_cmdQueue;
    std::mutex m_cmdMtx;

    pid_t m_recPid = 0; // parecord child while push-to-talk is held

    // Console
    std::vector<std::string> m_scroll;
    std::mutex m_scrollMtx; // m_scroll is written by the async `ask` thread too
    std::string m_input;
    std::atomic<bool> m_hudDirty{true};
    uint32_t m_hud = 0;
    std::vector<unsigned char> m_hudPix;
    int m_hudW = 560, m_hudH = 940; // tall, narrow — a right-side console panel
};

int main() {
    // Run from the executable's own directory so ./shaders resolves no matter
    // how it was launched (terminal, double-click, or a menu shortcut).
    char exePath[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (n > 0) { exePath[n] = '\0'; if (chdir(dirname(exePath)) != 0) {} }

    try {
        Vieuphoria app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
