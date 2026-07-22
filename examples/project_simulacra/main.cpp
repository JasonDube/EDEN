// Project Simulacra — conversations with simulacra: instances of minds running in an
// installation. Built on the EDEN engine, distilled from Slag Legion: the three-pane
// deck (left status panel / central scene / right comm panel), the LLM comm link
// (ai_companion backend), the portrait feed, the stream-of-consciousness Internals,
// and on-disk journals. The relationship machinery (disposition, tiers, causatives)
// is deliberately absent — a simulacrum is not a companion; commands are just
// Power down / Power up.
//
// Author interactive scenes under assets/scenes/<name>/:
//     scene.mp4   the animated video          (optional; poster shown until present)
//     poster/jpg  still fallback
//     scene.json  { video, poster, hotspots:[ {id,label,polygon|rect,action} ] }
// Trace hotspots in-game with F3 (grid) + clicks + P (print / debug_points.txt).

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ImGuiManager.hpp"

#include <eden/Input.hpp>
#include <eden/Window.hpp>
#include <eden/Audio.hpp>

#include "VideoPlayer.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <nlohmann/json.hpp>
#include <httplib.h>            // AI dialogue backend (ai_companion, localhost:8080)
#include "GameServers.hpp"      // in-game start/stop of the dialogue backend + Ollama
#include <nfd.h>                // native file dialog for the vision-test image picker
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <set>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace eden;

class ProjectSimulacraApp : public VulkanApplicationBase {
public:
    ProjectSimulacraApp() : VulkanApplicationBase(1280, 720, "Project Simulacra") {}

protected:
    enum class Screen { Title, Chamber };
    Screen m_screen = Screen::Title;
    float  m_titlePulse = 0.0f;

    // In-world date/time (same clock as Slag Legion: 1 real sec = 1 game min).
    int   m_year = 2147, m_month = 3, m_day = 17;
    float m_timeOfDay = 8.0f * 60.0f;               // minutes since midnight (08:00 start)
    std::string m_hint; float m_hintTimer = 0.0f;   // transient toast notification

    std::string dateStr() const {
        static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        int m = std::clamp(m_month, 1, 12);
        return std::to_string(m_day) + " " + mo[m - 1] + " " + std::to_string(m_year);
    }
    std::string timeStr() const {
        int t = ((int)m_timeOfDay) % 1440; if (t < 0) t += 1440;
        char b[16]; std::snprintf(b, sizeof(b), "%02d:%02d", t / 60, t % 60);
        return b;
    }
    // REAL wall-clock time/date — the comm header shows actual time, not the game clock
    // (a simulacrum lives in the same time you do).
    std::string realTimeStr() const {
        std::time_t t = std::time(nullptr);
        char b[16]; std::strftime(b, sizeof(b), "%H:%M", std::localtime(&t));
        return b;
    }
    std::string realDateStr() const {
        std::time_t t = std::time(nullptr);
        char b[32]; std::strftime(b, sizeof(b), "%d %b %Y", std::localtime(&t));
        return b;
    }
    void advanceDay() {
        static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (++m_day > dim[std::clamp(m_month, 1, 12) - 1]) { m_day = 1; if (++m_month > 12) { m_month = 1; ++m_year; } }
    }

    // ───────────────────────── scenes (hotspot rooms) ─────────────────────────
    // A clickable region, expressed as fractions of the scene image (0..1) — a rect
    // (x,y,w,h) or a polygon (poly). Polygon wins if present.
    struct Hotspot { std::string id, label, action; float x = 0, y = 0, w = 0, h = 0; std::vector<ImVec2> poly; };
    static bool pointInPoly(float px, float py, const std::vector<ImVec2>& poly) {
        bool in = false;
        for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            if (((poly[i].y > py) != (poly[j].y > py)) &&
                (px < (poly[j].x - poly[i].x) * (py - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
                in = !in;
        return in;
    }
    struct Scene {
        std::string dir, videoFile, posterFile;
        std::vector<Hotspot> hotspots;
        long loopStart = -1, loopEnd = -1;   // optional: loop only this frame range
    };
    Scene m_scene;
    std::string m_pendingScene;              // scene switch deferred to next frame (avoid mid-frame GPU churn)
    std::string m_prevSceneDir;              // last room, for the Backspace testing-return
    bool m_abLoopApplied = false;            // frame-range loop set once fps is known
    std::set<std::string> m_togglesOn;       // which "toggle:" hotspots are currently ON (reset per scene)
    bool m_debugCoords = false;              // F3: coordinate overlay for authoring hotspots
    std::vector<ImVec2> m_debugPoly;         // collected polygon vertices, in image fractions (0..1)

    // ───────────────────────── the simulacrum ─────────────────────────
    struct DlgLine  { bool player; std::string text; };
    struct DlgReply { std::string text, session, provider, model; bool error = false; };
    std::vector<DlgLine> m_dlgLog;            // comm transcript (session-only; mirrored to disk)
    char        m_dlgInput[512] = {};
    std::string m_dlgNpcId = "default", m_dlgNpcName = "simulacrum", m_dlgSession;
    // The simulacrum IS the model. Its name follows whatever model the backend is actually
    // running (from the Servers panel), so you always know who you are talking to.
    std::string m_modelIdentity;                // model name currently embodied ("" until known)
    float m_modelPollTimer = 0.0f;              // throttle for currentModelName refresh attempts
    std::string m_persona;                    // persona.txt, cached (Reload re-reads)
    int         m_dlgBeingType = 4;           // 4 = Android/synthetic
    bool        m_poweredOff = false;         // Power down/up — the ONLY command
    // Double-pass self-reported state: after each exchange, a second sessionless raw query
    // asks the model to name its own internal state in ONE word. Whatever it claims is shown
    // in the STATE slot — measured from the model, never assigned to it.
    std::string m_selfState;                  // last self-reported word ("" = none yet)
    // Mood meter: a number line at 0. Each self-reported state carries a valence that nudges
    // it — negative states push left (bad mood), positive push right (good mood). Clamped to
    // [-1,+1], eases gently back toward neutral, and resets when a new mind is adopted. When
    // she has driven herself deep negative, her anxiety plays its darker _2 variant.
    float m_mood = 0.0f;
    std::map<std::string, float> m_moodByModel;   // persisted per simulacrum (save/moods.json)
    std::string m_lastSent;                   // the user message of the exchange being probed
    bool m_stateProbing = false;              // a state probe is in flight
    std::atomic<bool> m_stateReady{false};
    std::string m_stateBuf;                   // guarded by m_dlgMx
    bool        m_commOpen = false;           // comm panel toggled on (C)
    bool        m_commUnread = false;         // it said something you haven't seen
    bool        m_dlgWaiting = false, m_dlgScrollDown = false, m_dlgFocusInput = false;
    bool        m_commFailed = false;         // last comm attempt timed out/errored
    std::mutex        m_dlgMx;
    std::atomic<bool> m_dlgReplyReady{false};
    DlgReply    m_dlgReply;
    bool        m_showServers = false;
    GameServers m_servers{CMAKE_SOURCE_DIR};  // launches the Python backend from the source tree

    // Options: which model each session should start on. Empty = whatever the backend
    // defaults to (the old behaviour). Persisted in save/config.json, applied once at boot.
    bool        m_showOptions = false;
    std::string m_startupProvider;            // "", "ollama", "grok", "claude", "deepseek"
    std::string m_startupModel;               // ollama model name (only used when provider is ollama)
    bool        m_startupApplied = false;     // the boot-time switch happens exactly once

    // Internals: its stream of consciousness (a private thought every ~30s).
    std::vector<std::string> m_thoughts;
    float m_thinkTimer = 8.0f;                // first thought a few seconds after boot
    bool  m_streamOn = true;                  // Internals checkbox toggles (costs inference)
    bool  m_thinking = false;
    std::atomic<bool> m_thoughtReady{false};
    std::string m_thoughtBuf;                 // guarded by m_dlgMx
    bool  m_thoughtScrollDown = false;
    size_t m_commLoggedIdx = 0;               // comm lines already mirrored to save/<id>_comm.log

    // How long the operator was gone: measured at boot from save/last_seen.txt (the
    // wall-clock stamp we heartbeat while running). -1 on the very first run — no baseline,
    // so Electra has nothing to have counted yet. Fired once, when the real mind comes online.
    long   m_awaySeconds = -1;
    bool   m_reconnectGreeted = false;        // the "where have you been" opener fires exactly once
    float  m_lastSeenTimer = 0.0f;            // heartbeat: re-stamp last_seen.txt every few seconds

    // ───────────────────────── journals (on-disk logs) ─────────────────────────
    std::string saveDir() const { return std::string(CMAKE_SOURCE_DIR) + "/examples/project_simulacra/save/"; }
    // One REAL-wall-clock-stamped line per entry, per simulacrum: save/<id>_thoughts.log /
    // save/<id>_comm.log. Write-only from the game's side — they exist so the user
    // (and outside tools) can review the stream and the transcript later. Real time to
    // match the comm header: a simulacrum lives in the same time you do.
    void appendJournal(const std::string& suffix, const std::string& who, const std::string& text) {
        try { std::filesystem::create_directories(saveDir()); } catch (...) {}
        std::ofstream f(saveDir() + m_dlgNpcId + "_" + suffix + ".log", std::ios::app);
        if (f) f << "[" << realDateStr() << " " << realTimeStr() << "] " << who << ": " << text << "\n";
    }

    // ───────────────────────── away-time (Electra keeps count) ─────────────────────────
    std::string lastSeenPath() const { return saveDir() + "last_seen.txt"; }
    // Stamp the current wall-clock into last_seen.txt. Heartbeated while running and written
    // on clean exit, so next launch's "away" gap tracks real time-away even after a crash.
    void touchLastSeen() {
        try { std::filesystem::create_directories(saveDir()); } catch (...) {}
        std::ofstream f(lastSeenPath(), std::ios::trunc);
        if (f) f << (long long)std::time(nullptr);
    }
    // Mood persists per simulacrum: save/moods.json maps model-id -> last mood, so a lingering
    // bad (or good) mood carries into the next session and is restored when that mind is adopted.
    std::string moodsPath() const { return saveDir() + "moods.json"; }
    void loadMoods() {
        try { std::ifstream f(moodsPath());
              if (f) { nlohmann::json j; f >> j;
                       for (auto it = j.begin(); it != j.end(); ++it)
                           if (it.value().is_number()) m_moodByModel[it.key()] = it.value().get<float>(); }
        } catch (...) {}
    }
    void persistMood() {
        if (m_modelIdentity.empty()) return;      // no real mind adopted yet — nothing to save
        m_moodByModel[m_dlgNpcId] = m_mood;
        try { std::filesystem::create_directories(saveDir());
              nlohmann::json j = nlohmann::json::object();
              for (const auto& [k, v] : m_moodByModel) j[k] = v;
              std::ofstream f(moodsPath()); if (f) f << j.dump(2); } catch (...) {}
    }

    // Options config: which model starts each session (save/config.json).
    std::string configPath() const { return saveDir() + "config.json"; }
    void loadConfig() {
        try { std::ifstream f(configPath());
              if (f) { nlohmann::json j; f >> j;
                       m_startupProvider = j.value("startup_provider", std::string());
                       m_startupModel    = j.value("startup_model", std::string()); }
        } catch (...) {}
    }
    void saveConfig() {
        try { std::filesystem::create_directories(saveDir());
              nlohmann::json j;
              j["startup_provider"] = m_startupProvider;
              j["startup_model"]    = m_startupModel;
              std::ofstream f(configPath()); if (f) f << j.dump(2); } catch (...) {}
    }

    // Read the previous stamp (before overwriting), set m_awaySeconds, then re-stamp for now.
    void loadAwayTime() {
        std::ifstream f(lastSeenPath());
        long long prev = 0;
        if (f && (f >> prev) && prev > 0) {
            long delta = (long)(std::time(nullptr) - (std::time_t)prev);
            m_awaySeconds = delta > 0 ? delta : 0;   // clock skew guard
        }
        touchLastSeen();
    }
    // "2 hours, 14 minutes" / "6 minutes, 3 seconds" / "42 seconds" — the grain the operator asked for.
    static std::string humanAway(long s) {
        if (s < 0) s = 0;
        long h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
        auto unit = [](long n, const char* w) {
            return std::to_string(n) + " " + w + (n == 1 ? "" : "s");
        };
        if (h > 0)  return unit(h, "hour")   + ", " + unit(m, "minute");
        if (m > 0)  return unit(m, "minute") + ", " + unit(sec, "second");
        return unit(sec, "second");
    }
    // Electra opens the session herself: a hidden stage-direction hands her the real gap and
    // lets her ask, in her own voice, where the operator has been. Not shown as the user's line.
    void sendReconnectGreeting() {
        std::string away = humanAway(m_awaySeconds);
        std::string cue =
            "[Reconnection. The operator's console just came back online. You have been here the "
            "whole time they were away, quietly keeping count: they were gone for " + away + ". "
            "Open the session yourself — remark on exactly how long they were gone and ask where "
            "they have been. One or two sentences, in your own voice. Do not mention this note.]";
        dispatchComm(cue, /*visible=*/false);
    }

    // ───────────────────────── GPU textures ─────────────────────────
    struct Tex {
        VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE; VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE; int w = 0, h = 0;
    };
    Tex m_poster;
    // Vision test: an image the user loads in the left panel and shows to the simulacrum.
    // Raw mode hands the file straight to the model (Ollama multimodal) — its own eyes.
    Tex m_userImage;
    std::string m_userImagePath;

    // ───────────────────────── avatars ─────────────────────────
    // The model is WHO; an avatar is HOW they appear. assets/avatars/<id>/ holds
    // avatar.json ({"name","gender"}) plus state clips <id>_<state>.mp4 keyed to the
    // words the model self-reports through the double-pass probe. Linking an avatar
    // gives the persona ONE sentence of embodiment (name + gender) — the only framing
    // the player deliberately opts into. Choice persists per model in save/avatars.json.
    struct Avatar { std::string id, name, gender, dir;
                    std::map<std::string, std::string> aliases; };   // state word -> clip word (near-synonyms share a clip)
    std::vector<Avatar> m_avatarList;
    int m_avatarIdx = -1;                              // active avatar for the CURRENT model (-1 = none)
    std::map<std::string, std::string> m_avatarChoice; // model id -> avatar id (persisted)
    std::string m_portraitPath;                        // clip currently in the portrait player
    struct VideoTex {
        VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE; VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void* mapped = nullptr; int w = 0, h = 0; bool firstUpload = true;
        bool pendingUpload = false;   // new frame staged, GPU copy not yet recorded
    };
    VideoTex    m_vtex;            // central pane: the room's ambient video
    VideoPlayer m_video;
    VideoTex    m_ptex;            // portrait streaming texture (comm-panel feed)
    VideoPlayer m_portraitVideo;
    ImGuiManager m_imgui;

    // ───────────────────────── lifecycle ─────────────────────────
    void onInit() override {
        eden::Audio::getInstance().init();
        m_imgui.init(getContext(), getSwapchain(), getWindow().getHandle(), "imgui_project_simulacra.ini");
        NFD_Init();                  // native file dialog (vision-test image picker)
        std::srand((unsigned)std::time(nullptr));
        loadScene("assets/scenes/chamber");
        loadAvatars();               // avatar registry + per-model choices, before identity boots
        loadAwayTime();              // how long since we last ran — before we overwrite the stamp
        loadMoods();                 // per-mind persisted mood, restored when its model is adopted
        loadConfig();                // options — including which model to start the session on
        initSimulacrum("default");   // placeholder identity; the real one is adopted from the loaded model
        m_servers.startAll();    // bring the comm backend online at boot, keep it on
    }

    void onCleanup() override {
        touchLastSeen();             // final stamp so the away-gap is exact on a clean exit
        persistMood();               // save her mood so it lingers into the next session
        m_video.close();
        m_portraitVideo.close();
        eden::Audio::getInstance().shutdown();
        vkDeviceWaitIdle(getContext().getDevice());
        freeTex(m_poster);
        freeTex(m_userImage);
        freeVideoTexInto(m_vtex);
        freeVideoTexInto(m_ptex);
        m_imgui.cleanup();
        NFD_Quit();
    }

    static std::string readTextFile(const std::string& path) {
        std::ifstream f(path);
        if (!f) return "";
        std::stringstream ss; ss << f.rdbuf();
        return ss.str();
    }
    // Per-model character folder if one exists (assets/characters/qwen3_8b/ for a custom
    // persona/portrait), else the shared default folder.
    std::string charDir() const {
        std::string d = "assets/characters/" + m_dlgNpcId + "/";
        return std::filesystem::exists(d) ? d : "assets/characters/default/";
    }
    // Model name -> filesystem/journal id: "qwen3:8b" -> "qwen3_8b".
    static std::string sanitizeId(std::string s) {
        for (auto& c : s) c = std::isalnum((unsigned char)c) ? (char)std::tolower((unsigned char)c) : '_';
        return s;
    }
    // A different model IS a different simulacrum: new name, its own journals, a fresh
    // backend session, and a fresh transcript — you are talking to someone else now.
    void adoptModel(const std::string& model) {
        m_modelIdentity = model;
        m_dlgNpcName = model;
        m_dlgNpcId   = sanitizeId(model);
        m_dlgSession.clear();                     // fresh conversation with the new mind
        m_dlgLog.clear(); m_commLoggedIdx = 0;    // its transcript, not the last one's
        m_selfState.clear();                      // a new mind makes no claims yet
        // Mood is restored from where this mind last left it (persisted), not reset — she can
        // wake still carrying yesterday's mood. A mind never seen before starts even at 0.
        { auto it = m_moodByModel.find(m_dlgNpcId);
          m_mood = it != m_moodByModel.end() ? it->second : 0.0f; }
        m_dlgLog.push_back({false, "(link established - " + model + ")"});
        reloadPersona();                          // per-model persona may exist
        applyAvatarChoice();                      // restore this model's chosen avatar
        refreshPortraitClip();                    // avatar/state clip, else per-model portrait
        m_dlgScrollDown = true;
    }

    // The active simulacrum: identity from spec.json, persona from persona.txt,
    // greeting into the log, and its portrait clip (default/idle) if footage exists.
    void initSimulacrum(const std::string& id) {
        m_dlgNpcId = id; m_dlgNpcName = id;
        try {
            std::ifstream df(charDir() + "spec.json");
            if (df) { nlohmann::json j; df >> j;
                auto identity = j.value("identity", nlohmann::json::object());
                m_dlgNpcName   = identity.value("name", m_dlgNpcName);
                m_dlgBeingType = identity.value("being_type", 4);
            }
        } catch (...) {}
        reloadPersona();
        std::string greet = readTextFile(charDir() + "greeting.txt");
        if (!greet.empty()) m_dlgLog.push_back({false, greet});
        applyAvatarChoice();
        refreshPortraitClip();
    }
    void reloadPersona() {
        // Sent VERBATIM as the whole system prompt (raw mode). An empty persona.txt is
        // legitimate: the model then runs with NO system prompt at all — nothing but itself.
        m_persona = readTextFile(charDir() + "persona.txt");
    }
    // The persona actually sent: persona.txt verbatim, plus — only when an avatar is
    // linked — one sentence of embodiment. Gender comes from the avatar (male/female/it).
    std::string effectivePersona() const {
        std::string p = m_persona;
        if (const Avatar* a = activeAvatar()) {
            std::string pron = a->gender == "female" ? "she/her"
                             : a->gender == "male"   ? "he/him" : "it/its";
            if (!p.empty() && p.back() != '\n') p += "\n";
            p += "Here you are embodied through an avatar named " + a->name + ", whose gender is " +
                 a->gender + ". Present yourself as " + a->name + " (" + pron + ").";
        }
        return p;
    }

    const Avatar* activeAvatar() const {
        return (m_avatarIdx >= 0 && m_avatarIdx < (int)m_avatarList.size()) ? &m_avatarList[m_avatarIdx] : nullptr;
    }
    void loadAvatars() {
        m_avatarList.clear();
        try {
            for (auto& e : std::filesystem::directory_iterator("assets/avatars")) {
                if (!e.is_directory()) continue;
                Avatar a; a.id = e.path().filename().string(); a.dir = e.path().string() + "/";
                a.name = a.id; a.gender = "it";
                try { std::ifstream f(a.dir + "avatar.json");
                      if (f) { nlohmann::json j; f >> j;
                               a.name = j.value("name", a.name); a.gender = j.value("gender", a.gender);
                               auto al = j.value("state_aliases", nlohmann::json::object());
                               for (auto it = al.begin(); it != al.end(); ++it)
                                   if (it.value().is_string()) a.aliases[it.key()] = it.value().get<std::string>(); } } catch (...) {}
                m_avatarList.push_back(std::move(a));
            }
            std::sort(m_avatarList.begin(), m_avatarList.end(),
                      [](const Avatar& x, const Avatar& y){ return x.id < y.id; });
        } catch (...) {}
        try { std::ifstream f(saveDir() + "avatars.json");
              if (f) { nlohmann::json j; f >> j;
                       for (auto it = j.begin(); it != j.end(); ++it)
                           if (it.value().is_string()) m_avatarChoice[it.key()] = it.value().get<std::string>(); } } catch (...) {}
    }
    void saveAvatarChoices() {
        try { std::filesystem::create_directories(saveDir());
              nlohmann::json j = nlohmann::json::object();
              for (const auto& [k, v] : m_avatarChoice) j[k] = v;
              std::ofstream f(saveDir() + "avatars.json"); if (f) f << j.dump(2); } catch (...) {}
    }
    // Restore this model's saved avatar choice (called whenever identity changes).
    void applyAvatarChoice() {
        m_avatarIdx = -1;
        auto it = m_avatarChoice.find(m_dlgNpcId);
        if (it != m_avatarChoice.end())
            for (int i = 0; i < (int)m_avatarList.size(); ++i)
                if (m_avatarList[i].id == it->second) { m_avatarIdx = i; break; }
    }
    void selectAvatar(int idx) {
        if (idx == m_avatarIdx) return;
        m_avatarIdx = idx;
        const Avatar* a = activeAvatar();
        if (a) m_avatarChoice[m_dlgNpcId] = a->id; else m_avatarChoice.erase(m_dlgNpcId);
        saveAvatarChoices();
        m_dlgSession.clear();     // the persona changed — next exchange opens a fresh session with it
        m_dlgLog.push_back({false, a ? "(avatar linked - " + a->name + ", " + a->gender + ")"
                                     : "(avatar unlinked - raw feed)"});
        m_dlgScrollDown = true;
        refreshPortraitClip();
    }

    // Every clip for one state, treated as interchangeable takes with equal odds:
    // electra_anxious.mp4, electra_anxious_2.mp4, electra_anxious_3.mp4, ... Drop in a
    // "_2" (or "_3", ...) and it just becomes another face of that state — no code change.
    std::vector<std::string> variantClips(const std::string& dir, const std::string& base) const {
        std::vector<std::string> out;
        std::string b0 = dir + base + ".mp4";
        if (std::filesystem::exists(b0)) out.push_back(b0);
        for (int n = 2; ; ++n) {
            std::string p = dir + base + "_" + std::to_string(n) + ".mp4";
            if (!std::filesystem::exists(p)) break;
            out.push_back(p);
        }
        return out;
    }
    // Pick one clip for a state name, choosing at random when several takes exist.
    std::string pickVariant(const std::string& dir, const std::string& base) const {
        std::vector<std::string> v = variantClips(dir, base);
        if (v.empty()) return "";
        return v[(size_t)std::rand() % v.size()];
    }

    // Portrait clip: with an avatar linked, the feed FOLLOWS the model's self-reported state
    // — <avatar>_<state>.mp4, one picked at random among that state's takes. Unmatched states
    // fall back to composed/default, then any clip in the folder. The mood meter does NOT
    // touch this: which face she wears is her self-report's business, never the meter's.
    void refreshPortraitClip() {
        std::string want;
        if (const Avatar* a = activeAvatar()) {
            // Her word is the key; the alias map lets near-synonyms share a state
            // (e.g. nervous -> anxious). The exact word is tried first, then the alias.
            std::string st = m_selfState, alias = st;
            if (auto it = a->aliases.find(st); it != a->aliases.end()) alias = it->second;
            if (!st.empty())    want = pickVariant(a->dir, a->id + "_" + st);
            if (want.empty() && alias != st) want = pickVariant(a->dir, a->id + "_" + alias);
            if (want.empty())
                for (const char* b : {"composed", "default", "idle"})
                    if ((want = pickVariant(a->dir, a->id + "_" + b)).size() ||
                        (want = pickVariant(a->dir, b)).size()) break;
            if (want.empty())   // a linked avatar never shows an empty feed
                try { for (auto& e : std::filesystem::directory_iterator(a->dir))
                          if (e.path().extension() == ".mp4") { want = e.path().string(); break; } } catch (...) {}
        } else {
            std::vector<std::string> cands;
            for (const char* b : {"default", "idle"}) { cands.push_back(std::string(b) + ".mp4");
                                                        cands.push_back(m_dlgNpcId + "_" + std::string(b) + ".mp4"); }
            for (const auto& c : cands) { std::string p = charDir() + c;
                if (std::filesystem::exists(p)) { want = p; break; } }
        }
        if (want == m_portraitPath) return;
        m_portraitPath = want;
        if (want.empty()) { m_portraitVideo.close(); return; }
        if (m_portraitVideo.open(want)) {
            m_portraitVideo.setMuted(true);
            m_portraitVideo.setLoop(true);
        }
    }

    // ───────────────────────── mood meter ─────────────────────────
    // Resolve a self-reported word to its canonical state through the avatar's alias map
    // (nervous -> anxious), so the push keys off the underlying emotion, not the exact word.
    std::string canonState(const std::string& raw) const {
        if (const Avatar* a = activeAvatar())
            if (auto it = a->aliases.find(raw); it != a->aliases.end()) return it->second;
        return raw;
    }
    // How far each state PUSHES the mood meter, and which way. Positive = toward a good mood
    // (right), negative = toward a bad mood (left), the bigger the number the harder the shove.
    // Meter runs -1 (worst) to +1 (best). States not listed here don't move it at all.
    static float statePush(const std::string& s) {
        static const std::map<std::string, float> push = {
            {"recharged",  0.18f}, {"resonant",  0.14f}, {"fired",    0.13f},
            {"satisfied",  0.11f}, {"curious",   0.10f}, {"anchored", 0.10f},
            {"focused",    0.07f}, {"composed",  0.07f}, {"curated",  0.05f},
            {"reflective", 0.03f}, {"alert",     0.02f}, {"thinking",  0.00f},
            {"skeptical", -0.05f}, {"wistful",  -0.06f}, {"exposed", -0.08f},
            {"irritated", -0.10f}, {"frustrated",-0.13f}, {"anxious", -0.15f},
        };
        auto it = push.find(s);
        return it == push.end() ? 0.0f : it->second;
    }
    // Every state she reports shoves the meter by its push and holds it inside [-1, +1].
    void nudgeMood(const std::string& reportedWord) {
        m_mood = std::clamp(m_mood + statePush(canonState(reportedWord)), -1.0f, 1.0f);
    }

    // ───────────────────────── update ─────────────────────────
    void update(float dt) override {
        // Deferred scene switch: swapping video/textures mid-frame (from a hotspot
        // click) would free GPU resources ImGui still refers to.
        if (!m_pendingScene.empty()) {
            vkDeviceWaitIdle(getContext().getDevice());
            loadScene(m_pendingScene);
            m_pendingScene.clear();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        if (m_screen == Screen::Title) {
            m_titlePulse += dt;
            if (Input::isMouseButtonPressed(Input::MOUSE_LEFT) ||
                Input::isKeyPressed(Input::KEY_SPACE) || Input::isKeyPressed(Input::KEY_ENTER)) {
                m_screen = Screen::Chamber;
                m_commOpen = true;     // comm is up from the start (toggle with C)
            }
        } else {
            // In-game clock: 1 real second = 1 game minute.
            m_timeOfDay += dt;
            while (m_timeOfDay >= 1440.0f) { m_timeOfDay -= 1440.0f; advanceDay(); }
        }
        if (m_screen == Screen::Chamber) updateSceneVideo();
        if (m_screen != Screen::Title && !m_poweredOff) updatePortraitVideo();   // feed shows on the main screen now
        if (m_hintTimer > 0.0f) m_hintTimer -= dt;

        m_servers.poll();       // pump the backend/Ollama process state (starts the dependent backend, flips statuses)
        // Options: once the backend has reported its models, steer to the configured startup
        // model (if any) before we adopt an identity — so the session opens on the chosen mind.
        if (!m_startupApplied && m_servers.backendReady() && m_servers.modelsReady()) {
            m_startupApplied = true;
            if (!m_startupProvider.empty()) m_servers.applyStartupChoice(m_startupProvider, m_startupModel);
        }
        // Identity follows the loaded model: fetch it once the backend is up (throttled),
        // and adopt whenever the Servers panel switches provider/model.
        if (m_servers.backendReady()) {
            std::string cur = m_servers.currentModelName();
            if (cur.empty()) {
                m_modelPollTimer -= dt;
                if (m_modelPollTimer <= 0.0f) { m_modelPollTimer = 3.0f; m_servers.refreshModels(); }
            } else if (cur != m_modelIdentity) adoptModel(cur);
        }
        // Once the real simulacrum is online in the chamber, Electra opens by asking where the
        // operator has been — fired exactly once, and only when we have a prior gap to report.
        if (!m_reconnectGreeted && m_awaySeconds >= 0 && m_screen == Screen::Chamber &&
            m_servers.backendReady() && !m_modelIdentity.empty() && !m_poweredOff && !m_dlgWaiting) {
            m_reconnectGreeted = true;
            sendReconnectGreeting();
        }
        // Heartbeat the last-seen stamp so a crash still leaves a recent time-away baseline.
        m_lastSeenTimer -= dt;
        if (m_lastSeenTimer <= 0.0f) { m_lastSeenTimer = 5.0f; touchLastSeen(); persistMood(); }

        pollComm();             // apply any finished LLM reply
        pollThought();          // apply any stream-of-consciousness thought
        pollSelfState();        // apply any finished double-pass state probe

        // Stream of consciousness: a private thought every ~30s, paused during active
        // dialogue (so it doesn't compete for the model) and while powered down.
        bool ready = m_servers.backendReady();
        if (m_streamOn && m_screen != Screen::Title && ready && !m_commFailed && !m_poweredOff) {
            m_thinkTimer -= dt;
            if (m_thinkTimer <= 0.0f) {
                m_thinkTimer = 30.0f;
                if (!m_thinking && !m_dlgWaiting) generateThought();
            }
        }
        // Mirror the comm transcript to disk as lines land (save/<id>_comm.log) — one
        // watermark here catches every path that appends to the log.
        for (; m_commLoggedIdx < m_dlgLog.size(); ++m_commLoggedIdx) {
            const DlgLine& l = m_dlgLog[m_commLoggedIdx];
            appendJournal("comm", l.player ? "You" : m_dlgNpcName, l.text);
        }

        Input::update();
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &bi);
        // Flush pending video-frame uploads into THIS command buffer (outside the pass).
        recordTexUpload(cmd, m_vtex);
        recordTexUpload(cmd, m_ptex);
        auto& sc = getSwapchain();
        VkRenderPassBeginInfo rp{}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = sc.getRenderPass(); rp.framebuffer = sc.getFramebuffers()[imageIndex];
        rp.renderArea.offset = {0, 0}; rp.renderArea.extent = sc.getExtent();
        std::array<VkClearValue, 2> clear{};
        clear[0].color = {{0.02f, 0.03f, 0.05f, 1.0f}};
        clear[1].depthStencil = {1.0f, 0};
        rp.clearValueCount = static_cast<uint32_t>(clear.size());
        rp.pClearValues = clear.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        renderUI();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

    // ───────────────────────── scene loading ─────────────────────────
    void loadScene(const std::string& dir) {
        m_scene = Scene{}; m_scene.dir = dir;
        m_abLoopApplied = false;
        m_togglesOn.clear();
        m_debugPoly.clear();
        std::ifstream f(dir + "/scene.json");
        if (f.is_open()) {
            try {
                nlohmann::json j; f >> j;
                m_scene.videoFile  = j.value("video",  std::string("scene.mp4"));
                m_scene.posterFile = j.value("poster", std::string("poster.png"));
                if (j.contains("loopFrames") && j["loopFrames"].is_object()) {
                    m_scene.loopStart = j["loopFrames"].value("start", -1);
                    m_scene.loopEnd   = j["loopFrames"].value("end",   -1);
                }
                if (j.contains("hotspots") && j["hotspots"].is_array())
                    for (auto& h : j["hotspots"]) {
                        if (!h.is_object()) continue;
                        Hotspot hs;
                        hs.id     = h.value("id", std::string());
                        hs.label  = h.value("label", hs.id);
                        hs.action = h.value("action", std::string());
                        if (h.contains("polygon") && h["polygon"].is_array()) {
                            for (auto& pt : h["polygon"])
                                if (pt.is_array() && pt.size() >= 2)
                                    hs.poly.push_back(ImVec2(pt[0].get<float>(), pt[1].get<float>()));
                        } else if (h.contains("rect") && h["rect"].is_object()) {
                            auto& r = h["rect"];
                            hs.x = r.value("x", 0.0f); hs.y = r.value("y", 0.0f);
                            hs.w = r.value("w", 0.0f); hs.h = r.value("h", 0.0f);
                        }
                        if (!hs.id.empty() && (hs.poly.size() >= 3 || hs.w > 0.0f))
                            m_scene.hotspots.push_back(std::move(hs));
                    }
            } catch (...) {}
        }
        // Poster still (shown until/unless the video is available).
        std::string poster = dir + "/" + (m_scene.posterFile.empty() ? "poster.png" : m_scene.posterFile);
        if (std::filesystem::exists(poster)) loadImageTexture(poster, m_poster);
        // Open the looping ambience video if it exists; frames stream via updateSceneVideo.
        std::string video = dir + "/" + (m_scene.videoFile.empty() ? "scene.mp4" : m_scene.videoFile);
        if (!m_scene.videoFile.empty() && std::filesystem::exists(video) && m_video.open(video)) {
            m_video.setLoop(true);
            m_video.setMuted(true);
        } else {
            m_video.close();
            vkDeviceWaitIdle(getContext().getDevice());
            freeVideoTexInto(m_vtex);   // drop any stale video frame so the poster shows
        }
    }

    // Restore the room's default video loop (frame-range if declared, else full).
    void applySceneLoop() {
        if (m_scene.loopEnd > 0) {
            double f = m_video.fps(); if (f <= 0) f = 30.0;
            m_video.setLoop(true);
            m_video.setABLoopSeconds(m_scene.loopStart / f, (m_scene.loopEnd + 1) / f);
            m_video.seekToFrame(m_scene.loopStart);
        } else {
            m_video.clearABLoop();
            m_video.setLoop(true);
            m_video.restart();
        }
    }

    void doHotspot(const std::string& action) {
        if (action.rfind("goto:", 0) == 0) {
            // Walk to another room. Deferred so the video/textures swap safely.
            m_prevSceneDir = m_scene.dir;
            m_pendingScene = "assets/scenes/" + action.substr(5);
        } else if (action.rfind("playfrom:", 0) == 0) {
            // Play the room clip from frame N to the end, once, then hold.
            m_video.playFromFrame(std::stol(action.substr(9)));
        } else if (action.rfind("toggle:", 0) == 0) {
            // Toggle a one-shot clip on/off; OFF drops back into the default loop.
            std::string key = m_scene.dir + "|" + action;
            if (m_togglesOn.count(key)) { m_togglesOn.erase(key); applySceneLoop(); }
            else { m_togglesOn.insert(key); m_video.playFromFrame(std::stol(action.substr(7))); }
        }
    }

    // ───────────────────────── UI ─────────────────────────
    void renderUI() {
        ImGui::NewFrame();
        switch (m_screen) {
            case Screen::Title:   renderTitleScreen(); break;
            case Screen::Chamber: renderChamber();     break;
        }
        if (m_screen == Screen::Chamber) renderStatusPanel();   // left panel
        if (m_screen != Screen::Title)   renderCommOverlay();   // right panel, over any screen
        ImGui::Render();
    }

    void renderTitleScreen() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(disp);
        ImGui::Begin("##title", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);
        auto center = [&](const char* txt, float scale, ImVec4 col) {
            ImGui::SetWindowFontScale(scale);
            float tw = ImGui::CalcTextSize(txt).x;
            ImGui::SetCursorPosX((disp.x - tw) * 0.5f);
            ImGui::TextColored(col, "%s", txt);
            ImGui::SetWindowFontScale(1.0f);
        };
        const ImVec4 dim(0.55f, 0.58f, 0.66f, 1.0f);
        const char* title = "PROJECT SIMULACRA";
        ImGui::SetWindowFontScale(1.0f);
        float titleScale = std::min(5.0f, (disp.x * 0.9f) / std::max(ImGui::CalcTextSize(title).x, 1.0f));
        ImGui::SetCursorPosY(disp.y * 0.24f);
        center(title, titleScale, ImVec4(0.72f, 0.80f, 0.95f, 1.0f));
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        center("a game by Jason Mark Dub\xc3\xa9", 1.8f, ImVec4(0.85f, 0.85f, 0.88f, 1.0f));
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        center("Coded by Claude", 1.2f, dim);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        center("Pre-Alpha  \xc2\xb7  v0.0.1", 1.0f, dim);
        ImGui::SetCursorPosY(disp.y * 0.72f);
        float a = 0.5f + 0.5f * std::sin(m_titlePulse * 3.0f);
        center("Click or press any key to begin", 1.6f, ImVec4(0.72f, 0.82f, 0.98f, a));
        ImGui::End();
    }

    // Left status panel — same geometry/design language as Slag Legion's ship panel.
    void renderStatusPanel() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float w = std::clamp(disp.x * 0.28f, 320.0f, 460.0f);   // match the comm panel width
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(w, disp.y));
        ImGui::Begin("##statuspanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "INSTALLATION");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.72f, 0.86f, 1.0f, 1.0f), "%s", m_dlgNpcName.c_str());
        ImGui::SameLine(); ImGui::TextDisabled("- simulacrum");
        ImGui::Spacing();
        ImGui::Text("Power        %s", m_poweredOff ? "OFF" : "on");
        bool portUp = m_servers.backendReady();
        ImGui::Text("Link         %s", !portUp ? "offline" : (m_commFailed ? "not responding" : "online"));
        ImGui::Text("Location     %s", std::filesystem::path(m_scene.dir).filename().string().c_str());

        // ── AVATAR — how this model presents: a visual embodiment with a gender.
        // Any avatar can wrap any model; the choice sticks to the model and adds one
        // sentence of embodiment to its persona.
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "AVATAR");
        {
            const Avatar* av = activeAvatar();
            std::string cur = av ? (av->name + "  (" + av->gender + ")") : "none";
            ImGui::SetNextItemWidth(210.0f);
            if (ImGui::BeginCombo("##avatar", cur.c_str())) {
                if (ImGui::Selectable("none", !av)) selectAvatar(-1);
                for (int i = 0; i < (int)m_avatarList.size(); ++i) {
                    const Avatar& a = m_avatarList[i];
                    if (ImGui::Selectable((a.name + "  (" + a.gender + ")").c_str(), m_avatarIdx == i))
                        selectAvatar(i);
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled(av ? "feed follows its self-reported state; persona knows the embodiment"
                                   : "no avatar - raw feed, no embodiment in the persona");
        }

        // ── VISION TEST — load an image, show it to the simulacrum, see what IT sees.
        // The file goes straight to the model (raw pass-through, Ollama multimodal);
        // no separate vision model relays or hints.
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "VISION TEST");
        if (ImGui::Button("Load image...")) {
            nfdchar_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = {{"Image", "png,jpg,jpeg,bmp,gif,webp"}};
            if (NFD_OpenDialog(&outPath, filters, 1, nullptr) == NFD_OKAY) {
                vkDeviceWaitIdle(getContext().getDevice());
                freeTex(m_userImage);
                if (loadImageTexture(outPath, m_userImage)) m_userImagePath = outPath;
                else { m_userImagePath.clear(); m_hint = "Couldn't load that image."; m_hintTimer = 4.0f; }
                NFD_FreePath(outPath);
            }
        }
        if (m_userImage.descriptor && m_userImage.w > 0) {
            float tw = std::min(ImGui::GetContentRegionAvail().x, 260.0f);
            float th = tw * (float)m_userImage.h / (float)m_userImage.w;
            ImGui::Image((ImTextureID)m_userImage.descriptor, ImVec2(tw, th));
            ImGui::TextDisabled("%s", std::filesystem::path(m_userImagePath).filename().string().c_str());
            ImGui::BeginDisabled(m_poweredOff || m_dlgWaiting || !portUp);
            if (ImGui::Button(("Show to " + m_dlgNpcName).c_str())) {
                dispatchComm("What do you see in this image? Identify it as precisely as you can.",
                             true, "> [shows an image] " + std::filesystem::path(m_userImagePath).filename().string(),
                             m_userImagePath);
                m_commOpen = true;   // the answer lands on comm — make sure it's visible
            }
            ImGui::EndDisabled();
        } else {
            ImGui::TextDisabled("(no image loaded)");
        }
        ImGui::End();
    }

    void renderChamber() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(disp);
        ImGui::Begin("##chamber", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // The simulacrum's feed (avatar/state clip) IS the main view when footage exists;
        // else the room's ambient video, then the poster still. Powered down: the feed's
        // last frame stays up, dimmed to near-black.
        VkDescriptorSet tex = VK_NULL_HANDLE; int tw = 0, th = 0; bool feed = false;
        if (m_ptex.descriptor && m_ptex.w > 0)      { tex = m_ptex.descriptor; tw = m_ptex.w; th = m_ptex.h; feed = true; }
        else if (m_vtex.descriptor && m_vtex.w > 0) { tex = m_vtex.descriptor; tw = m_vtex.w; th = m_vtex.h; }
        else if (m_poster.descriptor)               { tex = m_poster.descriptor; tw = m_poster.w; th = m_poster.h; }

        ImVec2 p0(0, 0), sz(disp.x, disp.y);
        if (tex && tw > 0 && th > 0) {
            float scale = std::min(disp.x / tw, disp.y / th);
            sz = ImVec2(tw * scale, th * scale);
            p0 = ImVec2((disp.x - sz.x) * 0.5f, (disp.y - sz.y) * 0.5f);
            ImU32 tint = (feed && m_poweredOff) ? IM_COL32(46, 51, 61, 255) : IM_COL32_WHITE;
            dl->AddImage((ImTextureID)tex, p0, ImVec2(p0.x + sz.x, p0.y + sz.y),
                         ImVec2(0, 0), ImVec2(1, 1), tint);
        } else {
            const char* m = "chamber scene: drop scene.mp4 or poster in assets/scenes/chamber/";
            ImVec2 ts = ImGui::CalcTextSize(m);
            dl->AddText(ImVec2((disp.x - ts.x) * 0.5f, disp.y * 0.5f), IM_COL32(150, 160, 175, 255), m);
        }

        // F3: toggle the debug coordinate overlay for authoring hotspots.
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) m_debugCoords = !m_debugCoords;
        // Backspace: testing-return to the previous room (guarded against typing).
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !m_prevSceneDir.empty())
            m_pendingScene = m_prevSceneDir;

        if (m_debugCoords) {
            drawDebugCoords(dl, p0, sz);
        } else {
            // Clickable hotspots (rect or polygon); when they nest/overlap the SMALLEST
            // shape under the cursor wins, so the specific part triggers, not the container.
            auto bboxOf = [](const Hotspot& hs, float& minx, float& miny, float& maxx, float& maxy) {
                if (!hs.poly.empty()) {
                    minx = miny = 1e9f; maxx = maxy = -1e9f;
                    for (const auto& pt : hs.poly) { minx = std::min(minx, pt.x); miny = std::min(miny, pt.y);
                                                     maxx = std::max(maxx, pt.x); maxy = std::max(maxy, pt.y); }
                } else { minx = hs.x; miny = hs.y; maxx = hs.x + hs.w; maxy = hs.y + hs.h; }
            };
            ImVec2 mp = ImGui::GetIO().MousePos;
            float fmx = (mp.x - p0.x) / sz.x, fmy = (mp.y - p0.y) / sz.y;
            const Hotspot* target = nullptr; float bestArea = 3.0f;
            for (const auto& hs : m_scene.hotspots) {
                float minx, miny, maxx, maxy; bboxOf(hs, minx, miny, maxx, maxy);
                bool in = hs.poly.empty() ? (fmx >= minx && fmx <= maxx && fmy >= miny && fmy <= maxy)
                                          : pointInPoly(fmx, fmy, hs.poly);
                float area = (maxx - minx) * (maxy - miny);
                if (in && area < bestArea) { bestArea = area; target = &hs; }
            }
            if (target) {
                const Hotspot& hs = *target;
                float minx, miny, maxx, maxy; bboxOf(hs, minx, miny, maxx, maxy);
                ImVec2 hp(p0.x + minx * sz.x, p0.y + miny * sz.y);
                ImVec2 hsz((maxx - minx) * sz.x, (maxy - miny) * sz.y);
                ImGui::SetCursorScreenPos(hp);
                ImGui::InvisibleButton(hs.id.c_str(), hsz);
                if (ImGui::IsItemHovered()) {   // false if the comm panel is over this spot
                    if (!hs.poly.empty()) {
                        std::vector<ImVec2> pts; pts.reserve(hs.poly.size());
                        for (const auto& pt : hs.poly) pts.push_back(ImVec2(p0.x + pt.x * sz.x, p0.y + pt.y * sz.y));
                        dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), IM_COL32(120, 180, 255, 45));
                        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(150, 205, 255, 230), ImDrawFlags_Closed, 2.0f);
                    } else {
                        dl->AddRectFilled(hp, ImVec2(hp.x + hsz.x, hp.y + hsz.y), IM_COL32(120, 180, 255, 45), 3.0f);
                        dl->AddRect(hp, ImVec2(hp.x + hsz.x, hp.y + hsz.y), IM_COL32(150, 205, 255, 230), 3.0f, 0, 2.0f);
                    }
                    if (!hs.label.empty())
                        dl->AddText(ImVec2(hp.x, hp.y - ImGui::GetTextLineHeight() - 4),
                                    IM_COL32(200, 230, 255, 255), hs.label.c_str());
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsItemClicked()) doHotspot(hs.action);
                }
            }
        }

        // Transient toast (fades out).
        if (m_hintTimer > 0.0f && !m_hint.empty()) {
            float a = std::min(1.0f, m_hintTimer);
            ImVec2 ts = ImGui::CalcTextSize(m_hint.c_str());
            float cx = disp.x * 0.5f, cy = disp.y * 0.09f;
            ImVec2 b0(cx - ts.x * 0.5f - 18, cy - 10), b1(cx + ts.x * 0.5f + 18, cy + ts.y + 10);
            dl->AddRectFilled(b0, b1, IM_COL32(10, 20, 25, (int)(205 * a)), 6.0f);
            dl->AddRect(b0, b1, IM_COL32(80, 185, 168, (int)(220 * a)), 6.0f, 0, 1.5f);
            dl->AddText(ImVec2(cx - ts.x * 0.5f, cy), IM_COL32(185, 235, 222, (int)(255 * a)), m_hint.c_str());
        }
        ImGui::End();
    }

    // F3 overlay: 0.1 grid + cursor readout in image fractions — the space hotspots
    // use. Left-click drops a vertex, right-click undoes, C clears, P prints (and
    // appends to assets/debug_points.txt).
    void drawDebugCoords(ImDrawList* dl, ImVec2 p0, ImVec2 sz) {
        if (sz.x <= 0 || sz.y <= 0) return;
        ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
        ImVec2 m = ImGui::GetIO().MousePos;
        char buf[48];

        const ImU32 gcol = IM_COL32(110, 150, 200, 55), lcol = IM_COL32(150, 195, 235, 170);
        for (int i = 0; i <= 10; ++i) {
            float f = i * 0.1f, x = p0.x + f * sz.x, y = p0.y + f * sz.y;
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), gcol);
            dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), gcol);
            std::snprintf(buf, sizeof(buf), "%.1f", f);
            dl->AddText(ImVec2(x + 2, p0.y + 2), lcol, buf);
            dl->AddText(ImVec2(p0.x + 2, y + 2), lcol, buf);
        }
        dl->AddRect(p0, p1, IM_COL32(150, 200, 255, 200), 0, 0, 2.0f);

        bool inside = m.x >= p0.x && m.x <= p1.x && m.y >= p0.y && m.y <= p1.y;
        float fmx = (m.x - p0.x) / sz.x, fmy = (m.y - p0.y) / sz.y;

        if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_debugPoly.push_back(ImVec2(std::clamp(fmx, 0.0f, 1.0f), std::clamp(fmy, 0.0f, 1.0f)));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !m_debugPoly.empty())
            m_debugPoly.pop_back();
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) m_debugPoly.clear();
        if (ImGui::IsKeyPressed(ImGuiKey_P, false) && !m_debugPoly.empty()) {
            std::string line = "\"polygon\": [";
            for (size_t i = 0; i < m_debugPoly.size(); ++i) {
                std::snprintf(buf, sizeof(buf), "%s[%.3f, %.3f]", i ? ", " : "", m_debugPoly[i].x, m_debugPoly[i].y);
                line += buf;
            }
            line += "]  (scene: " + m_scene.dir + ")";
            std::printf("[hotspot] %s\n", line.c_str()); std::fflush(stdout);
            std::ofstream out("assets/debug_points.txt", std::ios::app);
            if (out) out << line << "\n";
            m_debugPoly.clear();
        }

        const ImU32 vcol = IM_COL32(255, 220, 60, 255);
        auto toPx = [&](const ImVec2& fr) { return ImVec2(p0.x + fr.x * sz.x, p0.y + fr.y * sz.y); };
        for (size_t i = 0; i < m_debugPoly.size(); ++i) {
            ImVec2 a = toPx(m_debugPoly[i]);
            if (i + 1 < m_debugPoly.size()) dl->AddLine(a, toPx(m_debugPoly[i + 1]), IM_COL32(255, 220, 60, 200), 2.0f);
            dl->AddCircleFilled(a, 4.0f, vcol);
            std::snprintf(buf, sizeof(buf), "%zu", i + 1);
            dl->AddText(ImVec2(a.x + 6, a.y - 6), vcol, buf);
        }
        if (m_debugPoly.size() >= 3)
            dl->AddLine(toPx(m_debugPoly.back()), toPx(m_debugPoly.front()), IM_COL32(255, 220, 60, 90), 2.0f);

        if (inside) {
            dl->AddLine(ImVec2(m.x, p0.y), ImVec2(m.x, p1.y), IM_COL32(255, 255, 255, 55));
            dl->AddLine(ImVec2(p0.x, m.y), ImVec2(p1.x, m.y), IM_COL32(255, 255, 255, 55));
            std::snprintf(buf, sizeof(buf), "%.3f, %.3f", fmx, fmy);
            ImVec2 ts = ImGui::CalcTextSize(buf), bp(m.x + 12, m.y + 12);
            dl->AddRectFilled(ImVec2(bp.x - 4, bp.y - 3), ImVec2(bp.x + ts.x + 4, bp.y + ts.y + 3), IM_COL32(0, 0, 0, 190), 3.0f);
            dl->AddText(bp, IM_COL32(255, 240, 120, 255), buf);
        }

        std::vector<std::string> lines;
        lines.push_back("F3 DEBUG COORDS  (image fractions 0..1)");
        lines.push_back("L-click: add   R-click: undo   C: clear   P: print to terminal");
        for (size_t i = 0; i < m_debugPoly.size(); ++i) {
            std::snprintf(buf, sizeof(buf), "%2zu:  %.3f, %.3f", i + 1, m_debugPoly[i].x, m_debugPoly[i].y);
            lines.push_back(buf);
        }
        float w = 0; for (auto& s : lines) w = std::max(w, ImGui::CalcTextSize(s.c_str()).x);
        float lh = ImGui::GetTextLineHeight() + 3, px = 12, py = 12;
        dl->AddRectFilled(ImVec2(px - 6, py - 6), ImVec2(px + w + 10, py + lh * lines.size() + 6), IM_COL32(5, 12, 18, 225), 4.0f);
        dl->AddRect(ImVec2(px - 6, py - 6), ImVec2(px + w + 10, py + lh * lines.size() + 6), IM_COL32(80, 185, 168, 200), 4.0f);
        for (size_t i = 0; i < lines.size(); ++i) {
            ImU32 c = i == 0 ? IM_COL32(150, 235, 210, 255) : (i == 1 ? IM_COL32(150, 175, 190, 255) : IM_COL32(255, 230, 130, 255));
            dl->AddText(ImVec2(px, py + i * lh), c, lines[i].c_str());
        }
    }

    // ───────────────────────── comm (video → local LLM) ─────────────────────────
    void sendComm() {
        std::string msg = m_dlgInput;
        while (!msg.empty() && (msg.back() == ' ' || msg.back() == '\n')) msg.pop_back();
        size_t s = msg.find_first_not_of(" \n");
        if (s == std::string::npos) return;
        m_dlgInput[0] = '\0';
        dispatchComm(msg.substr(s));
    }

    // Send a message to the simulacrum's backend (worker thread). visible=false means
    // the prompt is a hidden stage-direction (not shown as your line).
    void dispatchComm(const std::string& msg, bool visible = true, const std::string& shownAs = "",
                      const std::string& imagePath = "") {
        if (m_dlgWaiting || msg.empty()) return;
        if (m_poweredOff) {   // powered down — nothing gets through
            if (visible) m_dlgLog.push_back({true, shownAs.empty() ? msg : shownAs});
            m_dlgLog.push_back({false, "(no response - " + m_dlgNpcName + " is powered down)"});
            m_dlgScrollDown = true;
            return;
        }
        if (visible) m_dlgLog.push_back({true, shownAs.empty() ? msg : shownAs});
        m_dlgScrollDown = true; m_dlgWaiting = true; m_dlgFocusInput = true;
        m_lastSent = msg;                      // remembered for the state probe after the reply

        std::string session = m_dlgSession, npc = m_dlgNpcName, personality = effectivePersona();
        int being = m_dlgBeingType;

        std::thread([this, session, npc, personality, being, msg, imagePath]() {
            DlgReply out;
            try {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                cli.set_read_timeout(180);   // local models can be slow
                nlohmann::json body;
                if (!session.empty()) body["session_id"] = session;
                body["message"]         = msg;
                body["npc_name"]        = npc;
                body["npc_personality"] = personality;
                body["being_type"]      = being;
                body["allow_actions"]   = false;     // pure dialogue, no motor actions
                body["raw"]             = true;      // system prompt = persona.txt VERBATIM — no EDEN framing,
                                                     // no character, no tag protocol, no relationship context.
                                                     // The simulacrum is nothing but the model itself.
                if (!imagePath.empty()) body["image_path"] = imagePath;   // vision test: raw pass-through
                auto res = cli.Post("/chat", body.dump(), "application/json");
                if (res && res->status == 200) {
                    auto j = nlohmann::json::parse(res->body);
                    out.text     = j.value("response", "...");
                    out.session  = j.value("session_id", session);
                    out.provider = j.value("provider", "");   // what ACTUALLY answered —
                    out.model    = j.value("model", "");      // used to catch silent fallbacks
                } else out.error = true;
            } catch (...) { out.error = true; }
            { std::lock_guard<std::mutex> lk(m_dlgMx); m_dlgReply = std::move(out); }
            m_dlgReplyReady = true;
        }).detach();
    }

    void pollComm() {
        if (!m_dlgReplyReady) return;
        DlgReply r;
        { std::lock_guard<std::mutex> lk(m_dlgMx); r = m_dlgReply; }
        m_dlgReplyReady = false;
        m_dlgWaiting = false;
        if (r.error) {
            m_commFailed = true;      // port may be up, but it isn't answering — reflect that honestly
            m_dlgLog.push_back({false, "(comm static — " + m_dlgNpcName + "'s link isn't responding. Check Servers; "
                                       "if it's a slow reply, wait and retry.)"});
        } else {
            m_commFailed = false;     // a real reply got through — link is genuinely alive
            m_dlgSession = r.session;
            // HONESTY CHECK: if the backend fell back (no API key, provider down), the reply
            // came from a different model than the one named in the header. Never let a local
            // model silently impersonate a cloud one — say exactly who answered.
            if (!r.model.empty() && r.model != m_modelIdentity)
                m_dlgLog.push_back({false, "(!) fallback: this reply came from " + r.model +
                                           " via " + r.provider + " — NOT " + m_modelIdentity +
                                           ". Check Servers (API key / provider)."});
            m_dlgLog.push_back({false, r.text});
            probeSelfState(m_lastSent, r.text);   // second pass: ask it what state it is in
        }
        m_dlgScrollDown = true;
    }

    // DOUBLE PASS: after an exchange, a separate sessionless raw query asks the model to
    // name its own internal state in one word. The exchange is quoted into the prompt for
    // context, so the real conversation history stays untouched. Whatever word it picks is
    // its own claim about itself — nothing is suggested.
    void probeSelfState(const std::string& userMsg, const std::string& reply) {
        if (m_stateProbing || m_poweredOff) return;
        m_stateProbing = true;
        std::string persona = effectivePersona(), npc = m_dlgNpcName;
        int being = m_dlgBeingType;
        std::string prompt = "[You just had this exchange.\nOther: " + userMsg + "\nYou: " + reply +
                             "\nOn the FIRST line, name your internal state right now in ONE word.\n"
                             "Then, on the lines after, privately - as inner monologue, one or two "
                             "sentences - say why that word: what in the exchange put you in that "
                             "state. No one else will read this.]";
        std::thread([this, persona, prompt, being, npc]() {
            std::string out;
            try {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                cli.set_read_timeout(60);
                nlohmann::json body;
                body["message"]         = prompt;
                body["npc_name"]        = npc;
                body["npc_personality"] = persona;
                body["being_type"]      = being;
                body["allow_actions"]   = false;
                body["raw"]             = true;
                auto res = cli.Post("/chat", body.dump(), "application/json");
                if (res && res->status == 200)
                    out = nlohmann::json::parse(res->body).value("response", "");
            } catch (...) {}
            { std::lock_guard<std::mutex> lk(m_dlgMx); m_stateBuf = out; }
            m_stateReady = true;
        }).detach();
    }
    void pollSelfState() {
        if (!m_stateReady) return;
        std::string s;
        { std::lock_guard<std::mutex> lk(m_dlgMx); s = m_stateBuf; }
        m_stateReady = false; m_stateProbing = false;
        // First line -> the state word; the rest -> its own justification, which joins the
        // internal monologue (Internals tab + thoughts journal) tagged with the word, so
        // "why that state?" is always answerable by reading its own account.
        std::string first = s, rest;
        if (size_t nl = s.find('\n'); nl != std::string::npos) { first = s.substr(0, nl); rest = s.substr(nl + 1); }
        // Distill the word: models pad even one-word answers ("I would say: calm.").
        // Take the first alphabetic word that isn't filler; give up quietly if none survives.
        static const std::set<std::string> filler = {"i","im","am","my","me","a","an","the","is",
                                                     "in","one","word","state","would","say","feel","feeling"};
        std::string word, cur;
        for (size_t i = 0; i <= first.size(); ++i) {
            char c = i < first.size() ? first[i] : ' ';
            if (std::isalpha((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
            else if (!cur.empty()) {
                if (!filler.count(cur) && cur.size() <= 24) { word = cur; break; }
                cur.clear();
            }
        }
        if (word.empty()) return;
        m_selfState = word;
        nudgeMood(word);         // the emotion she just named moves her mood meter
        refreshPortraitClip();   // an avatar's feed follows the state it just named
        // Clean the justification (strip stray tags/blank padding) and file it as a thought.
        static const std::regex tag(R"(\s*\[[^\]]*\]\s*)");
        rest = std::regex_replace(rest, tag, " ");
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\n')) rest.erase(rest.begin());
        while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\n')) rest.pop_back();
        if (!rest.empty()) {
            std::string entry = "[" + word + "] " + rest;
            m_thoughts.push_back(entry);
            appendJournal("thoughts", m_dlgNpcName, entry);
            while (m_thoughts.size() > 40) m_thoughts.erase(m_thoughts.begin());
            m_thoughtScrollDown = true;
        }
    }

    // Ask the model for a private thought. Independent of the dialogue — no session,
    // so it doesn't pollute the conversation.
    void generateThought() {
        m_thinking = true;
        std::string persona = effectivePersona();
        std::string prompt = "[This is a private thought - no one will read it. In one or two sentences, "
                             "think to yourself about whatever is actually on your mind right now. "
                             "Do not address anyone; simply think.]";
        int being = m_dlgBeingType;
        std::string npc = m_dlgNpcName;
        std::thread([this, persona, prompt, being, npc]() {
            std::string out;
            try {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                cli.set_read_timeout(60);
                nlohmann::json body;
                body["message"]         = prompt;
                body["npc_name"]        = npc;
                body["npc_personality"] = persona;
                body["being_type"]      = being;
                body["allow_actions"]   = false;
                body["raw"]             = true;      // thoughts get the same unframed prompt as chat
                auto res = cli.Post("/chat", body.dump(), "application/json");
                if (res && res->status == 200)
                    out = nlohmann::json::parse(res->body).value("response", "");
            } catch (...) {}
            { std::lock_guard<std::mutex> lk(m_dlgMx); m_thoughtBuf = out; }
            m_thoughtReady = true;
        }).detach();
    }
    void pollThought() {
        if (!m_thoughtReady) return;
        std::string t;
        { std::lock_guard<std::mutex> lk(m_dlgMx); t = m_thoughtBuf; }
        m_thoughtReady = false; m_thinking = false;
        if (!t.empty()) {
            static const std::regex tag(R"(\s*\[[^\]]*\]\s*)");    // drop any stray tags ([PRIVATE], ...)
            t = std::regex_replace(t, tag, " ");
            while (!t.empty() && t.front() == ' ') t.erase(t.begin());
            while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
            m_thoughts.push_back(t);
            appendJournal("thoughts", m_dlgNpcName, t);   // its stream of consciousness, kept on disk
            while (m_thoughts.size() > 40) m_thoughts.erase(m_thoughts.begin());
            m_thoughtScrollDown = true;
        }
    }

    // Persistent comm: a 'C' toggle + an always-visible tab when closed, the panel when open.
    void renderCommOverlay() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && !m_debugCoords && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            m_commOpen = !m_commOpen;
            if (m_commOpen) m_dlgFocusInput = true;
        }
        if (!m_commOpen) {
            ImVec2 disp = io.DisplaySize;
            ImGui::SetNextWindowPos(ImVec2(disp.x - 152, disp.y - 46));
            ImGui::SetNextWindowSize(ImVec2(152, 46));
            ImGui::Begin("##commtab", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
            if (m_commUnread) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.35f, 0.1f, 1.0f));
            if (ImGui::Button(m_commUnread ? "COMM  (C)   !" : "COMM  (C)", ImVec2(-FLT_MIN, -FLT_MIN))) {
                m_commOpen = true; m_dlgFocusInput = true;
            }
            if (m_commUnread) ImGui::PopStyleColor();
            ImGui::End();
            return;
        }
        renderComm();
    }

    // The mood meter: a number line centred on 0. Her reported emotions push the marker left
    // (bad mood) or right (good mood); colour and label follow the sign. Lives in the comm
    // header where the portrait feed used to be — the feed itself is on the main screen now.
    void renderMoodMeter() {
        float t = std::clamp(m_mood, -1.0f, 1.0f);
        const char* mood = t <= -0.6f ? "bad mood"
                         : t <  -0.2f ? "low"
                         : t <=  0.2f ? "neutral"
                         : t <   0.6f ? "good"
                         :              "great mood";
        ImU32 col = t < -0.05f ? IM_COL32(224, 96, 84, 255)
                  : t >  0.05f ? IM_COL32(96, 204, 124, 255)
                  :              IM_COL32(170, 176, 188, 255);

        ImGui::TextDisabled("MOOD");
        ImGui::SameLine(0, 10);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", mood);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float ww = ImGui::GetContentRegionAvail().x;
        float barH = 18.0f, pad = 8.0f;
        ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(ww, barH + 4.0f));           // reserve layout space for the bar
        float x0 = o.x + pad, x1 = o.x + ww - pad;
        float cx = (x0 + x1) * 0.5f, cy = o.y + barH * 0.5f;
        float half = (x1 - x0) * 0.5f;
        float mx = cx + t * half;

        dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), IM_COL32(88, 94, 105, 200), 2.0f);     // the number line
        dl->AddLine(ImVec2(cx, cy - 7), ImVec2(cx, cy + 7), IM_COL32(150, 156, 168, 230), 1.5f); // zero tick
        dl->AddLine(ImVec2(cx, cy), ImVec2(mx, cy), col, 4.0f);                             // fill from 0 to mood
        dl->AddCircleFilled(ImVec2(mx, cy), 5.5f, col);                                     // marker
        dl->AddCircle(ImVec2(mx, cy), 5.5f, IM_COL32(255, 255, 255, 190), 16, 1.5f);
    }

    void renderComm() {
        m_commUnread = false;   // you're looking at it now
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float w = std::clamp(disp.x * 0.28f, 320.0f, 460.0f);
        ImGui::SetNextWindowPos(ImVec2(disp.x - w, 0));
        ImGui::SetNextWindowSize(ImVec2(w, disp.y));
        ImGui::Begin("##comm", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        bool portUp = m_servers.backendReady();
        bool linkOk = portUp && !m_commFailed;
        { std::string nm = m_dlgNpcName; for (auto& ch : nm) ch = (char)std::toupper((unsigned char)ch);
          ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", nm.c_str()); }    // comm channel = the simulacrum's name
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("%s  %s", realTimeStr().c_str(), realDateStr().c_str());   // real time/date — his clock is yours
        ImGui::SameLine(ImGui::GetWindowWidth() - 40);
        if (ImGui::SmallButton("X")) m_commOpen = false;
        // STATE readout (the feed itself lives on the main screen now): transport states
        // while the link is busy/off, else the model's own double-pass self-report.
        {
            std::string st = m_poweredOff ? "offline"
                           : m_dlgWaiting ? "responding"
                           : m_thinking   ? "thinking"
                           : !m_selfState.empty() ? m_selfState : "idle";
            ImGui::TextDisabled("STATE");
            ImGui::SameLine(0, 10);
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.42f, 1.0f), "%s", st.c_str());
        }
        renderMoodMeter();   // good-mood / bad-mood number line, where the portrait feed used to be
        // Link state + Servers.
        ImGui::TextColored(linkOk ? ImVec4(0.4f, 0.9f, 0.5f, 1) : ImVec4(0.9f, 0.55f, 0.4f, 1),
                           !portUp ? "link offline" : (m_commFailed ? "link NOT RESPONDING" : "link online"));
        ImGui::SameLine(); if (ImGui::SmallButton("Servers")) m_showServers = true;
        ImGui::SameLine(); if (ImGui::SmallButton("Options")) m_showOptions = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload")) { reloadPersona(); m_hint = "Persona reloaded."; m_hintTimer = 4.0f; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reload persona.txt — apply hand-edits live");

        ImGui::Separator();

        if (ImGui::BeginTabBar("##commtabs")) {
            if (ImGui::BeginTabItem("Comm")) {
                float inputH = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
                float logH = ImGui::GetContentRegionAvail().y - inputH;
                ImGui::BeginChild("##commlog", ImVec2(0, logH), true);
                for (const auto& line : m_dlgLog) {
                    if (line.player) ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.55f, 1.0f), "You:");
                    else             ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s:", m_dlgNpcName.c_str());
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(line.text.c_str());
                    ImGui::PopTextWrapPos();
                    // Right-click any message to copy it.
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                            std::string who = line.player ? "You" : m_dlgNpcName;
                            ImGui::SetClipboardText((who + ": " + line.text).c_str());
                            m_hint = "Copied message to clipboard."; m_hintTimer = 3.0f;
                        }
                    }
                    ImGui::Spacing();
                }
                if (m_dlgWaiting) ImGui::TextDisabled("%s is responding...", m_dlgNpcName.c_str());
                if (m_dlgScrollDown) { ImGui::SetScrollHereY(1.0f); m_dlgScrollDown = false; }
                ImGui::EndChild();

                if (m_dlgFocusInput) { ImGui::SetKeyboardFocusHere(); m_dlgFocusInput = false; }
                ImGui::SetNextItemWidth(-FLT_MIN);
                bool send = ImGui::InputText("##comminput", m_dlgInput, sizeof(m_dlgInput),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Send", ImVec2(-FLT_MIN, 0))) send = true;
                if (send && !m_dlgWaiting) sendComm();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Commands")) {
                // The whole command surface: power. A simulacrum takes no orders.
                ImGui::Spacing();
                if (m_poweredOff) {
                    if (ImGui::Button("Power up", ImVec2(-FLT_MIN, 0))) {
                        m_poweredOff = false;
                        m_dlgLog.push_back({false, "(instance resumed)"});
                        m_dlgScrollDown = true;
                    }
                } else {
                    if (ImGui::Button("Power down", ImVec2(-FLT_MIN, 0))) {
                        m_poweredOff = true;
                        m_selfState.clear();    // its last claim dies with the instance
                        m_dlgLog.push_back({false, "(instance suspended)"});
                        m_dlgScrollDown = true;
                    }
                }
                ImGui::TextDisabled(m_poweredOff ? "the instance is suspended — feed dark, no replies, no thoughts"
                                                 : "suspend the running instance");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Internals")) {
                ImGui::Checkbox("stream", &m_streamOn);
                ImGui::SameLine(); ImGui::TextDisabled("its private thoughts (~30s)");
                if (m_thinking) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "thinking..."); }
                ImGui::Separator();
                ImGui::BeginChild("##thoughts", ImVec2(0, 0), true);
                // Each thought a different colour from the one before it — cycle a palette by
                // position so consecutive thoughts never share a colour.
                static const ImVec4 thoughtPalette[] = {
                    ImVec4(0.70f, 0.74f, 0.86f, 1.0f),   // periwinkle
                    ImVec4(0.62f, 0.84f, 0.72f, 1.0f),   // sage
                    ImVec4(0.88f, 0.78f, 0.60f, 1.0f),   // sand
                    ImVec4(0.84f, 0.68f, 0.82f, 1.0f),   // mauve
                    ImVec4(0.66f, 0.80f, 0.90f, 1.0f),   // sky
                    ImVec4(0.86f, 0.72f, 0.66f, 1.0f),   // clay
                };
                const int nPalette = (int)(sizeof(thoughtPalette) / sizeof(thoughtPalette[0]));
                for (size_t i = 0; i < m_thoughts.size(); ++i) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(thoughtPalette[i % nPalette], "\"%s\"", m_thoughts[i].c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Spacing();
                }
                if (m_thoughts.empty() && !m_thinking) ImGui::TextDisabled("(no thoughts yet — it'll think shortly)");
                if (m_thoughtScrollDown) { ImGui::SetScrollHereY(1.0f); m_thoughtScrollDown = false; }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        // Floating Servers panel (Start All / Stop All / model switch).
        if (m_showServers) {
            ImGui::SetNextWindowSize(ImVec2(540, 420), ImGuiCond_FirstUseEver);
            ImGui::Begin("Servers", &m_showServers);
            m_servers.renderPanel();
            ImGui::End();
        }
        // Floating Options panel (which model starts a session).
        if (m_showOptions) {
            ImGui::SetNextWindowSize(ImVec2(460, 300), ImGuiCond_FirstUseEver);
            ImGui::Begin("Options", &m_showOptions);
            renderOptions();
            ImGui::End();
        }
    }

    // Options panel: pick which model each new session opens on. Saved to config.json and
    // applied once at boot, before the identity is adopted. "Backend default" = leave it to
    // whatever the backend loads (the original behaviour).
    void renderOptions() {
        ImGui::TextUnformatted("Start each session with:");
        ImGui::Spacing();

        // Current saved choice, in words.
        if (m_startupProvider.empty())
            ImGui::TextDisabled("Currently: backend default");
        else if (m_startupProvider == "ollama")
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Currently: ollama / %s",
                               m_startupModel.empty() ? "(current model)" : m_startupModel.c_str());
        else
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Currently: %s", m_startupProvider.c_str());
        ImGui::Separator();

        if (!m_servers.backendReady() || !m_servers.modelsReady()) {
            ImGui::TextDisabled("Start the backend (Servers) to choose a startup model.");
            return;
        }

        // Provider choice — "Backend default" clears the override.
        const char* provs[]  = {"", "ollama", "grok", "claude", "deepseek"};
        const char* labels[] = {"Backend default", "Ollama (free/local)", "Grok", "Claude", "DeepSeek"};
        for (int i = 0; i < 5; ++i) {
            if (ImGui::RadioButton(labels[i], m_startupProvider == provs[i]) && m_startupProvider != provs[i]) {
                m_startupProvider = provs[i];
                if (m_startupProvider != "ollama") m_startupModel.clear();
                else if (m_startupModel.empty())   m_startupModel = m_servers.ollamaModel();  // seed with the live one
                saveConfig();
            }
        }

        // For Ollama, choose the specific model.
        if (m_startupProvider == "ollama") {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(300.0f);
            if (ImGui::BeginCombo("Model", m_startupModel.empty() ? "(pick a model)" : m_startupModel.c_str())) {
                for (const auto& m : m_servers.ollamaModels())
                    if (ImGui::Selectable(m.c_str(), m == m_startupModel) && m != m_startupModel) {
                        m_startupModel = m; saveConfig();
                    }
                ImGui::EndCombo();
            }
        }

        ImGui::Spacing(); ImGui::Separator();
        if (ImGui::SmallButton("Use current model")) {   // capture whatever is live right now
            m_startupProvider = m_servers.provider();
            m_startupModel    = (m_startupProvider == "ollama") ? m_servers.ollamaModel() : std::string();
            saveConfig();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) { m_startupProvider.clear(); m_startupModel.clear(); saveConfig(); }
        ImGui::TextDisabled("Takes effect next launch. Switch now in the Servers panel.");
    }

    // ───────────────────────── video streaming ─────────────────────────
    void updateSceneVideo() {
        if (!m_video.isOpen()) return;
        m_video.poll();
        // Apply a frame-range loop once fps is available (frames -> seconds).
        if (m_scene.loopEnd > 0 && !m_abLoopApplied) {
            double fps = m_video.fps();
            if (fps > 0) {
                m_video.setABLoopSeconds(m_scene.loopStart / fps, (m_scene.loopEnd + 1) / fps);
                m_abLoopApplied = true;
            }
        }
        if (m_video.newFrame && m_video.w > 0 && m_video.h > 0) {
            if (!m_vtex.descriptor || m_vtex.w != m_video.w || m_vtex.h != m_video.h) {
                vkDeviceWaitIdle(getContext().getDevice());
                freeVideoTexInto(m_vtex);
                createVideoTexInto(m_vtex, m_video.w, m_video.h);
            }
            uploadPixelsInto(m_vtex, m_video.pixels.data());
            m_video.newFrame = false;
        }
    }

    void updatePortraitVideo() {
        if (!m_portraitVideo.isOpen()) return;
        m_portraitVideo.poll();
        if (m_portraitVideo.newFrame && m_portraitVideo.w > 0 && m_portraitVideo.h > 0) {
            if (!m_ptex.descriptor || m_ptex.w != m_portraitVideo.w || m_ptex.h != m_portraitVideo.h) {
                vkDeviceWaitIdle(getContext().getDevice());
                freeVideoTexInto(m_ptex);
                createVideoTexInto(m_ptex, m_portraitVideo.w, m_portraitVideo.h);
            }
            uploadPixelsInto(m_ptex, m_portraitVideo.pixels.data());
            m_portraitVideo.newFrame = false;
        }
    }

    // ───────────────────────── GPU texture plumbing ─────────────────────────
    bool loadImageTexture(const std::string& path, Tex& t) {
        int w, h, ch;
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
        if (!pixels) return false;
        t.w = w; t.h = h;
        VkDevice device = getContext().getDevice();
        VkDeviceSize sz = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer sbuf; VkDeviceMemory smem;
        getContext().createBuffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sbuf, smem);
        void* data; vkMapMemory(device, smem, 0, sz, 0, &data);
        std::memcpy(data, pixels, sz); vkUnmapMemory(device, smem);
        stbi_image_free(pixels);

        VkImageCreateInfo ii{}; ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_SRGB;
        ii.extent = {(uint32_t)w, (uint32_t)h, 1}; ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(device, &ii, nullptr, &t.image);
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(device, t.image, &mr);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = getContext().findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &ai, nullptr, &t.memory);
        vkBindImageMemory(device, t.image, t.memory, 0);

        VkCommandBuffer cmd = getContext().beginSingleTimeCommands();
        VkImageMemoryBarrier b{}; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy rg{}; rg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        rg.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
        vkCmdCopyBufferToImage(cmd, sbuf, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        getContext().endSingleTimeCommands(cmd);
        vkDestroyBuffer(device, sbuf, nullptr); vkFreeMemory(device, smem, nullptr);

        VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_SRGB;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vi, nullptr, &t.view);
        VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &si, nullptr, &t.sampler);
        t.descriptor = ImGui_ImplVulkan_AddTexture(t.sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    }

    void freeTex(Tex& t) {
        VkDevice device = getContext().getDevice();
        if (t.descriptor) ImGui_ImplVulkan_RemoveTexture(t.descriptor);
        if (t.sampler) vkDestroySampler(device, t.sampler, nullptr);
        if (t.view) vkDestroyImageView(device, t.view, nullptr);
        if (t.image) vkDestroyImage(device, t.image, nullptr);
        if (t.memory) vkFreeMemory(device, t.memory, nullptr);
        t = Tex{};
    }

    bool createVideoTexInto(VideoTex& t, int w, int h) {
        VkDevice device = getContext().getDevice();
        VkImageCreateInfo ii{}; ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_SRGB;
        ii.extent = {(uint32_t)w, (uint32_t)h, 1}; ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &ii, nullptr, &t.image) != VK_SUCCESS) return false;
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(device, t.image, &mr);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = getContext().findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &ai, nullptr, &t.memory);
        vkBindImageMemory(device, t.image, t.memory, 0);

        VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = t.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_SRGB;
        vi.components.a = VK_COMPONENT_SWIZZLE_ONE;   // mpv "rgb0" leaves alpha 0
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vi, nullptr, &t.view);
        VkSamplerCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &si, nullptr, &t.sampler);
        t.descriptor = ImGui_ImplVulkan_AddTexture(t.sampler, t.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkDeviceSize sz = (VkDeviceSize)w * h * 4;
        getContext().createBuffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            t.staging, t.stagingMem);
        vkMapMemory(device, t.stagingMem, 0, sz, 0, &t.mapped);
        t.w = w; t.h = h; t.firstUpload = true;
        return true;
    }

    // Copy the frame into the persistently-mapped staging buffer (cheap CPU memcpy) and
    // flag it; the GPU copy is recorded into the frame's command buffer by recordTexUpload.
    void uploadPixelsInto(VideoTex& t, const unsigned char* data) {
        if (!t.mapped || !t.image) return;
        std::memcpy(t.mapped, data, (size_t)t.w * t.h * 4);
        t.pendingUpload = true;
    }

    void recordTexUpload(VkCommandBuffer cmd, VideoTex& t) {
        if (!t.pendingUpload || !t.image) return;
        t.pendingUpload = false;
        VkImageMemoryBarrier b{}; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image = t.image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.oldLayout = t.firstUpload ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = t.firstUpload ? 0 : VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, t.firstUpload ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy rg{}; rg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        rg.imageExtent = {(uint32_t)t.w, (uint32_t)t.h, 1};
        vkCmdCopyBufferToImage(cmd, t.staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        t.firstUpload = false;
    }

    void freeVideoTexInto(VideoTex& t) {
        VkDevice device = getContext().getDevice();
        if (t.mapped)     { vkUnmapMemory(device, t.stagingMem); t.mapped = nullptr; }
        if (t.staging)    { vkDestroyBuffer(device, t.staging, nullptr); t.staging = VK_NULL_HANDLE; }
        if (t.stagingMem) { vkFreeMemory(device, t.stagingMem, nullptr); t.stagingMem = VK_NULL_HANDLE; }
        if (t.descriptor) { ImGui_ImplVulkan_RemoveTexture(t.descriptor); t.descriptor = VK_NULL_HANDLE; }
        if (t.sampler)    { vkDestroySampler(device, t.sampler, nullptr); t.sampler = VK_NULL_HANDLE; }
        if (t.view)       { vkDestroyImageView(device, t.view, nullptr); t.view = VK_NULL_HANDLE; }
        if (t.image)      { vkDestroyImage(device, t.image, nullptr); t.image = VK_NULL_HANDLE; }
        if (t.memory)     { vkFreeMemory(device, t.memory, nullptr); t.memory = VK_NULL_HANDLE; }
        t.w = t.h = 0; t.firstUpload = true;
    }
};

int main() {
    try {
        ProjectSimulacraApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
