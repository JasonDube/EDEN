#pragma once

// First-person 6-DOF space-flight mode, rendered into a sub-rectangle of the screen
// (the central pane where room video normally plays) and composited UNDER the ImGui
// interface. Self-contained: owns its ModelRenderer, ship/enemy/sphere state, input,
// and draw. main.cpp only calls init / update / render / toggle / recreate.
//
// The engine's built-in Camera is yaw/pitch only (no roll), so we keep the ship's own
// orientation as a quaternion and build the view matrix ourselves — true 6-DOF.
//
// Controls (per design): W/S thrust fwd/back, A/D pan (yaw), Q/E roll, X/Z throttle.
// (No pitch input by design; bank with roll then yaw to redirect. Easy to add later.)

#include "Renderer/ModelRenderer.hpp"
#include "Renderer/VulkanContext.hpp"
#include "Editor/GLBLoader.hpp"
#include "GalaxyGen.hpp"      // procedural starfield (stars -> planets -> moons, canonical resources)
#include <eden/Input.hpp>
#include <stb_image.h>        // load the explosion sprite sheet

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>
#include <string>

class FlightMode {
public:
    bool  active() const   { return m_active; }
    void  toggle()         { m_active = !m_active; if (m_active) { reset(); m_evEntered = true; } }
    void  setActive(bool a){ m_active = a; if (a) { reset(); m_evEntered = true; } }

    // Event polling so the game can drive NPC reactions to flight/combat.
    bool  consumeEntered()  { bool v = m_evEntered; m_evEntered = false; return v; }
    int   hitEvents() const { return m_evHits; }     // enemy hits accumulated since last clear
    void  clearHitEvents()  { m_evHits = 0; }
    int   consumeFired()    { int v = m_evFired; m_evFired = -1; return v; }  // battery that fired this frame, else -1
    int   consumeKills()    { int v = m_evKills; m_evKills = 0; return v; }   // enemies destroyed since last poll
    int   consumeExplosions() { int v = m_evExplosions; m_evExplosions = 0; return v; }  // blasts spawned since last poll
    int   scrap() const     { return m_scrap; }                              // scrap iron collected
    float throttle() const { return m_throttle; }
    float speed() const    { return m_speed; }
    int         gunBattery() const     { return m_gunBattery; }
    const char* gunBatteryName() const { return m_batteries.empty() ? "" : m_batteries[m_gunBattery].name; }
    int         score() const          { return m_score; }

    // View tier: Sector Map = look/rotate + scan only (no translation, no combat);
    // Solar System = full 6-DOF flight + weapons. Tab opens the Sector Map.
    enum class Mode { Sector, System };
    void setMode(Mode m) { m_mode = m; }
    Mode mode() const    { return m_mode; }
    bool inSector() const { return m_mode == Mode::Sector; }
    bool inSolarView() const { return m_mode == Mode::System && m_solar.active; }   // keyboard-flown, cursor-free
    glm::vec3 heading() const { return forward(); }   // for a HUD heading readout (diagnostics)

    // Starfield access for scan / explore UI.
    int   nearestStarIdx()  const { return m_nearestStar; }
    float nearestStarDist() const { return m_nearestDist; }
    const galaxy::StarSystem* nearestStar() const {
        return (m_nearestStar >= 0 && m_nearestStar < (int)m_stars.size()) ? &m_stars[m_nearestStar] : nullptr;
    }
    size_t starCount() const { return m_stars.size(); }
    const std::vector<galaxy::StarSystem>& stars() const { return m_stars; }

    // Re-roll the sector's starfield for a new (deterministic) seed — called when the ship
    // arrives in a different sector. densityMul/controlProb come from the destination's tier
    // (core = dense & civilized, rim/unexplored = sparse & wild). Reuses the per-colour sphere
    // models built at init; only the star instances change. Resets scan targeting.
    void regenField(uint32_t seed, float densityMul, float controlProb) {
        int count = std::max(20, (int)(120.0f * densityMul));
        auto all = galaxy::generateField(seed, count, kSectorRadius, controlProb, /*clearRadius*/150.0f);
        m_stars.clear();
        m_spheres.clear();
        for (auto& st : all)
            if (glm::length(st.pos) <= kSectorRadius) m_stars.push_back(st);
        for (auto& star : m_stars) {
            Sphere s;
            s.pos   = star.pos;
            s.scale = star.size;
            s.color = star.typeIndex;
            s.alive = true; s.respawnT = 0.0f; s.backdrop = false;
            m_spheres.push_back(s);
        }
        // Far REAL systems (with worlds) filling a shell beyond the sector — ~3x the near count,
        // rendered smaller/dimmer but fully scannable. Deterministic off the sector seed.
        galaxy::Rng brng((uint64_t)seed * 0x9E3779B97F4A7C15ull + 0xABCDEFull);
        auto randDir = [&brng]() {
            glm::vec3 d(brng.f(-1.0f, 1.0f), brng.f(-1.0f, 1.0f), brng.f(-1.0f, 1.0f));
            float L = glm::length(d);
            return (L > 1e-3f) ? d / L : glm::vec3(0, 0, -1);
        };
        int nearN = (int)m_stars.size();
        for (int i = 0; i < nearN * 3; ++i) {
            glm::vec3 pos = randDir() * (kSectorRadius * brng.f(1.15f, 2.2f));
            galaxy::StarSystem st = galaxy::genStar(brng, pos, controlProb);
            m_stars.push_back(st);
            Sphere s;
            s.pos = pos; s.scale = st.size; s.color = st.typeIndex;
            s.alive = true; s.respawnT = 0.0f; s.backdrop = true;
            m_spheres.push_back(s);
        }
        // Scenery DUST — even more, tiny, very distant; NO worlds, not scannable. Pure depth.
        m_bgStars.clear();
        if (!m_sphereModels.empty())
            for (int i = 0; i < nearN * 6; ++i) {
                BgStar b;
                b.pos   = randDir() * (kSectorRadius * brng.f(1.6f, 4.0f));
                b.color = brng.range(0, (int)m_sphereModels.size() - 1);
                b.scale = brng.f(1.0f, 2.6f);
                m_bgStars.push_back(b);
            }
        m_nearestStar = -1; m_nearestDist = 0.0f;
        if (!m_sphereModels.empty()) {
            m_sphereInstances.assign(m_sphereModels.size(), {});
            rebuildSphereInstances();
        }
    }
    float starDistance(int idx) const {
        return (idx >= 0 && idx < (int)m_stars.size()) ? glm::length(m_stars[(size_t)idx].pos - starEye()) : 0.0f;
    }

    // Engage the star FTL toward a scanned star (Sector Map only). arriveSeed re-seeds the field
    // on arrival (the destination locale). Locks the autopilot — the player can't steer during warp.
    void startStarWarp(const galaxy::StarSystem& sys, uint32_t arriveSeed) {
        if (m_mode != Mode::Sector || m_starWarp) return;
        m_starWarp = true; m_warpArrived = false; m_warpTime = 0.0f;
        m_warpSystem = sys; m_warpTarget = sys.pos; m_warpSeed = arriveSeed ? arriveSeed : 1u;
        // Seed the "tunnel dust" — extra transient streak stars strung along the travel direction so
        // the hyperspace tube is dense (these aren't real stars; they exist only during the warp).
        glm::vec3 to = sys.pos - m_pos;
        glm::vec3 f  = (glm::length(to) > 1e-3f) ? glm::normalize(to) : forward();
        glm::vec3 upRef = (std::abs(f.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 rt = glm::normalize(glm::cross(f, upRef));
        glm::vec3 u  = glm::cross(rt, f);
        m_warpStreaks.clear();
        for (int i = 0; i < 300; ++i) {
            float along = frand(-300.0f, 5000.0f), rad = frand(30.0f, 640.0f), ang = frand(0.0f, 6.28318f);
            m_warpStreaks.push_back(m_pos + f * along + (rt * std::cos(ang) + u * std::sin(ang)) * rad);
        }
    }
    bool  warping() const     { return m_starWarp; }
    float warpFactor() const  { return m_warpFactor; }
    bool  consumeArrived()    { bool v = m_warpArrived; m_warpArrived = false; return v; }

    // Screen-pick the star under pane-space pixel (mx,my). rect = the central pane in screen
    // pixels (same proj/view/viewport as render, so it matches what's drawn). You can click
    // anywhere on a star's disk (or within a small min radius). Returns index or -1.
    int pickStarScreen(float mx, float my, float rx, float ry, float rw, float rh,
                       glm::vec2* outScreen = nullptr) const {
        if (rw < 1.0f || rh < 1.0f) return -1;
        glm::vec3 se = starEye();   // stars are drawn with the parallax camera — pick against it too
        glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(70.0f), rw / rh, 0.2f, 8000.0f);
        glm::mat4 view = glm::lookAt(se, se + forward(), up());
        glm::mat4 vp = proj * view;
        const float tanHalf = std::tan(glm::radians(35.0f));
        int best = -1; float bestD = 1e18f;
        for (size_t i = 0; i < m_stars.size(); ++i) {
            glm::vec4 clip = vp * glm::vec4(m_stars[i].pos, 1.0f);
            if (clip.w <= 0.0001f) continue;                 // behind the camera
            float sx = rx + ((clip.x / clip.w) * 0.5f + 0.5f) * rw;
            float sy = ry + ((clip.y / clip.w) * 0.5f + 0.5f) * rh;
            float d  = (sx - mx) * (sx - mx) + (sy - my) * (sy - my);
            float dist    = glm::length(m_stars[i].pos - se);
            float floor   = (i < m_spheres.size() && m_spheres[i].backdrop) ? kBgMinAngular : kStarMinAngular;
            float effSize = std::max(m_stars[i].size, dist * floor);            // same size floor as the render
            float screenR = (effSize / dist) * (rh * 0.5f / tanHalf);            // matches the drawn disc
            float pickR   = screenR + 3.0f;                                      // click on/near the visible dot only
            if (d <= pickR * pickR && d < bestD) { bestD = d; best = (int)i; if (outScreen) *outScreen = glm::vec2(sx, sy); }
        }
        return best;
    }

    // Build the renderer + scene once. rp/extent = the swapchain render pass we draw into.
    void init(eden::VulkanContext& ctx, VkRenderPass rp, VkExtent2D extent, const std::string& enemyGlb) {
        m_renderer = std::make_unique<eden::ModelRenderer>(ctx, rp, extent);
        m_renderer->setDayNight(1.0f, 0.4f);   // grass shader: lighting = ambient*1.91, so 0.4 -> ~0.76
        m_renderer->setLights({});             // push the light UBO so the ambient reaches the GPU
        buildSphereField();
        buildPlanetModels();
        loadEnemy(enemyGlb);
        loadExplosions();
        loadScrap();
        reset();
    }
    // A palette of planet-coloured icospheres for the Solar System View (index = planetColorForType).
    void buildPlanetModels() {
        std::vector<eden::ModelVertex> verts; std::vector<uint32_t> idx;
        makeIcosphere(1.0f, 2, verts, idx);
        const glm::vec3 pal[] = {
            {0.55f,0.55f,0.58f}, {0.62f,0.36f,0.22f}, {0.20f,0.45f,0.85f}, {0.30f,0.66f,0.36f},
            {0.75f,0.85f,0.95f}, {0.80f,0.68f,0.42f}, {0.86f,0.56f,0.30f}, {0.70f,0.76f,0.30f},
            {0.86f,0.32f,0.20f}, {0.32f,0.26f,0.32f},
        };
        for (auto& c : pal) {
            auto vv = verts; for (auto& v : vv) v.color = glm::vec4(c, 1.0f);
            m_planetModels.push_back(m_renderer->createModel(vv, idx));
        }
    }
    void recreate(VkRenderPass rp, VkExtent2D extent) {
        if (m_renderer) m_renderer->recreatePipeline(rp, extent);
    }

    void reset() {
        m_pos = glm::vec3(0.0f);
        m_orient = glm::quat(1, 0, 0, 0);
        m_throttle = m_speed = m_targetSpeed = 0.0f;
        m_derelictPos = glm::vec3(0, 0, -1400);   // a foreground object waiting ~1.4k ahead
        m_derelictSpin = 0.0f;
        m_starWarp = false; m_warpArrived = false; m_warpFactor = 0.0f;
        m_warpTime = 0.0f; m_warpStreaks.clear();
    }

    void update(float dt, bool inputEnabled) {
        if (!m_active) return;
        using I = eden::Input;
        m_laserOn = false;   // reset each frame (so no stale beam renders when input is off)
        if (m_starWarp) { updateStarWarp(dt); return; }   // locked autopilot — no player control during FTL
        if (inputEnabled) {
            // Turning is always available — the Sector Map surveys by rotating in place.
            float pitch = 0, yaw = 0, roll = 0;
            if (I::isKeyDown(I::KEY_W)) pitch -= 1.0f;   // nose down
            if (I::isKeyDown(I::KEY_S)) pitch += 1.0f;   // nose up
            if (I::isKeyDown(I::KEY_A)) yaw   += 1.0f;   // yaw left
            if (I::isKeyDown(I::KEY_D)) yaw   -= 1.0f;   // yaw right
            if (I::isKeyDown(I::KEY_Q)) roll  += 1.0f;   // roll left
            if (I::isKeyDown(I::KEY_E)) roll  -= 1.0f;   // roll right
            const float pitchRate = glm::radians(55.0f), yawRate = glm::radians(55.0f), rollRate = glm::radians(90.0f);
            m_orient = glm::normalize(m_orient * glm::angleAxis(pitch * pitchRate * dt, glm::vec3(1, 0,  0)));
            m_orient = glm::normalize(m_orient * glm::angleAxis(yaw   * yawRate   * dt, glm::vec3(0, 1,  0)));
            m_orient = glm::normalize(m_orient * glm::angleAxis(roll  * rollRate  * dt, glm::vec3(0, 0, -1)));
            // Throttle: X/Z now cruises in BOTH modes (the Sector Map cruises between the stars,
            // gently; combat speed is faster). Weapons remain Solar-System only.
            if (I::isKeyDown(I::KEY_X)) m_throttle += 1.2f * dt;   // throttle up (forward)
            if (I::isKeyDown(I::KEY_Z)) m_throttle -= 1.2f * dt;   // throttle down (into reverse)
            m_throttle = glm::clamp(m_throttle, -0.35f, 1.0f);
            m_targetSpeed = m_throttle * (m_mode == Mode::System ? m_maxSpeed : m_cruiseSpeed);
            // Weapons in BOTH modes now — the guns are reinstated for the flyable Sector Map.
            for (int k = 0; k < (int)m_batteries.size() && k < 9; ++k)
                if (I::isKeyPressed(I::KEY_1 + k)) m_gunBattery = k;
            if (I::isKeyDown(I::KEY_SPACE)) {
                if (m_batteries[m_gunBattery].laser)   fireLaser(dt);
                else if (m_fireCooldown <= 0.0f)       fire();
            }
        }
        m_fireCooldown -= dt;
        // Ease toward target speed (a touch of inertia), then coast along forward — BOTH modes cruise.
        m_speed += (m_targetSpeed - m_speed) * glm::clamp(dt * 2.0f, 0.0f, 1.0f);
        m_pos += forward() * m_speed * dt;
        // Combat systems run in BOTH modes now (guns work in the Sector Map). No enemy in the
        // peaceful Solar System View.
        updateProjectiles(dt);
        if (!m_solar.active) updateEnemy(dt);
        updateExplosions(dt);
        updateScrap(dt);
        if (m_mode == Mode::System && m_solar.active) {
            // Planets orbit the star; moons orbit their planet.
            for (auto& p : m_solar.planets) {
                p.angle += p.speed * dt;
                p.pos = glm::vec3(std::cos(p.angle) * p.radius,
                                  std::sin(p.angle) * p.radius * p.incl,
                                  std::sin(p.angle) * p.radius);
                for (auto& m : p.moons) m.angle += m.speed * dt;
            }
        }
        if (m_mode == Mode::Sector) {
            // Sector cruise: the derelict tumbles slowly, and once you leave it well behind it
            // re-drops ahead (slightly off-axis) so there's always a carrot to fly toward.
            m_derelictSpin += dt * 0.25f;
            glm::vec3 toD = m_derelictPos - m_pos;
            if (glm::dot(toD, forward()) < 0.0f && glm::length(toD) > 800.0f)
                m_derelictPos = m_pos + forward() * 1500.0f + right() * frand(-220.0f, 220.0f) + up() * frand(-130.0f, 130.0f);
        }
        updateSpheres(dt);
        updateNearestStar();
    }

    // Locked-autopilot FTL toward a scanned star: ramp the warp (parallax -> full), lock heading
    // onto the target, and rush in. On arrival the field re-seeds around the destination.
    void updateStarWarp(float dt) {
        m_fireCooldown -= dt;
        m_warpTime += dt;
        const float kLockPhase = 1.3f, kWarpDuration = 4.6f;   // lock heading, then ride the tunnel
        // Turn to face the destination during the lock-on phase, then fly STRAIGHT through the tunnel
        // (don't keep re-aiming at the target, or passing it would U-turn the ship).
        if (m_warpTime < kLockPhase) {
            glm::vec3 to = m_warpTarget - m_pos;
            if (glm::length(to) > 1e-3f) {
                glm::quat want = glm::quatLookAtRH(glm::normalize(to), glm::vec3(0, 1, 0));
                m_orient = glm::normalize(glm::slerp(m_orient, want, glm::clamp(dt * 5.0f, 0.0f, 1.0f)));
            }
        }
        m_warpFactor = glm::clamp(m_warpTime / 1.6f, 0.0f, 1.0f);      // ramp the effect in over ~1.6s
        float warpSpeed = glm::mix(m_cruiseSpeed, 2600.0f, m_warpFactor);
        m_pos += forward() * warpSpeed * dt;
        // Recycle any tunnel dust that fell behind, back out ahead of the ship.
        glm::vec3 f = forward(), rt = right(), u = up();
        for (auto& p : m_warpStreaks)
            if (glm::dot(p - m_pos, f) < -250.0f) {
                float rad = frand(30.0f, 640.0f), ang = frand(0.0f, 6.28318f);
                p = m_pos + f * frand(3500.0f, 5200.0f) + (rt * std::cos(ang) + u * std::sin(ang)) * rad;
            }
        if (m_warpTime >= kWarpDuration) {         // drop out into the destination's Solar System View
            m_starWarp = false; m_warpArrived = true; m_warpFactor = 0.0f;
            regenField(m_warpSeed, 1.0f, 0.4f);    // fresh distant-sky backdrop
            enterSolarSystem(m_warpSystem);         // switch to System view among the star's real planets
        }
        updateSpheres(dt);
        updateNearestStar();
    }

    // ── Solar System View ────────────────────────────────────────────
    static int planetColorForType(const std::string& type, int hab) {
        auto has = [&](const char* k) { return type.find(k) != std::string::npos; };
        if (hab >= 60)                              return 3;  // garden green
        if (has("Ocean") || has("Water"))           return 2;  // ocean blue
        if (has("Ice")   || has("Frozen"))          return 4;  // ice
        if (has("Desert")|| has("Arid"))            return 5;  // desert tan
        if (has("Gas")   || has("Giant") || has("Jovian")) return 6;  // gas giant
        if (has("Toxic") || has("Greenhouse"))      return 7;  // toxic
        if (has("Volcanic")|| has("Lava") || has("Chthonian")) return 8;  // molten
        if (has("Iron")  || has("Metal"))           return 1;  // iron/rust
        return 0;                                              // rocky grey
    }
    // Build the destination system: star at origin, its real planets on widening orbits, moons on
    // sub-orbits. Places the ship out beyond the system for a good arrival view, looking inward.
    void enterSolarSystem(const galaxy::StarSystem& sys) {
        m_mode = Mode::System;
        m_solar = SolarSystem{};
        m_solar.active = true;
        m_solar.starColor = sys.typeIndex;
        m_solar.starSize  = 220.0f;
        galaxy::Rng orng((uint64_t)m_warpSeed * 1099511628211ull + 7u);
        float r = 700.0f;
        for (auto& p : sys.planets) {
            OrbitPlanet op;
            op.radius = r; r += orng.f(500.0f, 1100.0f);
            op.angle  = orng.f(0.0f, 6.2831f);
            op.speed  = orng.f(0.03f, 0.08f) * (700.0f / op.radius);   // outer planets orbit slower
            op.incl   = orng.f(-0.12f, 0.12f);                         // slight orbital tilt
            op.size   = 22.0f + (p.habitability >= 50 ? 14.0f : 0.0f) + orng.f(0.0f, 22.0f);
            op.color  = planetColorForType(p.type, p.habitability);
            op.pos    = glm::vec3(std::cos(op.angle) * op.radius, 0.0f, std::sin(op.angle) * op.radius);
            for (size_t m = 0; m < p.moons.size(); ++m) {
                OrbitMoon om;
                om.radius = op.size * 2.0f + orng.f(25.0f, 70.0f);
                om.angle  = orng.f(0.0f, 6.2831f);
                om.speed  = orng.f(0.4f, 1.0f);
                om.size   = op.size * orng.f(0.22f, 0.38f);
                op.moons.push_back(om);
            }
            m_solar.planets.push_back(op);
        }
        // Vantage: out past the system, up a little, looking at the star.
        m_pos    = glm::vec3(0.0f, r * 0.35f, r * 1.15f);
        m_orient = glm::quatLookAtRH(glm::normalize(m_solar.starPos - m_pos), glm::vec3(0, 1, 0));
        m_speed  = m_targetSpeed = m_throttle = 0.0f;
    }
    // Leave the Solar System View back to the Sector Map (the caller supplies the sector's seed).
    void exitToSector(uint32_t sectorSeed, float densityMul, float controlProb) {
        m_solar = SolarSystem{};
        m_mode = Mode::Sector;
        reset();
        regenField(sectorSeed, densityMul, controlProb);
    }

    // Target the star the ship is AIMED at (the one nearest the crosshair) so the player
    // turns to face a star and scans it. Falls back to nothing if none is within the cone.
    void updateNearestStar() {
        m_nearestStar = -1; m_nearestDist = 1e9f;
        glm::vec3 fwd = forward(), se = starEye();
        float bestDot = 0.985f;   // ~10-degree cone around the crosshair
        for (size_t i = 0; i < m_stars.size(); ++i) {
            glm::vec3 to = m_stars[i].pos - se;
            float len = glm::length(to);
            if (len < 1e-3f) continue;
            float dot = glm::dot(to / len, fwd);
            if (dot > bestDot) { bestDot = dot; m_nearestStar = (int)i; m_nearestDist = len; }
        }
    }

    // Draw the scene into pixel rect (rx,ry,rw,rh) within the swapchain pass, then restore
    // the full viewport/scissor so the ImGui pass that follows paints the whole screen.
    void render(VkCommandBuffer cmd, VkExtent2D fullExtent, float rx, float ry, float rw, float rh) {
        if (!m_active || !m_renderer || rw < 1.0f || rh < 1.0f) return;

        VkViewport vp{}; vp.x = rx; vp.y = ry; vp.width = rw; vp.height = rh; vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        VkRect2D   sc{}; sc.offset = {(int32_t)rx, (int32_t)ry}; sc.extent = {(uint32_t)rw, (uint32_t)rh};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);

        float aspect = rw / rh;
        glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(70.0f), aspect, 0.2f, 8000.0f);
        glm::mat4 view = glm::lookAt(m_pos, m_pos + forward(), up());
        glm::mat4 vpMat = proj * view;   // FOREGROUND camera (ships, derelicts, combat, effects)

        // Starfield — one draw PER STAR (not instanced): renderInstanced shares a single
        // instance buffer, so multiple groups per frame collapse onto the last group's
        // positions/colours. Per-star render() uses push-constant transforms — no conflict.
        // The stars use a PARALLAX camera (eye = starEye(), translating at only kStarParallax of the
        // ship's motion) so they barely drift and are never reached — a scannable backdrop.
        glm::vec3 se = starEye();
        glm::mat4 starVp = proj * glm::lookAt(se, se + forward(), up());
        bool inSolar = (m_mode == Mode::System && m_solar.active);
        // In the Solar System View there's no backdrop starfield — just the star and its worlds.
        if (inSolar) {
            // (skip the starfield entirely)
        }
        // In FTL the stars stretch into streaks along travel — the "hella fast" hyperspace look.
        else if (m_warpFactor > 0.03f) {
            glm::vec3 sdir = forward();
            float len = glm::mix(0.0f, 700.0f, m_warpFactor);
            for (auto& b : m_bgStars) {
                float thick = std::max(b.scale, glm::length(b.pos - se) * kBgMinAngular);
                renderStreak(cmd, starVp, m_sphereModels[b.color], b.pos, sdir, len * 0.6f, thick, 1.9f);
            }
            for (auto& s : m_spheres) {
                if (!s.alive) continue;
                float floor  = s.backdrop ? kBgMinAngular : kStarMinAngular;
                float thick  = std::max(s.scale, glm::length(s.pos - se) * floor);
                renderStreak(cmd, starVp, m_sphereModels[s.color], s.pos, sdir, len, thick, s.backdrop ? 2.4f : 3.0f);
            }
            // Extra tunnel dust (foreground, real camera): dense white-blue streaks that thicken the tube.
            if (!m_planetModels.empty())
                for (auto& p : m_warpStreaks)
                    renderStreak(cmd, vpMat, m_planetModels[4], p, forward(), len * 0.6f, 1.4f, 2.3f);
        } else {
            // Scenery dust first (tiny, faint), then the real stars — near ones bright/large, far
            // ones (backdrop) small/dim. All on the parallax camera so they read as a deep sky.
            for (auto& b : m_bgStars) {
                float dist  = glm::length(b.pos - se);
                float scale = std::max(b.scale, dist * kBgMinAngular);
                glm::mat4 m = glm::translate(glm::mat4(1.0f), b.pos) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
                m_renderer->render(cmd, starVp, m_sphereModels[b.color], m, 0.0f, 1.0f, 1.9f, /*twoSided*/ true);
            }
            for (auto& s : m_spheres) {
                if (!s.alive) continue;
                float dist   = glm::length(s.pos - se);
                float floor  = s.backdrop ? kBgMinAngular : kStarMinAngular;
                float bright = s.backdrop ? 2.4f : 3.0f;
                float scale  = std::max(s.scale, dist * floor);
                glm::mat4 m = glm::translate(glm::mat4(1.0f), s.pos) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
                m_renderer->render(cmd, starVp, m_sphereModels[s.color], m, 0.0f, 1.0f, bright, /*twoSided*/ true);
            }
        }
        // PROTOTYPE — a drifting derelict in the FOREGROUND (real camera / full parallax): it grows
        // and sweeps past as you cruise. That's the motion cue the (near-static) stars can't give.
        if (m_mode == Mode::Sector && m_enemyModel != UINT32_MAX) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), m_derelictPos)
                        * glm::rotate(glm::mat4(1.0f), m_derelictSpin, glm::vec3(0.2f, 1.0f, 0.15f))
                        * glm::scale(glm::mat4(1.0f), glm::vec3(m_derelictScale));
            m_renderer->render(cmd, vpMat, m_enemyModel, m, 0.0f, 1.0f, 1.2f, /*twoSided*/ true);
        }
        // Solar System View: the star (bright), its planets on their orbits, and moons around them.
        if (m_mode == Mode::System && m_solar.active && !m_planetModels.empty()) {
            glm::mat4 sm = glm::translate(glm::mat4(1.0f), m_solar.starPos)
                         * glm::scale(glm::mat4(1.0f), glm::vec3(m_solar.starSize));
            m_renderer->render(cmd, vpMat, m_sphereModels[m_solar.starColor], sm, 0.0f, 1.0f, 4.5f, /*twoSided*/ true);
            // Orbit path rings (faint), one per planet, in each planet's tilted plane.
            for (auto& p : m_solar.planets)
                renderRing(cmd, vpMat, m_planetModels[4], m_solar.starPos, p.radius, p.incl, 5.0f, 0.9f);
            for (auto& p : m_solar.planets) {
                glm::mat4 pm = glm::translate(glm::mat4(1.0f), p.pos) * glm::scale(glm::mat4(1.0f), glm::vec3(p.size));
                m_renderer->render(cmd, vpMat, m_planetModels[p.color], pm, 0.0f, 1.0f, 1.7f, /*twoSided*/ true);
                for (auto& m : p.moons) {
                    glm::vec3 mp = p.pos + glm::vec3(std::cos(m.angle) * m.radius, 0.0f, std::sin(m.angle) * m.radius);
                    glm::mat4 mm = glm::translate(glm::mat4(1.0f), mp) * glm::scale(glm::mat4(1.0f), glm::vec3(m.size));
                    m_renderer->render(cmd, vpMat, m_planetModels[0], mm, 0.0f, 1.0f, 1.5f, /*twoSided*/ true);
                }
            }
        }
        // Enemy ship (brightens briefly when hit) — Sector Map combat only, not in the Solar View.
        if (m_enemyModel != UINT32_MAX && m_enemyAlive && !m_starWarp && !m_solar.active) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), m_enemyPos) * glm::toMat4(m_enemyOrient)
                        * glm::scale(glm::mat4(1.0f), glm::vec3(m_enemyScale));
            float bright = (m_enemyHitFlash > 0.0f) ? 3.0f : 1.0f;
            m_renderer->render(cmd, vpMat, m_enemyModel, m, 0.0f, 1.0f, bright, /*twoSided*/ true);
        }
        // Scrap iron chunks tumbling in the void.
        if (m_scrapModel != UINT32_MAX) {
            for (auto& s : m_scrapItems) {
                glm::mat4 m = glm::translate(glm::mat4(1.0f), s.pos)
                            * glm::rotate(glm::mat4(1.0f), s.spin, glm::vec3(0.3f, 1.0f, 0.2f))
                            * glm::scale(glm::mat4(1.0f), glm::vec3(2.2f));
                m_renderer->render(cmd, vpMat, m_scrapModel, m, 0.0f, 1.0f, 1.0f, /*twoSided*/ true);
            }
        }
        // Weapon bolts — glowing streaks along their travel (so the corner origin reads).
        for (auto& p : m_bolts)
            renderStreak(cmd, vpMat, m_sphereModels[p.color], p.pos, p.vel, 3.0f, 0.45f, 2.0f);
        // Laser beams (gun 2) — two separated beams, each a bright core inside a wider glow.
        if (m_laserOn) {
            uint32_t col = m_sphereModels[m_batteries[m_gunBattery].color];
            for (int i = 0; i < 2; ++i) {
                glm::vec3 c = (m_laserA[i] + m_laserB[i]) * 0.5f;
                glm::vec3 d = m_laserB[i] - m_laserA[i];
                float hl = glm::length(d) * 0.5f;
                renderStreak(cmd, vpMat, col, c, d, hl, 1.7f, 1.2f);   // outer glow halo
                renderStreak(cmd, vpMat, col, c, d, hl, 0.7f, 3.5f);   // bright inner core
            }
        }

        // Explosions — camera-facing billboards playing the sprite sheet (drawn last so the
        // transparent blast composites over the scene). Frame steps through the 64 cells.
        if (!m_explVariants.empty()) {
            for (auto& e : m_explosions) {
                auto& frames = m_explVariants[e.variant];
                int nf = (int)frames.size();
                int f = (int)(e.t / m_explDuration * (float)nf);
                if (f < 0) f = 0; else if (f >= nf) f = nf - 1;
                glm::mat4 bb(1.0f);
                bb[0] = glm::vec4(right()   * e.size, 0.0f);
                bb[1] = glm::vec4(up()      * e.size, 0.0f);
                bb[2] = glm::vec4(forward() * e.size, 0.0f);
                bb[3] = glm::vec4(e.pos, 1.0f);
                m_renderer->render(cmd, vpMat, frames[f], bb, 0.0f, /*saturation*/ 1.4f, /*brightness*/ 1.9f,
                                   /*twoSided*/ false, /*indoor*/ false, /*transparent*/ false, /*additive*/ true);
            }
        }

        VkViewport fv{}; fv.width = (float)fullExtent.width; fv.height = (float)fullExtent.height; fv.minDepth = 0.0f; fv.maxDepth = 1.0f;
        VkRect2D   fs{}; fs.extent = fullExtent;
        vkCmdSetViewport(cmd, 0, 1, &fv);
        vkCmdSetScissor(cmd, 0, 1, &fs);
    }

private:
    struct Sphere  { glm::vec3 pos; float scale; int color; bool alive; float respawnT; bool backdrop; };
    struct BgStar  { glm::vec3 pos; float scale; int color; };   // far scenery dust (no worlds, not scannable)
    // Solar System View — the destination you drop into after an FTL warp. A large flyable volume
    // with the star's real planets orbiting it (and moons orbiting them).
    struct OrbitMoon   { float radius, angle, speed, size; };
    struct OrbitPlanet { float radius, angle, speed, size, incl; int color; glm::vec3 pos; std::vector<OrbitMoon> moons; };
    struct SolarSystem { bool active = false; glm::vec3 starPos{0}; float starSize = 200.0f; int starColor = 0;
                         std::vector<OrbitPlanet> planets; };
    struct Bolt    { glm::vec3 pos; glm::vec3 vel; float life; int color; };
    struct Battery { const char* name; int color; float cooldown; float speed; bool laser; };

    std::unique_ptr<eden::ModelRenderer> m_renderer;
    std::vector<uint32_t> m_sphereModels;   // one model per star-type colour (reused for bolts/beams)
    std::vector<Sphere>   m_spheres;        // render proxies for the starfield (index-parallel to m_stars)
    std::vector<BgStar>   m_bgStars;        // distant, smaller, ~3x-as-many backdrop stars (scenery, not scannable)
    std::vector<uint32_t> m_planetModels;   // per-colour planet spheres (for the Solar System View)
    SolarSystem           m_solar;          // the destination system currently being flown, if any
    galaxy::StarSystem    m_warpSystem;     // the star (with worlds) we're warping to
    std::vector<std::vector<eden::InstanceData>> m_sphereInstances;   // per-colour, prebuilt (stars are static)
    // Procedural galaxy: each star in m_stars has a render proxy in m_spheres at the same index.
    std::vector<galaxy::StarSystem> m_stars;
    int   m_nearestStar = -1;               // index of the closest star to the ship (for scan/explore)
    float m_nearestDist = 1e9f;
    bool  m_starTargets = false;            // stars are navigable scenery, not weapons targets

    // Weapons: selectable gun batteries. One (Ion) is a continuous laser beam.
    std::vector<Bolt> m_bolts;
    std::vector<Battery> m_batteries = {
        {"Pulse",   0, 0.12f, 260.0f, false},   // red   — rapid tracer bolts
        {"Ion",     1, 0.10f, 0.0f,   true },    // blue  — continuous LASER beam
        {"Scatter", 2, 0.07f, 200.0f, false},    // green — very rapid, slow bolts
        {"Rail",    3, 0.55f, 600.0f, false},    // amber — heavy, slow-fire, fast bolts
    };
    int   m_gunBattery = 0;
    float m_fireCooldown = 0.0f;
    int   m_score = 0;
    float m_enemyHitFlash = 0.0f;
    bool  m_evEntered = false;   // flight was just entered (for NPC reaction)
    int   m_evHits = 0;          // enemy hits since the game last cleared them
    int   m_evFired = -1;        // battery index that fired this frame (for the gun sound), else -1
    bool  m_laserOn = false;     // gun-2 beam active this frame
    float m_laserHitAccum = 0.0f;
    float m_laserSoundCD = 0.0f;
    glm::vec3 m_laserA[2]{}, m_laserB[2]{};   // the two beam segments, for rendering

    uint32_t   m_enemyModel = UINT32_MAX;
    glm::vec3  m_enemyPos{0, 0, -80};
    glm::quat  m_enemyOrient{1, 0, 0, 0};
    glm::vec3  m_enemyVel{0};
    glm::vec3  m_enemyTarget{0, 0, -80};
    float      m_enemyScale = 1.0f;
    float      m_enemyRetarget = 0.0f;
    bool       m_enemyAlive = true;
    float      m_enemyHealth = 100.0f, m_enemyMaxHealth = 100.0f;
    float      m_enemyRespawn = 0.0f;
    int        m_evKills = 0;
    int        m_evExplosions = 0;

    // Explosion: each sprite sheet loaded as 64 textured billboard quads; several variants
    // for visual variety, one picked at random per blast.
    struct Explosion { glm::vec3 pos; float t; float size; int variant; };
    std::vector<Explosion> m_explosions;
    std::vector<std::vector<uint32_t>> m_explVariants;
    const float m_explDuration = 0.9f;

    // Scrap iron dropped on a kill; drifts, magnetises to the ship, collected on contact.
    struct Scrap { glm::vec3 pos; glm::vec3 vel; float spin; };
    std::vector<Scrap> m_scrapItems;
    uint32_t m_scrapModel = UINT32_MAX;
    int m_scrap = 0;

    bool       m_active = false;
    Mode       m_mode = Mode::Sector;   // Tab opens the Sector Map (look-only); System view is entered from a star
    glm::vec3  m_pos{0};
    glm::quat  m_orient{1, 0, 0, 0};
    float      m_throttle = 0, m_speed = 0, m_targetSpeed = 0;
    const float m_maxSpeed = 260.0f;   // exploration scale (starfield spans ~1400 units)
    static constexpr float kSectorRadius = 420.0f;   // stars beyond this are culled — no distant faint "empty circle" dots
    static constexpr float kStarMinAngular = 0.022f; // min apparent size so even the small blue/white types stay clearly visible (SIZE, not brightness)
    static constexpr float kBgMinAngular   = 0.008f; // backdrop stars keep a much smaller floor — they read as faint distant dots

    // ── Sector-Map cruise prototype ──────────────────────────────────
    // The stars are drawn with a PARALLAX camera whose eye translates at only kStarParallax of the
    // ship's real motion — so as you cruise they barely drift and are never reached (a scannable
    // backdrop). Foreground objects (the derelict) use the real eye, so they grow and sweep past —
    // that foreground motion is what actually sells "we're going somewhere."
    static constexpr float kStarParallax = 0.02f;
    const float m_cruiseSpeed = 120.0f;              // Sector-Map top cruise speed (gentler than combat)
    glm::vec3   m_derelictPos{0, 0, -1400};          // a drifting foreground object ~1.4k ahead
    float       m_derelictScale = 16.0f;
    float       m_derelictSpin  = 0.0f;
    // Star FTL warp: engaged from a scanned star. Parallax lerps to full (m_warpFactor 0->1) so the
    // stars stop being a backdrop and rush past; a locked autopilot flies straight in; on arrival the
    // field re-seeds around the destination. The eye's parallax follows m_warpFactor.
    bool        m_starWarp = false;
    bool        m_warpArrived = false;               // one-shot arrival flag (app consumes it)
    glm::vec3   m_warpTarget{0};
    uint32_t    m_warpSeed = 1u;                      // sector seed to regenerate on arrival
    float       m_warpFactor = 0.0f;                  // 0 = normal parallax, 1 = full (stars are real space)
    float       m_warpTime = 0.0f;                    // seconds into the warp (drives a fixed-duration trip)
    std::vector<glm::vec3> m_warpStreaks;             // transient "tunnel dust" streak stars, warp-only
    glm::vec3 starEye() const { return m_pos * glm::mix(kStarParallax, 1.0f, m_warpFactor); }

    glm::vec3 forward() const { return m_orient * glm::vec3(0, 0, -1); }
    glm::vec3 up()      const { return m_orient * glm::vec3(0, 1,  0); }
    glm::vec3 right()   const { return m_orient * glm::vec3(1, 0,  0); }
    static float frand(float a, float b) { return a + (b - a) * (std::rand() / (float)RAND_MAX); }
    static unsigned char srgb8(unsigned char v) {   // linear->sRGB encode a 0..255 channel
        float c = v / 255.0f;
        float e = (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
        int o = (int)(e * 255.0f + 0.5f);
        return (unsigned char)(o < 0 ? 0 : (o > 255 ? 255 : o));
    }
    static unsigned char boost8(unsigned char v) {  // brighten a channel (counter the ambient dim)
        int b = (int)(v * 1.4f + 0.5f);
        return (unsigned char)(b > 255 ? 255 : b);
    }

    void buildSphereField() {
        std::vector<eden::ModelVertex> verts; std::vector<uint32_t> idx;
        makeIcosphere(1.0f, 2, verts, idx);
        // One glowing model per star TYPE colour (also reused for bolts/beams by index).
        for (auto& st : galaxy::starTypes()) {
            auto vv = verts; for (auto& v : vv) v.color = glm::vec4(st.color, 1.0f);
            m_sphereModels.push_back(m_renderer->createModel(vv, idx));
        }
        // Fill the starfield for the starting sector (deterministic seed). The models above are
        // created once; regenField() re-rolls just the stars whenever the ship changes sector.
        regenField(/*seed*/1u, /*densityMul*/1.0f, /*controlProb*/0.4f);
    }

    void rebuildSphereInstances() {
        for (auto& g : m_sphereInstances) g.clear();
        for (auto& s : m_spheres) {
            if (!s.alive) continue;
            // Visibility is a SIZE floor, not a brightness one: far stars get a minimum apparent
            // size so they stay a clear dot, while colour renders exactly like the shooting range.
            float dist  = glm::length(s.pos - m_pos);
            float scale = std::max(s.scale, dist * kStarMinAngular);
            eden::InstanceData inst;
            inst.model = glm::translate(glm::mat4(1.0f), s.pos) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
            // Grass shader: baseColor *= colorAdjust.z BEFORE lighting (~0.76). z~1.8 lifts every
            // star to a bright, clearly-visible dot; saturated cool colours keep them distinct.
            inst.colorAdjust = glm::vec4(0.0f, 1.0f, 1.8f, 0.0f);
            m_sphereInstances[s.color].push_back(inst);
        }
    }
    void updateSpheres(float dt) {
        for (auto& s : m_spheres) {
            if (!s.alive) {
                s.respawnT -= dt;
                if (s.respawnT <= 0.0f) {   // warp a fresh target back into the field
                    s.pos   = glm::vec3(frand(-600, 600), frand(-600, 600), frand(-600, 600));
                    s.scale = frand(3.0f, 18.0f);
                    s.alive = true;
                }
            }
        }
        rebuildSphereInstances();   // cheap (~220 mat4s); keeps the draw data current
    }
    // A shot pops a target sphere: score, explosion, a scrap chunk, then it respawns later.
    void destroySphere(Sphere& s) {
        s.alive = false; s.respawnT = 4.0f;
        m_score++;
        spawnExplosion(s.pos, s.scale * 3.0f);
        spawnScrap(s.pos, 1);
    }

    void loadEnemy(const std::string& path) {
        auto res = eden::GLBLoader::load(path);
        if (!res.success || res.meshes.empty()) return;
        const auto& mesh = res.meshes[0];
        m_enemyModel = m_renderer->createModel(mesh.vertices, mesh.indices);
        glm::vec3 ext = mesh.bounds.max - mesh.bounds.min;
        float maxdim = std::max(ext.x, std::max(ext.y, ext.z));
        m_enemyScale = (maxdim > 0.001f) ? (12.0f / maxdim) : 1.0f;   // normalize to ~12 units
        m_enemyAlive = true; m_enemyHealth = m_enemyMaxHealth;
    }

    // Load the three explosion variants (8x8 sheets) as 64 billboard quads each.
    void loadExplosions() {
        const char* sheets[] = {"assets/flight/explosion1_sheet.png",
                                "assets/flight/explosion2_sheet.png",
                                "assets/flight/explosion3_sheet.png"};
        for (const char* s : sheets) {
            auto frames = loadSheet(s);
            if (!frames.empty()) m_explVariants.push_back(std::move(frames));
        }
    }

    // One 8x8 sheet -> 64 textured billboard quads. Black keyed to transparent (alpha =
    // brightest channel); RGB brightened then sRGB pre-encoded so the _SRGB texture's decode
    // is a no-op (the renderer outputs to a linear swapchain — vertex-coloured geometry skips
    // that decode, which is why it looked right and the texture read dim/red).
    std::vector<uint32_t> loadSheet(const std::string& sheetPath) {
        std::vector<uint32_t> out;
        int w = 0, h = 0, ch = 0;
        unsigned char* img = stbi_load(sheetPath.c_str(), &w, &h, &ch, 4);
        if (!img) return out;
        const int cols = 8, rows = 8, cell = w / cols;
        std::vector<eden::ModelVertex> qv = {
            {{-0.5f, -0.5f, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}},
            {{ 0.5f, -0.5f, 0}, {0, 0, 1}, {1, 1}, {1, 1, 1, 1}},
            {{ 0.5f,  0.5f, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}},
            {{-0.5f,  0.5f, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}},
        };
        // Single winding: the additive pipeline uses no backface culling, so one set of
        // tris shows at any orientation (two sets would draw twice and double the glow).
        std::vector<uint32_t> qi = {0, 1, 2, 0, 2, 3};
        std::vector<unsigned char> cellPix((size_t)cell * cell * 4);
        for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
            for (int y = 0; y < cell; ++y) for (int x = 0; x < cell; ++x) {
                const unsigned char* p = img + (((size_t)(r * cell + y) * w) + (c * cell + x)) * 4;
                unsigned char* d = &cellPix[((size_t)y * cell + x) * 4];
                d[3] = std::max(p[0], std::max(p[1], p[2]));
                d[0] = srgb8(boost8(p[0])); d[1] = srgb8(boost8(p[1])); d[2] = srgb8(boost8(p[2]));
            }
            out.push_back(m_renderer->createModel(qv, qi, cellPix.data(), cell, cell));
        }
        stbi_image_free(img);
        return out;
    }

    void loadScrap() {
        std::vector<eden::ModelVertex> v; std::vector<uint32_t> i;
        makeIcosphere(1.0f, 1, v, i);                       // low-poly chunk
        for (auto& vert : v) vert.color = glm::vec4(0.46f, 0.42f, 0.38f, 1.0f);   // dull iron
        m_scrapModel = m_renderer->createModel(v, i);
    }

    // Twin guns sit at the bottom corners of the view (port + starboard) and CONVERGE on
    // the crosshair (straight ahead at `converge` units). +up -> lower corners on screen.
    void gunMuzzles(glm::vec3& aim, glm::vec3 mz[2]) const {
        const float gunX = 5.0f, gunDown = 4.0f, gunFwd = 3.0f, converge = 260.0f;
        aim = m_pos + forward() * converge;
        glm::vec3 base = m_pos + forward() * gunFwd + up() * gunDown;
        mz[0] = base - right() * gunX;   // bottom-left  (port)
        mz[1] = base + right() * gunX;   // bottom-right (starboard)
    }

    void fire() {
        const Battery& b = m_batteries[m_gunBattery];
        glm::vec3 aim, mz[2]; gunMuzzles(aim, mz);
        float shipSpeed = std::max(0.0f, m_speed);
        for (const glm::vec3& m : mz) {
            Bolt bolt;
            bolt.pos   = m;
            bolt.vel   = glm::normalize(aim - m) * (b.speed + shipSpeed);
            bolt.life  = 3.0f;
            bolt.color = b.color;
            m_bolts.push_back(bolt);
        }
        while (m_bolts.size() > 400) m_bolts.erase(m_bolts.begin());
        m_evFired = m_gunBattery;
        m_fireCooldown = b.cooldown;
    }

    // Continuous laser: two beams from the corner guns, hitscan against the enemy (damage
    // while dwelling on target), sound retriggered on the battery's cadence.
    void fireLaser(float dt) {
        m_laserOn = true;
        const Battery& b = m_batteries[m_gunBattery];
        // Two PARALLEL beams from widely-spaced corner guns — clear space between them; they
        // converge at the crosshair by perspective. Hitscan uses the CENTRE ray, so the laser
        // damages whatever the crosshair is on regardless of the beams' visual spread.
        const float lgunX = 8.0f, lgunDown = 4.0f, lgunFwd = 3.0f;
        glm::vec3 dir = forward();
        glm::vec3 lbase = m_pos + dir * lgunFwd + up() * lgunDown;
        m_laserA[0] = lbase - right() * lgunX; m_laserB[0] = m_laserA[0] + dir * 3000.0f;
        m_laserA[1] = lbase + right() * lgunX; m_laserB[1] = m_laserA[1] + dir * 3000.0f;
        bool onTarget = false;
        if (m_enemyModel != UINT32_MAX && m_enemyAlive) {
            float t = glm::dot(m_enemyPos - m_pos, dir);
            if (t > 0.0f && glm::length((m_pos + dir * t) - m_enemyPos) < m_enemyScale * 8.0f) onTarget = true;
        }
        if (onTarget) {
            m_enemyHitFlash = 0.15f;
            m_laserHitAccum += dt;
            if (m_laserHitAccum >= 0.12f) { m_laserHitAccum = 0.0f; m_score++; m_evHits++; damageEnemy(9.0f); }
        } else m_laserHitAccum = 0.0f;
        // Stars are navigable scenery, not weapons targets (unless m_starTargets is enabled).
        if (m_starTargets)
            for (auto& s : m_spheres) {
                if (!s.alive) continue;
                float ts = glm::dot(s.pos - m_pos, dir);
                if (ts > 0.0f && glm::length((m_pos + dir * ts) - s.pos) < s.scale) destroySphere(s);
            }
        m_laserSoundCD -= dt;
        if (m_laserSoundCD <= 0.0f) { m_evFired = m_gunBattery; m_laserSoundCD = b.cooldown; }
    }

    // Draw a glowing stretched ellipsoid oriented along `dir` — used for tracer bolts and
    // laser beams so shots read as streaks (and their corner origin is visible).
    // A faint orbit ring in the star's orbital plane (segmented, matching the planet's tilt).
    void renderRing(VkCommandBuffer cmd, const glm::mat4& vp, uint32_t model,
                    glm::vec3 center, float radius, float incl, float thick, float bright) {
        const int N = 200;                       // dense enough to read as a smooth circle
        glm::vec3 prev(0.0f);
        for (int i = 0; i <= N; ++i) {
            float a = (float)i / (float)N * 6.28318531f;
            glm::vec3 pt = center + glm::vec3(std::cos(a) * radius, std::sin(a) * radius * incl, std::sin(a) * radius);
            if (i > 0) {
                glm::vec3 dir = pt - prev;
                // overlap each segment past its neighbour (x1.3 half-length) so the joints have no gaps
                renderStreak(cmd, vp, model, (prev + pt) * 0.5f, dir, glm::length(dir) * 0.65f, thick, bright);
            }
            prev = pt;
        }
    }
    void renderStreak(VkCommandBuffer cmd, const glm::mat4& vp, uint32_t model,
                      const glm::vec3& center, const glm::vec3& dir, float halfLen, float thick, float bright) {
        glm::vec3 d = glm::normalize(dir);
        glm::vec3 upRef = (std::abs(d.y) > 0.99f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), center) * glm::toMat4(glm::quatLookAtRH(d, upRef))
                    * glm::scale(glm::mat4(1.0f), glm::vec3(thick, thick, halfLen));   // long axis = local Z = beam
        m_renderer->render(cmd, vp, model, m, 0.0f, 1.0f, bright, /*twoSided*/ true);
    }

    void updateProjectiles(float dt) {
        for (auto& p : m_bolts) { p.pos += p.vel * dt; p.life -= dt; }
        if (m_enemyModel != UINT32_MAX && m_enemyAlive) {
            float hitR = m_enemyScale * 8.0f;   // rough enemy radius
            for (auto& p : m_bolts) {
                if (p.life > 0.0f && glm::length(p.pos - m_enemyPos) < hitR) {
                    p.life = 0.0f;
                    m_score++; m_evHits++;
                    damageEnemy(20.0f);
                }
            }
        }
        // Bolts also pop the target spheres (big, easy targets).
        if (m_starTargets)
            for (auto& p : m_bolts) {
                if (p.life <= 0.0f) continue;
                for (auto& s : m_spheres) {
                    if (s.alive && glm::length(p.pos - s.pos) < s.scale) { p.life = 0.0f; destroySphere(s); break; }
                }
            }
        m_bolts.erase(std::remove_if(m_bolts.begin(), m_bolts.end(),
                      [](const Bolt& b){ return b.life <= 0.0f; }), m_bolts.end());
    }

    // Apply damage; flash + jink; on death spawn the blast and scrap and start a respawn timer.
    void damageEnemy(float dmg) {
        if (!m_enemyAlive) return;
        m_enemyHitFlash = 0.15f;
        m_enemyRetarget = 0.0f;                 // jink when hit
        m_enemyHealth -= dmg;
        if (m_enemyHealth <= 0.0f) killEnemy();
    }

    void killEnemy() {
        m_evKills++;
        m_enemyAlive = false;
        m_enemyRespawn = 2.5f;
        spawnExplosion(m_enemyPos, m_enemyScale * 42.0f);
        spawnScrap(m_enemyPos, 2 + (std::rand() % 3));   // 2-4 chunks
    }

    void respawnEnemy() {
        m_enemyAlive = true;
        m_enemyHealth = m_enemyMaxHealth;
        m_enemyPos = glm::vec3(frand(-300, 300), frand(-300, 300), frand(-500, -150));
        m_enemyVel = glm::vec3(0.0f);
        m_enemyTarget = m_enemyPos;
        m_enemyRetarget = 0.0f;
    }

    void spawnExplosion(const glm::vec3& pos, float size) {
        if (m_explVariants.empty()) return;
        int variant = std::rand() % (int)m_explVariants.size();
        m_explosions.push_back({pos, 0.0f, size, variant});
        m_evExplosions++;
    }
    void updateExplosions(float dt) {
        for (auto& e : m_explosions) e.t += dt;
        m_explosions.erase(std::remove_if(m_explosions.begin(), m_explosions.end(),
                           [this](const Explosion& e){ return e.t >= m_explDuration; }), m_explosions.end());
    }

    void spawnScrap(const glm::vec3& pos, int count) {
        for (int k = 0; k < count; ++k) {
            Scrap s;
            s.pos  = pos + glm::vec3(frand(-4, 4), frand(-4, 4), frand(-4, 4));
            s.vel  = glm::vec3(frand(-14, 14), frand(-14, 14), frand(-14, 14));
            s.spin = frand(0.0f, 6.28f);
            m_scrapItems.push_back(s);
        }
    }
    void updateScrap(float dt) {
        const float magnetR = 70.0f, collectR = 13.0f;
        for (auto& s : m_scrapItems) {
            s.pos  += s.vel * dt;
            s.vel  *= (1.0f - std::min(0.9f, dt * 0.6f));    // drift drag
            s.spin += dt * 1.5f;
            glm::vec3 toShip = m_pos - s.pos;
            float d = glm::length(toShip);
            if (d < magnetR && d > 0.01f) s.vel += (toShip / d) * (140.0f * dt);   // tractor pull
        }
        int before = (int)m_scrapItems.size();
        m_scrapItems.erase(std::remove_if(m_scrapItems.begin(), m_scrapItems.end(),
                           [this, collectR](const Scrap& s){ return glm::length(m_pos - s.pos) < collectR; }),
                           m_scrapItems.end());
        m_scrap += before - (int)m_scrapItems.size();
    }

    void updateEnemy(float dt) {
        if (m_enemyModel == UINT32_MAX) return;
        if (m_enemyHitFlash > 0.0f) m_enemyHitFlash -= dt;
        if (!m_enemyAlive) {                 // destroyed — count down to a fresh warp-in
            m_enemyRespawn -= dt;
            if (m_enemyRespawn <= 0.0f) respawnEnemy();
            return;
        }
        m_enemyRetarget -= dt;
        if (m_enemyRetarget <= 0.0f) {
            m_enemyTarget = glm::vec3(frand(-300, 300), frand(-300, 300), frand(-400, -40));
            m_enemyRetarget = frand(3.0f, 7.0f);
        }
        glm::vec3 to = m_enemyTarget - m_enemyPos;
        float d = glm::length(to);
        if (d > 0.001f) {
            glm::vec3 dir = to / d;
            m_enemyVel += dir * 20.0f * dt;
            float sp = glm::length(m_enemyVel);
            if (sp > 40.0f) m_enemyVel = (m_enemyVel / sp) * 40.0f;
            m_enemyPos += m_enemyVel * dt;
            glm::vec3 vdir = (glm::length(m_enemyVel) > 0.01f) ? glm::normalize(m_enemyVel) : dir;
            m_enemyOrient = glm::quatLookAtRH(vdir, glm::vec3(0, 1, 0));
        }
    }

    // Golden-ratio icosahedron subdivided `subdiv` times, all verts on the unit sphere
    // (so normal == normalized position — free lighting normals). Adapted from the
    // engine's ProceduralSkybox icosphere.
    static void makeIcosphere(float radius, int subdiv,
                              std::vector<eden::ModelVertex>& outV, std::vector<uint32_t>& outI) {
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
        std::vector<glm::vec3> pos = {
            {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
            {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
            {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
        };
        for (auto& p : pos) p = glm::normalize(p);
        std::vector<glm::uvec3> faces = {
            {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
            {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
            {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
            {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1},
        };
        for (int s = 0; s < subdiv; ++s) {
            std::vector<glm::uvec3> nf;
            std::map<uint64_t, uint32_t> cache;
            auto mid = [&](uint32_t a, uint32_t b) -> uint32_t {
                uint64_t key = (a < b) ? (((uint64_t)a << 32) | b) : (((uint64_t)b << 32) | a);
                auto it = cache.find(key);
                if (it != cache.end()) return it->second;
                glm::vec3 m = glm::normalize((pos[a] + pos[b]) * 0.5f);
                uint32_t idx = (uint32_t)pos.size();
                pos.push_back(m); cache[key] = idx; return idx;
            };
            for (auto& f : faces) {
                uint32_t a = mid(f.x, f.y), b = mid(f.y, f.z), c = mid(f.z, f.x);
                nf.push_back({f.x, a, c}); nf.push_back({f.y, b, a});
                nf.push_back({f.z, c, b}); nf.push_back({a, b, c});
            }
            faces.swap(nf);
        }
        outV.clear(); outV.reserve(pos.size());
        for (auto& p : pos) {
            eden::ModelVertex v{};
            v.position = p * radius; v.normal = p; v.texCoord = {0, 0}; v.color = {1, 1, 1, 1};
            outV.push_back(v);
        }
        outI.clear(); outI.reserve(faces.size() * 3);
        for (auto& f : faces) { outI.push_back(f.x); outI.push_back(f.y); outI.push_back(f.z); }
    }
};
