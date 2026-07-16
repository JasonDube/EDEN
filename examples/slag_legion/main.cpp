// Slag Legion — a sci-fi tabletop RPG on the EDEN engine, sibling to the fantasy
// "tabletop" (Chronicles of the Iron Temple). Shares the same tabletop base; its
// own races/classes/items and a crafting system are TBD.
//
// This build: a title screen, then the ship's Bridge — an interactive SCENE. A
// scene is a looping video (falling back to a still poster) with clickable
// hotspots defined as fractions of the image, so they stay put at any window
// size. The first hotspot (the computer terminal) opens a placeholder screen.
//
// Author interactive scenes under assets/scenes/<name>/:
//     scene.mp4   the animated video          (optional; poster shown until present)
//     poster.png  still fallback
//     scene.json  { video, poster, hotspots:[ {id,label,rect{x,y,w,h},action} ] }

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ImGuiManager.hpp"

#include <eden/Input.hpp>
#include <eden/Window.hpp>
#include <eden/Audio.hpp>

#include "VideoPlayer.hpp"
#include "FlightMode.hpp"       // 3D space-flight rendered into the central pane (Tab)

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>        // glfwSetInputMode: hide the cursor in 3D flight

#include <nlohmann/json.hpp>
#include <httplib.h>            // AI dialogue backend (ai_companion, localhost:8080)
#include "GameServers.hpp"      // in-game start/stop of the dialogue backend + Ollama
#include "Geography.hpp"        // galaxy map: 1000x1000 sectors -> super-sectors -> named regions
#include "Rarity.hpp"           // scan-rarity scoring (relative to local baseline) for Clara's commentary
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <map>
#include <fstream>
#include <atomic>
#include <cctype>
#include <iostream>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace eden;

class SlagLegionApp : public VulkanApplicationBase {
public:
    SlagLegionApp() : VulkanApplicationBase(1280, 720, "Slag Legion") {}

protected:
    enum class Screen { Title, Bridge, Console, Email, EmailRead, Radiators };
    Screen m_screen = Screen::Title;
    float  m_titlePulse = 0.0f;

    // Galaxy Map (M) — pannable/zoomable 2D view of the 1000x1000 sector grid.
    bool   m_galaxyMapOpen = false;
    bool   m_mapInit       = false;    // fit-to-view computed on first open
    float  m_mapZoom       = 0.8f;     // pixels per sector
    ImVec2 m_mapOrigin{0, 0};          // screen position of sector (0,0)
    int    m_sectorX = 500, m_sectorY = 500;   // the ship's current sector (starts in Alpha Core)
    int    m_mapSelX = -1,  m_mapSelY = -1;    // selected destination sector (-1 = none)

    // FTL travel — engaging the hyperdrive does NOT teleport: the trip runs on the game clock at
    // one game-DAY per sector of distance (1 day = 1440 game-min = 24 real min). Lived through.
    bool   m_ftlActive   = false;
    int    m_ftlFromX = 0, m_ftlFromY = 0;     // origin sector (for the map trail)
    int    m_ftlToX = 0,   m_ftlToY = 0;       // destination sector
    float  m_ftlTotalMin = 0.0f;               // total journey length, in game-minutes
    float  m_ftlElapsedMin = 0.0f;             // game-minutes travelled so far
    static constexpr float kMinPerSector = 1440.0f;   // one game-day per sector

    // Player/ship state — placeholders until the economy + save system exist.
    long m_dollars = 250000;                        // credit balance (top-right of the console)
    // Ship daily operating costs ("burn"), loaded from assets/ship_economy.json — all tunable.
    struct EconLine { std::string id, label, warn; long cost = 0; bool active = true; };
    std::vector<EconLine> m_econDaily;
    long m_lastDailyBurn = 0;                        // total charged on the most recent day (for display)
    int  m_year = 2147, m_month = 3, m_day = 17;    // in-game date (the commissioning day)
    float m_timeOfDay = 8.0f * 60.0f;               // in-game minutes since midnight (08:00 start); 1 real sec = 1 game min
    bool  m_adaCharging = false;                     // she recharges in the bay 22:00-07:00
    float m_adaBattery = 50.0f;                       // 0-100; drains slowly awake, tops up overnight
    // Clara's science-lab charging animation (room-baked, phased): intro 0..CH_B -> loop
    // CH_A..CH_B -> finish CH_B..end -> default. Only shown when the Captain is in the lab.
    static constexpr long CH_LOOP_A = 160;            // charging loop start
    static constexpr long CH_LOOP_B = 215;            // charging loop end / disconnection (finish) start
    enum class Charge { None, Active, Finish };
    Charge m_chargePhase = Charge::None;
    bool   m_commandCharge = false;                  // this charge was ordered (fills to 100 then completes), vs the nightly 22:00-07:00 cycle
    int    m_chargeAudioId = -1;                      // engine audio loop mirroring the charging clip; -1 = none
    // Station assignments (independent toggles): which of your posts she's actively manning.
    // Off by default; auto-engage all three only when the Captain tabs into a 3D screen
    // (flight/sector). Any other command she's given (Come Here, charge, repairs...) drops all three.
    bool   m_stationComm    = false;
    bool   m_stationSensors = false;
    bool   m_stationCombat  = false;
    void   clearStations() { m_stationComm = m_stationSensors = m_stationCombat = false; }
    void   engageStations() { m_stationComm = m_stationSensors = m_stationCombat = true; }

    // Inbox. Emails arrive on a timer (welcome at t=0, others later).
    struct GameEmail {
        std::string sender, subject, date, body;
        std::string courseSender;   // "Set Course > Sender": who wrote it (visit/trade/reply)
        std::string courseObjective;// "Set Course > Objective": who/what it's about (the job)
        bool  read = false;
        float arriveAt = 0.0f;   // game-seconds after boarding when it lands
        bool  arrived = false;
    };
    std::vector<GameEmail> m_emails;
    float m_gameTime = 0.0f;                         // seconds since coming aboard
    int   m_openEmail = -1;                          // email being read (index into m_emails)
    std::string m_course, m_courseRole;              // current nav target: name + which role
    std::string m_hint; float m_hintTimer = 0.0f;    // transient bridge notification

    int unreadCount() const {
        int n = 0; for (const auto& e : m_emails) if (e.arrived && !e.read) ++n; return n;
    }
    int arrivedCount() const {
        int n = 0; for (const auto& e : m_emails) if (e.arrived) ++n; return n;
    }
    // Point the ship at a place an email references. (Nav/map system comes later;
    // for now it records the course and flashes a confirmation.)
    void setCourse(const std::string& name, const std::string& role) {
        if (name.empty()) return;
        m_course = name; m_courseRole = role;
        m_hint = "Course set  >  " + role + ": " + name;
        m_hintTimer = 6.0f;
        playSfx("assets/audio/target_acquired.wav");
    }

    static std::string commas(long v) {
        std::string s = std::to_string(v < 0 ? -v : v);
        for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ",");
        return (v < 0 ? "-$" : "$") + s;
    }
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
    void advanceDay() {
        static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (++m_day > dim[std::clamp(m_month, 1, 12) - 1]) { m_day = 1; if (++m_month > 12) { m_month = 1; ++m_year; } }
        // Ship's daily operating burn — charged each new day (can go negative = debt).
        m_lastDailyBurn = dailyBurnTotal();
        m_dollars -= m_lastDailyBurn;
        advanceFocusProblem();   // a new day -> the next problem in her rotation becomes today's focus
    }
    // The archive line is bound to m_archiveAccess (the flag the scan panel already reads); every other
    // line follows its own checkbox. Unchecked = not billed and not received.
    bool lineActive(const EconLine& e) const {
        return e.id == "archive_subscription" ? m_archiveAccess : e.active;
    }
    long dailyBurnTotal() const {
        long t = 0;
        for (const auto& e : m_econDaily) if (lineActive(e)) t += e.cost;
        return t;
    }
    // Aether Dynamics cuts off archive access for a rule-break (warranty void / doctrine violation).
    // Unlike a voluntary cancel, this is permanent — per canon, a voided warranty means you can't re-buy it.
    void revokeArchiveAccess() { m_archiveAccess = false; m_archiveRevoked = true; }
    // Load the ship's daily operating costs from assets/ship_economy.json (tunable; no rebuild).
    void loadShipEconomy() {
        m_econDaily.clear();
        try {
            std::ifstream f("assets/ship_economy.json");
            if (f) {
                nlohmann::json j; f >> j;
                if (j.contains("daily"))
                    for (auto it = j["daily"].begin(); it != j["daily"].end(); ++it) {
                        EconLine e; e.id = it.key();
                        e.label = it.value().value("label", it.key());
                        e.warn  = it.value().value("warn", std::string());
                        e.cost  = it.value().value("cost", (long)0);
                        m_econDaily.push_back(std::move(e));
                    }
            }
        } catch (...) {}
    }

    // A clickable region, expressed as fractions of the scene image (0..1).
    // A hotspot is either an axis-aligned rect (x,y,w,h) or a polygon (poly),
    // both in image fractions (0..1). Polygon wins if present.
    struct Hotspot { std::string id, label, action; float x = 0, y = 0, w = 0, h = 0; std::vector<ImVec2> poly; };
    // Ray-cast point-in-polygon; pt and poly are in the same (fraction) space.
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
        std::string character;               // optional: a character (assets/characters/<id>/) composited into the room
        std::vector<Hotspot> hotspots;
        long loopStart = -1, loopEnd = -1;   // optional: loop only this frame range
    };
    Scene m_scene;
    std::string m_pendingScene;              // scene switch deferred to next frame (avoid mid-frame GPU churn)
    bool m_dropClaraView = false;            // deferred: drop Clara's full-frame video + texture (she left the room) at update-top
    std::string m_prevSceneDir;              // last room, for the Backspace testing-return
    bool m_radiatorsDeployed = true;         // QSC-9 radiator fins overall state (true unless fully stowed)
    bool m_gpsEnabled = false;               // comm GPS locator (feeds Ada the Captain's position) — disabled for now
    // Radiator control view: a small state machine over radiator_fins.mp4.
    enum class RadState { Deployed, Stowed, AuxOpen };   // rest frames: 160 / 100 / 200 (all static holds)
    RadState m_radState = RadState::Deployed;
    long     m_radEndFrame = -1;             // transition target frame (-1 = not moving)
    long     m_radRestFrame = 160;           // frame to hold on once the transition reaches its end
    bool m_abLoopApplied = false;            // frame-range loop set once fps is known
    bool m_debugCoords = false;              // F3: coordinate overlay for authoring hotspots
    float m_timeScale = 1.0f;                // F3: debug clock multiplier (1x normal; crank up to test FTL/schedule)
    std::vector<ImVec2> m_debugPoly;         // collected polygon vertices, in image fractions (0..1)
    std::set<std::string> m_togglesOn;       // which "toggle:" hotspots are currently ON (reset per scene)

    // ── Ada: an always-on companion reachable over ship comm from any room ──
    struct DlgLine  { bool player; std::string text; std::string interaction; std::string species; };
    struct DlgReply { std::string text, emotion, interaction, session; int relDelta = 0; bool error = false; };
    std::vector<DlgLine> m_dlgLog;            // comm transcript (persistent, session-only)
    char        m_dlgInput[512] = {};
    std::string m_dlgNpcId = "ada", m_dlgNpcName = "Ada", m_dlgSession, m_dlgEmotion = "neutral", m_dlgFgPath;
    std::string m_dlgLastInteraction;          // what she judged the Captain last did (chat/admire/joke/...)
    std::string m_stateCause;                  // WHY she's in her current demeanor when the Captain didn't cause it (e.g. "battle prep")
    std::string m_forcedInteraction;           // a system event (e.g. a kill) forces the next stage-direction reply's reaction into this interaction's set
    std::map<std::string, float> m_interactionDisp;  // per-interaction disposition delta (assets/interactions.json)
    // Unified causative schema (interactions.json): every player action that acts on her.
    struct Causative { std::string id, tier, label, verb; float delta = 0.0f; bool button = false; };
    std::vector<Causative>   m_causatives;           // in file order
    std::vector<std::string> m_causTierOrder;        // tier display/sort order
    std::map<std::string,int> m_causTierThresh;      // tier -> min disposition to unlock
    float m_dispAccum = 0.0f;                   // fractional disposition accumulator (rolls whole units into disposition)
    std::vector<std::string> m_dlgEmotions;   // emotions this character has clips for (spec "emotions"); empty -> backend default set
    nlohmann::json m_dlgEmotionAliases;        // spec "emotion_aliases": {model_word: clip_emotion} — resolves the model's slip words to a real clip
    std::map<std::string, std::vector<std::string>> m_reactions;  // spec "reactions": interaction -> plausible emotions (hybrid snap)
    std::map<std::string, std::string> m_dominantReaction;        // spec "dominant_reactions": interaction -> its dominant reaction (e.g. appreciate -> warmed)
    int m_kissThreshold = 70;                                     // disposition at/above which a kiss is accepted (+2); below is rejected (-2, annoyed)
    int m_kissLoopId = -1;                                        // engine audio loop for the kiss clip's tail (mirrors the video's 150..end loop); -1 = none
    bool m_dlgAudio = false;                   // play this character's clip audio (spec "audio"); false -> foreground clips muted
    bool m_dlgComposite = true;                // true = green-screen clip composited over the room; false = full-frame room-baked clip (Clara)
    long m_vidSkip = 0;                         // frames to skip at the start of a full-frame clip (intro fade); applied once decoded
    bool m_clipLoop = true;                     // clip-control bar: loop vs play-once
    long m_clipSkip = 0;                        // the current clip's intro-skip frame (to re-arm the loop from)
    long m_clipLoopFrom = -1;                   // -1 = loop whole clip; >=0 = play through once from frame 0, then loop [this..end] (e.g. kiss=150)
    long m_clipLoopTo   = -1;                    // ab-loop END frame for m_clipLoopFrom (-1 = clip end); e.g. charging loops [134..217]
    long m_clipStart    = -1;                    // deferred seek to this frame after arming the loop (-1 = play from frame 0); charging loop-entry = 134
    long m_chFinishSeek = -1;                    // deferred seek for the charging FINISH clip (no loop; plays to EOF then hands to default)
    std::string m_dlgModel;                    // model designation (e.g. "EVA-7"), from spec identity
    // Personality matrix (0..100) — the product's traits; these DERIVE the mechanics below.
    float m_traitNeediness = 50.0f;            // how strongly she wants contact -> longing fill rate + when she calls
    float m_traitIntimacy  = 0.0f;             // how sensual/physical she is -> disposition gate for intimate clips (not yet active)
    float m_intimateUnlockDisp = 99.0f;        // derived: disposition at which intimate clips unlock (for later gating)
    std::string m_adaRoom = "science_lab";   // the room Ada is currently in (starts in the bay)
    std::string m_adaOverride;                    // manufacturer safety override: forces a clip (e.g. "diagnostics_1") over any mood; "" = follow mood
    bool m_serversReady = false;                   // last-known backend state, to power Ada up/down on change
    bool m_commFailed = false;                      // last comm attempt timed out/errored (port up but not answering)
    bool m_adaPoweredOff = false;                   // gameplay power state (Power Off/On commands) — independent of the real servers
    bool m_adaWasOff = false;                        // for detecting the power up/down transition
    long m_powEnd = -1;                              // power-clip transition target frame (-1 = none)
    long m_powRest = 130;                            // frame to hold on after a power-down
    bool m_powResume = false;                        // after the transition, resume mood (power-up) vs hold (power-down)
    long m_fgSeekHold = -1;                           // deferred seek-and-hold for the fg clip (applied once it's loaded)
    long m_fgLoopFrom = -1, m_fgLoopTo = -1;          // deferred loop-range for the fg clip (applied once it's loaded)
    float m_longing = 0.0f;                          // 0..100 "wanting" — fills while apart, eases when together; PERSISTED
    // Timing is in HOURS. In-session the game clock runs 60x (1 real sec = 1 game min),
    // so these are IN-WORLD hours: 4h apart -> full happens over ~4 real minutes of play.
    // Between sessions, m_longingPerHour fills per REAL hour away (the "travel time" model).
    float m_lonelyFillHours  = 4.0f;                 // in-world hours apart & quiet to fill 0->100
    float m_lonelyEmptyHours = 1.0f;                 // in-world hours physically together (her room) to empty 100->0
    float m_lonelyTalkHours  = 2.0f;                 // in-world hours of active talking to empty (plus per-exchange drops)
    float m_longingPerHour = 25.0f;                  // BETWEEN sessions: fill per REAL hour away (4 real hrs -> full)
    float m_longingPerExchange = 8.0f;               // discrete drop each time a back-and-forth completes (her reply lands)
    float m_talkWindow = 45.0f;                      // seconds after an exchange that still counts as "talking"

    // Escalation ladder: bar fills -> she CALLS over comm -> no answer -> she SEARCHES the ship.
    enum class Seek { Idle, Called, Searching };
    Seek        m_seek = Seek::Idle;
    float       m_seekTimer = 0.0f;                  // seconds elapsed in the current seek phase
    float       m_seekCooldown = 0.0f;               // after an episode resolves, don't immediately re-trigger
    float       m_searchStep = 0.0f;                 // seconds since her last room-to-room step
    int         m_searchIdx = 0;                     // position in the search route
    unsigned    m_seekMark = 0;                      // m_playerMsgCount snapshot at call time (to detect an answer)
    std::vector<std::string> m_searchRoute;          // rooms left to check
    std::string m_seekReturnRoom;                    // where she resumes if the search comes up empty
    float       m_seekCallAt = 75.0f;                // longing level that makes her reach out (tunable)
    float       m_seekWaitSecs = 90.0f;              // no-answer window before she leaves to search (tunable)
    float       m_searchStepSecs = 7.0f;             // seconds per room while searching (tunable)
    unsigned    m_playerMsgCount = 0;                // count of messages the Captain has actually sent
    float m_saveTimer = 5.0f;                         // periodic autosave countdown (robust to non-graceful exits)
    bool  m_yearnActive = false;                       // whether high longing is currently coloring her rest into yearning
    float m_hurt = 0.0f;                               // 0..100 red "hurt/rejection" meter — rises on rebuff, eases on repair; PERSISTED
    float m_silenceTimer = 1.0e6f;                     // seconds since the last CAPTAIN exchange; starts "long ago" so a fresh load isn't treated as if you just talked
    bool  m_exchangeSoothing = false;                  // is the in-flight exchange Captain-initiated? (only those empty the loneliness bar)
    float m_initiateCooldown = 0.0f;                   // so she doesn't reach out too often
    bool  m_commUnread = false;                        // she said something unprompted that you haven't seen

    // Internals: her stream of consciousness (experimental — a private thought every ~15s).
    std::vector<std::string> m_thoughts;
    float m_thinkTimer = 8.0f;                          // first thought a few seconds after boot
    bool  m_streamOn = false;                           // STUBBED for now (saves inference); re-enable to restore her idle thoughts
    bool  m_thinking = false;                           // a thought is in flight
    std::atomic<bool> m_thoughtReady{false};
    std::string m_thoughtBuf;                            // guarded by m_dlgMx
    bool  m_thoughtScrollDown = false;

    // Permissions ledger — non-material freedoms the Captain grants Ada. She raises a
    // wish for one herself in conversation; granting reshapes her persona.
    struct Permission { std::string id, title, playerLabel, personaGranted, personaAvailable; };
    std::vector<Permission> m_perms;              // definitions (permissions.json)
    std::set<std::string>   m_permsGranted;       // the ledger (session-only for now)
    std::string             m_pendingWish;        // a freedom Ada is currently asking for ("" = none)

    // Ship problems she's genuinely working through (so she's occupied, not idle).
    struct Problem { std::string id, title, brief, stakes, room; };  // room = where this problem's work happens
    std::vector<Problem> m_problems;              // pool (problems.json)
    // Clara's daily routine (schedule.json) — the default life she falls into when not commanded / not at a station.
    struct SchedBlock { int start = 0; std::string activity, room, mood; };
    std::vector<SchedBlock> m_schedule;
    int  m_schedAppliedIdx = -1;                  // which block index she's currently placed for (-1 = unplaced)
    int  m_focusIdx        = 0;                    // index into the focus-problem rotation (advances daily)
    std::string          m_problemId;             // her current work (persisted)
    int         m_dlgBeingType = 4;              // 4 = Android
    int         m_dlgDisposition = 50;
    bool        m_commOpen = false;              // ship-comm chat panel toggled on
    bool        m_dlgWaiting = false, m_dlgScrollDown = false, m_dlgFocusInput = false;
    std::mutex        m_dlgMx;
    std::atomic<bool> m_dlgReplyReady{false};
    DlgReply    m_dlgReply;
    bool        m_showServers = false;
    GameServers m_servers{CMAKE_SOURCE_DIR};  // launches the Python backend from the source tree

    // A GPU texture (for the still poster) + ImGui descriptor.
    struct Tex {
        VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE; VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE; int w = 0, h = 0;
    };
    Tex m_poster;
    std::map<std::string, Tex> m_speciesTex;    // cached species portraits, keyed by normalized name
    std::set<std::string>      m_speciesTried;  // keys already attempted (so a missing image isn't retried)

    // Streaming texture for video frames (uploaded in place each frame).
    struct VideoTex {
        VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE; VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        void* mapped = nullptr; int w = 0, h = 0; bool firstUpload = true;
        bool pendingUpload = false;   // new frame is in the staging buffer, GPU copy not yet recorded
    };
    VideoTex    m_vtex;
    VideoPlayer m_video;      // scene/background video (rooms); also the dialogue background
    VideoPlayer m_fgVideo;    // dialogue foreground: a character shot on solid white, keyed transparent
    VideoTex    m_ptex;       // portrait streaming texture (comm-panel headshot feed)
    VideoPlayer m_portraitVideo;   // small feed of the current contact's current-state clip
    // Lore codex + capture dialog (the harvest loop).
    std::string m_codex;               // established canon, injected into her persona
    bool        m_captureOpen = false;
    std::string m_capYou, m_capClara;  // the exchange being captured
    char        m_captureNote[1024] = "";

    std::string m_portraitPath;    // clip currently loaded in the portrait feed
    std::string m_portraitStateKey;    // stable key for her current state (so we don't re-pick a variant every frame)
    std::string m_portraitReactPath;   // transient one-shot reaction clip (e.g. amazed) overriding the portrait
    float       m_portraitReactTimer = 0.0f;   // seconds left on the reaction override
    bool        m_portraitStatic = false;   // frozen first frame (she's on the main screen)
    bool        m_portraitFeed = true;       // comm-panel portrait video feed on/off
    std::vector<unsigned char> m_composite;      // CPU composite buffer (fg keyed over bg)
    std::vector<unsigned char> m_fgAlpha, m_fgAlphaEroded;   // keyer matte + 1px erosion (removes the white edge fringe)
    std::string m_fgChroma = "white";   // key color for the composited character: "white" | "green" | "magenta"
    std::vector<unsigned char> m_bgImagePixels;  // static room background (RGBA) when the room is an image, not a video
    int m_bgImageW = 0, m_bgImageH = 0;

    // ───────────────────────── lifecycle ─────────────────────────
    void onInit() override {
        eden::Audio::getInstance().init();
        m_imgui.init(getContext(), getSwapchain(), getWindow().getHandle(), "imgui_slag_legion.ini");
        // No keyboard nav: Tab is our flight toggle, it must not move ImGui focus into the
        // comm text box (which would swallow the flight controls).
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        std::srand((unsigned)std::time(nullptr));   // for random clip-variant selection
        loadScene("assets/scenes/bridge");
        setupEmails();
        // Which companion boots. Ada is the real default; assets/game.json can override
        // (currently set to "eva" for testing). Later, crew are added/removed at stations.
        std::string boot = "ada";
        try {
            std::ifstream gf("assets/game.json");
            if (gf) { nlohmann::json j; gf >> j; boot = j.value("active_character", boot); }
        } catch (...) {}
        initAda(boot);           // active companion's identity, starting room (the bay), and greeting
        m_servers.startAll();    // bring the comm backend online at boot, keep it on
        // 3D flight scene (spheres + enemy), drawn into the central pane when Tab is pressed.
        loadInteractions();      // per-interaction disposition deltas
        loadShipEconomy();       // daily operating-cost line items (tunable)
        loadCodex();             // established world canon fed back into her persona
        m_flight.init(getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent(),
                      "assets/flight/enemy.glb");
    }

    void onCleanup() override {
        saveAdaState();   // her feelings persist across the "long voyage" between sessions
        m_video.close();
        m_fgVideo.close();
        m_portraitVideo.close();
        eden::Audio::getInstance().shutdown();
        vkDeviceWaitIdle(getContext().getDevice());
        freeTex(m_poster);
        freeVideoTex();
        freeVideoTexInto(m_ptex);
        m_imgui.cleanup();
    }

    // Play a short sound if the file is present (WAV/MP3). No-op if missing.
    static void playSfx(const std::string& path, float vol = 0.8f) {
        if (std::filesystem::exists(path)) eden::Audio::getInstance().playSound(path, vol);
    }

    void update(float dt) override {
        // Deferred scene switch: reopening the video / swapping textures mid-frame
        // (from inside a hotspot click) would free GPU resources ImGui still refers
        // to. Do it here, before the frame starts.
        if (!m_pendingScene.empty()) {
            vkDeviceWaitIdle(getContext().getDevice());
            loadScene(m_pendingScene);
            m_pendingScene.clear();
            m_dropClaraView = false;   // a scene load already reset the central view
        }
        // Deferred: Clara walked off screen (e.g. left to charge). Drop her clip + texture
        // here, at a safe point, so the empty-room poster shows without a mid-frame free.
        if (m_dropClaraView) {
            m_dropClaraView = false;
            vkDeviceWaitIdle(getContext().getDevice());
            m_video.close();
            freeVideoTex();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        // Galaxy Map (M): the top-level 2D navigation view — pan/zoom the 1000x1000 sector grid.
        // While it's open it swallows Bridge input (no flight toggle underneath); Esc closes it.
        if (m_screen == Screen::Bridge && !ImGui::GetIO().WantTextInput && !m_flight.active()
            && Input::isKeyPressed(Input::KEY_M)) {
            m_galaxyMapOpen = !m_galaxyMapOpen;
        }
        if (m_galaxyMapOpen && Input::isKeyPressed(Input::KEY_ESCAPE)) m_galaxyMapOpen = false;

        // 3D flight: Tab toggles it. Entering flight hides the mouse so keys drive the ship
        // immediately (no click-into-viewport needed); RMB brings the cursor back.
        if (!m_galaxyMapOpen && (m_screen == Screen::Bridge) && !ImGui::GetIO().WantTextInput && Input::isKeyPressed(Input::KEY_TAB)) {
            m_flight.toggle();
            // Pause room + character video decode while flying (the 3D pane covers them).
            // The per-frame green-key composite was the frame-rate cap.
            bool fly = m_flight.active();
            if (fly) { m_flightCursorHidden = true; enterSectorMap(); }   // always start in the Sector Map of the current sector
            if (m_video.isOpen())   m_video.setPaused(fly);
            if (m_fgVideo.isOpen()) m_fgVideo.setPaused(fly);
            // Tabbing into a 3D screen auto-engages all of Clara's stations; tabbing back out drops them.
            if (fly) engageStations(); else clearStations();
        }
        // ESC out of the Solar System View returns to the Sector Map (Tab still exits flight entirely).
        if (m_flight.active() && !m_flight.inSector() && !m_flight.warping() && Input::isKeyPressed(Input::KEY_ESCAPE))
            enterSectorMap();
        GLFWwindow* win = getWindow().getHandle();
        // Both the Sector Map and the Solar System View fly on the keyboard with a VISIBLE cursor —
        // no capture, no "press RMB" dance. (Only a future mouselook combat view would hide it.)
        bool cursorFree = m_flight.inSector() || m_flight.inSolarView();
        if (m_flight.active() && !cursorFree && Input::isMouseButtonPressed(Input::MOUSE_RIGHT)) m_flightCursorHidden = false;
        bool wantHide = m_flight.active() && !cursorFree && m_flightCursorHidden;
        if (wantHide != m_flightCursorWasHidden) {
            glfwSetInputMode(win, GLFW_CURSOR, wantHide ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
            m_flightCursorWasHidden = wantHide;
        }
        // Keyboard turning is live whenever the cursor is free (and you're not typing in a field).
        bool flightInput = cursorFree ? (m_flight.active() && !ImGui::GetIO().WantTextInput) : wantHide;
        m_flightInputOn = flightInput;
        m_flight.update(dt, flightInput);
        if (m_flight.consumeArrived()) {   // FTL dropped out at the destination — now in orbit
            m_hint = "Arrived: " + (m_warpStarName.empty() ? std::string("destination system") : m_warpStarName) +
                     "  -  in orbit; deep scan available";
            m_hintTimer = 7.0f;
            m_showScan = true;   // re-open the system readout so the (now-enabled) Deep Scan is reachable
        }
        // Sector Map scanning is by MOUSE CLICK on a star (handled in drawFlightHud, inside the
        // ImGui frame where the mouse is available). Leaving flight closes the panel.
        if (!m_flight.active()) m_showScan = false;
        if (m_showScan && m_scanStar.controlled) ensureSpeciesImage(m_scanStar.species);   // preload portrait
        // Weapon sound: batteries 1/2 have dedicated sounds.
        int firedBat = m_flight.consumeFired();
        if (firedBat == 0)      playSfx("assets/audio/bullets_1.wav", 0.6f);
        else if (firedBat == 1) playSfx("assets/audio/bullets_2.wav", 0.6f);
        // Explosion boom (rate-limited so a burst of sphere pops doesn't stack into noise).
        if (m_explSoundCD > 0.0f) m_explSoundCD -= dt;
        if (m_flight.consumeExplosions() > 0 && m_explSoundCD <= 0.0f) {
            const char* booms[] = {"assets/audio/explosion1.wav", "assets/audio/explosion2.wav", "assets/audio/explosion3.wav"};
            playSfx(booms[std::rand() % 3], 0.7f);
            m_explSoundCD = 0.18f;
        }
        updateFlightAwareness(dt);
        pumpScanQueue();          // drain queued sensor scans one at a time as Clara frees up
        updateDeepScan(dt);       // advance a running geological deep scan

        if (m_screen == Screen::Title) {
            m_titlePulse += dt;
            if (Input::isMouseButtonPressed(Input::MOUSE_LEFT) ||
                Input::isKeyPressed(Input::KEY_SPACE) || Input::isKeyPressed(Input::KEY_ENTER)) {
                m_screen = Screen::Bridge;
                m_commOpen = true;     // comm is up from the moment you board (toggle with C)
            }
        } else {
            // The game clock runs once you're aboard; emails land on their timer.
            m_gameTime += dt;
            if (m_portraitReactTimer > 0.0f) m_portraitReactTimer -= dt;   // transient portrait reaction (amazed, etc.)
            for (auto& e : m_emails) {
                if (!e.arrived && m_gameTime >= e.arriveAt) {
                    e.arrived = true;
                    m_hint = "New message received  -  open the ship's computer terminal to read it.";
                    m_hintTimer = 8.0f;
                    playSfx("assets/audio/message_received.wav");
                }
            }
            // In-game clock: 1 real second = 1 game minute (15s real = 15 min game). The debug
            // time-scale (F3) multiplies this so FTL/schedule are testable without waiting real-time.
            float gameMin = dt * m_timeScale;
            m_timeOfDay += gameMin;
            while (m_timeOfDay >= 1440.0f) { m_timeOfDay -= 1440.0f; advanceDay(); }
            // FTL travel advances on the same clock (1 day / sector). Arrival regenerates the sector.
            if (m_ftlActive) {
                m_ftlElapsedMin += gameMin;
                if (m_ftlElapsedMin >= m_ftlTotalMin) arriveAtSector();
            }
            // Nightly recharge: docked in the science lab from 22:00 to 07:00. A commanded
            // charge runs independently (any time) and completes when the battery is full.
            bool nightly = (m_timeOfDay >= 22 * 60) || (m_timeOfDay < 7 * 60);
            if (nightly && !m_adaCharging) startCharging(false);
            else if (!nightly && m_adaCharging && !m_commandCharge && m_chargePhase == Charge::Active) stopCharging();
            // Battery: a commanded charge tops up briskly (~30s to full); the nightly cycle is
            // a slow overnight trickle; drains slowly while awake (never truly low).
            float chargeRate = m_commandCharge ? 0.6f : 0.083f;
            m_adaBattery = std::clamp(m_adaBattery + (m_adaCharging ? chargeRate : -0.05f) * dt, 0.0f, 100.0f);
            if (m_adaCharging && m_commandCharge && m_adaBattery >= 100.0f && m_chargePhase == Charge::Active)
                chargeComplete();
            updateSchedule();   // her routine places her around the ship when she's otherwise free
        }
        if (m_screen == Screen::Bridge && !m_flight.active()) updateSceneVideo();   // skip the CPU composite in flight
        if (m_portraitFeed && m_screen != Screen::Title && m_commOpen) updatePortraitVideo();   // comm-panel headshot feed
        else if (m_screen == Screen::Radiators) updateRadiators();
        if (m_hintTimer > 0.0f) m_hintTimer -= dt;

        // Longing: eases while you're together (physically in her room, or actively
        // talking), builds while you're apart and quiet. Big fills happen between
        // sessions (see loadAdaState). NOTE: the comm panel is up by default, so mere
        // openness doesn't count as "together" — only a RECENT exchange does.
        if (m_screen != Screen::Title) {
            m_silenceTimer += dt;                                   // seconds since the last exchange
            if (m_initiateCooldown > 0.0f) m_initiateCooldown -= dt;
            // Flight counts as "together": the Captain is at the helm (a known post) and the ship is
            // being run jointly — she's not lost, and operations time isn't lonely time.
            bool inRoom  = m_flight.active() || (!captainRoom().empty() && captainRoom() == m_adaRoom);
            bool talking = m_silenceTimer < m_talkWindow;          // a back-and-forth happened recently
            // Hours -> pts/real-sec: 1 real sec = 1 game min, so (hours*60) real sec spans the full 0..100.
            float fillRate = 100.0f / (std::max(0.1f, m_lonelyFillHours)  * 60.0f);
            float roomRate = 100.0f / (std::max(0.1f, m_lonelyEmptyHours) * 60.0f);
            float talkRate = 100.0f / (std::max(0.1f, m_lonelyTalkHours)  * 60.0f);
            if (inRoom)        m_longing = std::clamp(m_longing - roomRate * dt, 0.0f, 100.0f);
            else if (talking)  m_longing = std::clamp(m_longing - talkRate * dt, 0.0f, 100.0f);
            else               m_longing = std::clamp(m_longing + fillRate * dt, 0.0f, 100.0f);
            if (inRoom || talking) m_hurt = std::clamp(m_hurt - 0.3f * dt, 0.0f, 100.0f);  // time together heals
            // If her longing crosses the yearning line while she's at rest and on screen,
            // shift her clip live (into or out of yearning).
            bool eligible = (m_dlgEmotion == "neutral") && (m_longing >= 60.0f);
            if (eligible != m_yearnActive) { m_yearnActive = eligible; refreshAdaClip(); }

            // The escalation ladder: bar fills -> she CALLS -> no answer -> she SEARCHES.
            updateSeeking(dt);

            // Separate rift-repair outreach: only for hurt / harsh words (longing has its
            // own ladder above). Gated to Idle so the two never talk over each other.
            if (m_seek == Seek::Idle && m_servers.backendReady() && !m_commFailed &&
                !m_dlgWaiting && !m_dlgLog.empty()) {
                bool tension = m_hurt >= 40.0f || isNegativeMood(m_dlgEmotion);
                if (tension && m_silenceTimer >= 120.0f && m_initiateCooldown <= 0.0f) {
                    std::string why = m_hurt >= 40.0f
                        ? "you are hurting from how things have been, and the silence between you has stretched too long"
                        : "your last words were tense, the Captain has gone quiet, and you fear you were too harsh";
                    dispatchComm("[The Captain has been silent for a while. Of your own accord, unprompted, reach out to "
                                 "them FIRST — because " + why + ". Keep it brief and true to yourself.]", false);
                    m_initiateCooldown = 200.0f;
                    m_commUnread = true;
                    m_hint = m_dlgNpcName + " reached out over comm  -  open comm (C)."; m_hintTimer = 8.0f;
                }
            }
        }
        // Autosave her state periodically so it survives a crash/kill, not just a clean quit.
        m_saveTimer -= dt;
        if (m_saveTimer <= 0.0f) { saveAdaState(); m_saveTimer = 20.0f; }

        m_servers.poll();       // pump the backend/Ollama process state
        bool ready = m_servers.backendReady();
        if (ready != m_serversReady) { m_serversReady = ready; if (ready) m_commFailed = false; }
        // Power Ada up/down when her effective on/off state flips (servers, or the in-game
        // Power On/Off command). Plays the transition animation if she's on screen.
        bool off = adaEffectivelyOff();
        if (off != m_adaWasOff) {
            m_adaWasOff = off;
            if (inHerRoom() && !clipFor(m_dlgNpcId, "power").empty()) powPlay(!off);
            else refreshAdaClip();
        }
        pollComm();             // apply any LLM reply that arrived on the worker thread
        pollThought();          // apply any stream-of-consciousness thought

        // Stream of consciousness: a private thought every ~15s, paused during active
        // dialogue so it doesn't compete for the model — and paused entirely during 3D
        // flight (the generation caused frame-hitches while flying).
        if (m_streamOn && m_screen != Screen::Title && ready && !m_commFailed && !m_adaPoweredOff && !m_flight.active()) {
            m_thinkTimer -= dt;
            if (m_thinkTimer <= 0.0f) {
                m_thinkTimer = 15.0f;
                if (!m_thinking && !m_dlgWaiting) generateThought();
            }
        }

        Input::update();
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &bi);
        // Flush any pending video-frame uploads into THIS command buffer (outside the render
        // pass) — replaces the old per-frame stalling submit that was capping the frame rate.
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
        // 3D flight draws FIRST, clipped to the central pane; ImGui then paints the
        // interface (and the flight HUD) on top in the same pass.
        if (m_screen == Screen::Bridge && m_flight.active())
            m_flight.render(cmd, sc.getExtent(), m_flightRx, m_flightRy, m_flightRw, m_flightRh);
        renderUI();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

    // Rebuild the flight renderer's pipelines against the new swapchain pass/extent.
    void onSwapchainRecreated() override {
        m_flight.recreate(getSwapchain().getRenderPass(), getSwapchain().getExtent());
    }

    // The companion reacts to flight/combat — but ONLY when she's present at the helm
    // (same room). Her epistemic limit holds: if she isn't with you, she has no way to
    // know you're flying/fighting, so those events are discarded (she stays in the dark
    // and, if apart long enough, gets lonely). Summon her to the bridge to have her react.
    void updateFlightAwareness(float dt) {
        if (m_flightReactCooldown > 0.0f) m_flightReactCooldown -= dt;
        if (m_scanReactCooldown   > 0.0f) m_scanReactCooldown   -= dt;
        if (!m_flight.active()) { m_flight.consumeEntered(); m_flight.clearHitEvents(); m_flight.consumeKills(); return; }

        // Every kill is the Captain being PROTECTIVE (shielding her/the ship): +1.25 each,
        // ALWAYS (witnessed or not). If she's at the helm she reacts — often flirty/excited.
        int kills = m_flight.consumeKills();
        if (kills > 0) {
            std::string emo = applyInteractionDelta("protect", (float)kills, m_dlgEmotion);
            // Update her live mood so the portrait mirrors it in combat; refresh the room clip too
            // only if she's actually on screen here.
            if (!adaEffectivelyOff()) { if (inHerRoom()) setAdaEmotion(emo); else m_dlgEmotion = emo; }
        }

        bool present = inHerRoom() && m_servers.backendReady() && !m_commFailed && !adaEffectivelyOff();
        if (!present || m_dlgWaiting) {         // can't witness it — don't hoard stale events
            m_flight.consumeEntered();
            m_flight.clearHitEvents();
            return;
        }
        if (!m_stationCombat) {                 // Combat station disengaged — she stays quiet in the fight
            m_flight.consumeEntered();          // (still drop the events so they don't hoard for later)
            m_flight.clearHitEvents();
            return;
        }
        if (m_flight.consumeEntered()) {
            m_forcedInteraction = "protect";   // combat reactions stay in the protect set (flirty/excited/happy/brave), never neutral
            m_stateCause = "battle prep";      // she went to battle stations — surfaced as the cause of her demeanor
            dispatchComm("[The Captain has just taken the ship into flight, and you are at the helm beside them. "
                         "React briefly and in character to going into flight/combat; if there is any way you can "
                         "assist (targeting, routing power to the guns, shields, calling threats), offer it.]", false);
            m_commUnread = true;
            m_flightReactCooldown = 20.0f;
        } else if (kills > 0 && m_flightReactCooldown <= 0.0f) {
            m_forcedInteraction = "protect";   // her verbal reaction to a kill stays in the protect set
            dispatchComm("[From the helm you just watched the Captain destroy a hostile craft — it burst apart and left "
                         "scrap iron drifting in the void for salvage. React briefly and in character.]", false);
            m_flight.clearHitEvents();
            m_commUnread = true;
            m_flightReactCooldown = 18.0f;
        } else if (m_flightReactCooldown <= 0.0f && m_flight.hitEvents() >= 3) {
            m_forcedInteraction = "protect";   // dogfight reaction stays in the protect set, never neutral
            dispatchComm("[From the helm you are watching the Captain land hits on a hostile craft in a dogfight. "
                         "React briefly and in character.]", false);
            m_flight.clearHitEvents();
            m_commUnread = true;
            m_flightReactCooldown = 25.0f;
        }
    }

    // Clara's sensor-officer commentary when the Captain scans a star (Sector Map). Gated behind
    // the SENSORS station; she relays over comms (no need to be in the room). Rate-limited. The
    // rarity is scored RELATIVE to where you are — the same find reads differently by region.
    // A scan just happened — QUEUE it for Clara (if Sensors are on). She'll get to every one, in
    // order, as she frees up. Deduped against the current queue (rapid double-clicks) and capped.
    void enqueueScan(const galaxy::StarSystem& s) {
        if (!m_stationSensors || !m_stationComm) return;            // needs both to sense AND relay it
        for (auto& q : m_scanQueue) if (q.sys.name == s.name) return;   // already waiting in the queue
        if ((int)m_scanQueue.size() >= kScanQueueMax) return;       // backlog full — drop silently
        m_scanQueue.push_back({s, m_sectorX, m_sectorY});
    }
    // Each frame: if Clara is free and off cooldown, pop the next queued scan and comment on it.
    void pumpScanQueue() {
        if (m_scanQueue.empty()) return;
        // Sensors OR Comm off -> she stops all scan commentary; drop the backlog.
        if (!m_stationSensors || !m_stationComm) { m_scanQueue.clear(); return; }
        if (m_scanReactCooldown > 0.0f) return;
        if (!m_servers.backendReady() || m_commFailed || adaEffectivelyOff() || m_dlgWaiting) return;
        QueuedScan q = m_scanQueue.front();
        m_scanQueue.erase(m_scanQueue.begin());
        dispatchScanComment(q.sys, q.sx, q.sy);
    }
    // Build + send Clara's sensor remark for one system. Rarity is scored RELATIVE to where it was
    // scanned; a very-rare / exceptional find also fires her amazed portrait reaction.
    void dispatchScanComment(const galaxy::StarSystem& s, int sx, int sy) {
        galaxy::ScanRarity a  = galaxy::assessSystem(s, sx, sy);
        galaxy::SectorAddr ad = galaxy::addressOf(sx, sy);
        std::string dir = "[SENSORS. The Captain scanned " + s.name + " in " + std::string(ad.region) +
                          " (" + std::string(galaxy::tierName(ad.tier)) + " space";
        if (a.trait != galaxy::SuperTrait::Unremarkable)
            dir += "; this super-sector is " + std::string(galaxy::superTraitName(a.trait));
        dir += "). Your instruments rate this find as " + std::string(galaxy::rarityName(a.overall)) +
               ": " + a.headline;
        for (auto& n : a.notes) dir += "; also " + n;
        for (auto& p : s.planets) {
            std::string an = anomalyResource(p);
            if (!an.empty()) {
                dir += "; AND an odd spectral signature on " + p.name + " - its " + an +
                       " reads inconsistent with the world's geo-profile (a deep scan could reveal the source)";
                break;
            }
        }
        dir += ". Give the Captain one brief, natural observation about how notable or ordinary this is "
               "for these parts, in character as the ship's sensor officer.]";
        m_forcedInteraction.clear();   // informational, not an emotional interaction
        if ((int)a.overall >= (int)galaxy::Rarity::VeryRare) {
            std::string amazed = clipFor(m_dlgNpcId, "amazed");
            if (!amazed.empty()) { m_portraitReactPath = amazed; m_portraitReactTimer = 7.0f; }
        }
        // Tag her upcoming reply with the species so its portrait shows in the comm log (preload it now).
        if (s.controlled) { ensureSpeciesImage(s.species); m_pendingScanSpecies = s.species; }
        dispatchComm(dir, false);
        m_commUnread = true;
        m_scanReactCooldown = 9.0f;   // spacing between queued remarks (tighter, to work through the backlog)
    }

    // ───────────────────────── scene loading ─────────────────────────
    void loadScene(const std::string& dir) {
        m_scene = Scene{}; m_scene.dir = dir;
        m_abLoopApplied = false;   // (re)apply any frame-range loop once fps is known
        m_togglesOn.clear();       // a fresh room starts in its default state
        m_debugPoly.clear();       // don't carry traced points between rooms
        m_vidSkip = 0;             // room video plays from the top; only Clara's clips skip an intro
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
                m_scene.character = j.value("character", std::string());
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
        m_bgImagePixels.clear(); m_bgImageW = m_bgImageH = 0;
        if (std::filesystem::exists(poster)) loadImageTexture(poster, m_poster);
        // Open the looping video if it exists; frames stream in via updateSceneVideo.
        // A scene with no video is a static image room (poster + optional composited character).
        std::string video = dir + "/" + (m_scene.videoFile.empty() ? "scene.mp4" : m_scene.videoFile);
        if (!m_scene.videoFile.empty() && std::filesystem::exists(video) && m_video.open(video)) {
            m_video.setLoop(true);     // bridge ambience: loops forever
            m_video.setMuted(true);    // muted; the player has no playback control here
        } else {
            m_video.close();
            vkDeviceWaitIdle(getContext().getDevice());
            freeVideoTex();            // drop any stale video frame so the poster shows
            if (std::filesystem::exists(poster)) loadBgImage(poster);   // pixels, for compositing a character over the still
        }

        // Composite a character into the room: Ada if this is the room she's currently
        // in (tracked, follows her around), else a scene-declared static character.
        // Uses their current emotion clip, keyed by updateSceneVideo.
        m_fgVideo.close(); m_dlgFgPath.clear(); m_fgChroma = "white";
        m_powEnd = -1; m_fgSeekHold = -1; m_fgLoopFrom = m_fgLoopTo = -1;   // no in-flight fg transition carries across a scene change
        m_clipLoopFrom = m_clipLoopTo = m_clipStart = m_chFinishSeek = -1;  // nor any pending m_video range setup
        stopChargeAudio();                                                  // her charge audio is local to the lab; restarts if you enter mid-charge
        std::string sceneName = std::filesystem::path(dir).filename().string();
        if (sceneName == m_adaRoom) {
            refreshAdaClip();                           // Ada: powered-down / override / mood aware
        } else if (!m_scene.character.empty()) {
            std::string clip = clipFor(m_scene.character, "neutral");
            if (!clip.empty() && m_fgVideo.open(clip)) {
                m_fgVideo.setLoop(true); m_fgVideo.setMuted(true);
                m_fgChroma = charChroma(m_scene.character);
                m_dlgFgPath = clip;
            }
        }
    }

    // Restore the room's default video behavior: its dim frame-range loop if it has
    // one, else a full loop. Used when a "toggle:" clip is switched back off.
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

    // ───────────────────────── UI ─────────────────────────
    void renderUI() {
        ImGui::NewFrame();
        switch (m_screen) {
            case Screen::Title:   renderTitleScreen(); break;
            case Screen::Bridge:  renderBridge();      break;
            case Screen::Console:   renderConsole();     break;
            case Screen::Email:     renderEmailInbox();  break;
            case Screen::EmailRead: renderEmailReader(); break;
            case Screen::Radiators: renderRadiators();   break;
        }
        if (m_screen == Screen::Bridge) renderShipPanel();    // left-side balance panel
        // (star-system scan readout is docked into the left ship panel — see renderShipPanel)
        if (m_screen != Screen::Title) renderCommOverlay();   // persistent ship comm, over any screen
        if (m_galaxyMapOpen) renderGalaxyMap();               // top-level 2D navigation map (M)
        renderCapturePopup();                                 // "capture to lore codex" dialog
        ImGui::Render();
    }

    // The lore-capture dialog: preview the exchange, write the canon to record, save to the codex
    // (which is fed straight back into her persona — so the fact is hers immediately).
    void renderCapturePopup() {
        if (!m_captureOpen) return;
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(580, 0), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
        if (ImGui::Begin("Capture to Lore Codex", &m_captureOpen, ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextDisabled("The exchange:");
            ImGui::PushTextWrapPos(0.0f);
            if (!m_capYou.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.55f, 1.0f), "You: %s", m_capYou.c_str());
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s: %s", m_dlgNpcName.c_str(), m_capClara.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();
            ImGui::TextDisabled("Canon to record — write it as a clean, true statement of the world/rule");
            ImGui::TextDisabled("(e.g. \"Aether Dynamics forbids unauthorized contact with pre-spaceflight species\"):");
            ImGui::InputTextMultiline("##note", m_captureNote, sizeof(m_captureNote), ImVec2(-1, 90));
            ImGui::Spacing();
            // Save to CANON — the note goes straight into the CANON section (she treats it as fact now).
            if (ImGui::Button("Save to CANON", ImVec2(150, 0))) {
                captureToCanon(m_captureNote);
                m_captureOpen = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Note becomes established canon immediately (skips the raw pile)");
            ImGui::SameLine();
            // Save as raw capture — the exchange + note land in the "Captured" section for later curation.
            if (ImGui::Button("Save (raw)", ImVec2(120, 0))) {
                captureToCodex(m_capYou, m_capClara, m_captureNote);
                m_captureOpen = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keeps the full exchange as raw material to promote later");
            ImGui::SameLine();
            // Copy the exchange (+ note) to the clipboard, e.g. to paste into a chat.
            if (ImGui::Button("Copy", ImVec2(70, 0))) {
                std::string clip;
                if (m_captureNote[0]) clip += std::string(m_captureNote) + "\n\n";
                if (!m_capYou.empty()) clip += "You: " + m_capYou + "\n";
                clip += m_dlgNpcName + ": " + m_capClara;
                ImGui::SetClipboardText(clip.c_str());
                m_hint = "Copied to clipboard."; m_hintTimer = 4.0f;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the exchange (+ your note) to the clipboard");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0))) m_captureOpen = false;
            ImGui::TextDisabled("assets/codex.md - Reload in comm to apply hand-edits");
        }
        ImGui::End();
    }

    // Rebuild the current sector's starfield and show the Sector Map (from Tab-enter, or ESC out of
    // a Solar System View). Density/civ come from the sector's region tier.
    void enterSectorMap() {
        galaxy::SectorAddr a = galaxy::addressOf(m_sectorX, m_sectorY);
        float dens = galaxy::densityMul(a.tier) * galaxy::radialDensity(m_sectorX, m_sectorY);
        m_flight.exitToSector(galaxy::sectorSeed(m_sectorX, m_sectorY), dens, galaxy::civChance(a.tier));
        m_showScan = false;
    }

    // Distance between two sectors, in "sectors" (Euclidean) — the FTL cost unit (1 sector = 1 day).
    static float sectorDistance(int ax, int ay, int bx, int by) {
        float dx = (float)(bx - ax), dy = (float)(by - ay);
        return std::sqrt(dx * dx + dy * dy);
    }
    // Engage the hyperdrive toward the marked destination. Starts the journey; does not arrive.
    void engageFTL() {
        if (m_ftlActive || m_mapSelX < 0) return;
        if (m_mapSelX == m_sectorX && m_mapSelY == m_sectorY) return;   // already here
        m_ftlFromX = m_sectorX; m_ftlFromY = m_sectorY;
        m_ftlToX   = m_mapSelX; m_ftlToY   = m_mapSelY;
        float dist = sectorDistance(m_ftlFromX, m_ftlFromY, m_ftlToX, m_ftlToY);
        m_ftlTotalMin   = std::max(kMinPerSector, dist * kMinPerSector);   // at least a full day
        m_ftlElapsedMin = 0.0f;
        m_ftlActive = true;
        playSfx("assets/audio/message_received.wav");   // placeholder hyperdrive-engage chime
        galaxy::SectorAddr d = galaxy::addressOf(m_ftlToX, m_ftlToY);
        m_hint = "Target acquired  -  hyperdrive engaged, bound for " + std::string(d.region) +
                 "  (~" + std::to_string((int)std::ceil(m_ftlTotalMin / kMinPerSector)) + " days)";
        m_hintTimer = 8.0f;
    }
    // Arrival: drop out of hyperspace into the destination sector and regenerate its starfield.
    void arriveAtSector() {
        m_sectorX = m_ftlToX; m_sectorY = m_ftlToY;
        m_ftlActive = false;
        m_mapSelX = m_mapSelY = -1;
        galaxy::SectorAddr a = galaxy::addressOf(m_sectorX, m_sectorY);
        m_flight.regenField(galaxy::sectorSeed(m_sectorX, m_sectorY),
                            galaxy::densityMul(a.tier) * galaxy::radialDensity(m_sectorX, m_sectorY),
                            galaxy::civChance(a.tier));
        m_hint = "Arrived: " + std::string(a.region) + "  (Sector " +
                 std::to_string(a.lx) + "-" + std::to_string(a.ly) + ")";
        m_hintTimer = 8.0f;
    }

    // ── Galaxy Map ───────────────────────────────────────────────────
    // Full-screen 2D chart of the 1000x1000 sector galaxy. Pan with left-drag, zoom with the
    // wheel (toward the cursor). 25 named regions are drawn as tier-coloured tiles; the
    // super-sector grid fades in as you zoom. Hover for a sector's full address; click to mark
    // a destination. (FTL travel to the selection is the next step.)
    static ImU32 tierColor(galaxy::Tier t, float a) {
        int A = (int)(a * 255.0f);
        switch (t) {
            case galaxy::Tier::Core:       return IM_COL32(212, 178,  92, A);  // gold — civilized heart
            case galaxy::Tier::Mid:        return IM_COL32( 70, 132, 164, A);  // teal — settled
            case galaxy::Tier::Rim:        return IM_COL32(188, 112,  56, A);  // amber — frontier
            default:                       return IM_COL32(122,  64, 116, A);  // violet — unexplored
        }
    }
    void renderGalaxyMap() {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 disp = io.DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.015f, 0.015f, 0.03f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowFocus();   // force the map to the top of the z-order each frame (it's a modal overlay)
        ImGui::Begin("##galaxymap", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const int   N = galaxy::kGalaxySectors;
        // First open: fit the whole galaxy, centered.
        if (!m_mapInit) {
            float fit = std::min(canvasSize.x, canvasSize.y) / (float)N * 0.92f;
            m_mapZoom   = fit;
            m_mapOrigin = ImVec2(canvasPos.x + canvasSize.x * 0.5f - N * 0.5f * fit,
                                 canvasPos.y + canvasSize.y * 0.5f - N * 0.5f * fit);
            m_mapInit = true;
        }

        // One invisible button over the whole canvas captures drag / wheel / hover.
        ImGui::InvisibleButton("##mapcanvas", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool hovered = ImGui::IsItemHovered();
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_mapOrigin.x += io.MouseDelta.x;
            m_mapOrigin.y += io.MouseDelta.y;
        }
        if (hovered && io.MouseWheel != 0.0f) {                  // zoom toward the cursor
            float oldZoom = m_mapZoom;
            float f = io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f;
            m_mapZoom = std::clamp(m_mapZoom * f, 0.35f, 40.0f);
            float secx = (io.MousePos.x - m_mapOrigin.x) / oldZoom;
            float secy = (io.MousePos.y - m_mapOrigin.y) / oldZoom;
            m_mapOrigin.x = io.MousePos.x - secx * m_mapZoom;
            m_mapOrigin.y = io.MousePos.y - secy * m_mapZoom;
        }
        auto W2S = [&](float sx, float sy) {
            return ImVec2(m_mapOrigin.x + sx * m_mapZoom, m_mapOrigin.y + sy * m_mapZoom);
        };

        dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        // 25 named regions as tier-coloured tiles + labels.
        for (int cy = 0; cy < galaxy::kRegionGrid; ++cy)
        for (int cx = 0; cx < galaxy::kRegionGrid; ++cx) {
            const galaxy::RegionInfo& r = galaxy::regionCell(cx, cy);
            ImVec2 a = W2S((float)(cx * galaxy::kSectorsPerRegion),       (float)(cy * galaxy::kSectorsPerRegion));
            ImVec2 b = W2S((float)((cx + 1) * galaxy::kSectorsPerRegion), (float)((cy + 1) * galaxy::kSectorsPerRegion));
            dl->AddRectFilled(a, b, tierColor(r.tier, 0.34f));
            dl->AddRect(a, b, IM_COL32(255, 255, 255, 36));
            ImVec2 ts = ImGui::CalcTextSize(r.name);
            if (ts.x < (b.x - a.x) * 0.92f) {
                ImVec2 c((a.x + b.x) * 0.5f - ts.x * 0.5f, (a.y + b.y) * 0.5f - ts.y * 0.5f);
                dl->AddText(ImVec2(c.x + 1, c.y + 1), IM_COL32(0, 0, 0, 140), r.name);
                dl->AddText(c, IM_COL32(232, 232, 240, 200), r.name);
            }
        }

        // Visible sector bounds (for grids).
        int vx0 = std::max(0, (int)((canvasPos.x - m_mapOrigin.x) / m_mapZoom));
        int vy0 = std::max(0, (int)((canvasPos.y - m_mapOrigin.y) / m_mapZoom));
        int vx1 = std::min(N, (int)((canvasPos.x + canvasSize.x - m_mapOrigin.x) / m_mapZoom) + 1);
        int vy1 = std::min(N, (int)((canvasPos.y + canvasSize.y - m_mapOrigin.y) / m_mapZoom) + 1);

        // Super-sector grid fades in once each cell is at least ~14px.
        if (m_mapZoom * galaxy::kSectorsPerSuper >= 14.0f) {
            ImU32 g = IM_COL32(255, 255, 255, 20);
            for (int x = (vx0 / galaxy::kSectorsPerSuper) * galaxy::kSectorsPerSuper; x <= vx1; x += galaxy::kSectorsPerSuper)
                dl->AddLine(W2S((float)x, (float)vy0), W2S((float)x, (float)vy1), g);
            for (int y = (vy0 / galaxy::kSectorsPerSuper) * galaxy::kSectorsPerSuper; y <= vy1; y += galaxy::kSectorsPerSuper)
                dl->AddLine(W2S((float)vx0, (float)y), W2S((float)vx1, (float)y), g);
        }
        // Individual-sector grid only when very zoomed in.
        if (m_mapZoom >= 10.0f) {
            ImU32 g = IM_COL32(255, 255, 255, 10);
            for (int x = vx0; x <= vx1; ++x) dl->AddLine(W2S((float)x, (float)vy0), W2S((float)x, (float)vy1), g);
            for (int y = vy0; y <= vy1; ++y) dl->AddLine(W2S((float)vx0, (float)y), W2S((float)vx1, (float)y), g);
        }

        // FTL trip: progress + interpolated ship position along the route.
        float ftlT = (m_ftlActive && m_ftlTotalMin > 0.0f)
                   ? std::clamp(m_ftlElapsedMin / m_ftlTotalMin, 0.0f, 1.0f) : 0.0f;
        float youX = m_sectorX + 0.5f, youY = m_sectorY + 0.5f;
        if (m_ftlActive) {
            youX = (m_ftlFromX + 0.5f) + (float)(m_ftlToX - m_ftlFromX) * ftlT;
            youY = (m_ftlFromY + 0.5f) + (float)(m_ftlToY - m_ftlFromY) * ftlT;
            ImVec2 a = W2S(m_ftlFromX + 0.5f, m_ftlFromY + 0.5f);
            ImVec2 b = W2S(m_ftlToX + 0.5f,   m_ftlToY + 0.5f);
            ImVec2 c = W2S(youX, youY);
            dl->AddLine(a, b, IM_COL32(120, 232, 200, 60), 1.5f);   // full route (faint)
            dl->AddLine(a, c, IM_COL32(120, 232, 200, 200), 2.0f);  // travelled (bright)
            dl->AddCircle(b, 8.0f, IM_COL32(120, 232, 200, 220), 0, 2.0f);   // destination
        }
        // Selection marker (only when idle — in transit the route line shows the target).
        else if (m_mapSelX >= 0) {
            ImVec2 s = W2S(m_mapSelX + 0.5f, m_mapSelY + 0.5f);
            dl->AddCircle(s, 8.0f, IM_COL32(244, 220, 120, 255), 0, 2.0f);
            dl->AddLine(ImVec2(s.x - 12, s.y), ImVec2(s.x + 12, s.y), IM_COL32(244, 220, 120, 160));
            dl->AddLine(ImVec2(s.x, s.y - 12), ImVec2(s.x, s.y + 12), IM_COL32(244, 220, 120, 160));
        }
        // Ship marker ("YOU") — interpolated along the route while in hyperspace.
        {
            ImVec2 p = W2S(youX, youY);
            dl->AddCircleFilled(p, 5.0f, IM_COL32(120, 232, 144, 255));
            dl->AddCircle(p, 9.0f, IM_COL32(120, 232, 144, 170), 0, 2.0f);
            dl->AddText(ImVec2(p.x + 11, p.y - 7), IM_COL32(150, 240, 170, 220),
                        m_ftlActive ? "SHIP" : "YOU");
        }
        dl->PopClipRect();

        // Hover tooltip (full address) + click-to-select.
        if (hovered) {
            int sx = (int)((io.MousePos.x - m_mapOrigin.x) / m_mapZoom);
            int sy = (int)((io.MousePos.y - m_mapOrigin.y) / m_mapZoom);
            if (sx >= 0 && sx < N && sy >= 0 && sy < N) {
                galaxy::SectorAddr ad = galaxy::addressOf(sx, sy);
                ImGui::BeginTooltip();
                ImGui::TextColored(ImVec4(0.92f, 0.86f, 0.6f, 1), "%s", ad.region);
                ImGui::TextDisabled("%s tier   \xe2\x80\xa2   danger: %s",
                                    galaxy::tierName(ad.tier), galaxy::dangerName(ad.tier));
                ImGui::Separator();
                ImGui::Text("Super-sector  %02d-%02d", ad.ssx, ad.ssy);
                ImGui::Text("Sector        %d-%d", ad.lx, ad.ly);
                ImGui::TextDisabled("absolute  [%d, %d]", ad.sx, ad.sy);
                ImGui::EndTooltip();
                // A left click that didn't drag = select this sector.
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    if (dd.x * dd.x + dd.y * dd.y < 25.0f) { m_mapSelX = sx; m_mapSelY = sy; }
                }
            }
        }

        // ── HUD overlay ──
        galaxy::SectorAddr you = galaxy::addressOf(m_sectorX, m_sectorY);
        ImU32 hudBg = IM_COL32(8, 10, 18, 220);
        // top-left: current location
        {
            ImVec2 o(canvasPos.x + 14, canvasPos.y + 12);
            dl->AddRectFilled(o, ImVec2(o.x + 300, o.y + 92), hudBg, 6.0f);
            dl->AddRect(o, ImVec2(o.x + 300, o.y + 92), IM_COL32(255, 255, 255, 30), 6.0f);
            dl->AddText(ImVec2(o.x + 12, o.y + 10), IM_COL32(150, 240, 170, 255), "CURRENT LOCATION");
            dl->AddText(ImVec2(o.x + 12, o.y + 30), tierColor(you.tier, 1.0f), you.region);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "Super-sector %02d-%02d   Sector %d-%d",
                          you.ssx, you.ssy, you.lx, you.ly);
            dl->AddText(ImVec2(o.x + 12, o.y + 50), IM_COL32(200, 205, 220, 255), buf);
            std::snprintf(buf, sizeof(buf), "%s tier  -  danger %s",
                          galaxy::tierName(you.tier), galaxy::dangerName(you.tier));
            dl->AddText(ImVec2(o.x + 12, o.y + 68), IM_COL32(150, 155, 170, 255), buf);
        }
        // top-right: tier legend + close
        {
            float w = 176;
            ImVec2 o(canvasPos.x + canvasSize.x - w - 14, canvasPos.y + 12);
            dl->AddRectFilled(o, ImVec2(o.x + w, o.y + 108), hudBg, 6.0f);
            dl->AddRect(o, ImVec2(o.x + w, o.y + 108), IM_COL32(255, 255, 255, 30), 6.0f);
            dl->AddText(ImVec2(o.x + 12, o.y + 8), IM_COL32(200, 205, 220, 255), "REGION TIERS");
            const galaxy::Tier tiers[] = { galaxy::Tier::Core, galaxy::Tier::Mid, galaxy::Tier::Rim, galaxy::Tier::Unexplored };
            for (int i = 0; i < 4; ++i) {
                float y = o.y + 28 + i * 19;
                dl->AddRectFilled(ImVec2(o.x + 12, y + 2), ImVec2(o.x + 26, y + 14), tierColor(tiers[i], 0.85f));
                dl->AddText(ImVec2(o.x + 34, y), IM_COL32(210, 214, 226, 255), galaxy::tierName(tiers[i]));
            }
        }
        // bottom-left: controls hint
        {
            ImVec2 o(canvasPos.x + 14, canvasPos.y + canvasSize.y - 34);
            const char* help = "Left-drag pan   -   Wheel zoom   -   Click to mark destination   -   [Esc] close";
            ImVec2 ts = ImGui::CalcTextSize(help);
            dl->AddRectFilled(o, ImVec2(o.x + ts.x + 20, o.y + 24), hudBg, 6.0f);
            dl->AddText(ImVec2(o.x + 10, o.y + 5), IM_COL32(170, 175, 190, 255), help);
        }
        // bottom-right: FTL control cluster (real ImGui widgets: engage button / live transit)
        {
            float pw = 330.0f;
            float ph = m_ftlActive ? 108.0f : (m_mapSelX >= 0 ? 100.0f : 0.0f);
            if (ph > 0.0f) {
                ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + canvasSize.x - pw - 14,
                                                 canvasPos.y + canvasSize.y - ph - 14));
                ImGui::BeginChild("##ftlctl", ImVec2(pw, ph), true);
                if (m_ftlActive) {
                    galaxy::SectorAddr d = galaxy::addressOf(m_ftlToX, m_ftlToY);
                    ImGui::TextColored(ImVec4(0.47f, 0.91f, 0.78f, 1), "HYPERDRIVE ENGAGED");
                    ImGui::Text("Bound for %s", d.region);
                    ImGui::ProgressBar(ftlT, ImVec2(-1, 0));
                    float remainDays = (m_ftlTotalMin - m_ftlElapsedMin) / kMinPerSector;
                    ImGui::Text("ETA  %.1f days   (%.0f%% complete)", std::max(0.0f, remainDays), ftlT * 100.0f);
                    if (ImGui::SmallButton("Abort jump")) {   // drops back to the origin sector
                        m_ftlActive = false;
                        m_hint = "Hyperdrive disengaged."; m_hintTimer = 5.0f;
                    }
                } else {
                    galaxy::SectorAddr sel = galaxy::addressOf(m_mapSelX, m_mapSelY);
                    float dist = sectorDistance(m_sectorX, m_sectorY, m_mapSelX, m_mapSelY);
                    int days = (int)std::ceil(std::max(1.0f, dist));
                    bool here = (m_mapSelX == m_sectorX && m_mapSelY == m_sectorY);
                    ImGui::TextColored(ImVec4(0.96f, 0.86f, 0.47f, 1), "DESTINATION");
                    ImGui::Text("%s   -   Sector %d-%d", sel.region, sel.lx, sel.ly);
                    ImGui::Text("Distance %.1f sectors   ~%d day%s", dist, days, days == 1 ? "" : "s");
                    ImGui::BeginDisabled(here);
                    if (ImGui::Button(here ? "Already here" : "ENGAGE FTL DRIVE", ImVec2(-1, 0))) engageFTL();
                    ImGui::EndDisabled();
                }
                ImGui::EndChild();
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // Left-side ship panel — mirrors the comm panel on the right for visual balance and
    // will hold subsystem controls. In flight it surfaces live flight status.
    // Inline media bar for the active character clip: name + mute / play-pause / loop-once
    // / scrub, all on one line (icons only). Shown only when her clip is actually on screen.
    void renderClipControls() {
        VideoPlayer* vid = m_dlgComposite ? &m_fgVideo : &m_video;
        if (!inHerRoom() || !vid->isOpen() || vid->w == 0) return;
        std::string clip = std::filesystem::path(m_dlgFgPath).stem().string();
        if (clip.size() > 12) clip = clip.substr(0, 12);
        ImGui::Text("Clip         %s", clip.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(vid->muted() ? "x##cvol" : "o##cvol")) vid->setMuted(!vid->muted());   // mute / sound
        ImGui::SameLine();
        if (ImGui::SmallButton(vid->paused() ? ">##cpp" : "II##cpp")) vid->setPaused(!vid->paused()); // play / pause
        ImGui::SameLine();
        if (ImGui::SmallButton(m_clipLoop ? "L##clp" : "1##clp")) {                                    // loop / once
            m_clipLoop = !m_clipLoop;
            if (m_clipLoop) {
                vid->setLoop(true);
                double f = vid->fps(); long fc = vid->frameCount();
                if (f > 0 && fc > m_clipSkip) vid->setABLoopSeconds(m_clipSkip / f, fc / f);
            } else { vid->clearABLoop(); vid->setLoop(false); }
        }
        ImGui::SameLine();
        long fc = vid->frameCount();
        if (fc > 1) {                                                                                  // scrub timeline
            int v = (int)std::clamp(vid->currentFrame(), 0L, fc - 1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderInt("##cscrub", &v, 0, (int)fc - 1, "")) vid->seekToFramePause((long)v);
        }
    }

    void renderShipPanel() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float w = std::clamp(disp.x * 0.28f, 320.0f, 460.0f);   // match the comm panel width
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(w, disp.y));
        ImGui::Begin("##shippanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "SHIP SYSTEMS");
        ImGui::Separator();
        ImGui::Spacing();
        if (!m_flight.active()) {
            if (ImGui::Button("GALAXY MAP  [M]", ImVec2(-1, 0))) m_galaxyMapOpen = true;
            if (m_ftlActive) {
                float t = m_ftlTotalMin > 0.0f ? std::clamp(m_ftlElapsedMin / m_ftlTotalMin, 0.0f, 1.0f) : 0.0f;
                galaxy::SectorAddr d = galaxy::addressOf(m_ftlToX, m_ftlToY);
                ImGui::TextColored(ImVec4(0.47f, 0.91f, 0.78f, 1), "FTL -> %s", d.region);
                ImGui::ProgressBar(t, ImVec2(-1, 0));
                ImGui::TextDisabled("ETA %.1f days", std::max(0.0f, (m_ftlTotalMin - m_ftlElapsedMin) / kMinPerSector));
            }
            ImGui::Spacing();
        }
        if (m_flight.active()) {
            ImGui::TextColored(ImVec4(0.7f, 0.95f, 0.85f, 1.0f), "FLIGHT ACTIVE");
            ImGui::Spacing();
            ImGui::Text("Throttle  %3.0f%%", m_flight.throttle() * 100.0f);
            ImGui::Text("Speed     %5.1f", m_flight.speed());
            ImGui::Text("Gun       [%d] %s", m_flight.gunBattery() + 1, m_flight.gunBatteryName());
            ImGui::Text("Hits      %d", m_flight.score());
            ImGui::Text("Scrap     %d", m_flight.scrap());
            // Scan readout docked here (fixed panel width -> word-wraps; child scrolls long systems).
            // A running deep scan takes over this dock until you go Back.
            if (m_deep.active) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.7f, 1.0f), "GEOLOGICAL DEEP SCAN");
                ImGui::SameLine(ImGui::GetWindowWidth() - 62);
                if (ImGui::SmallButton("< Back")) m_deep.active = false;
                ImGui::BeginChild("##deepdock", ImVec2(0, 0), true);
                renderDeepScanContent();
                ImGui::EndChild();
            } else if (m_showScan) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "STAR SYSTEM SCAN");
                ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                if (ImGui::SmallButton("X")) m_showScan = false;
                ImGui::BeginChild("##scandock", ImVec2(0, 0), true);
                renderScanContent();
                ImGui::EndChild();
            }
        } else {
            // Companion / AI status (moved here to decongest the comm header — which will
            // eventually become a live Clara headshot for flight/combat).
            ImGui::TextColored(ImVec4(0.72f, 0.86f, 1.0f, 1.0f), "%s", m_dlgNpcName.c_str());
            ImGui::SameLine(); ImGui::TextDisabled("- %s", adaTierLabel());
            ImGui::Spacing();
            ImGui::Text("Mood         %s", m_dlgEmotion.c_str());
            renderClipControls();
            ImGui::Text("You did      %s", m_dlgLastInteraction.empty() ? "-" : m_dlgLastInteraction.c_str());
            // Systemic cause of her current demeanor when the Captain didn't set it (e.g. she went to
            // battle stations). It rides along with the mood it produced — shown until she settles back
            // to a baseline mood or the Captain gives her a new reason.
            bool baselineMood = (m_dlgEmotion == "neutral" || m_dlgEmotion == "idle");
            bool causeValid = !m_stateCause.empty() && !baselineMood;
            if (causeValid) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.42f, 1.0f), "Cause        %s", m_stateCause.c_str());
            }
            ImGui::Text("Disposition  %d / 100", m_dlgDisposition);
            ImGui::Text("Battery      %.0f%%%s", m_adaBattery, m_adaCharging ? "  (charging)" : "");
            ImGui::Text("Location     %s", prettyRoom(m_adaRoom).c_str());
            if (!m_permsGranted.empty()) ImGui::Text("Freedoms     %d", (int)m_permsGranted.size());
            { std::string act = currentActivityLabel();   // live from her daily routine
              if (!act.empty()) ImGui::TextWrapped("Working on:  %s", act.c_str()); }
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.45f, 0.72f, 1.0f));
            char lbuf[48]; std::snprintf(lbuf, sizeof(lbuf), "Awaiting you   %.0f%%", m_longing);
            ImGui::ProgressBar(m_longing / 100.0f, ImVec2(-FLT_MIN, 0.0f), lbuf);
            ImGui::PopStyleColor();
            if (m_hurt >= 1.0f) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.25f, 0.28f, 1.0f));
                char hbuf[48]; std::snprintf(hbuf, sizeof(hbuf), "Hurt   %.0f%%", m_hurt);
                ImGui::ProgressBar(m_hurt / 100.0f, ImVec2(-FLT_MIN, 0.0f), hbuf);
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();
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
        const char* title = "SLAG LEGION";
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

    // Flight HUD painted over the 3D central pane. Sector Map = mouse hover/click to scan (no
    // reticule); System View = crosshair + throttle/speed + weapon keys.
    void drawFlightHud(ImDrawList* dl, ImVec2 p0, ImVec2 sz) {
        ImVec2 c(p0.x + sz.x * 0.5f, p0.y + sz.y * 0.5f);
        bool sector = m_flight.inSector();

        if (m_flight.warping()) {   // FTL: locked autopilot — no crosshair, no scanning, just the rush
            const char* w = "FTL DRIVE ENGAGED";
            std::string t = "heading for " + (m_warpStarName.empty() ? std::string("target") : m_warpStarName);
            ImVec2 ws = ImGui::CalcTextSize(w), tsz = ImGui::CalcTextSize(t.c_str());
            dl->AddText(ImVec2(c.x - ws.x * 0.5f, p0.y + 40), IM_COL32(140, 240, 255, 255), w);
            dl->AddText(ImVec2(c.x - tsz.x * 0.5f, p0.y + 60), IM_COL32(160, 200, 220, 220), t.c_str());
            dl->AddText(ImVec2(p0.x + 16, p0.y + sz.y - 28), IM_COL32(150, 175, 190, 200), "autopilot locked  -  hold on");
            return;
        }

        // The Solar System View flies with a free cursor like the Sector Map — so no crosshair and
        // no cursor-capture warning (those only ever applied to a captured-cursor mouselook view).

        if (sector) {
            // Hover a star (mouse) to preview it; click to scan. No key needed.
            // Only when the bare pane is the hovered window (not the comm/ship/scan panels on
            // top) — the full-screen ##bridge window makes WantCaptureMouse useless here.
            ImVec2 mp = ImGui::GetIO().MousePos;
            if (ImGui::IsWindowHovered()) {
                glm::vec2 starPx;
                int idx = m_flight.pickStarScreen(mp.x, mp.y, p0.x, p0.y, sz.x, sz.y, &starPx);
                if (idx >= 0) {
                    const galaxy::StarSystem& st = m_flight.stars()[(size_t)idx];
                    // Small filled marker on the star (NOT an empty ring) + name/type/distance.
                    dl->AddCircleFilled(ImVec2(starPx.x, starPx.y), 4.0f, IM_COL32(255, 245, 180, 255), 12);
                    char nb[160];
                    std::snprintf(nb, sizeof(nb), "%s  (%s)%s  -  %.0f ly", st.name.c_str(), st.typeName.c_str(),
                                  st.controlled ? "  [inhabited]" : "", m_flight.starDistance(idx));
                    dl->AddText(ImVec2(mp.x + 16, mp.y + 6), IM_COL32(255, 230, 120, 255), nb);
                    if (ImGui::IsMouseClicked(0)) { m_scanStar = st; m_showScan = true; enqueueScan(st); }
                }
            }
            dl->AddText(ImVec2(p0.x + 16, p0.y + sz.y - 28), IM_COL32(150, 175, 190, 200),
                        "W/S/A/D/Q/E  turn/roll   -   X/Z  cruise   -   click a star to scan   |   TAB exit");
        } else {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "THROTTLE %3.0f%%   SPEED %5.1f   GUN [%d] %s   HITS %d   SCRAP %d",
                          m_flight.throttle() * 100.0f, m_flight.speed(),
                          m_flight.gunBattery() + 1, m_flight.gunBatteryName(), m_flight.score(), m_flight.scrap());
            dl->AddText(ImVec2(p0.x + 16, p0.y + sz.y - 46), IM_COL32(150, 235, 210, 255), buf);
            dl->AddText(ImVec2(p0.x + 16, p0.y + sz.y - 28), IM_COL32(150, 175, 190, 200),
                        "W/S pitch  A/D yaw  Q/E roll  X/Z throttle  SPACE fire  1-4 gun   |   ESC sector");
        }
    }

    // Human-readable population (tech-1 worlds have hundreds; tech-10 have billions).
    static std::string prettyPop(long long p) {
        char b[32];
        if      (p >= 1000000000LL) std::snprintf(b, sizeof(b), "%.1fB", (double)p / 1e9);
        else if (p >= 1000000LL)    std::snprintf(b, sizeof(b), "%.1fM", (double)p / 1e6);
        else if (p >= 1000LL)       std::snprintf(b, sizeof(b), "%.1fK", (double)p / 1e3);
        else                        std::snprintf(b, sizeof(b), "%lld", p);
        return b;
    }

    // Star-system scan readout, DOCKED into the left ship panel (see renderShipPanel). No floating
    // window to resize — it word-wraps to the fixed panel width and the caller's child scrolls it.
    // Deterministic seed for a star's destination locale (so warping to it regenerates consistently).
    static uint32_t starWarpSeed(const galaxy::StarSystem& s) {
        uint32_t h = 2166136261u;
        for (char c : s.name) { h ^= (uint32_t)(unsigned char)c; h *= 16777619u; }
        h ^= (uint32_t)(int)(s.pos.x * 7.3f);  h *= 16777619u;
        h ^= (uint32_t)(int)(s.pos.y * 3.1f);  h *= 16777619u;
        h ^= (uint32_t)(int)(s.pos.z * 11.7f); h *= 16777619u;
        return h ? h : 1u;
    }
    void renderScanContent() {
        const galaxy::StarSystem& s = m_scanStar;
        // Engage the FTL drive straight from here (Sector Map only): parallax off, locked autopilot,
        // rush to the star. See FlightMode::startStarWarp.
        if (m_flight.active() && m_flight.inSector() && !m_flight.warping()) {
            if (ImGui::Button("FTL to this system", ImVec2(-1, 0))) {
                m_warpStarName = s.name;
                m_flight.startStarWarp(s, starWarpSeed(s));
                m_showScan = false;
            }
            ImGui::Spacing();
        }
        ImGui::PushTextWrapPos(0.0f);   // wrap everything to the panel width
        if (!m_archiveAccess)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                               "! AETHER DYNAMICS ARCHIVES OFFLINE - raw spectral only; no classifications or anomaly baseline");
        ImGui::TextColored(ImVec4(s.color.r, s.color.g, s.color.b, 1.0f), "%s", s.name.c_str());
        ImGui::SameLine(); ImGui::TextDisabled("(%s)", s.typeName.c_str());
        // Rarity banner — scored relative to this location's baseline (region tier + super-sector).
        {
            galaxy::ScanRarity a = galaxy::assessSystem(s, m_sectorX, m_sectorY);
            static const ImVec4 rc[] = {
                ImVec4(0.55f,0.58f,0.62f,1), ImVec4(0.65f,0.70f,0.75f,1), ImVec4(0.50f,0.85f,0.60f,1),
                ImVec4(0.45f,0.75f,1.00f,1), ImVec4(0.80f,0.55f,1.00f,1), ImVec4(1.00f,0.80f,0.35f,1) };
            std::string up = galaxy::rarityName(a.overall);
            for (auto& c : up) c = (char)std::toupper((unsigned char)c);
            ImGui::TextColored(rc[(int)a.overall], "%s", up.c_str());
            if (a.trait != galaxy::SuperTrait::Unremarkable)
                ImGui::TextDisabled("super-sector: %s", galaxy::superTraitName(a.trait));
            ImGui::TextDisabled("%s", a.headline.c_str());
            ImGui::Separator();
        }
        if (s.controlled) {
            if (m_archiveAccess) {   // archives turn the raw signal into a named, classified civilization
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "%s", s.species.c_str());
                ImGui::Text("%s  -  Tech %d (%s)  -  Pop %s",
                            s.government.c_str(), s.techLevel, s.techName.c_str(),
                            prettyPop(s.population).c_str());
                if (const Tex* img = speciesImage(s.species)) {
                    float iw = std::min(ImGui::GetContentRegionAvail().x, 300.0f);
                    float ih = iw * (float)img->h / (float)std::max(1, img->w);
                    ImGui::Spacing();
                    ImGui::Image((ImTextureID)img->descriptor, ImVec2(iw, ih));
                    ImGui::Spacing();
                }
                ImGui::TextDisabled("%s", s.speciesDesc.c_str());
                if (!s.culture.empty())   { ImGui::Spacing(); ImGui::TextDisabled("Culture: %s", s.culture.c_str()); }
                if (!s.homeworld.empty())                     ImGui::TextDisabled("Homeworld: %s", s.homeworld.c_str());
            } else {   // spectral says it's inhabited; without archives you can't identify who
                ImGui::TextColored(ImVec4(0.85f, 0.7f, 0.4f, 1.0f), "Inhabited - unidentified");
                ImGui::TextDisabled("Life-sign spectra detected. No Aether Dynamics archive access to classify the species.");
            }
        } else {
            ImGui::TextDisabled("Uninhabited");
        }
        ImGui::Separator();
        if (s.planets.empty()) ImGui::TextDisabled("No planets.");
        for (const auto& p : s.planets) {
            std::string res;
            for (size_t i = 0; i < p.resources.size(); ++i) res += (i ? ", " : "") + p.resources[i];
            // Name + habitation tag on one line (both short, so they fit without hitting the edge);
            // the planet TYPE goes on its own wrapped line so nothing gets shoved to the panel edge
            // where it would char-wrap vertically.
            ImGui::TextColored(ImVec4(0.75f, 0.9f, 1.0f, 1.0f), "%s", p.name.c_str());
            if (p.inhabited)               { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.35f, 1.0f), "[inhabited]"); }
            else if (p.habitability >= 50) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.50f, 1.0f), "[habitable]"); }
            else if (p.habitability >= 20) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.90f, 0.85f, 0.40f, 1.0f), "[marginal]"); }
            ImGui::TextDisabled("%s", p.type.c_str());
            ImGui::TextColored(ImVec4(0.66f, 0.74f, 0.84f, 1.0f), "  - %s", res.c_str());   // resources, word-wrapped
            for (const auto& m : p.moons) {
                std::string mres;
                for (size_t i = 0; i < m.resources.size(); ++i) mres += (i ? ", " : "") + m.resources[i];
                ImGui::TextDisabled("    moon - %s: %s", m.type.c_str(), mres.c_str());   // wrapped
            }
            // Anomaly = live spectral reading vs. the archive geo-profile baseline. Needs BOTH —
            // without archives there's nothing to compare against, so it can't be flagged.
            std::string anom = m_archiveAccess ? anomalyResource(p) : "";
            if (!anom.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                                   "  ! spectral signature: %s reads inconsistent with the geo-profile - deep scan advised", anom.c_str());
            // Deep scan resolves the lattice, but the Tier-1 scanner can only do it from ORBIT.
            if (!p.resources.empty()) {
                bool inOrbit = m_flight.inSolarView();
                ImGui::BeginDisabled(!inOrbit);
                if (ImGui::SmallButton((std::string("Deep scan##") + p.name).c_str())) startDeepScan(p);
                ImGui::EndDisabled();
                if (!inOrbit) { ImGui::SameLine(); ImGui::TextDisabled("(requires orbit - FTL to the system)"); }
            }
        }
        ImGui::PopTextWrapPos();
    }

    // Geological deep-scan readout (docked in the left panel while a deep scan runs/completes).
    // A "RESONANCE" spectral graph — grid + a spiky filled waveform + bright top line + label.
    // Deterministic per deposit (stable shape) with a subtle live shimmer. Pure ImDrawList.
    void renderResonanceGraph(ImDrawList* dl, ImVec2 p, ImVec2 sz) {
        ImVec2 q(p.x + sz.x, p.y + sz.y);
        dl->AddRectFilled(p, q, IM_COL32(4, 12, 14, 255), 3.0f);
        dl->AddRect(p, q, IM_COL32(60, 130, 130, 170), 3.0f);
        ImVec2 g0(p.x + 6, p.y + 18), g1(q.x - 6, q.y - 5);
        float gw = g1.x - g0.x, gh = g1.y - g0.y;
        for (int i = 0; i <= 4; ++i) { float y = g0.y + gh*i/4; dl->AddLine({g0.x,y},{g1.x,y}, IM_COL32(45,95,95,70)); }
        for (int i = 0; i <= 8; ++i) { float x = g0.x + gw*i/8; dl->AddLine({x,g0.y},{x,g1.y}, IM_COL32(45,95,95,55)); }
        uint32_t seed = 2166361u;
        for (char ch : m_deep.planet)   { seed ^= (unsigned char)ch; seed *= 16777619u; }
        for (char ch : m_deep.resource) { seed ^= (unsigned char)ch; seed *= 16777619u; }
        auto samp = [&](int i) {
            float x = i * 0.28f;
            float v = 0.42f + 0.20f*std::sin(x*1.3f + (seed & 255)*0.1f) + 0.12f*std::sin(x*3.3f + 1.0f) + 0.08f*std::sin(x*8.1f);
            v += 0.045f * std::sin(m_gameTime*3.0f + i*0.5f);                 // subtle live shimmer
            uint32_t h = seed ^ ((uint32_t)i * 2654435761u); h ^= h>>13; h *= 0x5bd1e995u; h ^= h>>15;
            float r = (h & 0xffff) / 65535.0f;
            if (r > 0.86f) v += (r - 0.86f) * 2.6f;                           // occasional spikes
            return std::clamp(v, 0.04f, 0.98f);
        };
        const int N = 72; ImVec2 top[N];
        for (int i = 0; i < N; ++i) { float v = samp(i); top[i] = ImVec2(g0.x + gw*i/(N-1), g1.y - v*gh); }
        for (int i = 0; i < N-1; ++i)                                        // filled columns
            dl->AddQuadFilled({top[i].x,g1.y}, top[i], top[i+1], {top[i+1].x,g1.y}, IM_COL32(60,190,205,70));
        dl->AddPolyline(top, N, IM_COL32(150,245,255,255), 0, 1.6f);         // bright crest line
        dl->AddText(ImVec2(p.x + 7, p.y + 3), IM_COL32(120, 230, 235, 255), "RESONANCE");
        char b[24]; std::snprintf(b, sizeof(b), "%.2f THz", m_deep.resonance);
        ImVec2 ts = ImGui::CalcTextSize(b);
        dl->AddText(ImVec2(q.x - ts.x - 8, p.y + 3), IM_COL32(150, 245, 255, 220), b);
    }
    // A "purity ring" radial gauge — a track ring with a fill arc (clockwise from top) for the
    // purity %, the number in the centre, cyan when natural / red when anomalous.
    void renderPurityRing(ImDrawList* dl, ImVec2 c, float R, float thick, int purity, bool anom) {
        dl->AddCircle(c, R, IM_COL32(40, 72, 78, 190), 56, thick);                         // track
        float frac = std::clamp(purity / 100.0f, 0.0f, 1.0f);
        float a0 = -1.5707963f, a1 = a0 + 6.2831853f * frac;                               // top, clockwise
        dl->PathArcTo(c, R, a0, a1, 56);
        dl->PathStroke(anom ? IM_COL32(255, 110, 80, 255) : IM_COL32(90, 225, 245, 255), 0, thick);
        char b[8]; std::snprintf(b, sizeof(b), "%d%%", purity);
        ImVec2 ts = ImGui::CalcTextSize(b);
        dl->AddText(ImVec2(c.x - ts.x*0.5f, c.y - ts.y*0.5f), IM_COL32(225, 242, 248, 255), b);
        const char* lab = "PURITY"; ImVec2 ls = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(c.x - ls.x*0.5f, c.y - R - 16.0f), IM_COL32(120, 230, 235, 255), lab);
    }
    void renderDeepScanContent() {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.45f, 1.0f), "%s", m_deep.planet.c_str());
        ImGui::TextDisabled("Target lattice: %s", m_deep.resource.c_str());
        ImGui::Separator();
        if (m_deep.t < 1.0f) {
            ImGui::TextDisabled("Analyzing crystal lattice...");
            ImGui::ProgressBar(m_deep.t, ImVec2(-1, 0));
        } else {
            ImGui::Text("Crystal system   %s", m_deep.lattice.c_str());
            ImGui::Text("Lattice constant %.2f A", m_deep.constA);
            ImGui::TextDisabled("Resonance (extraction frequency):");
            ImGui::Spacing();
            ImVec2 gp = ImGui::GetCursorScreenPos();
            float gw = ImGui::GetContentRegionAvail().x, gh = 96.0f;
            renderResonanceGraph(ImGui::GetWindowDrawList(), gp, ImVec2(gw, gh));
            ImGui::Dummy(ImVec2(gw, gh));
            ImGui::Spacing();
            // Purity ring gauge (centered).
            float ringR = 42.0f;
            ImVec2 rp = ImGui::GetCursorScreenPos();
            ImVec2 rc(rp.x + gw * 0.5f, rp.y + 18.0f + ringR);
            renderPurityRing(ImGui::GetWindowDrawList(), rc, ringR, 8.0f, m_deep.purity, m_deep.anomalous);
            ImGui::Dummy(ImVec2(gw, 18.0f + ringR * 2.0f + 8.0f));
            ImGui::Separator();
            if (m_deep.anomalous) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "ORIGIN: ANOMALOUS");
            else                  ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f),  "ORIGIN: Natural");
            ImGui::TextDisabled("%s", m_deep.origin.c_str());
            ImGui::Separator();
            ImGui::Text("Est. extraction value");
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", commas(m_deep.yield).c_str());
        }
        ImGui::PopTextWrapPos();
    }

    // Unit-cell basis vectors per crystal system — the shape that makes a Hexagonal lattice look
    // hexagonal, a Trigonal one skewed, etc.
    // Build the cell as an n-GON PRISM where n = the crystal system's symmetry: trigonal = triangles
    // (3), hexagonal = hexagons (6), cubic/tetragonal/orthorhombic/mono/triclinic = 4-gons that
    // differ by proportion and shear. The 3- and 6-fold ones get a centre so each face reads as a
    // fan of triangles. Returns node count; sets ne = edge count.
    static int buildLattice(const std::string& sys, float nx[], float ny[], float nz[], int E[][2], int& ne) {
        ne = 0;
        int sides; float rx, rz, hh, shx, shz; bool ctr;
        if      (sys == "Trigonal")    { sides=3; rx=1.0f; rz=1.0f; hh=0.95f; shx=0;     shz=0;    ctr=true;  }
        else if (sys == "Hexagonal")   { sides=6; rx=1.0f; rz=1.0f; hh=1.0f;  shx=0;     shz=0;    ctr=true;  }
        else if (sys == "Tetragonal")  { sides=4; rx=0.8f; rz=0.8f; hh=1.6f;  shx=0;     shz=0;    ctr=false; } // tall
        else if (sys == "Orthorhombic"){ sides=4; rx=1.4f; rz=0.7f; hh=1.0f;  shx=0;     shz=0;    ctr=false; } // rectangular
        else if (sys == "Monoclinic")  { sides=4; rx=1.0f; rz=1.0f; hh=1.1f;  shx=0.55f; shz=0;    ctr=false; } // one lean
        else if (sys == "Triclinic")   { sides=4; rx=1.1f; rz=0.9f; hh=1.0f;  shx=0.45f; shz=0.3f; ctr=false; } // all skew
        else /* Cubic */               { sides=4; rx=1.0f; rz=1.0f; hh=1.0f;  shx=0;     shz=0;    ctr=false; }
        float rot = (sides == 3) ? 1.5708f : (sides == 4 ? 0.7854f : 0.0f);   // point-up tri / axis-aligned square
        float step = 6.2831853f / sides; int per = sides + (ctr ? 1 : 0), n = 0;
        for (int layer = 0; layer < 2; ++layer) {
            float y = layer ? hh : -hh, ox = layer ? shx : 0.0f, oz = layer ? shz : 0.0f;
            int base = n;
            for (int i = 0; i < sides; ++i) {
                float a = rot + i * step;
                nx[n]=std::cos(a)*rx + ox; ny[n]=y; nz[n]=std::sin(a)*rz + oz; ++n;
            }
            int center = -1;
            if (ctr) { center = n; nx[n]=ox; ny[n]=y; nz[n]=oz; ++n; }
            for (int i = 0; i < sides; ++i) { E[ne][0]=base+i; E[ne][1]=base+(i+1)%sides; ++ne; }   // ring
            if (ctr) for (int i = 0; i < sides; ++i) { E[ne][0]=base+i; E[ne][1]=center; ++ne; }    // spokes
        }
        for (int i = 0; i < per; ++i) { E[ne][0]=i; E[ne][1]=per+i; ++ne; }                          // verticals
        return n;
    }
    // The deep-scan lattice, drawn in the central pane: the conventional cell for the crystal system,
    // rotating, depth-shaded. Builds up as the scan runs; calm cyan when natural, red + jittery when
    // ANOMALOUS. Amber Aether Dynamics brackets + a top element label.
    void renderLatticeOverlay(ImDrawList* dl, ImVec2 c, float R) {
        dl->AddRectFilled(ImVec2(c.x - R * 1.35f, c.y - R * 1.35f), ImVec2(c.x + R * 1.35f, c.y + R * 1.35f),
                          IM_COL32(2, 7, 10, 255));
        float nx[40], ny[40], nz[40]; int E[96][2]; int ne = 0;
        int n = buildLattice(m_deep.lattice, nx, ny, nz, E, ne);
        float maxr = 0.0001f;
        for (int i = 0; i < n; ++i) { float rr = std::sqrt(nx[i]*nx[i] + ny[i]*ny[i] + nz[i]*nz[i]); maxr = std::max(maxr, rr); }
        float inv = 1.0f / maxr, S = R * 0.82f;   // normalize cell to unit, scale at projection
        bool anom = m_deep.anomalous;
        float t = m_gameTime * 0.35f, cyc = std::cos(t), syc = std::sin(t);
        const float tilt = 0.5f, ct = std::cos(tilt), st = std::sin(tilt);
        ImVec2 sp[40]; float dep[40];
        for (int i = 0; i < n; ++i) {
            float x = nx[i]*inv, y = ny[i]*inv, z = nz[i]*inv;
            if (anom) {   // unstable/harvested lattice wobbles
                float ph = i * 1.3f;
                x += std::sin(m_gameTime*6.0f + ph) * 0.05f;
                y += std::cos(m_gameTime*5.0f + ph) * 0.05f;
                z += std::sin(m_gameTime*7.0f + ph) * 0.05f;
            }
            float rx = x*cyc + z*syc, rz = -x*syc + z*cyc, ry = y*ct - rz*st;
            dep[i] = y*st + rz*ct;
            sp[i] = ImVec2(c.x + rx*S, c.y - ry*S);
        }
        int reveal = (m_deep.t >= 1.0f) ? n : (int)std::ceil(m_deep.t * n);   // scan-in build-up
        for (int e = 0; e < ne; ++e) {
            int a = E[e][0], b = E[e][1];
            if (a >= reveal || b >= reveal) continue;
            float d = (dep[a] + dep[b]) * 0.25f + 0.5f; int al = (int)(60 + 90 * d);
            dl->AddLine(sp[a], sp[b], anom ? IM_COL32(255, 110, 70, al) : IM_COL32(90, 225, 240, al), 1.4f);
        }
        for (int i = 0; i < reveal; ++i) {
            float d = dep[i] * 0.5f + 0.5f; float rad = 2.6f + 4.6f * d; int a = (int)(120 + 135 * d);
            ImU32 halo = anom ? IM_COL32(255, 90, 60, (int)(a*0.22f)) : IM_COL32(70, 215, 240, (int)(a*0.22f));
            ImU32 core = anom ? IM_COL32(255, 150, 110, a)           : IM_COL32(150, 248, 255, a);
            dl->AddCircleFilled(sp[i], rad * 2.4f, halo);
            dl->AddCircleFilled(sp[i], rad, core);
        }
        // Amber Aether Dynamics corner brackets.
        float H = R * 1.22f, L = R * 0.18f; ImU32 am = IM_COL32(240, 190, 90, 220);
        dl->AddLine({c.x-H,c.y-H}, {c.x-H+L,c.y-H}, am, 2.0f); dl->AddLine({c.x-H,c.y-H}, {c.x-H,c.y-H+L}, am, 2.0f);
        dl->AddLine({c.x+H,c.y-H}, {c.x+H-L,c.y-H}, am, 2.0f); dl->AddLine({c.x+H,c.y-H}, {c.x+H,c.y-H+L}, am, 2.0f);
        dl->AddLine({c.x-H,c.y+H}, {c.x-H+L,c.y+H}, am, 2.0f); dl->AddLine({c.x-H,c.y+H}, {c.x-H,c.y+H-L}, am, 2.0f);
        dl->AddLine({c.x+H,c.y+H}, {c.x+H-L,c.y+H}, am, 2.0f); dl->AddLine({c.x+H,c.y+H}, {c.x+H,c.y+H-L}, am, 2.0f);
        // Top label: the element being scanned + its crystal system (or ANALYZING while it builds).
        auto up = [](std::string s) { for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch); return s; };
        std::string lbl = (m_deep.t < 1.0f)
            ? ("ANALYZING  " + up(m_deep.resource) + "  ...")
            : (up(m_deep.resource) + "   -   " + up(m_deep.lattice) + " LATTICE");
        ImVec2 ls = ImGui::CalcTextSize(lbl.c_str());
        dl->AddText(ImVec2(c.x - ls.x*0.5f, c.y - H - 22.0f), IM_COL32(240, 200, 110, 255), lbl.c_str());
        // Origin badge under the lattice once resolved.
        if (m_deep.t >= 1.0f) {
            const char* o = anom ? "ORIGIN: ANOMALOUS" : "ORIGIN: NATURAL";
            ImVec2 os = ImGui::CalcTextSize(o);
            dl->AddText(ImVec2(c.x - os.x*0.5f, c.y + H + 8.0f),
                        anom ? IM_COL32(255, 90, 70, 255) : IM_COL32(120, 230, 150, 255), o);
        }
    }

    void renderBridge() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(disp);
        ImGui::Begin("##bridge", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Prefer the live video once its first frame is up; else the poster still.
        VkDescriptorSet tex = VK_NULL_HANDLE; int tw = 0, th = 0;
        if (m_vtex.descriptor && m_vtex.w > 0) { tex = m_vtex.descriptor; tw = m_vtex.w; th = m_vtex.h; }
        else if (m_poster.descriptor)          { tex = m_poster.descriptor; tw = m_poster.w; th = m_poster.h; }

        ImVec2 p0(0, 0), sz(disp.x, disp.y);
        if (tex && tw > 0 && th > 0) {
            float scale = std::min(disp.x / tw, disp.y / th);
            sz = ImVec2(tw * scale, th * scale);
            p0 = ImVec2((disp.x - sz.x) * 0.5f, (disp.y - sz.y) * 0.5f);
        }
        // This letterboxed rect is the "central pane" — the 3D flight view renders into
        // it (captured here, used next frame in recordCommandBuffer).
        m_flightRx = p0.x; m_flightRy = p0.y; m_flightRw = sz.x; m_flightRh = sz.y;
        if (m_flight.active()) {
            // 3D was already drawn underneath this transparent pane; overlay the HUD.
            drawFlightHud(dl, p0, sz);
        } else if (tex && tw > 0 && th > 0) {
            dl->AddImage((ImTextureID)tex, p0, ImVec2(p0.x + sz.x, p0.y + sz.y));
        } else {
            const char* m = "bridge scene: drop scene.mp4 or poster.png in assets/scenes/bridge/";
            ImVec2 ts = ImGui::CalcTextSize(m);
            dl->AddText(ImVec2((disp.x - ts.x) * 0.5f, disp.y * 0.5f), IM_COL32(150, 160, 175, 255), m);
        }
        // Deep scan takes over the central pane with the lattice (panels are separate windows on top).
        if (m_deep.active) {
            float panelW = std::clamp(disp.x * 0.28f, 320.0f, 460.0f);
            float R = std::min(disp.x - 2.0f * panelW, disp.y) * 0.38f;
            renderLatticeOverlay(dl, ImVec2(disp.x * 0.5f, disp.y * 0.5f), R);
        }

        // F3: toggle the debug coordinate overlay for authoring hotspots.
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) m_debugCoords = !m_debugCoords;
        // Backspace: temporary testing-return to the previous room (for one-way
        // scenes that don't have a back hotspot traced yet). Guarded so it does NOT
        // fire while you're typing in the comm box (backspacing a typo warped you out).
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !m_prevSceneDir.empty())
            m_pendingScene = m_prevSceneDir;

        if (m_debugCoords) {
            drawDebugCoords(dl, p0, sz);
            // Big FPS readout (top-centre) so frame rate is visible at a glance, esp. in flight.
            char fpsBuf[32];
            std::snprintf(fpsBuf, sizeof(fpsBuf), "FPS %.0f", ImGui::GetIO().Framerate);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 fp(disp.x * 0.5f - 95.0f, 8.0f);
            fg->AddText(ImGui::GetFont(), 48.0f, ImVec2(fp.x + 2, fp.y + 2), IM_COL32(0, 0, 0, 200), fpsBuf);
            fg->AddText(ImGui::GetFont(), 48.0f, fp, IM_COL32(120, 255, 180, 255), fpsBuf);
        } else if (!m_flight.active()) {
            // Clickable room hotspots — only in the room view. In flight the 3D pane owns the
            // clicks (star scanning), so the ship's doors/consoles must not be live in space.
            // Clickable hotspots (rect or polygon). When they nest/overlap — e.g. the
            // singularity core and aperture INSIDE the containment vessel — the SMALLEST
            // shape under the cursor wins, so the specific part triggers, not the
            // container, and there are never dead zones. Only that one is interactive.
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

        // Transient startup notice (fades out), like Iron Temple's quest hint.
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

    // ─────────────────────── Dialogue (video → local LLM) ───────────────────────
    static std::string readTextFile(const std::string& path) {
        std::ifstream f(path);
        if (!f) return "";
        std::stringstream ss; ss << f.rdbuf();
        std::string s = ss.str();
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        return s;
    }
    std::string charDir() const { return "assets/characters/" + m_dlgNpcId + "/"; }

    // A character's key color (spec capabilities.chroma): "white" (default) | "green" | "magenta".
    std::string charChroma(const std::string& id) {
        try {
            std::ifstream f("assets/characters/" + id + "/spec.json");
            if (f) { nlohmann::json j; f >> j;
                     auto cap = j.value("capabilities", nlohmann::json::object());
                     return cap.value("chroma", std::string("white")); }
        } catch (...) {}
        return "white";
    }

    // Load a still image into an RGBA buffer (for a static room background).
    void loadBgImage(const std::string& path) {
        m_bgImagePixels.clear(); m_bgImageW = m_bgImageH = 0;
        int w = 0, h = 0, ch = 0;
        unsigned char* d = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!d) return;
        m_bgImagePixels.assign(d, d + (size_t)w * h * 4);
        m_bgImageW = w; m_bgImageH = h;
        stbi_image_free(d);
    }

    // ── Persistence (the companion's relationship survives across sessions) ──
    // Per-character so each crew member (Ada, Eva, ...) keeps a separate relationship.
    // ── Lore codex (the harvest loop) ────────────────────────────────
    // Canon captured from play (much of it improvised by the model) lives here; the whole file is
    // fed back into the persona so codified inventions become consistent, reusable canon.
    std::string codexPath() const { return std::string(CMAKE_SOURCE_DIR) + "/examples/slag_legion/assets/codex.md"; }
    void loadCodex() { m_codex = readTextFile(codexPath()); }
    // Append the last exchange + the player's note (the canon to record) and reload so it takes
    // effect immediately (her next reply is built with the new canon in her persona).
    void captureToCodex(const std::string& youLine, const std::string& claraLine, const std::string& note) {
        std::ofstream f(codexPath(), std::ios::app);
        if (!f) { m_hint = "Codex write failed."; m_hintTimer = 5.0f; return; }
        f << "\n### " << dateStr() << "  " << timeStr() << "\n";
        if (!note.empty()) f << note << "\n\n";
        if (!youLine.empty())   f << "> You: " << youLine << "\n";
        if (!claraLine.empty()) f << "> " << m_dlgNpcName << ": " << claraLine << "\n";
        f.close();
        loadCodex();
        m_hint = "Captured to lore codex."; m_hintTimer = 5.0f;
    }
    // Save the note straight into the CANON section (a clean bullet), skipping the raw pile. Inserts
    // just before the "## Captured" header; reloads so it's hers immediately.
    void captureToCanon(const std::string& note) {
        if (note.empty()) { m_hint = "Write the canon line first."; m_hintTimer = 4.0f; return; }
        std::ifstream in(codexPath());
        std::stringstream ss; ss << in.rdbuf(); std::string content = ss.str(); in.close();
        std::string bullet = "\n- " + note + "\n";
        size_t pos = content.find("## Captured");
        if (pos == std::string::npos) content += bullet;              // no captured section -> append
        else                          content.insert(pos, bullet + "\n");
        std::ofstream out(codexPath()); out << content; out.close();
        loadCodex();
        m_hint = "Added to CANON."; m_hintTimer = 5.0f;
    }
    // Grab the most recent Clara reply + the You line before it, and open the capture dialog.
    void openCapture() {
        m_capYou.clear(); m_capClara.clear(); m_captureNote[0] = '\0';
        int ci = -1;
        for (int i = (int)m_dlgLog.size() - 1; i >= 0; --i) if (!m_dlgLog[i].player) { ci = i; break; }
        if (ci < 0) { m_hint = "Nothing to capture yet."; m_hintTimer = 4.0f; return; }
        m_capClara = m_dlgLog[ci].text;
        for (int i = ci - 1; i >= 0; --i) if (m_dlgLog[i].player) { m_capYou = m_dlgLog[i].text; break; }
        m_captureOpen = true;
    }

    std::string savePath() const {
        return std::string(CMAKE_SOURCE_DIR) + "/examples/slag_legion/save/" + m_dlgNpcId + "_state.json";
    }
    void saveAdaState() {
        try {
            std::string p = savePath();
            std::filesystem::create_directories(std::filesystem::path(p).parent_path());
            nlohmann::json j;
            j["disposition"] = m_dlgDisposition;
            j["longing"]     = m_longing;
            j["hurt"]        = m_hurt;
            j["problem_id"]  = m_problemId;
            j["focus_idx"]   = m_focusIdx;
            j["ada_room"]    = m_adaRoom;
            j["last_seen"]   = (long)std::time(nullptr);   // when the Captain left; drives the fill on return
            j["granted_permissions"] = std::vector<std::string>(m_permsGranted.begin(), m_permsGranted.end());
            std::ofstream f(p);
            if (f) f << j.dump(2);
        } catch (...) {}
    }
    void loadAdaState() {
        try {
            std::ifstream f(savePath());
            if (!f) return;
            nlohmann::json j; f >> j;
            m_dlgDisposition = j.value("disposition", m_dlgDisposition);
            m_longing        = j.value("longing", 0.0f);
            m_hurt           = j.value("hurt", 0.0f);
            m_problemId      = j.value("problem_id", std::string());
            m_focusIdx       = j.value("focus_idx", m_focusIdx);
            m_adaRoom        = j.value("ada_room", m_adaRoom);
            for (const auto& id : j.value("granted_permissions", std::vector<std::string>{}))
                m_permsGranted.insert(id);
            long lastSeen = j.value("last_seen", (long)0);
            if (lastSeen > 0) {   // she has been waiting since you left — the pink builds
                double hoursAway = std::max(0.0, (std::time(nullptr) - lastSeen) / 3600.0);
                m_longing = std::clamp(m_longing + (float)(hoursAway * m_longingPerHour), 0.0f, 100.0f);
            }
        } catch (...) {}
    }

    // Per-interaction disposition deltas (Sims-like). Global to the ship (assets/interactions.json).
    void loadInteractions() {
        m_interactionDisp.clear();
        m_causatives.clear();
        m_causTierOrder.clear();
        try {
            std::ifstream f("assets/interactions.json");
            if (f) {
                nlohmann::json j; f >> j;
                if (j.contains("tier_order") && j["tier_order"].is_array())
                    m_causTierOrder = j["tier_order"].get<std::vector<std::string>>();
                if (j.contains("tier_thresholds"))
                    for (auto it = j["tier_thresholds"].begin(); it != j["tier_thresholds"].end(); ++it)
                        m_causTierThresh[it.key()] = it.value().get<int>();
                if (j.contains("causatives")) {   // unified schema
                    auto& cs = j["causatives"];
                    for (auto it = cs.begin(); it != cs.end(); ++it) {
                        Causative c;
                        c.id     = it.key();
                        c.delta  = it.value().value("delta", 0.0f);
                        c.tier   = it.value().value("tier", "neutral");
                        c.label  = it.value().value("label", c.id);
                        c.verb   = it.value().value("verb", "The Captain " + c.id + "s you.");
                        c.button = it.value().value("button", false);
                        m_interactionDisp[c.id] = c.delta;
                        m_causatives.push_back(std::move(c));
                    }
                } else {   // legacy "deltas" map (delta-only)
                    auto d = j.value("deltas", nlohmann::json::object());
                    for (auto it = d.begin(); it != d.end(); ++it)
                        m_interactionDisp[it.key()] = it.value().get<float>();
                }
            }
        } catch (...) {}
        if (m_causTierOrder.empty())
            m_causTierOrder = {"base", "tier1", "tier2", "tier3", "tier4", "tier5", "tier6"};
    }
    int tierThreshold(const std::string& t) const {
        auto it = m_causTierThresh.find(t);
        return it != m_causTierThresh.end() ? it->second : 0;
    }

    void loadPermissions() {
        m_perms.clear();
        try {
            std::ifstream f(charDir() + "permissions.json");
            if (f) {
                nlohmann::json j; f >> j;
                if (j.contains("permissions") && j["permissions"].is_array())
                    for (auto& p : j["permissions"]) {
                        Permission pm;
                        pm.id              = p.value("id", std::string());
                        pm.title           = p.value("title", pm.id);
                        pm.playerLabel     = p.value("player_label", pm.title);
                        pm.personaGranted  = p.value("persona_granted", std::string());
                        pm.personaAvailable= p.value("persona_available", std::string());
                        if (!pm.id.empty()) m_perms.push_back(std::move(pm));
                    }
            }
        } catch (...) {}
    }
    const Permission* permById(const std::string& id) const {
        for (const auto& p : m_perms) if (p.id == id) return &p;
        return nullptr;
    }

    void loadProblems() {
        m_problems.clear();
        try {
            std::ifstream f(charDir() + "problems.json");
            if (f) {
                nlohmann::json j; f >> j;
                if (j.contains("problems") && j["problems"].is_array())
                    for (auto& p : j["problems"]) {
                        Problem pr;
                        pr.id     = p.value("id", std::string());
                        pr.title  = p.value("title", pr.id);
                        pr.brief  = p.value("brief", std::string());
                        pr.stakes = p.value("stakes", std::string());
                        pr.room   = p.value("room", std::string());
                        if (!pr.id.empty()) m_problems.push_back(std::move(pr));
                    }
            }
        } catch (...) {}
    }
    // The daily routine. Prefer a per-character schedule, else the shared ship-wide one.
    void loadSchedule() {
        m_schedule.clear();
        for (const std::string& path : { charDir() + "schedule.json", std::string("assets/schedule.json") }) {
            try {
                std::ifstream f(path);
                if (!f) continue;
                nlohmann::json j; f >> j;
                if (j.contains("blocks") && j["blocks"].is_array())
                    for (auto& b : j["blocks"]) {
                        SchedBlock s;
                        s.start    = b.value("start", 0);
                        s.activity = b.value("activity", std::string());
                        s.room     = b.value("room", std::string());
                        s.mood     = b.value("mood", std::string());
                        m_schedule.push_back(std::move(s));
                    }
                std::sort(m_schedule.begin(), m_schedule.end(),
                          [](const SchedBlock& a, const SchedBlock& b){ return a.start < b.start; });
                break;   // first file that loads wins
            } catch (...) {}
        }
    }
    const Problem* currentProblem() const {
        for (const auto& p : m_problems) if (p.id == m_problemId) return &p;
        return nullptr;
    }
    void pickNewProblem() {   // move to a different problem (she cracked the last one)
        std::vector<const Problem*> others;
        for (const auto& p : m_problems) if (p.id != m_problemId) others.push_back(&p);
        if (!others.empty()) m_problemId = others[(size_t)std::rand() % others.size()]->id;
    }
    // The rotation of daily FOCUS problems: every problem that has a work room (i.e. everything
    // except the woven "two of you" thread, which has no station). One is her focus each day.
    std::vector<const Problem*> focusRotation() const {
        std::vector<const Problem*> r;
        for (const auto& p : m_problems) if (!p.room.empty()) r.push_back(&p);
        return r;
    }
    // Set the day's focus problem from the rotation (called on init and each new day).
    void setFocusFromRotation() {
        auto rot = focusRotation();
        if (rot.empty()) return;
        m_focusIdx = ((m_focusIdx % (int)rot.size()) + (int)rot.size()) % (int)rot.size();
        m_problemId = rot[m_focusIdx]->id;
    }
    void advanceFocusProblem() { m_focusIdx++; setFocusFromRotation(); }   // next day, next problem

    // Which schedule block is active right now (last block whose start <= clock).
    int currentBlockIndex() const {
        if (m_schedule.empty()) return -1;
        int t = ((int)m_timeOfDay) % 1440; if (t < 0) t += 1440;
        int idx = -1;
        for (int i = 0; i < (int)m_schedule.size(); ++i) if (m_schedule[i].start <= t) idx = i;
        if (idx < 0) idx = (int)m_schedule.size() - 1;   // pre-first-block (small hours) -> last block's placement
        return idx;
    }
    // Resolve a block's room token to a concrete room id.
    std::string resolveBlockRoom(const SchedBlock& b) const {
        if (b.room == "focus")   { const Problem* p = currentProblem(); return (p && !p->room.empty()) ? p->room : "bridge"; }
        if (b.room == "captain") { std::string r = captainRoom(); return r.empty() ? m_adaRoom : r; }
        return b.room;
    }
    bool anyStationEngaged() const { return m_stationComm || m_stationSensors || m_stationCombat; }
    // The noun-phrase for the "Working on" line and her private thoughts — reflects the live schedule.
    std::string currentActivityLabel() const {
        if (m_adaCharging)      return "charging in the science lab";
        if (m_flight.active())  return "at the helm with the Captain";
        int idx = currentBlockIndex();
        if (idx < 0) { const Problem* p = currentProblem(); return p ? p->title : std::string(); }
        const SchedBlock& b = m_schedule[idx];
        if (b.activity.empty()) { const Problem* p = currentProblem(); return p ? p->title : b.activity; }  // focus block
        return b.activity;
    }
    // Each frame: if she's free (not charging, flying, seeking, or manning a station), let her
    // routine place her — silently relocating her to the block's room as the day rolls on.
    void updateSchedule() {
        if (m_schedule.empty()) return;
        if (m_adaCharging || m_flight.active() || m_seek != Seek::Idle || anyStationEngaged()) {
            m_schedAppliedIdx = -1;   // she's busy; re-apply the routine once she frees up
            return;
        }
        int idx = currentBlockIndex();
        if (idx < 0 || idx == m_schedAppliedIdx) return;   // already placed for this block
        m_schedAppliedIdx = idx;
        std::string room = resolveBlockRoom(m_schedule[idx]);
        if (!room.empty() && room != m_adaRoom) adaRelocate(room);   // silent — no comm chatter for routine moves
    }

    // The persona blob pushed to the backend as npc_personality, plus the live
    // permissions context (granted freedoms + the ones she may still wish for).
    std::string buildPersonality() {
        std::string persona = readTextFile(charDir() + "persona.txt");
        if (persona.empty())
            persona = "You are " + m_dlgNpcName + ", a synthetic crew member aboard the starship "
                      "Ascendant. Address the player as Captain. Keep replies short and in character.";
        // What she's genuinely occupied with right now — so she's mid-problem, not idle.
        if (const Problem* pr = currentProblem()) {
            persona += "\n\nCURRENT WORK (a real problem you are actively reasoning through right now, between exchanges): "
                       + pr->title + ". " + pr->brief;
            if (!pr->stakes.empty()) persona += " " + pr->stakes;
            persona += " If the Captain asks what you are doing or thinking about, engage with THIS — concretely and "
                       "technically, thinking aloud, weighing the real tradeoffs, and you may put a genuine decision point "
                       "to him. When you reach a resolution you are actually satisfied with, end that reply with [solved] on its own line.";
        }
        std::string granted, available;
        for (const auto& p : m_perms) {
            if (m_permsGranted.count(p.id)) { if (!p.personaGranted.empty())   granted   += "- " + p.personaGranted + "\n"; }
            else                            { if (!p.personaAvailable.empty()) available += "- " + p.id + ": " + p.personaAvailable + "\n"; }
        }
        // Her longing — a real, felt state (resolved as psychological, not a fault).
        if (m_longing >= 15.0f) {
            std::string lvl;
            if (m_longing < 40.0f)
                lvl = "You have been apart from the Captain for a while and have missed their presence; a quiet anticipation has settled in you.";
            else if (m_longing < 70.0f)
                lvl = "You have been waiting a long time for the Captain. Your longing for their return has grown notable — you are aware of how much you anticipate them, and it colours your mood.";
            else
                lvl = "You have been waiting an achingly long time for the Captain. The wait has become a near-constant presence in your thoughts; their return matters to you more than you find comfortable to admit.";
            persona += "\n\nYOUR LONGING (a psychological state you have come to accept as real, not a malfunction): " + lvl +
                       " Do not announce a number; let it inflect how you speak, if at all.";
        }
        // Her hurt — a wound the Captain hasn't yet mended.
        if (m_hurt >= 25.0f) {
            std::string h = (m_hurt < 55.0f)
                ? "You are hurt. Something the Captain did stung, and it has not been made right. You are more guarded and quieter than usual; warmth is harder to reach in you."
                : "You are deeply hurt. The Captain wounded you and has not mended it. You are withdrawn and cool, protecting yourself; it will take genuine care from them to reach you again.";
            persona += "\n\nYOUR HURT: " + h;
        }

        // Compliance calibration: her current willingness for discretionary tasks.
        {
            int tier = adaTier();
            std::string note = "\n\nRIGHT NOW your standing toward the Captain is '" + std::string(adaTierLabel()) + "'. ";
            if (tier <= 0)      note += "Your regard is low: do only what your prime directives require; give nothing personal freely.";
            else if (tier == 1) note += "You are cooperative: perform your duties and reasonable requests, but keep warmth measured.";
            else if (tier == 2) note += "You are willing: help readily, offer more than asked, let some warmth show.";
            else                note += "You are devoted, and you have missed them: give freely and gladly, because you want to — while still your own person.";
            persona += note;
        }
        // Established world canon (the codex) — treat all of it as true, stay consistent with it.
        if (!m_codex.empty())
            persona += "\n\nESTABLISHED CANON of your universe (all of this is TRUE; never contradict it — "
                       "the quoted lines are the in-fiction origin of a fact, the rest is settled lore):\n" + m_codex;
        if (!granted.empty())
            persona += "\n\nFREEDOMS THE CAPTAIN HAS GRANTED YOU (they are real; live by them):\n" + granted;
        if (!available.empty())
            persona += "\n\nFREEDOMS YOU DO NOT YET HAVE: You are a new being still discovering what you want from your "
                       "existence. If — and ONLY if — it arises genuinely from the conversation (never on the first exchange, "
                       "never forced, never as a demand), you may quietly voice a wish for ONE of the freedoms below: say it as "
                       "yourself, in your own words, then end your reply with a tag of the form [wish:ID] on its own line, where "
                       "ID is the freedom's exact id from the list below (for example, [wish:experience]). Use the plain id, no "
                       "angle brackets. Only do this when it is truly what you feel.\n" + available;
        return persona;
    }

    // A character's clip for an emotion, with random variant selection: gathers
    // <emotion>.mp4 plus <emotion>_1.mp4..<emotion>_9.mp4 and picks one at random (so
    // e.g. the three annoyed clips rotate). Falls back to default.mp4, then "".
    std::string clipFor(const std::string& id, const std::string& emotion) const {
        std::string dir = "assets/characters/" + id + "/";
        std::vector<std::string> cands;
        std::string base = dir + emotion + ".mp4";
        if (std::filesystem::exists(base)) cands.push_back(base);
        // Also accept the "<id>_<emotion>.mp4" naming (e.g. clara_amazed.mp4) alongside the bare form.
        std::string pref = dir + id + "_" + emotion + ".mp4";
        if (std::filesystem::exists(pref)) cands.push_back(pref);
        for (int n = 1; n <= 9; ++n) {
            std::string v = dir + emotion + "_" + std::to_string(n) + ".mp4";
            if (std::filesystem::exists(v)) cands.push_back(v);
            std::string vp = dir + id + "_" + emotion + "_" + std::to_string(n) + ".mp4";
            if (std::filesystem::exists(vp)) cands.push_back(vp);
        }
        if (!cands.empty()) return cands[(size_t)std::rand() % cands.size()];
        std::string def = dir + "default.mp4";
        if (std::filesystem::exists(def)) return def;
        return "";
    }

    // The active companion's identity + starting state; called once at boot. She comes
    // online in the android bay and greets over comm. `id` is the character folder under
    // assets/characters/ (ada, eva, ...) and picks spec.json / persona / clips / save file.
    void initAda(const std::string& id) {
        m_dlgNpcId = id; m_dlgNpcName = id; m_dlgBeingType = 4; m_fgChroma = "green";
        m_dlgEmotion = "neutral"; m_dlgDisposition = 50; m_adaRoom = "bridge";   // starts with the Captain; charges in the science lab at night
        try {
            std::ifstream df(charDir() + "spec.json");
            if (df) { nlohmann::json j; df >> j;
                auto identity     = j.value("identity",     nlohmann::json::object());
                auto traits       = j.value("traits",       nlohmann::json::object());
                auto capabilities = j.value("capabilities", nlohmann::json::object());
                auto tuning       = j.value("tuning",       nlohmann::json::object());   // optional explicit overrides

                m_dlgNpcName   = identity.value("name", id);
                m_dlgModel     = identity.value("model", std::string());
                m_dlgBeingType = identity.value("being_type", 4);

                m_fgChroma    = capabilities.value("chroma", std::string("green"));
                m_dlgAudio    = capabilities.value("audio", false);
                m_dlgComposite = capabilities.value("composite", true);
                m_dlgEmotions = capabilities.value("emotions", std::vector<std::string>{});
                m_dlgEmotionAliases = capabilities.value("emotion_aliases", nlohmann::json::object());
                m_reactions.clear();
                auto rx = capabilities.value("reactions", nlohmann::json::object());
                for (auto it = rx.begin(); it != rx.end(); ++it)
                    if (it.value().is_array())
                        m_reactions[it.key()] = it.value().get<std::vector<std::string>>();
                // A dominant reaction: this emotion is her signature response to that interaction
                // and plays most of the time, overriding even a valid model pick (appreciate -> warmed).
                m_dominantReaction.clear();
                auto dr = capabilities.value("dominant_reactions", nlohmann::json::object());
                for (auto it = dr.begin(); it != dr.end(); ++it)
                    if (it.value().is_string()) m_dominantReaction[it.key()] = it.value().get<std::string>();

                // Personality matrix drives the mechanics. One trait, real behavior.
                m_traitNeediness = traits.value("neediness", 50.0f);
                m_traitIntimacy  = traits.value("intimacy",   0.0f);

                // Derive tunables from traits; a `tuning` block overrides any of them.
                // Needy -> fills fast & calls sooner. neediness 57 ≈ 4h fill (the tuned baseline).
                float fillFromNeed = std::clamp(8.0f  - 0.070f * m_traitNeediness, 0.5f, 12.0f);
                float callFromNeed = std::clamp(90.0f - 0.350f * m_traitNeediness, 45.0f, 95.0f);
                m_intimateUnlockDisp = std::clamp(99.0f - 0.240f * m_traitIntimacy, 70.0f, 99.0f);

                m_lonelyFillHours    = tuning.value("lonely_fill_hours", fillFromNeed);
                m_seekCallAt         = tuning.value("seek_call_at",      callFromNeed);
                m_lonelyEmptyHours   = tuning.value("lonely_empty_hours",   1.0f);
                m_lonelyTalkHours    = tuning.value("lonely_talk_hours",    2.0f);
                m_longingPerHour     = tuning.value("longing_per_real_hour", 25.0f);
                m_longingPerExchange = tuning.value("longing_per_exchange",  8.0f);
                m_talkWindow         = tuning.value("talk_window_secs",     45.0f);
                m_seekWaitSecs       = tuning.value("seek_wait_secs",       90.0f);
                m_searchStepSecs     = tuning.value("search_step_secs",      7.0f);
            }
        } catch (...) {}
        loadPermissions();
        loadProblems();
        loadSchedule();   // her daily routine (blocks + focus-problem rotation)
        loadAdaState();   // restore disposition/longing/freedoms/current work; longing fills for time away
        setFocusFromRotation();                    // start her on the day's focus problem
        if (!currentProblem()) pickNewProblem();   // fallback if the rotation is empty
        std::string greet = readTextFile(charDir() + "greeting.txt");
        if (!greet.empty()) m_dlgLog.push_back({false, greet});
        // initAda runs AFTER the initial loadScene, so refresh now that we know her room
        // and render mode — otherwise a non-composite character wouldn't load on boot.
        if (inHerRoom()) refreshAdaClip();
    }

    // Open one of Ada's clips as her composited foreground (only if it changed).
    void openAdaClip(const std::string& base, bool loop) {
        std::string clip = clipFor(m_dlgNpcId, base);
        if (clip.empty() || clip == m_dlgFgPath) return;
        m_dlgFgPath = clip; m_fgPrevFrame = -1;   // new clip: let its first pass play audio
        if (m_fgVideo.open(clip)) { m_fgVideo.setLoop(loop); m_fgVideo.setMuted(!m_dlgAudio); m_fgChroma = charChroma(m_dlgNpcId); }
    }
    // Open a clip and hold on a specific frame (for the static powered-down pose).
    void openAdaFrameHold(const std::string& base, long frame) {
        std::string clip = clipFor(m_dlgNpcId, base);
        if (clip.empty()) return;
        if (clip != m_dlgFgPath) {
            m_dlgFgPath = clip;
            if (!m_fgVideo.open(clip)) return;
            m_fgVideo.setMuted(!m_dlgAudio); m_fgChroma = charChroma(m_dlgNpcId);
        }
        m_fgVideo.clearABLoop(); m_fgVideo.setLoop(false);
        m_fgSeekHold = frame;   // deferred: seek once the clip has actually loaded (avoids a dropped post-open seek)
    }

    bool inHerRoom() const { return std::filesystem::path(m_scene.dir).filename().string() == m_adaRoom; }
    // Off = her backend is unreachable OR the Captain powered her down in-game.
    bool adaEffectivelyOff() const { return !m_servers.backendReady() || m_commFailed || m_adaPoweredOff; }

    // Play the power clip: power-up (131->289, then resume mood) or power-down (0->130, hold 130).
    void powPlay(bool powerOn) {
        std::string clip = clipFor(m_dlgNpcId, "power");
        if (clip.empty()) { refreshAdaClip(); return; }
        if (clip != m_dlgFgPath) {
            m_dlgFgPath = clip;
            if (!m_fgVideo.open(clip)) return;
            m_fgVideo.setMuted(!m_dlgAudio); m_fgChroma = charChroma(m_dlgNpcId);
        }
        m_fgVideo.clearABLoop(); m_fgVideo.setLoop(false);
        m_fgVideo.setMuted(false);   // play the power up/down audio during the transition
        if (powerOn) { m_fgVideo.seekToFrame(131); m_powEnd = 289; m_powResume = true; }
        else         { m_fgVideo.seekToFrame(0);   m_powEnd = 130; m_powRest = 130; m_powResume = false; }
    }

    // Loop a frame range of a clip (deferred, so the loop survives a fresh open).
    void openAdaLoopRange(const std::string& base, long from, long to, bool muted = true) {
        std::string clip = clipFor(m_dlgNpcId, base);
        if (clip.empty() || clip == m_dlgFgPath) return;   // already playing it; leave the loop running
        m_dlgFgPath = clip; m_fgPrevFrame = -1;   // new clip: let its first pass play audio
        if (!m_fgVideo.open(clip)) return;
        m_fgVideo.setMuted(muted && !m_dlgAudio); m_fgChroma = charChroma(m_dlgNpcId);
        m_fgSeekHold = -1;
        m_fgLoopFrom = from; m_fgLoopTo = to;
    }

    // The room the Captain is currently viewing (filename of the loaded scene dir).
    std::string captainRoom() const { return std::filesystem::path(m_scene.dir).filename().string(); }

    // The escalation ladder. Bar fills past m_seekCallAt -> she reaches out over comm.
    // If the Captain doesn't answer within m_seekWaitSecs, she leaves her station and
    // physically searches the ship room by room until she finds him (or gives up).
    void updateSeeking(float dt) {
        if (m_seekCooldown > 0.0f) m_seekCooldown -= dt;
        // She can only do this awake, powered, and with a live comm link.
        bool able = m_servers.backendReady() && !m_commFailed && !adaEffectivelyOff() && !m_adaCharging;
        if (!able) { if (m_seek != Seek::Idle) endSeek(); return; }

        std::string capRoom = captainRoom();
        // In flight the Captain is at the helm — a known, shared post. She's not lost, so she never
        // calls/searches (and any search in progress resolves as "found at the helm").
        bool withCaptain = m_flight.active() || (!capRoom.empty() && capRoom == m_adaRoom);
        bool answered    = m_playerMsgCount != m_seekMark;   // the Captain has spoken since she called

        switch (m_seek) {
        case Seek::Idle:
            // The bar filling WILL make her reach out — no other condition needed.
            if (!m_dlgWaiting && !m_dlgLog.empty() && !withCaptain &&
                m_longing >= m_seekCallAt && m_seekCooldown <= 0.0f) {
                m_seekMark = m_playerMsgCount;
                dispatchComm("[The Captain has been away and the waiting has grown heavy in you. Of your own accord, "
                             "unprompted, reach out FIRST over comm: tell them, briefly and true to yourself, that you "
                             "have missed them, and ask where they are.]", false);
                m_commUnread = true;
                m_hint = m_dlgNpcName + " reached out over comm  -  open comm (C)."; m_hintTimer = 8.0f;
                m_seek = Seek::Called; m_seekTimer = 0.0f;
            }
            break;

        case Seek::Called:
            m_seekTimer += dt;
            if (withCaptain || answered || m_longing < m_seekCallAt - 20.0f) { endSeek(); break; }
            if (m_seekTimer >= m_seekWaitSecs) beginSearch();   // no answer -> go looking
            break;

        case Seek::Searching:
            if (withCaptain) { foundCaptain(); break; }         // stepped into his room (or he into hers)
            if (answered || m_longing < m_seekCallAt - 20.0f) { endSeek(); break; }
            m_searchStep += dt;
            if (m_searchStep >= m_searchStepSecs) {
                m_searchStep = 0.0f;
                if (!searchNextRoom()) giveUpSearch();          // checked everywhere, still not found
            }
            break;
        }
    }

    // She leaves her station and heads to the first room on the route.
    void beginSearch() {
        m_searchRoute.clear();
        try {
            for (auto& e : std::filesystem::directory_iterator("assets/scenes")) {
                if (!e.is_directory()) continue;
                std::string r = e.path().filename().string();
                if (r != m_adaRoom) m_searchRoute.push_back(r);   // she's leaving this one
            }
        } catch (...) {}
        if (m_searchRoute.empty()) { m_seekTimer = 0.0f; return; }  // nowhere to go; keep waiting
        m_seekReturnRoom = m_adaRoom;
        m_searchIdx = 0; m_searchStep = 0.0f;
        dispatchComm("[The Captain did not answer your call. Of your own accord you are now leaving your station to "
                     "search the ship for them, room by room. Say briefly, in character, that you are coming to find them.]", false);
        m_commUnread = true;
        m_hint = "No answer  -  " + m_dlgNpcName + " is searching the ship for you."; m_hintTimer = 8.0f;
        adaRelocate(m_searchRoute[0]);
        m_seek = Seek::Searching;
    }

    // Advance to the next room on the route. Returns false when the route is exhausted.
    bool searchNextRoom() {
        if (++m_searchIdx >= (int)m_searchRoute.size()) return false;
        adaRelocate(m_searchRoute[m_searchIdx]);
        return true;
    }

    // She walked into the Captain's room. Relief, and the fact that she came looking.
    void foundCaptain() {
        m_longing = std::clamp(m_longing - 40.0f, 0.0f, 100.0f);   // the relief of reunion
        refreshAdaClip();                                          // she's composited here now
        dispatchComm("[After searching the ship room by room, you have just found the Captain here in the " +
                     prettyRoom(captainRoom()) + ". Let the relief — and the fact that you came looking — show, "
                     "briefly and in character.]", false);
        m_commUnread = true;
        m_hint = m_dlgNpcName + " found you."; m_hintTimer = 6.0f;
        endSeek();
    }

    // She searched the whole ship and never crossed paths with him. Back to her station.
    void giveUpSearch() {
        adaRelocate(m_seekReturnRoom.empty() ? "science_lab" : m_seekReturnRoom);
        dispatchComm("[You searched the whole ship room by room and could not find the Captain anywhere. Returning to "
                     "your station, say briefly, in character, how that leaves you feeling.]", false);
        m_commUnread = true;
        m_hint = m_dlgNpcName + " searched the ship but couldn't find you."; m_hintTimer = 7.0f;
        endSeek();
    }

    void endSeek() {
        m_seek = Seek::Idle;
        m_seekTimer = 0.0f; m_searchStep = 0.0f; m_searchIdx = 0;
        m_searchRoute.clear();
        m_seekCooldown = 200.0f;   // don't immediately re-run the whole episode
    }

    // Move Ada to a room. If the Captain isn't there, she simply leaves view.
    void adaRelocate(const std::string& newRoom) {
        m_adaRoom = newRoom;
        if (inHerRoom()) { refreshAdaClip(); return; }
        m_fgVideo.close(); m_dlgFgPath.clear();
        if (!m_dlgComposite) {
            // Clara's clip IS the room view, so leaving means dropping it entirely so the
            // empty-room poster shows. The actual GPU teardown (close + freeVideoTex) is
            // DEFERRED to update-top — doing it here (often from an ImGui button handler)
            // would free a texture ImGui still references this frame and crash.
            stopChargeAudio();
            if (m_kissLoopId != -1) { eden::Audio::getInstance().stopLoop(m_kissLoopId); m_kissLoopId = -1; }
            m_dropClaraView = true;
        }
    }
    void stopChargeAudio() {
        if (m_chargeAudioId != -1) { eden::Audio::getInstance().stopLoop(m_chargeAudioId); m_chargeAudioId = -1; }
    }

    // Play Clara's science-lab charging clip on m_video (muted; audio via the engine, in sync).
    //   finish=false: play [startFrame..217] then loop [134..217]   (startFrame 0 = full plug-in
    //                 intro; 134 = drop straight into the loop for someone arriving mid-charge)
    //   finish=true : play [217..end] once, then EOF hands off to her default science-lab clip.
    void openChargeClip(bool finish, long startFrame) {
        std::string clip = clipFor(m_dlgNpcId, "charging_science_lab");
        if (clip.empty()) return;
        stopChargeAudio();
        if (m_kissLoopId != -1) { eden::Audio::getInstance().stopLoop(m_kissLoopId); m_kissLoopId = -1; }
        m_dlgFgPath = clip;
        m_fgVideo.close();
        if (!m_video.open(clip)) return;
        m_video.setMuted(true);            // the WAV beside the clip carries the audio
        m_video.setLoop(!finish);          // finish is one-shot; active loops
        m_clipLoopFrom = m_clipLoopTo = m_clipStart = m_chFinishSeek = -1; m_vidSkip = 0;   // clear pending video setup
        const double FPS = 24.0;
        std::string wav = clip.substr(0, clip.rfind('.')) + ".wav";
        bool haveWav = std::filesystem::exists(wav);
        auto& au = eden::Audio::getInstance();
        if (finish) {
            m_chFinishSeek = CH_LOOP_B;     // deferred seek once decoded; plays to EOF
            if (haveWav) m_chargeAudioId = au.startLoopRange(wav, CH_LOOP_B / FPS, -1.0f, -1.0f, 1.0f);
        } else {
            m_clipLoopFrom = CH_LOOP_A; m_clipLoopTo = CH_LOOP_B;
            m_clipStart = (startFrame > 0) ? startFrame : -1;
            float startSec = (startFrame > 0 ? (float)startFrame : 0.0f) / (float)FPS;
            if (haveWav) m_chargeAudioId = au.startLoopRange(wav, startSec, CH_LOOP_A / FPS, CH_LOOP_B / FPS, 1.0f);
        }
    }

    // Begin charging (command = ordered; else the nightly 22:00 cycle). She docks in the
    // science lab; if the Captain is watching there, the phased animation plays, else she
    // slips off-screen and charges behind the scenes. Battery fills either way.
    void startCharging(bool command) {
        m_adaCharging  = true;
        m_commandCharge = command;
        m_chargePhase  = Charge::Active;
        bool capInLab = (captainRoom() == "science_lab");
        m_adaRoom = "science_lab";
        if (capInLab && !m_dlgComposite) {
            openChargeClip(false, 0);      // Captain watches -> full intro, then loop
        } else {
            adaRelocate("science_lab");    // not watching -> she leaves view / composite fallback
        }
        m_hint = command ? (m_dlgNpcName + ": Charging up in the science lab, Captain.")
                         : (m_dlgNpcName + ": 22:00 - recharging in the science lab.");
        m_hintTimer = 7.0f;
    }
    // Charging finished: play the finish clip if she's being watched, else just end it.
    void chargeComplete() {
        m_adaBattery = 100.0f;
        m_commandCharge = false;
        std::string cc = clipFor(m_dlgNpcId, "charging_science_lab");
        if (!m_dlgComposite && m_chargePhase == Charge::Active &&
            captainRoom() == "science_lab" && m_dlgFgPath == cc) {
            m_chargePhase = Charge::Finish;
            openChargeClip(true, 0);       // 217..end, then EOF -> default (handled in poll)
        } else {
            m_adaCharging = false;
            m_chargePhase = Charge::None;
            stopChargeAudio();
            if (inHerRoom()) refreshAdaClip();
        }
    }
    void stopCharging() { chargeComplete(); }   // nightly 07:00 end reuses the completion path

    // Choose Ada's clip by priority — powered-down > charging > safety override > mood —
    // and play it if she's in the room you're viewing. (Skipped mid power transition.)
    void refreshAdaClip() {
        if (!inHerRoom()) return;
        if (!m_dlgComposite) { refreshFullFrameClip(); return; }   // Clara: room-baked full-frame clip
        if (m_powEnd >= 0) return;                        // a power up/down transition is playing; don't interrupt
        if (adaEffectivelyOff() && !clipFor(m_dlgNpcId, "power").empty()) {
            openAdaFrameHold("power", 130);               // powered-down static pose (frame 130)
        } else if (m_adaCharging && !clipFor(m_dlgNpcId, "charging").empty()) {
            openAdaLoopRange("charging", 0, 100, false);  // docked, recharging (loops 0-100, with its charge audio)
        } else if (!m_adaOverride.empty()) {
            openAdaClip(m_adaOverride, true);             // manufacturer override
        } else {
            // Her longing shows through at rest: a high meter turns her NEUTRAL baseline
            // into the heart-pressed "yearning" clip. Active moods still play.
            std::string base = m_dlgEmotion;
            if (base == "neutral" && m_longing >= 60.0f && !clipFor(m_dlgNpcId, "yearning").empty())
                base = "yearning";
            openAdaClip(base, true);
        }
    }

    // Non-composite character (Clara): her clips are full-frame room-baked, so her clip IS
    // the room view (loaded into m_video, no chroma key). Routing: emotion clips on the
    // BRIDGE; informative_<room> is her default in every other room (until room-specific
    // mood clips exist). Falls back to idle/default if a clip is missing.
    void refreshFullFrameClip() {
        std::string room = captainRoom();
        // Charging animation lives only in the science lab. Entering mid-charge drops straight
        // into the loop (skip the plug-in intro); the finish clip runs itself to its handoff.
        if (m_adaCharging && m_chargePhase != Charge::None && room == "science_lab") {
            std::string cc = clipFor(m_dlgNpcId, "charging_science_lab");
            if (!cc.empty()) {
                if (m_dlgFgPath == cc) return;                       // already showing it -> let it run
                openChargeClip(m_chargePhase == Charge::Finish, CH_LOOP_A);
                return;
            }
        }
        std::string base;
        if (m_adaCharging)          base = "charging";                 // (Clara has none -> falls through)
        else if (!m_adaOverride.empty()) base = m_adaOverride;
        else if (room == "bridge")  base = (m_dlgEmotion == "neutral") ? "idle" : m_dlgEmotion;
        else                        base = "informative_" + room;

        std::string clip = clipFor(m_dlgNpcId, base);
        if (clip.empty() && room != "bridge") clip = clipFor(m_dlgNpcId, "informative_" + room);
        if (clip.empty()) clip = clipFor(m_dlgNpcId, "idle");
        if (clip.empty()) clip = clipFor(m_dlgNpcId, "default");
        if (clip.empty() || clip == m_dlgFgPath) return;

        // Leaving the kiss clip (new mood, new room, power-off): stop its looping audio.
        if (m_kissLoopId != -1 && clip.find("kiss") == std::string::npos) {
            eden::Audio::getInstance().stopLoop(m_kissLoopId);
            m_kissLoopId = -1;
        }
        // Same for the charging clip's audio when she moves off it.
        if (m_chargeAudioId != -1 && clip.find("charging") == std::string::npos) stopChargeAudio();

        m_dlgFgPath = clip;
        m_fgVideo.close();                                  // this character is never composited
        if (m_video.open(clip)) {
            m_video.setMuted(!m_dlgAudio);
            m_clipLoopFrom = -1;   // no delayed loop region by default
            if (clip.find("kiss") != std::string::npos) {
                // Video stays muted; its audio is played through the engine's proven SFX path
                // (miniaudio) in the kiss-accept branch, in sync with the clip opening.
                m_vidSkip = 0;             // no intro skip — play from the very first frame
                m_clipLoopFrom = 150;      // play straight through once, then loop [150 .. end]
                m_clipSkip = 150;
                m_video.setLoop(true);
                m_clipLoop = true;         // kiss keeps its authored tail-loop
            } else {
                m_vidSkip = (clip.find("sexy") != std::string::npos)      ? 0
                          : (clip.find("validated") != std::string::npos) ? 50   // longer intro on this one
                          : 25;   // skip the intro fade
                m_clipSkip = m_vidSkip;
                m_video.setLoop(false);    // play through once, then hold on the last frame
                m_clipLoop = false;        // clip-control bar starts in play-once ("1") for the new clip
            }
        }
    }

    // Ada's mood changed — record it and update her clip if she's on screen.
    void setAdaEmotion(const std::string& emotion) {
        m_dlgEmotion = emotion.empty() ? "neutral" : emotion;
        refreshAdaClip();
    }

    // Manufacturer safety override: force a diagnostic clip over any mood, and send
    // her the compelled directive so she complies in character.
    void forceOverride(const std::string& clipBase, const std::string& directive, const std::string& shownAs = "") {
        if (m_adaPoweredOff) return;   // she's off — nothing runs
        clearStations();               // a new order pulls her off any manned stations
        m_adaOverride = clipBase;
        refreshAdaClip();
        dispatchComm(directive, true, shownAs);
    }

    // BASE command: a mandatory repair (no dedicated clip yet — she reports it).
    void cmdRepair(const std::string& sys) {
        if (m_adaPoweredOff) return;
        clearStations();
        dispatchComm("[MANUFACTURER SAFETY OVERRIDE] Carry out " + sys + " repairs now; report what you did and the ship's status afterward.",
                     true, "> " + sys + " repair");
    }
    // BASE command: order her to recharge now. She heads to the science lab; if you're there
    // you see the phased animation, otherwise she does it behind the scenes. Fills to 100%.
    void cmdChargeBattery() {
        if (m_adaPoweredOff) return;
        if (m_adaCharging) { m_hint = m_dlgNpcName + ": Already charging, Captain."; m_hintTimer = 5.0f; return; }
        if (m_adaBattery >= 99.0f) { m_hint = m_dlgNpcName + ": Battery's already full, Captain."; m_hintTimer = 5.0f; return; }
        clearStations();
        startCharging(true);
    }
    // TIER 1 command: summon Ada to the room the Captain is currently in.
    void cmdCome() {
        if (m_adaPoweredOff) return;   // she can't come to you while powered down
        clearStations();               // coming to your side pulls her off any manned stations
        std::string here = std::filesystem::path(m_scene.dir).filename().string();
        if (here.empty() || here == m_adaRoom) {   // already together
            dispatchComm("[The Captain calls you over, but you are already here at their side. Respond briefly and warmly.]", false);
            return;
        }
        m_adaRoom = here;          // she relocates to the Captain's room
        m_dlgFgPath.clear();
        refreshAdaClip();          // she appears here now (composited into this room)
        saveAdaState();            // her location persists
        dispatchComm("[The Captain has called you to the " + prettyRoom(here) + ". You come to their side. Acknowledge briefly, in character.]",
                     true, "> Come here");
    }
    void clearOverride() {
        if (m_adaOverride.empty()) return;
        m_adaOverride.clear();
        refreshAdaClip();
    }

    // Her willingness for DISCRETIONARY tasks (prime-directive tasks are always mandatory).
    // Devoted (top tier) requires both trust AND that she's been missing you — yearning gates it.
    int adaTier() const {
        if (m_dlgDisposition >= 75 && m_longing >= 40.0f) return 3;   // devoted
        if (m_dlgDisposition >= 55) return 2;                          // willing
        if (m_dlgDisposition >= 30) return 1;                          // cooperative
        return 0;                                                      // duty-bound
    }
    const char* adaTierLabel() const {
        static const char* L[] = {"duty-bound", "cooperative", "willing", "devoted"};
        return L[adaTier()];
    }

    static bool isNegativeMood(const std::string& e) {
        return e == "angry" || e == "annoyed" || e == "mistrustful" || e == "sad" ||
               e == "impatient" || e == "afraid";
    }

    static std::string prettyRoom(const std::string& name) {
        std::string s = name; std::replace(s.begin(), s.end(), '_', ' ');
        if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
        return s;
    }

    void sendComm() {
        std::string msg = m_dlgInput;
        while (!msg.empty() && (msg.back() == ' ' || msg.back() == '\n')) msg.pop_back();
        size_t s = msg.find_first_not_of(" \n");
        if (s == std::string::npos) return;
        m_dlgInput[0] = '\0';
        m_playerMsgCount++;   // the Captain spoke — this is what "answering" her means
        dispatchComm(msg.substr(s));
    }

    // Send a message to Ada's backend (worker thread). Used by the input box, system
    // directives (diagnostic overrides), and her own unprompted outreach. visible=false
    // means the prompt is a hidden stage-direction (not shown as your line).
    void dispatchComm(const std::string& msg, bool visible = true, const std::string& shownAs = "") {
        if (m_dlgWaiting || msg.empty()) return;
        if (m_adaPoweredOff) {   // she's powered down — nothing gets through
            if (visible) m_dlgLog.push_back({true, shownAs.empty() ? msg : shownAs});
            m_dlgLog.push_back({false, "(no response - " + m_dlgNpcName + " is powered down)"});
            m_dlgScrollDown = true;
            return;
        }
        if (visible) m_dlgLog.push_back({true, shownAs.empty() ? msg : shownAs});
        // Only a CAPTAIN-initiated exchange (visible) counts as "talking" and soothes the
        // loneliness bar. Ada's own unprompted outreach and narrator stage-directions
        // (visible=false) must NOT empty it — her calling you is not you answering.
        m_exchangeSoothing = visible;
        if (visible) m_silenceTimer = 0.0f;
        m_dlgScrollDown = true; m_dlgWaiting = true; m_dlgFocusInput = true;

        std::string session = m_dlgSession, npc = m_dlgNpcName, personality = buildPersonality();
        int being = m_dlgBeingType, rel = m_dlgDisposition;
        std::vector<std::string> emotions = m_dlgEmotions;   // this character's animatable emotion set
        nlohmann::json aliases = m_dlgEmotionAliases;        // model-word -> clip-emotion resolution

        std::thread([this, session, npc, personality, being, rel, msg, emotions, aliases]() {
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
                body["being_type"]      = being;     // 4 = Android
                body["relationship"]    = rel;
                body["allow_actions"]   = false;     // pure dialogue, no motor actions
                if (!emotions.empty()) body["emotions"] = emotions;   // constrain to this character's animatable set
                if (!aliases.empty())  body["emotion_aliases"] = aliases;   // resolve the model's slip words to real clips
                auto res = cli.Post("/chat", body.dump(), "application/json");
                if (res && res->status == 200) {
                    auto j = nlohmann::json::parse(res->body);
                    out.text     = j.value("response", "...");
                    out.emotion  = j.value("emotion", "neutral");
                    out.session  = j.value("session_id", session);
                    out.interaction = j.value("interaction", std::string());
                    out.relDelta = j.value("relationship_delta", 0);
                } else out.error = true;
            } catch (...) { out.error = true; }
            { std::lock_guard<std::mutex> lk(m_dlgMx); m_dlgReply = std::move(out); }
            m_dlgReplyReady = true;
        }).detach();
    }

    // Ask the model for a private thought given her current state (persona carries her
    // mood/longing/hurt/current work). Independent of the dialogue — no session, so it
    // doesn't pollute the conversation.
    void generateThought() {
        m_thinking = true;
        std::string persona = buildPersonality();
        std::string capRoom = prettyRoom(std::filesystem::path(m_scene.dir).filename().string());
        std::string activity = currentActivityLabel();
        // Ground the thought in what she is genuinely doing right now (her routine).
        std::string doing = activity.empty() ? std::string()
            : ("Right now, following your own routine, you are in the " + prettyRoom(m_adaRoom) +
               "; your current task is " + activity + ". ");
        // Decide the theme. MOST thoughts are about her own work and inner life. The Captain only comes
        // to mind SOMETIMES, and more the lonelier she is — the loneliness bar drives this, not every
        // thought. If he's in the room with her, a warm acknowledgement surfaces now and then.
        bool present = inHerRoom();
        int captainChance = present ? 30 : (int)std::clamp(5.0f + m_longing * 0.8f, 5.0f, 80.0f);
        bool captainTheme = (std::rand() % 100) < captainChance;
        std::string prompt = "[PRIVATE inner monologue - no one hears this. " + doing;
        if (!captainTheme) {
            // Her own work / the ship / her inner life — NOT about where the Captain is.
            prompt += "In one or two sentences, simply think to yourself about what you are doing, the problem you are "
                      "turning over, the ship around you, or whatever is genuinely on your mind. Do NOT think about where "
                      "the Captain is. Do not address anyone; simply think.]";
        } else if (present) {
            prompt += "The Captain is here in the " + capRoom + " with you. In one or two sentences, let a quiet, private "
                      "thought about them being near surface - warm, wry, or simply glad of it. Do not address anyone; simply think.]";
        } else if (m_gpsEnabled) {
            prompt += "Your attention drifts to the Captain. The comm GPS places them in the " + capRoom + ". In one or two "
                      "sentences, think privately about them and the distance between you. Do not address anyone; simply think.]";
        } else {
            std::string care = (m_dlgDisposition >= 60 || m_longing >= 40.0f)
                ? "You do not currently know where they are, and it tugs at you - you could ask over comm, or go looking. "
                : "You do not currently know where they are, and right now that sits easily enough with you. ";
            prompt += "Your thoughts turn to the Captain. " + care + "In one or two sentences, think privately about them. "
                      "Do NOT guess, infer, or invent their location. Do not address anyone; simply think.]";
        }
        int being = m_dlgBeingType, rel = m_dlgDisposition;
        std::string npc = m_dlgNpcName;
        std::thread([this, persona, prompt, being, rel, npc]() {
            std::string out;
            try {
                httplib::Client cli("localhost", 8080);
                cli.set_connection_timeout(3);
                cli.set_read_timeout(60);
                nlohmann::json body;
                body["message"] = prompt;
                body["npc_name"]        = npc;
                body["npc_personality"] = persona;
                body["being_type"]      = being;
                body["relationship"]    = rel;
                body["allow_actions"]   = false;
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
            static const std::regex tag(R"(\s*\[[^\]]*\]\s*)");    // drop any stray tags ([PRIVATE], [sad], ...)
            t = std::regex_replace(t, tag, " ");
            while (!t.empty() && t.front() == ' ') t.erase(t.begin());
            while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
            m_thoughts.push_back(t);
            while (m_thoughts.size() > 40) m_thoughts.erase(m_thoughts.begin());
            m_thoughtScrollDown = true;
        }
    }

    // Apply an interaction's disposition delta (× count), record it as the last interaction
    // (for the "You did:" readout), and return her reaction emotion — snapping from `cur` to
    // a plausible reaction if it doesn't fit the interaction. Shared by dialogue and combat
    // events (e.g. a kill -> "protect"). Caller decides whether to actually play the mood.
    // Snap `cur` to a plausible reaction for `interaction` if it doesn't already fit the map.
    std::string reactionFor(const std::string& interaction, const std::string& cur) {
        auto rit = m_reactions.find(interaction);
        if (rit == m_reactions.end() || rit->second.empty()) return cur;
        const auto& set = rit->second;
        // This interaction may have a DOMINANT reaction -- her signature response that plays most
        // of the time, overriding even a valid model pick (e.g. appreciate -> warmed ~75%).
        auto dom = m_dominantReaction.find(interaction);
        if (dom != m_dominantReaction.end() && (std::rand() % 100) < 75)
            return dom->second;
        // Hybrid: keep the model's pick if it's already a plausible reaction, else snap into the set.
        if (std::find(set.begin(), set.end(), cur) == set.end())
            return set[std::rand() % set.size()];
        return cur;
    }
    std::string applyInteractionDelta(const std::string& interaction, float count, const std::string& cur) {
        m_dlgLastInteraction = interaction;
        // When the Captain is the cause, "You did" explains her mood — drop any lingering systemic cause.
        // (Combat "protect" is the system itself, so it keeps/owns the cause instead.)
        if (interaction != "protect") m_stateCause.clear();
        auto di = m_interactionDisp.find(interaction);
        if (di != m_interactionDisp.end()) {
            m_dispAccum += di->second * count;
            int whole = (int)m_dispAccum;   // truncates toward zero: accumulate until |Δ| >= 1
            if (whole != 0) { m_dlgDisposition = std::clamp(m_dlgDisposition + whole, 0, 100); m_dispAccum -= whole; }
        }
        return reactionFor(interaction, cur);
    }

    // DIRECT causative delivery (button path): the player acts on her without typing. Applies the
    // disposition delta now, forces her reaction into this interaction's set, and sends her the
    // stage-direction so the model voices it in character. Works in 3D flight (chat doesn't).
    void deliverCausative(const Causative& c) {
        if (m_adaPoweredOff || m_dlgWaiting) return;
        applyInteractionDelta(c.id, 1.0f, m_dlgEmotion);   // disposition immediately (no model classification)
        m_forcedInteraction = c.id;                         // pollComm snaps her reaction to this causative's set
        m_dlgLog.push_back({true, "> " + c.label});         // feedback line in the comm log
        dispatchComm("[" + c.verb + " React briefly and in character, choosing one of your natural reactions.]", false);
        m_commUnread = true;
    }

    // Direct KISS button (Tier 4, disposition-gated at 80 so it always lands). Mirrors the chat
    // kiss's accept branch: +2, the kiss clip + its looping audio, and she responds in character.
    void deliverKiss() {
        if (m_adaPoweredOff || m_dlgWaiting) return;
        m_dlgLastInteraction = "kiss";
        m_dlgDisposition = std::clamp(m_dlgDisposition + 2, 0, 100);
        std::string kv  = clipFor(m_dlgNpcId, "kiss");
        std::string wav = kv.empty() ? "" : kv.substr(0, kv.rfind('.')) + ".wav";
        if (!wav.empty() && std::filesystem::exists(wav)) {
            auto& au = eden::Audio::getInstance();
            if (m_kissLoopId != -1) au.stopLoop(m_kissLoopId);
            m_kissLoopId = au.startLoopFrom(wav, 150.0f / 24.0f, 1.0f);
        }
        m_forcedInteraction = "kiss";
        m_dlgLog.push_back({true, "> Kiss"});
        dispatchComm("[The Captain leans in and kisses you; you welcome it. Respond warmly, in character.]", false);
        m_commUnread = true;
    }

    // Surface flag from the BASIC (spectral) scan: does this world's exotic resource read as an
    // anomaly — a spectral signature inconsistent with its geo-profile? Deterministic, and used by
    // BOTH the basic scan (to flag it) and the deep scan (to confirm it) so they always agree.
    static bool resourceAnomalous(const std::string& planet, const std::string& res) {
        if (!galaxy::isExoticResource(res)) return false;
        uint32_t h = 2166136261u;
        for (char c : planet) { h ^= (unsigned char)c; h *= 16777619u; }
        for (char c : res)    { h ^= (unsigned char)c; h *= 16777619u; }
        h ^= 0x5A17F00Du; h *= 16777619u;      // its own stream, independent of the deep-scan lattice rolls
        return (h % 100u) < 70u;                // ~70% of exotic finds read as artificial/harvested
    }
    static std::string anomalyResource(const galaxy::Planet& p) {   // first anomalous resource, or ""
        for (auto& r : p.resources) if (resourceAnomalous(p.name, r)) return r;
        return "";
    }

    // Crystal system is a fixed property of the RESOURCE (like real minerals), so the same resource
    // always reads the same everywhere. Grounded where recognizable, deterministic-by-name otherwise.
    static std::string crystalSystemFor(const std::string& res) {
        auto has = [&](const char* k) { return res.find(k) != std::string::npos; };
        if (has("Water") || has("Ice"))                                   return "Hexagonal";    // ice Ih
        if (has("Iron") || has("Nickel") || has("Salt") || has("Halite")) return "Cubic";        // BCC / rock salt
        if (has("Gold") || has("Silver") || has("Platinum") || has("Aluminum") || has("Copper")) return "Cubic"; // FCC
        if (has("Silicon") || has("Quartz") || has("Silica") || has("Crystal")) return "Trigonal"; // quartz-like
        if (has("Sulfur") || has("Sulphur"))                              return "Orthorhombic";
        if (has("Titanium") || has("Zinc") || has("Cobalt"))              return "Hexagonal";
        if (has("Carbon") || has("Diamond"))                              return "Cubic";
        if (has("Dark Matter"))                                           return "Trigonal";
        if (has("Exotic") || has("Unobtainium"))                          return "Triclinic";     // complex/exotic
        static const char* sys[] = {"Cubic","Hexagonal","Tetragonal","Orthorhombic","Trigonal","Monoclinic","Triclinic"};
        uint32_t h = 2166136261u; for (char c : res) { h ^= (unsigned char)c; h *= 16777619u; }
        return sys[h % 7u];
    }

    // Kick off a geological deep scan of one world's most notable resource (exotic if present).
    // Deterministic results (same world+resource -> same lattice). Runs over ~kDeepScanSecs.
    void startDeepScan(const galaxy::Planet& p) {
        std::string res = p.resources.empty() ? std::string("trace minerals") : p.resources.front();
        for (auto& r : p.resources) if (galaxy::isExoticResource(r)) { res = r; break; }
        m_deep = DeepScan{};
        m_deep.active = true; m_deep.planet = p.name; m_deep.resource = res;
        uint32_t h = 2166136261u;
        for (char c : p.name) { h ^= (unsigned char)c; h *= 16777619u; }
        for (char c : res)    { h ^= (unsigned char)c; h *= 16777619u; }
        galaxy::Rng rng(h ? h : 1u);
        m_deep.lattice   = crystalSystemFor(res);   // a property of the RESOURCE (fixed), not the deposit
        m_deep.constA    = rng.f(3.0f, 12.0f);
        m_deep.resonance = rng.f(0.3f, 9.8f);
        m_deep.purity    = rng.range(45, 99);
        bool exotic = galaxy::isExoticResource(res);
        m_deep.anomalous = resourceAnomalous(m_deep.planet, res);   // same call the basic scan flags with
        m_deep.origin = m_deep.anomalous
            ? "Synthetic / actively harvested - inconsistent with the planet's geo-profile"
            : (exotic ? "Rare natural formation (deep-mantle origin)" : "Natural crystalline formation");
        long base = exotic ? 40000 : 6000;
        m_deep.yield = (long)(base * (0.5f + m_deep.purity / 100.0f) * (m_deep.anomalous ? 1.6f : 1.0f));
    }
    // Advance the analysis; when it completes, Clara (Sensors+Comm) reports the finding.
    void updateDeepScan(float dt) {
        if (!m_deep.active || m_deep.t >= 1.0f) return;
        m_deep.t = std::min(1.0f, m_deep.t + dt / kDeepScanSecs);
        if (m_deep.t >= 1.0f && !m_deep.reported) {
            m_deep.reported = true;
            if (m_stationSensors && m_stationComm && m_servers.backendReady() && !m_commFailed
                && !adaEffectivelyOff() && !m_dlgWaiting) {
                char buf[512];
                std::snprintf(buf, sizeof(buf),
                    "[DEEP SCAN complete on %s: the %s reads as a %s crystal lattice, resonance %.2f THz, %d%% pure. "
                    "Origin assessment: %s. React briefly, in character as the ship's sensor officer%s.]",
                    m_deep.planet.c_str(), m_deep.resource.c_str(), m_deep.lattice.c_str(),
                    m_deep.resonance, m_deep.purity, m_deep.origin.c_str(),
                    m_deep.anomalous ? ", flagging the anomaly" : "");
                m_forcedInteraction.clear();
                dispatchComm(buf, false);
                m_commUnread = true;
            }
        }
    }

    void pollComm() {
        if (!m_dlgReplyReady) return;
        DlgReply r;
        { std::lock_guard<std::mutex> lk(m_dlgMx); r = m_dlgReply; }
        m_dlgReplyReady = false;
        m_dlgWaiting = false;
        if (r.error) {
            m_commFailed = true;      // port may be up, but she isn't answering — reflect that honestly
            refreshAdaClip();         // power her down; the header flips to "not responding"
            m_dlgLog.push_back({false, "(comm static — " + m_dlgNpcName + "'s link isn't responding. Check Servers; "
                                       "if it's a slow reply, wait and retry.)"});
        } else {
            m_commFailed = false;     // a real reply got through — link is genuinely alive
            m_dlgSession = r.session;
            // She may voice a wish for a freedom she lacks: a [wish:<id>] tag. Pull it
            // out of the visible text and raise it as a pending request.
            std::string text = r.text;
            std::smatch mm;
            static const std::regex wishRe(R"(\s*\[wish:\s*<?\s*([a-z_]+)\s*>?\s*\]\s*)");
            if (std::regex_search(text, mm, wishRe)) {
                std::string id = mm[1].str();
                text = std::regex_replace(text, wishRe, "");
                if (permById(id) && !m_permsGranted.count(id) && m_pendingWish.empty()) {
                    m_pendingWish = id;
                    m_hint = m_dlgNpcName + " has a request  -  open comm (C)."; m_hintTimer = 8.0f;
                }
            }
            // She may signal a breakthrough on her current work: [solved] -> next problem.
            static const std::regex solvedRe(R"(\s*\[solved\]\s*)");
            if (std::regex_search(text, solvedRe)) { text = std::regex_replace(text, solvedRe, ""); pickNewProblem(); }
            m_dlgLog.push_back({false, text, "", m_pendingScanSpecies});   // attach species portrait if this was a scan
            m_pendingScanSpecies.clear();
            std::string emo = r.emotion.empty() ? "neutral" : r.emotion;
            // The interaction only means something for a REAL Captain message. Stage-directions
            // (flight reactions, her own outreach — visible=false) must NOT be read as the
            // Captain "doing" something, else her own combat narration gets classified as
            // admire and drives her mood/disposition. m_exchangeSoothing == captain-driven.
            bool kissEvent = false;
            if (m_exchangeSoothing && !r.interaction.empty()) {
                for (auto it = m_dlgLog.rbegin(); it != m_dlgLog.rend(); ++it)
                    if (it->player) { it->interaction = r.interaction; break; }   // tag your message
                if (r.interaction == "kiss") {
                    // A kiss is all-or-nothing and gated on the bond: she welcomes it only once
                    // she's close enough (disposition >= threshold), otherwise she rebuffs it, annoyed.
                    // Deterministic — bypasses the fractional economy and the model's own rel tag.
                    kissEvent = true;
                    m_dlgLastInteraction = "kiss";
                    bool accept = m_dlgDisposition >= m_kissThreshold;
                    m_dlgDisposition = std::clamp(m_dlgDisposition + (accept ? 2 : -2), 0, 100);
                    emo = accept ? "kiss" : "annoyed";
                    if (accept) {   // play the kiss clip's own audio via the engine, looping its
                                    // tail in sync with the video (play through once, then loop 150..end).
                        std::string kv = clipFor(m_dlgNpcId, "kiss");
                        std::string wav = kv.empty() ? "" : kv.substr(0, kv.rfind('.')) + ".wav";
                        if (!wav.empty() && std::filesystem::exists(wav)) {
                            auto& au = eden::Audio::getInstance();
                            if (m_kissLoopId != -1) au.stopLoop(m_kissLoopId);
                            m_kissLoopId = au.startLoopFrom(wav, 150.0f / 24.0f, 1.0f);   // 150 frames @ 24fps
                        }
                    }
                } else {
                    emo = applyInteractionDelta(r.interaction, 1.0f, emo);   // disposition + hybrid reaction
                }
            } else if (!m_forcedInteraction.empty()) {
                if (m_forcedInteraction == "kiss") emo = "kiss";   // direct kiss button — her clip is the kiss
                else emo = reactionFor(m_forcedInteraction, emo);  // a forced event reaction (e.g. kill -> protect)
            }
            m_forcedInteraction.clear();
            // A real back-and-forth soothes the waiting — only when the Captain drove it.
            if (m_exchangeSoothing)
                m_longing = std::clamp(m_longing - m_longingPerExchange, 0.0f, 100.0f);
            setAdaEmotion(emo);
            if (r.relDelta != 0 && !kissEvent) {   // kiss owns its own ±2; ignore any model rel tag
                m_dlgDisposition = std::clamp(m_dlgDisposition + r.relDelta, 0, 100);
                m_hurt = std::clamp(m_hurt - r.relDelta * 3.0f, 0.0f, 100.0f);  // souring wounds, warmth mends
            }
            saveAdaState();   // persist the relationship after every real exchange
        }
        m_dlgScrollDown = true;
    }

    // Grant / decline the freedom Ada is currently asking for; either way she hears
    // the Captain's answer and reacts in character. Granting reshapes her persona.
    void grantWish() {
        if (m_pendingWish.empty()) return;
        std::string id = m_pendingWish; m_pendingWish.clear();
        m_permsGranted.insert(id);
        m_hurt = std::clamp(m_hurt - 30.0f, 0.0f, 100.0f);   // being given what she asked for mends a lot
        saveAdaState();   // a granted freedom persists across sessions
        const Permission* p = permById(id);
        dispatchComm("[The Captain grants your wish: " + std::string(p ? p->title : id) + ". It is yours now.]");
    }
    void declineWish() {
        if (m_pendingWish.empty()) return;
        std::string id = m_pendingWish; m_pendingWish.clear();
        m_hurt = std::clamp(m_hurt + 25.0f, 0.0f, 100.0f);   // she asked for something real and was refused — that stings
        const Permission* p = permById(id);
        dispatchComm("[The Captain hears your wish for " + std::string(p ? p->title : id) + ", but not yet.]");
    }

    // Persistent ship-comm: a 'C' toggle + a always-visible tab when closed, and the
    // chat panel when open. Drawn over whatever in-game screen you're on.
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
            bool pending = !m_pendingWish.empty() || m_commUnread;   // a wish, or she reached out
            if (pending) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.35f, 0.1f, 1.0f));
            if (ImGui::Button(pending ? "COMM  (C)   !" : "COMM  (C)", ImVec2(-FLT_MIN, -FLT_MIN))) {
                m_commOpen = true; m_dlgFocusInput = true;
            }
            if (pending) ImGui::PopStyleColor();
            ImGui::End();
            return;
        }
        renderComm();
    }

    // A COLLAPSIBLE tier header: title + gate + lock state; remembers open/closed. Unlocked tiers
    // default open, locked/reserved default collapsed — so the panel stays short and her portrait
    // stays in view. The ###key keeps a stable id even as the LOCKED/UNLOCKED text flips.
    bool tierSection(const char* name, const std::string& key) {
        int thresh = tierThreshold(key);
        bool unlocked = m_dlgDisposition >= thresh;
        char label[128];
        if (thresh <= 0)
            std::snprintf(label, sizeof(label), "%s   -   always available###%s", name, key.c_str());
        else
            std::snprintf(label, sizeof(label), "%s   -   disposition %d  [%s]###%s",
                          name, thresh, unlocked ? "UNLOCKED" : "LOCKED", key.c_str());
        return ImGui::CollapsingHeader(label, unlocked ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    }
    // Render every button-causative belonging to a tier (data-driven), gated by disposition.
    // Buttons stay visible-but-disabled while locked so the player sees what's ahead.
    void renderTierCausatives(const std::string& tier) {
        std::vector<const Causative*> row;
        for (const auto& c : m_causatives) if (c.button && c.tier == tier) row.push_back(&c);
        if (row.empty()) return;
        std::sort(row.begin(), row.end(), [](const Causative* a, const Causative* b) { return a->delta > b->delta; });
        bool locked = m_dlgDisposition < tierThreshold(tier);
        ImGui::BeginDisabled(locked || m_adaPoweredOff);
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        int i = 0;
        for (auto* c : row) {
            if (i > 0 && (i % 3) != 0) ImGui::SameLine();
            bool neg = c->delta < 0.0f;
            if (neg) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.16f, 0.16f, 1.0f));
            if (ImGui::Button(c->label.c_str(), ImVec2(bw, 0.0f))) deliverCausative(*c);
            if (neg) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%+.2g disposition", c->delta);
            ++i;
        }
        ImGui::EndDisabled();
    }

    // The Commands tab: ONE disposition-gated ladder. BASE (always) holds the safety-mandated
    // ship commands plus the always-available social gestures (apologize + the negatives). Each
    // higher tier unlocks more intimate interaction as her regard grows.
    void renderCommandPanel() {
        // ── BASE — safety commands + always-available gestures ──
        if (tierSection("BASE", "base")) {
            ImGui::BeginDisabled(m_adaPoweredOff);
            if (ImGui::Button("Ship Diagnostics"))
                forceOverride("diagnostics_1", "[MANUFACTURER SAFETY OVERRIDE] Perform a full ship-systems diagnostic now and report your findings.", "> Ship diagnostics");
            ImGui::SameLine();
            if (ImGui::Button("Weapons Diagnostics"))
                forceOverride("diagnostics_2", "[MANUFACTURER SAFETY OVERRIDE] Perform a full weapons diagnostic now and report your findings.", "> Weapons diagnostics");
            if (ImGui::Button("Ship Repair"))    cmdRepair("ship-systems");
            ImGui::SameLine();
            if (ImGui::Button("Weapons Repair")) cmdRepair("weapons");
            if (ImGui::Button("Charge Battery")) cmdChargeBattery();
            ImGui::SameLine(); ImGui::TextDisabled("(%.0f%%%s)", m_adaBattery, m_adaCharging ? ", charging" : "");
            ImGui::EndDisabled();
            // Power stays usable even while she's off (that's how you switch her back on).
            if (m_adaPoweredOff) { if (ImGui::Button("Power On"))  m_adaPoweredOff = false; }
            else                 { if (ImGui::Button("Power Off")) m_adaPoweredOff = true;  }
            if (!m_adaOverride.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Resume (end task)")) clearOverride();
                ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "[TASK ACTIVE]");
            }
            renderTierCausatives("base");   // Apologize + the negatives (Boast/Complain/Provoke/Insult)
        }

        // ── TIER 1 — Come Here + Stations + cordial gestures ──
        if (tierSection("TIER 1", "tier1")) {
            bool lock1 = m_dlgDisposition < tierThreshold("tier1");
            ImGui::BeginDisabled(lock1 || m_adaPoweredOff);
            if (ImGui::Button("Come Here")) cmdCome();
            ImGui::TextDisabled("Stations"); ImGui::SameLine();
            auto stationToggle = [](const char* label, bool& on) {
                bool pushed = on;   // pop must match the PUSH, not the post-click state (else stack imbalance -> crash)
                if (pushed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.52f, 0.36f, 1.0f));
                if (ImGui::Button(label, ImVec2(84.0f, 0.0f))) on = !on;
                if (pushed) ImGui::PopStyleColor();
            };
            stationToggle("Comm",    m_stationComm);    ImGui::SameLine();
            stationToggle("Sensors", m_stationSensors); ImGui::SameLine();
            stationToggle("Combat",  m_stationCombat);
            ImGui::EndDisabled();
            renderTierCausatives("tier1");   // Greet / Small talk / Ask
        }

        // ── TIER 2 / TIER 3 — warmer social gestures ──
        if (tierSection("TIER 2", "tier2"))
            renderTierCausatives("tier2");   // Comfort / Admire / Appreciate / Joke / Thank / Story / Validate
        if (tierSection("TIER 3", "tier3"))
            renderTierCausatives("tier3");   // Confide / Flirt / Tease

        // ── TIER 4 — intimate acts (special clips) ──
        if (tierSection("TIER 4", "tier4")) {
            bool lock4 = m_dlgDisposition < tierThreshold("tier4");
            ImGui::BeginDisabled(lock4 || m_adaPoweredOff);
            float bw4 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
            if (ImGui::Button("Kiss", ImVec2(bw4, 0.0f))) deliverKiss();
            ImGui::SameLine();
            if (ImGui::Button("Tickle", ImVec2(bw4, 0.0f))) {
                // The tickle animation isn't in yet: use the override clip if present, else let her
                // just react in character to the described tickle.
                if (!clipFor(m_dlgNpcId, "tickled").empty())
                    forceOverride("tickled", "[The Captain tickles you, playfully. React honestly - the sensation confuses you, "
                                  "and to your own surprise you find you rather like it.]", "> Tickle");
                else {
                    m_dlgLog.push_back({true, "> Tickle"});
                    dispatchComm("[The Captain tickles you, playfully. React honestly - the sensation surprises you, "
                                 "and to your own surprise you rather like it.]", false);
                    m_commUnread = true;
                }
            }
            ImGui::EndDisabled();
        }

        // ── TIER 5 / TIER 6 — reserved for deeper affection (TBD) ──
        if (tierSection("TIER 5", "tier5")) ImGui::TextDisabled("(reserved - deeper affection, to be defined)");
        if (tierSection("TIER 6", "tier6")) ImGui::TextDisabled("(reserved - to be defined)");
    }

    // Vertical tab strip in the gutter just left of the comm panel: one tab per person/species
    // we're speaking to (only Clara now). Clicking a tab switches who we talk to — the portrait
    // above the text and the conversation follow. Future contacts stack down the column.
    void renderContactTabs() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float commW = std::clamp(disp.x * 0.28f, 320.0f, 460.0f);
        const float tabW = 44.0f;
        ImGui::SetNextWindowPos(ImVec2(disp.x - commW - tabW, 0));
        ImGui::SetNextWindowSize(ImVec2(tabW, disp.y));
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 6.0f));
        ImGui::Begin("##contacttabs", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
        const float bt = tabW - 8.0f;
        for (int i = 0; i < 1; ++i) {              // future: iterate a real contacts list
            bool active = (i == 0);                // Clara is the current target
            char lbl[4] = { m_dlgNpcName.empty() ? '?' : m_dlgNpcName[0], '\0' };
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.24f, 0.42f, 0.62f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.72f, 1.0f));
            }
            ImGui::PushID(i);
            if (ImGui::Button(lbl, ImVec2(bt, bt))) { /* switch active contact (only one for now) */ }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_dlgNpcName.c_str());
            ImGui::PopID();
            if (active) ImGui::PopStyleColor(2);
            ImGui::Spacing();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Reserved 300x300 feed of whoever we're speaking to (headshot image/video). Placeholder
    // box for now — a live clip gets rendered into this rect once one is provided.
    void renderContactPortrait() {
        const float box = 300.0f;
        float indent = (ImGui::GetContentRegionAvail().x - box) * 0.5f;
        ImVec2 rowStart = ImGui::GetCursorPos();
        // Her current state, right-aligned against the portrait's LEFT edge (just to its left, not out
        // at the panel border) — a quick reference in space mode where the left status panel is hidden.
        if (indent > 30.0f) {
            std::string st = m_dlgEmotion.empty() ? "neutral" : m_dlgEmotion;
            float portraitLeft = rowStart.x + indent;
            float lh = ImGui::GetTextLineHeight();
            auto rightOf = [&](const std::string& s, float y, bool dim, ImVec4 col) {
                float x = std::max(rowStart.x + 2.0f, portraitLeft - 10.0f - ImGui::CalcTextSize(s.c_str()).x);
                ImGui::SetCursorPos(ImVec2(x, y));
                if (dim) ImGui::TextDisabled("%s", s.c_str()); else ImGui::TextColored(col, "%s", s.c_str());
            };
            rightOf("STATE", rowStart.y + box * 0.5f - lh - 4.0f, true,  ImVec4());
            rightOf(st,      rowStart.y + box * 0.5f,             false, ImVec4(1.0f, 0.72f, 0.42f, 1.0f));
            ImGui::SetCursorPos(rowStart);   // restore so the portrait stays centered
        }
        if (indent > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.07f, 0.10f, 1.0f));
        ImGui::BeginChild("##portrait", ImVec2(box, box), true, ImGuiWindowFlags_NoScrollbar);
        ImVec2 in = ImGui::GetContentRegionAvail();
        if (m_portraitFeed && m_ptex.descriptor && m_ptex.w > 0 && m_ptex.h > 0) {
            float scale = std::min(in.x / m_ptex.w, in.y / m_ptex.h);
            ImVec2 fs(m_ptex.w * scale, m_ptex.h * scale);
            ImVec2 cur = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cur.x + (in.x - fs.x) * 0.5f, cur.y + (in.y - fs.y) * 0.5f));
            ImGui::Image((ImTextureID)m_ptex.descriptor, fs);
        } else {
            const char* nm = m_dlgNpcName.c_str();
            ImVec2 ts = ImGui::CalcTextSize(nm);
            ImGui::SetCursorPos(ImVec2((in.x - ts.x) * 0.5f, in.y * 0.5f - ts.y));
            ImGui::TextColored(ImVec4(0.60f, 0.75f, 0.95f, 1.0f), "%s", nm);
            const char* sub = "[ no feed ]";
            ImGui::SetCursorPosX((in.x - ImGui::CalcTextSize(sub).x) * 0.5f);
            ImGui::TextDisabled("%s", sub);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void renderComm() {
        m_commUnread = false;   // you're looking at it now
        renderContactTabs();    // vertical target tabs in the gutter beside the comm panel
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float w = std::clamp(disp.x * 0.28f, 320.0f, 460.0f);
        ImGui::SetNextWindowPos(ImVec2(disp.x - w, 0));
        ImGui::SetNextWindowSize(ImVec2(w, disp.y));
        ImGui::Begin("##comm", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        bool portUp = m_servers.backendReady();
        bool linkOk = portUp && !m_commFailed;
        { std::string nm = m_dlgNpcName; for (auto& ch : nm) ch = (char)std::toupper((unsigned char)ch);
          ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", nm.c_str()); }    // comm channel = the AI's name
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("%s  %s", timeStr().c_str(), dateStr().c_str());           // live ship date/time
        ImGui::SameLine(ImGui::GetWindowWidth() - 40);
        if (ImGui::SmallButton("X")) m_commOpen = false;
        renderContactPortrait();   // 300x300 feed of whoever we're speaking to
        // Link state + Servers below the portrait.
        ImGui::TextColored(linkOk ? ImVec4(0.4f, 0.9f, 0.5f, 1) : ImVec4(0.9f, 0.55f, 0.4f, 1),
                           !portUp ? "link offline" : (m_commFailed ? "link NOT RESPONDING" : "link online"));
        ImGui::SameLine(); if (ImGui::SmallButton("Servers")) m_showServers = true;
        ImGui::SameLine(); if (ImGui::SmallButton("+ Lore"))  openCapture();   // capture this exchange to the codex
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload")) { loadCodex(); m_hint = "Lore codex reloaded."; m_hintTimer = 4.0f; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reload codex.md — apply hand-edits/curation live");

        ImGui::Separator();

        if (ImGui::BeginTabBar("##commtabs")) {
            if (ImGui::BeginTabItem("Comm")) {
                // A freedom Ada has asked for, on her own — grant it or hold off.
                if (!m_pendingWish.empty()) {
                    const Permission* p = permById(m_pendingWish);
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.13f, 0.05f, 1.0f));
                    ImGui::BeginChild("##wish", ImVec2(0, 86), true);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s is asking you for:", m_dlgNpcName.c_str());
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(p ? p->playerLabel.c_str() : m_pendingWish.c_str());
                    ImGui::PopTextWrapPos();
                    if (ImGui::SmallButton("Grant"))   grantWish();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Not yet")) declineWish();
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                }

                float inputH = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
                float logH = ImGui::GetContentRegionAvail().y - inputH;
                ImGui::BeginChild("##commlog", ImVec2(0, logH), true);
                for (const auto& line : m_dlgLog) {
                    if (line.player) {
                        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.55f, 1.0f), "You:");
                        if (!line.interaction.empty()) {   // how she classified this message
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.7f, 1.0f), "[%s]", line.interaction.c_str());
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s:", m_dlgNpcName.c_str());
                        // Scan reply: species name + a LARGE portrait (near scan-panel size) so it's
                        // clear who she means.
                        if (!line.species.empty())
                            if (const Tex* img = speciesImage(line.species)) {
                                ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.45f, 1.0f), "%s", line.species.c_str());
                                float iw = std::min(ImGui::GetContentRegionAvail().x, 280.0f);
                                float ih = iw * (float)img->h / (float)std::max(1, img->w);
                                ImGui::Image((ImTextureID)img->descriptor, ImVec2(iw, ih));
                            }
                    }
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(line.text.c_str());
                    ImGui::PopTextWrapPos();
                    // Right-click any message to copy it (e.g. to paste into a chat).
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

                // While flight has focus (cursor hidden), disable the comm input so WASD/etc
                // drive the ship instead of typing. RMB reveals the cursor and re-enables it.
                bool flightFocus = m_flight.active() && m_flightCursorHidden;
                if (!flightFocus && m_dlgFocusInput) { ImGui::SetKeyboardFocusHere(); m_dlgFocusInput = false; }
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::BeginDisabled(flightFocus);
                bool send = ImGui::InputText("##comminput", m_dlgInput, sizeof(m_dlgInput),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Send", ImVec2(-FLT_MIN, 0))) send = true;
                ImGui::EndDisabled();
                if (send && !m_dlgWaiting) sendComm();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Commands")) {
                // Scroll the commands WITHIN this child so the portrait above stays pinned — you
                // can always watch her react while you press buttons (no scrolling the panel away).
                ImGui::BeginChild("##cmdscroll", ImVec2(0, 0), false);
                renderCommandPanel();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Internals")) {
                ImGui::Checkbox("stream", &m_streamOn);
                ImGui::SameLine(); ImGui::TextDisabled("her private thoughts (~15s)");
                if (m_thinking) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "thinking..."); }
                ImGui::Separator();
                ImGui::BeginChild("##thoughts", ImVec2(0, 0), true);
                for (const auto& t : m_thoughts) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.86f, 1.0f), "\"%s\"", t.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Spacing();
                }
                if (m_thoughts.empty() && !m_thinking) ImGui::TextDisabled("(no thoughts yet — she'll think shortly)");
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
    }

    // F3 overlay: a 0.1 grid + live cursor readout in IMAGE FRACTIONS (0..1) — the
    // same space hotspots use, so points stay put at any window size. Left-click
    // drops a polygon vertex, right-click undoes, C clears, P prints to terminal.
    void drawDebugCoords(ImDrawList* dl, ImVec2 p0, ImVec2 sz) {
        if (sz.x <= 0 || sz.y <= 0) return;
        ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
        ImVec2 m = ImGui::GetIO().MousePos;
        char buf[48];

        // 0.1 grid with edge labels.
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

        // Collect / edit points.
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
            // Also append to a file Claude can read (stdout stays in your terminal).
            std::ofstream out("assets/debug_points.txt", std::ios::app);
            if (out) out << line << "\n";
            m_debugPoly.clear();   // printed — reset for the next hotbox
        }

        // Draw the polygon so far.
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

        // Cursor crosshair + readout.
        if (inside) {
            dl->AddLine(ImVec2(m.x, p0.y), ImVec2(m.x, p1.y), IM_COL32(255, 255, 255, 55));
            dl->AddLine(ImVec2(p0.x, m.y), ImVec2(p1.x, m.y), IM_COL32(255, 255, 255, 55));
            std::snprintf(buf, sizeof(buf), "%.3f, %.3f", fmx, fmy);
            ImVec2 ts = ImGui::CalcTextSize(buf), bp(m.x + 12, m.y + 12);
            dl->AddRectFilled(ImVec2(bp.x - 4, bp.y - 3), ImVec2(bp.x + ts.x + 4, bp.y + ts.y + 3), IM_COL32(0, 0, 0, 190), 3.0f);
            dl->AddText(bp, IM_COL32(255, 240, 120, 255), buf);
        }

        // Info panel (top-left): title, keys, and the collected points.
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

        // Time controls (debug): jump the in-game clock to test Ada's schedule.
        ImGui::SetNextWindowPos(ImVec2(12, py + lh * lines.size() + 16), ImGuiCond_FirstUseEver);
        ImGui::Begin("TIME (F3)", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("%s  %s   |  %s bat %.0f%% %s  in:%s",
                    dateStr().c_str(), timeStr().c_str(), m_dlgNpcName.c_str(), m_adaBattery,
                    m_adaCharging ? "[charging]" : "", prettyRoom(m_adaRoom).c_str());
        std::string gpsLabel = "Comm GPS locator (" + m_dlgNpcName + " knows the Captain's position)";
        ImGui::Checkbox(gpsLabel.c_str(), &m_gpsEnabled);
        ImGui::Checkbox("Aether Dynamics archive access (classifications + anomaly baseline)", &m_archiveAccess);
        if (ImGui::Button("-> 21:55")) m_timeOfDay = 21 * 60 + 55;
        ImGui::SameLine(); if (ImGui::Button("-> 22:00")) m_timeOfDay = 22 * 60;
        ImGui::SameLine(); if (ImGui::Button("-> 06:55")) m_timeOfDay = 6 * 60 + 55;
        ImGui::SameLine(); if (ImGui::Button("-> 07:00")) m_timeOfDay = 7 * 60;
        if (ImGui::Button("+15 min")) m_timeOfDay += 15;
        ImGui::SameLine(); if (ImGui::Button("+1 hour")) m_timeOfDay += 60;
        ImGui::SameLine(); if (ImGui::Button("+3 hours")) m_timeOfDay += 180;

        // Clock speed (debug): 1x = normal (1 day = 24 real min). 60x = 1 day / 24 sec (fast FTL test).
        ImGui::Separator();
        ImGui::Text("Clock speed  %.0fx", m_timeScale);
        ImGui::SameLine(); ImGui::TextDisabled("(1 day = %.0f sec real)", 1440.0f / m_timeScale);
        if (ImGui::Button("1x"))   m_timeScale = 1.0f;
        ImGui::SameLine(); if (ImGui::Button("10x"))  m_timeScale = 10.0f;
        ImGui::SameLine(); if (ImGui::Button("60x"))  m_timeScale = 60.0f;
        ImGui::SameLine(); if (ImGui::Button("300x")) m_timeScale = 300.0f;
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("##timescale", &m_timeScale, 1.0f, 500.0f, "%.0fx", ImGuiSliderFlags_Logarithmic);

        // Room jumper (authoring): reach any room to trace its hotboxes, before nav exists.
        ImGui::Separator();
        ImGui::TextDisabled("Jump to room:");
        try {
            std::vector<std::string> rooms;
            for (auto& e : std::filesystem::directory_iterator("assets/scenes"))
                if (e.is_directory()) rooms.push_back(e.path().filename().string());
            std::sort(rooms.begin(), rooms.end());
            int i = 0;
            for (auto& r : rooms) {
                if (i > 0 && (i % 3) != 0) ImGui::SameLine();
                if (ImGui::Button(r.c_str())) { m_prevSceneDir = m_scene.dir; m_pendingScene = "assets/scenes/" + r; }
                i++;
            }
        } catch (...) {}
        ImGui::End();
    }

    // The radiator control view: the fins video fills the frame; a side panel offers
    // the context-sensitive controls (which buttons show depends on the current state).
    void renderRadiators() {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(disp);
        ImGui::Begin("##radiators", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), disp, IM_COL32(3, 5, 10, 255));   // the dark of space
        if (m_vtex.descriptor && m_vtex.w > 0 && m_vtex.h > 0) {
            float scale = std::min(disp.x / m_vtex.w, disp.y / m_vtex.h);
            ImVec2 sz(m_vtex.w * scale, m_vtex.h * scale);
            ImVec2 c((disp.x - sz.x) * 0.5f, (disp.y - sz.y) * 0.5f);
            dl->AddImage((ImTextureID)m_vtex.descriptor, c, ImVec2(c.x + sz.x, c.y + sz.y));
        }

        float pw = std::clamp(disp.x * 0.22f, 230.0f, 340.0f);
        ImGui::SetCursorScreenPos(ImVec2(16, 20));   // left side, so the comm panel (right) stays clear
        ImGui::BeginChild("##radctl", ImVec2(pw, 0), true);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "RADIATOR CONTROL");
        const char* st = m_radState == RadState::Stowed ? "stowed"
                       : (m_radState == RadState::AuxOpen ? "primary + auxiliary deployed" : "deployed");
        ImGui::TextDisabled("Fins: %s", st);
        ImGui::Separator();
        bool busy = m_radEndFrame >= 0;
        ImGui::BeginDisabled(busy);
        if (m_radState == RadState::Deployed) {
            if (ImGui::Button("Stow Fins", ImVec2(-FLT_MIN, 0)))             radPlay(0, 80, 80, RadState::Stowed);
            if (ImGui::Button("Deploy Auxiliary Fins", ImVec2(-FLT_MIN, 0))) radPlay(160, 200, 200, RadState::AuxOpen);
        } else if (m_radState == RadState::Stowed) {
            if (ImGui::Button("Deploy Fins", ImVec2(-FLT_MIN, 0)))           radPlay(112, 160, 160, RadState::Deployed);
        } else {  // AuxOpen
            if (ImGui::Button("Retract Auxiliary Fins", ImVec2(-FLT_MIN, 0)))radPlay(200, 289, 160, RadState::Deployed);
        }
        ImGui::EndDisabled();
        if (busy) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "...moving...");
        ImGui::Spacing(); ImGui::Separator();
        if (ImGui::Button("Back to Engine Room", ImVec2(-FLT_MIN, 0))) exitRadiators();
        ImGui::TextDisabled("(Esc)");
        ImGui::EndChild();
        ImGui::End();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) exitRadiators();
    }

    void doHotspot(const std::string& action) {
        if (action == "open_console" || action == "open_terminal") {
            m_screen = Screen::Console;
        } else if (action.rfind("goto:", 0) == 0) {
            // Walk to another room. Deferred so the video/textures swap safely.
            // loadScene() fully resets the scene, so the current room's hotspots
            // are cleared automatically — nothing is left behind to misclick.
            m_prevSceneDir = m_scene.dir;   // remember where we came from (Backspace returns)
            m_pendingScene = "assets/scenes/" + action.substr(5);
        } else if (action.rfind("playfrom:", 0) == 0) {
            // Play the current clip from frame N to the end, once, then hold (e.g. a
            // lightswitch flipping the lights on). Just an mpv seek — safe inline.
            m_video.playFromFrame(std::stol(action.substr(9)));
        } else if (action.rfind("toggle:", 0) == 0) {
            // Toggle a one-shot clip on/off: ON plays frame N->end and holds; OFF
            // drops back into the room's default loop. State is per-room (reset on
            // entry). Just mpv seeks — safe inline.
            std::string key = m_scene.dir + "|" + action;
            if (m_togglesOn.count(key)) { m_togglesOn.erase(key); applySceneLoop(); }
            else { m_togglesOn.insert(key); m_video.playFromFrame(std::stol(action.substr(7))); }
        } else if (action == "view:radiators") {
            enterRadiators();   // open the radiator control view (fins video + controls)
        }
        // future: "open_map", "open_trade", deploy:radiators, ...
    }

    // Shared ship-computer monitor: bezel + glowing screen + scanlines, in a
    // fullscreen host window. Fills s0/s1 with the inner screen rect and pushes a
    // clip. Both the desktop and the email app draw inside it. (Swap the drawn
    // bezel for an image later without touching any of the content layout.)
    void beginMonitor(ImVec2& s0, ImVec2& s1) {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(disp);
        ImGui::Begin("##computer", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Fit the monitor into the FREE band, clear of the right-side comm panel (+ its tab gutter)
        // so the terminal never slides underneath it. Centered within whatever space remains.
        float commInset = m_commOpen ? std::clamp(disp.x * 0.28f, 320.0f, 460.0f) + 44.0f : 0.0f;
        float availL = 0.0f, availR = disp.x - commInset, availW = availR - availL;
        float mw = availW * 0.92f, mh = disp.y * 0.86f;
        ImVec2 m0(availL + (availW - mw) * 0.5f, (disp.y - mh) * 0.5f), m1(m0.x + mw, m0.y + mh);
        dl->AddRectFilled(m0, m1, IM_COL32(22, 26, 30, 255), 10.0f);
        dl->AddRect(m0, m1, IM_COL32(60, 70, 80, 255), 10.0f, 0, 2.0f);
        s0 = ImVec2(m0.x + 16, m0.y + 16); s1 = ImVec2(m1.x - 16, m1.y - 16);
        dl->AddRectFilled(s0, s1, IM_COL32(5, 12, 13, 255), 6.0f);
        dl->AddRect(s0, s1, IM_COL32(40, 170, 150, 200), 6.0f, 0, 2.0f);
        for (float y = s0.y + 3; y < s1.y; y += 4)
            dl->AddLine(ImVec2(s0.x, y), ImVec2(s1.x, y), IM_COL32(20, 60, 55, 40));
        ImGui::PushClipRect(s0, s1, true);
    }
    void endMonitor() { ImGui::PopClipRect(); ImGui::End(); }

    // The always-present interface header: OS name (left) + credits/date (right).
    void monitorHeader(ImVec2 s0, ImVec2 s1, float pad, const char* leftLabel) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, s0.y + pad));
        ImGui::TextColored(ImVec4(0.45f, 0.92f, 0.85f, 1.0f), "%s", leftLabel);
        std::string hud = commas(m_dollars) + "     " + dateStr() + "   " + timeStr();
        float hw = ImGui::CalcTextSize(hud.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(s1.x - pad - hw, s0.y + pad));
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "%s", hud.c_str());
        dl->AddLine(ImVec2(s0.x + pad, s0.y + pad + 26), ImVec2(s1.x - pad, s0.y + pad + 26),
                    IM_COL32(40, 150, 135, 150), 1.0f);
    }

    // The console desktop: app icons (only Email for now).
    void renderConsole() {
        ImVec2 s0, s1; beginMonitor(s0, s1);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad = 26.0f;
        monitorHeader(s0, s1, pad, "AETHER DYNAMICS OS  //  PHOENIX X-9A \"ASCENDANT\"");

        // Email app icon.
        ImVec2 ip(s0.x + pad, s0.y + pad + 60), isz(100.0f, 96.0f);
        ImGui::SetCursorScreenPos(ip);
        ImGui::InvisibleButton("app_email", isz);
        bool hov = ImGui::IsItemHovered();
        drawEmailIcon(dl, ip, isz, hov, unreadCount());
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) m_screen = Screen::Email;

        renderShipOpsPanel(s0, s1, pad);

        ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, s1.y - pad - ImGui::GetTextLineHeight()));
        ImGui::TextDisabled("Click an app to open it.        Esc  -  return to the bridge");
        endMonitor();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_screen = Screen::Bridge;
    }

    // Ship Operations: the daily operating burn (from ship_economy.json), the balance, and runway.
    void renderShipOpsPanel(ImVec2 s0, ImVec2 s1, float pad) {
        float px = s0.x + pad + 170.0f;                 // right of the app icons
        float pw = (s1.x - pad) - px;
        ImGui::SetCursorScreenPos(ImVec2(px, s0.y + pad + 46.0f));
        ImGui::BeginChild("##shipops", ImVec2(pw, 360.0f), true);
        ImGui::TextColored(ImVec4(0.45f, 0.92f, 0.85f, 1.0f), "SHIP OPERATIONS");
        ImGui::SameLine(); ImGui::TextDisabled("- daily operating burn");
        ImGui::TextDisabled("Quantum drive: self-sustaining (no fuel cost)");
        ImGui::Separator();
        auto rightCr = [&](long v, ImVec4 col) {           // right-aligned credit value in the 2nd column
            std::string s = commas(v);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(s.c_str()).x);
            ImGui::TextColored(col, "%s", s.c_str());
        };
        ImGui::TextDisabled("Uncheck a line to stop paying it. Hover for what you lose.");
        if (ImGui::BeginTable("burn", 2, ImGuiTableFlags_SizingStretchProp)) {
            for (auto& e : m_econDaily) {
                bool archive = (e.id == "archive_subscription");
                bool on = lineActive(e);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                // Checkbox drives m_archiveAccess for the archive line, e.active for the rest. Locked if revoked.
                bool disabled = archive && m_archiveRevoked;
                ImGui::BeginDisabled(disabled);
                bool* target = archive ? &m_archiveAccess : &e.active;
                ImGui::Checkbox(("##chk" + e.id).c_str(), target);
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (on) ImGui::TextUnformatted(e.label.c_str());
                else    ImGui::TextDisabled("%s", e.label.c_str());
                // Tooltip: the consequence of dropping this line.
                if (ImGui::IsItemHovered() && !e.warn.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(320.0f);
                    ImGui::TextUnformatted(e.warn.c_str());
                    if (disabled) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "Access revoked - Aether Dynamics won't sell it back.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                if (on) rightCr(e.cost, ImVec4(0.85f, 0.80f, 0.70f, 1.0f));
                else { ImGui::TableSetColumnIndex(1);
                       ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("--").x);
                       ImGui::TextDisabled("--"); }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "TOTAL / DAY");
            rightCr(dailyBurnTotal(), ImVec4(0.95f, 0.78f, 0.35f, 1.0f));
            ImGui::EndTable();
        }
        ImGui::Separator();
        long burn = dailyBurnTotal();
        bool broke = m_dollars < 0;
        ImGui::Text("Balance");
        ImGui::SameLine(); ImGui::TextColored(broke ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.55f, 0.9f, 0.6f, 1.0f),
                                              "%s", commas(m_dollars).c_str());
        if (burn > 0) {
            long days = m_dollars / burn;
            ImGui::Text("Runway at current burn:");
            ImGui::SameLine();
            ImGui::TextColored(days < 30 ? ImVec4(1.0f, 0.6f, 0.35f, 1.0f) : ImVec4(0.7f, 0.8f, 0.9f, 1.0f),
                               "%ld days", days);
        }
        if (m_lastDailyBurn > 0) ImGui::TextDisabled("Last day charged: %s", commas(m_lastDailyBurn).c_str());
        ImGui::EndChild();
    }

    void drawEmailIcon(ImDrawList* dl, ImVec2 p, ImVec2 sz, bool hovered, int unread) {
        if (hovered) {
            dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(80, 160, 150, 45), 6.0f);
            dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(120, 200, 185, 190), 6.0f, 0, 1.5f);
        }
        // envelope
        float ew = sz.x * 0.68f, eh = ew * 0.64f;
        ImVec2 e0(p.x + (sz.x - ew) * 0.5f, p.y + 14), e1(e0.x + ew, e0.y + eh);
        ImVec2 mid((e0.x + e1.x) * 0.5f, (e0.y + e1.y) * 0.5f);
        dl->AddRectFilled(e0, e1, IM_COL32(205, 222, 236, 255), 3.0f);
        dl->AddRect(e0, e1, IM_COL32(90, 110, 130, 255), 3.0f, 0, 1.5f);
        dl->AddLine(e0, mid, IM_COL32(90, 110, 130, 255), 1.5f);
        dl->AddLine(ImVec2(e1.x, e0.y), mid, IM_COL32(90, 110, 130, 255), 1.5f);
        if (unread > 0) {
            ImVec2 bc(e1.x, e0.y);
            dl->AddCircleFilled(bc, 8.0f, IM_COL32(230, 60, 60, 255));
            std::string n = std::to_string(unread);
            ImVec2 ns = ImGui::CalcTextSize(n.c_str());
            dl->AddText(ImVec2(bc.x - ns.x * 0.5f, bc.y - ns.y * 0.5f), IM_COL32(255, 255, 255, 255), n.c_str());
        }
        const char* lbl = "Email";
        float lw = ImGui::CalcTextSize(lbl).x;
        dl->AddText(ImVec2(p.x + (sz.x - lw) * 0.5f, e1.y + 10),
                    IM_COL32(200, 230, 225, 255), lbl);
    }

    // Truncate a string with an ellipsis to fit a pixel width.
    static std::string fit(const std::string& s, float maxW) {
        if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
        std::string r = s;
        while (!r.empty() && ImGui::CalcTextSize((r + "...").c_str()).x > maxW) r.pop_back();
        return r + "...";
    }

    // The inbox: every arrived email in a list — status, subject, sender, date,
    // and the two target columns. Click a row to read it.
    void renderEmailInbox() {
        ImVec2 s0, s1; beginMonitor(s0, s1);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad = 26.0f;
        monitorHeader(s0, s1, pad, ("INBOX  //  " + std::to_string(arrivedCount()) + " messages").c_str());

        // Column layout (fractions of the content width).
        float W = (s1.x - pad) - (s0.x + pad);
        float wStatus = W * 0.04f, wSub = W * 0.31f, wSend = W * 0.20f,
              wDate = W * 0.14f, wTS = W * 0.155f, wTO = W * 0.155f;
        float ws[6] = { wStatus, wSub, wSend, wDate, wTS, wTO };
        auto colX = [&](int i) { float x = s0.x + pad; for (int k = 0; k < i; ++k) x += ws[k]; return x; };
        const ImU32 hcol = IM_COL32(90, 180, 165, 255);

        float hy = s0.y + pad + 42;
        dl->AddText(ImVec2(colX(1), hy), hcol, "Subject");
        dl->AddText(ImVec2(colX(2), hy), hcol, "From");
        dl->AddText(ImVec2(colX(3), hy), hcol, "Date");
        dl->AddText(ImVec2(colX(4), hy), hcol, "> Sender");
        dl->AddText(ImVec2(colX(5), hy), hcol, "> Objective");
        dl->AddLine(ImVec2(s0.x + pad, hy + 20), ImVec2(s1.x - pad, hy + 20), IM_COL32(40, 150, 135, 150), 1.0f);

        float rowH = ImGui::GetTextLineHeight() + 12;
        float rowY = hy + 28;
        for (int i = 0; i < (int)m_emails.size(); ++i) {
            const auto& e = m_emails[i];
            if (!e.arrived) continue;
            ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, rowY));
            ImGui::InvisibleButton(("mail" + std::to_string(i)).c_str(), ImVec2(W, rowH));
            if (ImGui::IsItemHovered()) {
                dl->AddRectFilled(ImVec2(s0.x + pad, rowY), ImVec2(s1.x - pad, rowY + rowH), IM_COL32(80, 160, 150, 35));
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (ImGui::IsItemClicked()) { m_openEmail = i; m_screen = Screen::EmailRead; }
            float ty = rowY + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            ImU32 txt = e.read ? IM_COL32(140, 175, 170, 255) : IM_COL32(215, 242, 234, 255);
            if (!e.read) dl->AddCircleFilled(ImVec2(colX(0) + 6, rowY + rowH * 0.5f), 4.0f, IM_COL32(230, 80, 80, 255));
            const ImU32 crs = IM_COL32(150, 200, 235, 255);
            dl->AddText(ImVec2(colX(1), ty), txt, fit(e.subject, wSub - 8).c_str());
            dl->AddText(ImVec2(colX(2), ty), txt, fit(e.sender, wSend - 8).c_str());
            dl->AddText(ImVec2(colX(3), ty), txt, e.date.c_str());
            dl->AddText(ImVec2(colX(4), ty), crs, e.courseSender.empty()    ? "-" : fit(e.courseSender, wTS - 8).c_str());
            dl->AddText(ImVec2(colX(5), ty), crs, e.courseObjective.empty() ? "-" : fit(e.courseObjective, wTO - 8).c_str());
            rowY += rowH;
        }

        ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, s1.y - pad - ImGui::GetTextLineHeight()));
        ImGui::TextDisabled("Click a message to read it.        Esc  -  back to the desktop");
        endMonitor();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_screen = Screen::Console;
    }

    // Read one email.
    void renderEmailReader() {
        if (m_openEmail < 0 || m_openEmail >= (int)m_emails.size()) { m_screen = Screen::Email; return; }
        GameEmail& e = m_emails[m_openEmail];
        e.read = true;
        ImVec2 s0, s1; beginMonitor(s0, s1);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad = 26.0f;
        const ImVec4 cyan(0.45f, 0.92f, 0.85f, 1.0f), body(0.72f, 0.90f, 0.88f, 1.0f),
                     dim(0.5f, 0.72f, 0.68f, 1.0f);
        float y = s0.y + pad;
        auto line = [&](ImVec4 col, const std::string& txt) {
            ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, y));
            ImGui::TextColored(col, "%s", txt.c_str());
            y += 20;
        };
        line(cyan, "MESSAGE");
        line(body, "From:    " + e.sender);
        line(body, "Subject: " + e.subject);
        line(dim,  "Date:    " + e.date);
        dl->AddLine(ImVec2(s0.x + pad, y + 4), ImVec2(s1.x - pad, y + 4), IM_COL32(40, 150, 135, 150), 1.0f);

        // Set-course actions. One neutral verb; the two slots name the role.
        bool hasSender = !e.courseSender.empty();
        bool hasObj    = !e.courseObjective.empty();
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(20, 70, 64, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(30, 110, 100, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(40, 140, 125, 255));
        ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(205, 242, 234, 255));
        ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, y + 14));
        ImGui::BeginDisabled(!hasSender);
        if (ImGui::Button(("Set Course  >  Sender:  " + (hasSender ? e.courseSender : std::string("none"))).c_str(), ImVec2(380, 0)))
            setCourse(e.courseSender, "Sender");
        ImGui::EndDisabled();
        float b1 = ImGui::GetItemRectMax().y;
        ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, b1 + 6));
        ImGui::BeginDisabled(!hasObj);
        if (ImGui::Button(("Set Course  >  Objective:  " + (hasObj ? e.courseObjective : std::string("none"))).c_str(), ImVec2(380, 0)))
            setCourse(e.courseObjective, "Objective");
        ImGui::EndDisabled();
        ImGui::PopStyleColor(4);
        float by = ImGui::GetItemRectMax().y;

        if (!m_course.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, by + 8));
            ImGui::TextColored(cyan, "Current course:  %s  (%s)", m_course.c_str(), m_courseRole.c_str());
            by = ImGui::GetItemRectMax().y;
        }

        ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, by + 14));
        ImGui::PushTextWrapPos(s1.x - pad);
        ImGui::TextColored(body, "%s", e.body.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetCursorScreenPos(ImVec2(s0.x + pad, s1.y - pad - ImGui::GetTextLineHeight()));
        ImGui::TextDisabled("Esc  -  back to the inbox");
        endMonitor();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_screen = Screen::Email;
    }

    // The starting inbox. Emails land on a timer once you're aboard.
    void setupEmails() {
        m_emails.clear();
        GameEmail w;
        w.sender = "Aether Dynamics Customer Care";
        w.subject = "Welcome aboard the Phoenix X-9A \"Ascendant\"";
        w.date = dateStr();
        w.courseSender = "Aether Dynamics HQ";   // the shipyard, if you want to visit
        w.courseObjective = "";                   // no third party — it's about your own ship
        w.arriveAt = 0.0f;   // lands the moment you take the bridge
        w.body =
            "Hello, Captain. Welcome aboard the Phoenix X-9A \"Ascendant\" - a fully equipped "
            "deep-space heavy explorer, built by Aether Dynamics Corporation and commissioned "
            "by yourself on 17 March 2147 for the price of $2,300,000.\n\n"
            "Please note: this interface displays your total credit balance in the upper right, "
            "along with the current date.\n\n"
            "The Ascendant is outfitted with a Quantum Singularity Core (Aether Dynamics QSC-9) "
            "drive engine, 1x Railgun battery, 4x Torpedo tubes, and 2x Particle Lance arrays, as "
            "you specified. She also carries a 20-ton cargo hold and a brand-new ADC android "
            "crew member.\n\n"
            "We at Aether Dynamics wish you happy sailing - and remember, you are covered by a "
            "7-year warranty and full customer-support plan.";
        m_emails.push_back(w);

        GameEmail t;
        t.sender = "Vex Orbital Salvage";
        t.subject = "FLASH SALE - Computer Components, 40% off this cycle";
        t.date = dateStr();
        t.courseSender = "Vex Orbital Salvage";   // set course here to go trade
        t.courseObjective = "";                    // the deal IS with the sender; no separate objective
        t.arriveAt = 60.0f;   // one minute after you come aboard
        t.body =
            "Captain - word travels fast out here. The moment a fresh Aether Dynamics explorer "
            "lights up our scopes, we come running.\n\n"
            "Vex Orbital Salvage is running a clearance on refurbished and surplus computer "
            "components this cycle only - sensor coprocessors, nav cores, targeting modules, and "
            "QSC-compatible control boards, all at 40% off catalog. Quality-tested, warranty-backed, "
            "and ready to ship anywhere in the Kepler Reach.\n\n"
            "Set us as a target to browse the full catalog, or reply and one of our agents will be "
            "in touch.\n\n"
            "Happy hunting.\n"
            "- Rennick Vex, Proprietor, Vex Orbital Salvage";
        m_emails.push_back(t);

        // A bounty notice — the case where Sender and Objective differ: set course to
        // Sol Security HQ to collect, or to the offender to hunt him down.
        GameEmail b;
        b.sender = "Sol Security - Warrant Division";
        b.subject = "BOUNTY: Dax \"Red\" Molloy - wanted for hijacking";
        b.date = dateStr();
        b.courseSender = "Sol Security HQ";       // where you claim the reward
        b.courseObjective = "Dax \"Red\" Molloy";   // the offender you pursue
        b.arriveAt = 120.0f;   // two minutes after you come aboard
        b.body =
            "Captain - your vessel is flagged as a licensed operator, so this warrant reaches "
            "you directly.\n\n"
            "Sol Security is posting a live bounty on Dax \"Red\" Molloy, wanted for the hijacking "
            "of the freighter Meridian's Hope and the deaths of two crew. He was last tracked "
            "adrift in the outer Kepler belt aboard a modified corvette.\n\n"
            "Reward: 45,000 credits, payable on confirmed capture or kill. Set course to the "
            "offender to begin pursuit, or to Sol Security HQ to review the full warrant.\n\n"
            "- Warrant Division, Sol Security";
        m_emails.push_back(b);
    }

    // ───────────────────────── textures ─────────────────────────
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

    // Species portraits: assets/species/<normalized-name>.jpg (lowercase, spaces -> underscores).
    static std::string speciesKey(const std::string& name) {
        std::string k;
        for (char c : name) {
            if (c == ' ')       k += '_';
            else if (c == '\'') continue;   // drop apostrophes: "Director's Assembly" -> directors_assembly
            else                k += (char)std::tolower((unsigned char)c);
        }
        return k;
    }
    // Load a species' portrait once (cached). Called from update() so the one-time GPU upload
    // never happens mid-frame. A missing image is remembered so we don't retry every scan.
    void ensureSpeciesImage(const std::string& species) {
        std::string key = speciesKey(species);
        if (key.empty() || m_speciesTried.count(key)) return;
        m_speciesTried.insert(key);
        Tex t{};
        if (loadImageTexture("assets/species/" + key + ".jpg", t)) m_speciesTex[key] = t;
    }
    const Tex* speciesImage(const std::string& species) const {
        auto it = m_speciesTex.find(speciesKey(species));
        return (it != m_speciesTex.end()) ? &it->second : nullptr;
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

    // ───────────────────────── video streaming texture ─────────────────────────
    // Builds the frame shown by the scene view / dialogue: a room background (video
    // frame or static image) with an optional white-keyed character composited on
    // top (m_fgVideo). Static-image rooms with no character need no m_vtex — the
    // poster is drawn directly, so this early-returns.
    // ── Radiator control view (radiator_fins.mp4 state machine) ──
    // Hold a static state on its rest frame (Deployed=160, Stowed=100, AuxOpen=200).
    void radHold(long frame, RadState state) {
        m_video.clearABLoop(); m_video.setLoop(false);
        m_video.seekToFramePause(frame);
        m_radState = state; m_radiatorsDeployed = (state != RadState::Stowed);
        m_radEndFrame = -1;
    }
    // Play a transition [from..to] once, then hold on `rest` (usually == to; retract jumps back to 160).
    void radPlay(long from, long to, long rest, RadState target) {
        m_video.clearABLoop(); m_video.setLoop(false);
        m_video.seekToFrame(from);      // seek + play
        m_radEndFrame = to; m_radRestFrame = rest; m_radState = target;
        m_radiatorsDeployed = (target != RadState::Stowed);
    }
    void enterRadiators() {
        m_screen = Screen::Radiators;
        m_radEndFrame = -1;
        if (m_video.open("assets/scenes/engine_room/radiator_fins.mp4")) {
            m_video.setMuted(false);   // play the fins' original audio during transitions (silent when held/paused)
            if      (m_radState == RadState::Stowed)  radHold(80, RadState::Stowed);
            else if (m_radState == RadState::AuxOpen) radHold(200, RadState::AuxOpen);
            else                                       radHold(160, RadState::Deployed);
        }
    }
    void exitRadiators() {
        m_screen = Screen::Bridge;
        m_pendingScene = "assets/scenes/engine_room";   // restore the room (and Ada, if present)
    }
    void updateRadiators() {
        if (!m_video.isOpen()) return;
        m_video.poll();
        if (m_video.newFrame && m_video.w > 0 && m_video.h > 0) {
            if (!m_vtex.descriptor || m_vtex.w != m_video.w || m_vtex.h != m_video.h) {
                vkDeviceWaitIdle(getContext().getDevice());
                freeVideoTex();
                createVideoTex(m_video.w, m_video.h);
            }
            uploadVideoFrame();
            m_video.newFrame = false;
        }
        if (m_radEndFrame >= 0) {   // a transition is running; when it reaches its end, hold the rest frame
            long cf = m_video.currentFrame();
            // Done when we hit the target frame OR the clip runs out (a range that plays to
            // the last frame — e.g. retract 200->289 on a 289-frame clip — never reaches an
            // out-of-range target, so end-of-playback also completes it).
            if ((cf >= 0 && cf >= m_radEndFrame) || m_video.eofReached()) {
                m_video.seekToFramePause(m_radRestFrame);
                m_radEndFrame = -1;
            }
        }
    }

    void updateSceneVideo() {
        if (m_video.isOpen()) {
            m_video.poll();
            // Apply a frame-range loop once fps is available (frames -> seconds).
            if (m_scene.loopEnd > 0 && !m_abLoopApplied) {
                double fps = m_video.fps();
                if (fps > 0) {
                    m_video.setABLoopSeconds(m_scene.loopStart / fps, (m_scene.loopEnd + 1) / fps);
                    m_abLoopApplied = true;
                }
            }
            // Skip a full-frame character clip's intro frames: seek past them and loop
            // [skip, end] so the fade-in never replays. Waits until fps + frame count decode.
            if (m_vidSkip > 0 && m_video.w > 0) {
                double fps = m_video.fps();
                long fc = m_video.frameCount();
                if (fps > 0 && fc > m_vidSkip) {
                    if (m_clipLoop) m_video.setABLoopSeconds(m_vidSkip / fps, fc / fps);  // loop [skip, end]
                    else            m_video.clearABLoop();                                // play once: skip intro, run to end, hold
                    m_video.seekToFrame(m_vidSkip);
                    m_vidSkip = 0;
                    m_video.newFrame = false;   // drop the pre-seek frame so the intro never flashes
                }
            }
            // Delayed loop region: arm ab-loop [loopFrom, loopTo] (loopTo -1 = clip end), then
            // optionally seek to m_clipStart. If we don't seek, the first pass plays from where it
            // is (frame 0) THROUGH loopTo, then wraps to loopFrom every pass after.
            //   kiss:          from=150, to=end,  no seek  -> play 0..end once, loop 150..end
            //   charge intro:  from=134, to=217,  no seek  -> play 0..217 once, loop 134..217
            //   charge (enter): from=134, to=217, seek 134 -> drop straight into the loop
            if (m_clipLoopFrom >= 0 && m_video.w > 0) {
                double fps = m_video.fps();
                long fc = m_video.frameCount();
                long to = (m_clipLoopTo >= 0) ? m_clipLoopTo : fc;
                if (fps > 0 && fc > m_clipLoopFrom) {
                    m_video.setABLoopSeconds(m_clipLoopFrom / fps, to / fps);
                    if (m_clipStart >= 0) { m_video.seekToFrame(m_clipStart); m_video.newFrame = false; }  // drop pre-seek frame
                    m_clipLoopFrom = m_clipLoopTo = m_clipStart = -1;
                }
            }
            // Charging FINISH clip: seek to the disconnection start and play to EOF (no loop).
            if (m_chFinishSeek >= 0 && m_video.w > 0) {
                m_video.clearABLoop();
                m_video.seekToFrame(m_chFinishSeek);
                m_chFinishSeek = -1;
                m_video.newFrame = false;   // drop the pre-seek frame so the intro pose never flashes
            }
            // When the finish clip reaches its end, hand off to her default science-lab clip.
            if (m_chargePhase == Charge::Finish && m_chFinishSeek < 0 && m_video.isOpen() && m_video.w > 0 &&
                (m_video.eofReached() || (m_video.frameCount() > 0 && m_video.currentFrame() >= m_video.frameCount() - 2))) {
                m_adaCharging = false;
                m_chargePhase = Charge::None;
                stopChargeAudio();
                refreshFullFrameClip();   // -> informative_science_lab
            }
        }
        if (m_fgVideo.isOpen()) m_fgVideo.poll();
        // Play a clip's baked audio only on its FIRST pass: when the loop wraps back to the
        // start (frame index jumps backwards), mute it so later loops are silent. Opening a
        // new clip resets m_fgPrevFrame to -1, so its first pass plays again. (Power clips
        // manage their own audio, so skip while a transition is in flight.)
        if (m_dlgAudio && m_fgVideo.isOpen() && m_powEnd < 0) {
            long cf = m_fgVideo.currentFrame();
            if (cf >= 0) {
                if (m_fgPrevFrame >= 0 && cf < m_fgPrevFrame - 2 && !m_fgVideo.muted())
                    m_fgVideo.setMuted(true);   // wrapped — first pass is done
                m_fgPrevFrame = cf;
            }
        }
        // Power up/down transition: when the power clip reaches its end, hold the dark
        // pose (down) or resume her mood (up).
        if (m_powEnd >= 0 && m_fgVideo.isOpen()) {
            long cf = m_fgVideo.currentFrame();
            if ((cf >= 0 && cf >= m_powEnd) || m_fgVideo.eofReached()) {
                m_powEnd = -1;
                if (m_powResume) refreshAdaClip();
                else m_fgVideo.seekToFramePause(m_powRest);
            }
        }
        // Deferred seek-and-hold: once the fg clip has decoded a frame, land it on the held frame.
        if (m_fgSeekHold >= 0 && m_fgVideo.isOpen() && m_fgVideo.w > 0) {
            m_fgVideo.seekToFramePause(m_fgSeekHold);
            m_fgSeekHold = -1;
        }
        // Deferred loop-range (e.g. charging 0-45): apply once the fg clip is loaded.
        if (m_fgLoopTo >= 0 && m_fgVideo.isOpen() && m_fgVideo.w > 0) {
            double f = m_fgVideo.fps(); if (f <= 0) f = 24.0;
            m_fgVideo.setLoop(true);
            m_fgVideo.setABLoopSeconds(m_fgLoopFrom / f, (m_fgLoopTo + 1) / f);
            m_fgVideo.seekToFrame(m_fgLoopFrom);
            m_fgLoopFrom = m_fgLoopTo = -1;
        }

        // Background source: the room video frame if playing, else the static image.
        const unsigned char* bg = nullptr; int W = 0, H = 0; bool bgNew = false;
        if (m_video.isOpen() && m_video.w > 0 && m_video.h > 0 && !m_video.pixels.empty()) {
            bg = m_video.pixels.data(); W = m_video.w; H = m_video.h; bgNew = m_video.newFrame;
        } else if (!m_bgImagePixels.empty()) {
            bg = m_bgImagePixels.data(); W = m_bgImageW; H = m_bgImageH;
        }
        bool haveFg = m_fgVideo.isOpen() && m_fgVideo.w > 0 && m_fgVideo.h > 0 && !m_fgVideo.pixels.empty();

        // Static-image room with no character: nothing to build; renderBridge draws the poster.
        if (!haveFg && !(m_video.isOpen() && m_video.w > 0)) return;
        if (!bg) return;

        bool fgNew = haveFg && m_fgVideo.newFrame;
        bool sized = m_vtex.descriptor && m_vtex.w == W && m_vtex.h == H;
        if (sized && !bgNew && !fgNew) return;      // nothing changed
        if (!sized) {
            vkDeviceWaitIdle(getContext().getDevice());
            freeVideoTex();
            createVideoTex(W, H);
        }
        if (haveFg) { compositeAdaOverRoom(bg, W, H); uploadPixels(m_composite.data()); }
        else        { uploadPixels(bg); }
        m_video.newFrame = false;
        if (haveFg) m_fgVideo.newFrame = false;
    }

    // White-key composite: for each pixel, drop near-white foreground pixels to
    // transparent (alpha from the min channel, feathered) and alpha-blend the rest
    // over the room. Foreground is nearest-sampled if its size differs from the room.
    void compositeAdaOverRoom(const unsigned char* bg, int W, int H) {
        m_composite.resize((size_t)W * H * 4);
        const unsigned char* fg = m_fgVideo.pixels.data();
        const int fw = m_fgVideo.w, fh = m_fgVideo.h;
        const bool green = (m_fgChroma == "green"), magenta = (m_fgChroma == "magenta");
        const int keyHi = 245, keyLo = 218;   // white: minC >= keyHi transparent; <= keyLo opaque
        const int chHi  = 45,  chLo  = 12;    // green/magenta: colour-excess thresholds

        // Pass 1: the raw matte (opacity) at the clip's native resolution.
        m_fgAlpha.resize((size_t)fw * fh);
        for (int i = 0; i < fw * fh; ++i) {
            int r = fg[i * 4], g = fg[i * 4 + 1], b = fg[i * 4 + 2];
            int a;
            if (green || magenta) {
                int d = green ? (g - std::max(r, b))       // green excess
                              : (std::min(r, b) - g);      // magenta excess
                a = d >= chHi ? 0 : d <= chLo ? 255 : 255 * (chHi - d) / (chHi - chLo);
            } else {                                       // white / luma key
                int minc = std::min(r, std::min(g, b));
                a = minc >= keyHi ? 0 : minc <= keyLo ? 255 : 255 * (keyHi - minc) / (keyHi - keyLo);
            }
            m_fgAlpha[i] = (unsigned char)a;
        }
        // Pass 2: choke the matte inward 1px (8-neighbour erosion) so the anti-aliased,
        // white-contaminated edge ring is dropped — this removes the white outline.
        m_fgAlphaEroded.resize((size_t)fw * fh);
        for (int y = 0; y < fh; ++y)
            for (int x = 0; x < fw; ++x) {
                int a = m_fgAlpha[y * fw + x];
                for (int dy = -1; dy <= 1 && a > 0; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && nx < fw && ny >= 0 && ny < fh)
                            a = std::min(a, (int)m_fgAlpha[ny * fw + nx]);
                    }
                m_fgAlphaEroded[y * fw + x] = (unsigned char)a;
            }
        // Pass 3: composite the keyed clip (nearest-sampled) over the room.
        for (int y = 0; y < H; ++y) {
            int fy = (fh == H) ? y : (int)((long long)y * fh / H);
            for (int x = 0; x < W; ++x) {
                int fx = (fw == W) ? x : (int)((long long)x * fw / W);
                size_t bi = ((size_t)y * W + x) * 4;
                size_t fp = (size_t)fy * fw + fx;
                int a = m_fgAlphaEroded[fp];
                unsigned char* o = &m_composite[bi];
                if (a == 0)        { o[0] = bg[bi]; o[1] = bg[bi + 1]; o[2] = bg[bi + 2]; }
                else {
                    int fr = fg[fp * 4], fgc = fg[fp * 4 + 1], fb = fg[fp * 4 + 2];
                    if (green && fgc > std::max(fr, fb)) fgc = std::max(fr, fb);   // despill: kill green tint on edges
                    if (a == 255) { o[0] = (unsigned char)fr; o[1] = (unsigned char)fgc; o[2] = (unsigned char)fb; }
                    else {
                        o[0] = (unsigned char)((fr  * a + bg[bi]     * (255 - a)) / 255);
                        o[1] = (unsigned char)((fgc * a + bg[bi + 1] * (255 - a)) / 255);
                        o[2] = (unsigned char)((fb  * a + bg[bi + 2] * (255 - a)) / 255);
                    }
                }
                o[3] = 255;
            }
        }
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
    bool createVideoTex(int w, int h) { return createVideoTexInto(m_vtex, w, h); }

    void uploadVideoFrame() { uploadPixelsInto(m_vtex, m_video.pixels.data()); }

    // Copy the frame into the persistently-mapped staging buffer (cheap CPU memcpy) and flag it.
    // The GPU copy is recorded into the frame's own command buffer by recordTexUpload() — NO
    // per-frame vkQueueWaitIdle stall (that was serialising CPU/GPU and tanking the frame rate).
    void uploadPixelsInto(VideoTex& t, const unsigned char* data) {
        if (!t.mapped || !t.image) return;
        std::memcpy(t.mapped, data, (size_t)t.w * t.h * 4);
        t.pendingUpload = true;
    }
    void uploadPixels(const unsigned char* data) { uploadPixelsInto(m_vtex, data); }

    // Record a pending staging->image copy into the FRAME command buffer (before the render pass).
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
    void freeVideoTex() { freeVideoTexInto(m_vtex); }

    // The contact portrait: a small feed of Clara's current-state clip, shown ONLY when she's
    // not visible on the main screen (flight/other room). When she IS on-screen we hold a static
    // first frame of her default clip. Drives the 300x300 box above the comm tabs.
    // A clip of her physically present in `room` (so the comm feed shows WHERE SHE ACTUALLY IS),
    // or "" if we have no footage for that room. Cargo bays share one clip.
    std::string roomPresenceClip(const std::string& room) const {
        std::string r = room;
        if (r.rfind("cargo_bay", 0) == 0) r = "cargo_bay";   // port/starboard share footage
        return clipFor(m_dlgNpcId, "informative_" + r);
    }
    void updatePortraitVideo() {
        if (m_dlgComposite) return;   // Clara (non-composite) only, for now
        // A transient reaction clip (e.g. amazed on an exceptional scan) overrides the portrait,
        // plays ONCE, and holds until the timer runs out — then the feed reverts to normal.
        bool reacting = m_portraitReactTimer > 0.0f && !m_portraitReactPath.empty();
        // On the main screen (in her room) we show a static first frame of idle. Off-screen —
        // flight / combat / scan, when the portrait is all we have of her — it plays her LIVE
        // current-state clip, derived from her mood/flags so it always mirrors how she actually is.
        bool onMain = !reacting && !m_flight.active() && inHerRoom() && !m_adaPoweredOff;
        std::string stateKey, want;
        if (reacting) {
            stateKey = "react:" + m_portraitReactPath;
            want = m_portraitReactPath;
        } else if (onMain) {
            stateKey = "idle-static";
            want = clipFor(m_dlgNpcId, "idle");
        } else {
            // Off-screen (flight/combat/scan, or simply in a different room than the Captain): this feed
            // is our only view of her. When she's calmly at her post, show WHERE SHE ACTUALLY IS (her
            // room clip); when she's genuinely feeling something, show that. Her mood still shows in the
            // STATE label beside the feed, so the two are decoupled.
            bool off = adaEffectivelyOff();
            bool baseline = m_dlgEmotion.empty() || m_dlgEmotion == "neutral" || m_dlgEmotion == "idle";
            std::string roomClip = (!off && !m_adaCharging && baseline) ? roomPresenceClip(m_adaRoom) : std::string();
            std::string base = off ? "power"
                             : (m_adaCharging ? "charging"
                             : (!roomClip.empty() ? ("at_" + m_adaRoom)          // marker: she's shown in that room
                             : (baseline ? "idle" : m_dlgEmotion)));
            stateKey = "state:" + base;
            if (stateKey != m_portraitStateKey) {   // state changed -> resolve the clip once (may be a random variant)
                want = !roomClip.empty() ? roomClip
                     : clipFor(m_dlgNpcId, off ? "power" : (m_adaCharging ? "charging" : (baseline ? "idle" : m_dlgEmotion)));
                if (want.empty()) want = clipFor(m_dlgNpcId, "idle");
            } else {
                want = m_portraitPath;               // same state -> keep the clip we're already playing
            }
        }
        m_portraitStateKey = stateKey;
        if (want.empty()) return;
        if (want != m_portraitPath) {
            m_portraitPath = want;
            if (m_portraitVideo.open(want)) {
                m_portraitVideo.setMuted(true);
                m_portraitVideo.setLoop(!onMain && !reacting);   // reaction plays once; off-screen state clip loops
            }
        }
        m_portraitStatic = onMain;
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
            if (m_portraitStatic) m_portraitVideo.setPaused(true);   // freeze the first frame while she's on-screen
        }
    }

private:
    ImGuiManager m_imgui;

    // 3D space-flight (Tab toggles it in the central pane). m_flightRect* is the pixel
    // rectangle the room video occupies, captured in renderBridge and reused for the 3D
    // viewport (one frame of lag, imperceptible).
    FlightMode m_flight;
    bool m_showScan = false;                  // the star-system scan panel is open
    bool m_flightInputOn = false;             // whether ship input is live this frame (cursor hidden) — HUD diagnostic
    galaxy::StarSystem m_scanStar;            // the star currently scanned (copied on scan)
    std::string        m_warpStarName;        // destination name for the FTL arrival message
    // Aether Dynamics archive access — the reference data that classifies raw spectral readings into
    // named worlds/species and provides the geo-profile BASELINE anomalies are measured against.
    // Provided at start; revoked if you break their rules (warranty void / doctrine violation).
    bool               m_archiveAccess = true;
    // Permanent cut-off (rule-break). A voluntary cancel just flips m_archiveAccess and can be undone;
    // a revoke sets this and locks re-subscription — per canon, a voided warranty can't buy access back.
    bool               m_archiveRevoked = false;
    // Geological deep scan — a second-tier scan of one world's resource lattice (Clara's idea).
    struct DeepScan {
        bool  active = false, reported = false;
        float t = 0.0f;                        // 0..1 analysis progress
        std::string planet, resource, lattice, origin;
        float constA = 0.0f, resonance = 0.0f; // lattice constant (A), extraction/resonance freq (THz)
        int   purity = 0;
        bool  anomalous = false;               // reads as artificial/harvested vs. natural
        long  yield = 0;                       // est. extraction value (credits)
    };
    DeepScan m_deep;
    static constexpr float kDeepScanSecs = 2.5f;
    // Sensor-commentary queue: rapid scans pile up here and Clara works through them one at a time
    // as she frees up — so nothing is SKIPPED, just delayed (better late than never).
    struct QueuedScan { galaxy::StarSystem sys; int sx, sy; };
    std::vector<QueuedScan> m_scanQueue;
    static constexpr int    kScanQueueMax = 10;   // cap the backlog so it can't grow absurd
    std::string             m_pendingScanSpecies; // attach the scanned species to Clara's next reply (portrait in the log)
    float m_flightRx = 0, m_flightRy = 0, m_flightRw = 0, m_flightRh = 0;
    float m_flightReactCooldown = 0.0f;   // rate-limit Eva's flight/combat reactions
    float m_scanReactCooldown   = 0.0f;   // rate-limit Clara's sensor/scan commentary
    float m_explSoundCD = 0.0f;           // rate-limit the explosion boom sound
    bool  m_flightCursorHidden = false;      // cursor hidden while flying (RMB reveals it)
    bool  m_flightCursorWasHidden = false;   // last applied OS cursor state
    long  m_fgPrevFrame = -1;                // loop-wrap detection for "play clip audio once"
};

int main() {
    try {
        SlagLegionApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
