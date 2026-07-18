// Tearsheet 3D — Phase 1a
//
// The first brick of folding Tearsheet WM into the EDEN engine: prove a minimal
// EDEN executable can draw a *textured quad* whose pixels come from a CPU RGBA
// buffer updated every frame — the exact path a Wayland window will use
// (surface pixels -> ModelRenderer::updateTexture). No wlroots yet; the texture
// is an animated test pattern so we can see updateTexture working live.
//
// See docs/EDEN_INTEGRATION.md in the tearsheet-wm repo for the full plan.

#include "Renderer/VulkanApplicationBase.hpp"
#include "Renderer/Swapchain.hpp"
#include "Renderer/ModelRenderer.hpp"

#include <eden/Camera.hpp>
#include <eden/Window.hpp>

#include "wayland_host.h"

#include <GLFW/glfw3.h>
#include <linux/input-event-codes.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace eden;

namespace {
// Double-winded so face winding never matters. (Same trick as vieuphoria.)
void addFace(std::vector<ModelVertex>& v, std::vector<uint32_t>& i,
             glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, glm::vec4 col) {
    uint32_t k = (uint32_t)v.size();
    v.push_back({a, n, {0, 0}, col}); v.push_back({b, n, {1, 0}, col});
    v.push_back({c, n, {1, 1}, col}); v.push_back({d, n, {0, 1}, col});
    i.insert(i.end(), {k, k + 1, k + 2, k + 2, k + 3, k, k, k + 2, k + 1, k, k + 3, k + 2});
}
void buildBox(glm::vec3 h, glm::vec4 c, std::vector<ModelVertex>& v, std::vector<uint32_t>& i) {
    addFace(v, i, {h.x,-h.y,-h.z}, {h.x,-h.y,h.z}, {h.x,h.y,h.z}, {h.x,h.y,-h.z}, {1,0,0}, c);
    addFace(v, i, {-h.x,-h.y,h.z}, {-h.x,-h.y,-h.z}, {-h.x,h.y,-h.z}, {-h.x,h.y,h.z}, {-1,0,0}, c);
    addFace(v, i, {-h.x,h.y,-h.z}, {h.x,h.y,-h.z}, {h.x,h.y,h.z}, {-h.x,h.y,h.z}, {0,1,0}, c);
    addFace(v, i, {-h.x,-h.y,h.z}, {h.x,-h.y,h.z}, {h.x,-h.y,-h.z}, {-h.x,-h.y,-h.z}, {0,-1,0}, c);
    addFace(v, i, {-h.x,-h.y,h.z}, {h.x,-h.y,h.z}, {h.x,h.y,h.z}, {-h.x,h.y,h.z}, {0,0,1}, c);
    addFace(v, i, {h.x,-h.y,-h.z}, {-h.x,-h.y,-h.z}, {-h.x,h.y,-h.z}, {h.x,h.y,-h.z}, {0,0,-1}, c);
}

// A flat mouse-pointer arrow in the XY plane, tip at the local origin (the
// hotspot). Body extends toward +X/+Y, which this engine maps to screen
// right/down, so it reads as a classic pointer aiming up-left. Two triangles:
// a larger dark one (outline) behind a white fill, so it stays visible on any
// background. Untextured — shaded by vertex colour, like the backdrop cubes.
void buildCursor(float L, std::vector<ModelVertex>& v, std::vector<uint32_t>& i) {
    const glm::vec3 n{0, 0, 1};
    auto tri = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c, float z, glm::vec4 col) {
        uint32_t k = (uint32_t)v.size();
        v.push_back({{a.x, a.y, z}, n, {0, 0}, col});
        v.push_back({{b.x, b.y, z}, n, {0, 0}, col});
        v.push_back({{c.x, c.y, z}, n, {0, 0}, col});
        // Both windings so back-face culling can never drop the pointer.
        i.insert(i.end(), {k, k + 1, k + 2, k, k + 2, k + 1});
    };
    const glm::vec4 dark{0.05f, 0.05f, 0.07f, 1.0f};
    const glm::vec4 white{0.97f, 0.97f, 1.0f, 1.0f};
    const float o = 1.35f; // outline is 35% larger than the fill
    // Outline (behind, z=0), then fill (in front, z slightly nearer the camera).
    tri({0, 0}, {0, o * L}, {0.70f * o * L, 0.70f * o * L}, 0.000f, dark);
    tri({0, 0}, {0,     L}, {0.70f * L,     0.70f * L    }, 0.002f, white);
}
} // namespace

class Tearsheet3D : public VulkanApplicationBase {
public:
    Tearsheet3D() : VulkanApplicationBase(1280, 800, "Tearsheet 3D — Phase 1a") {}

protected:
    void onInit() override {
        m_modelRenderer = std::make_unique<ModelRenderer>(
            getContext(), getSwapchain().getRenderPass(), getSwapchain().getExtent());

        // A unit quad in the XY plane, facing +Z, UV 0..1 (row 0 = top).
        const float hw = kHalfW, hh = kHalfH; // half-width / half-height
        // UV v is flipped (top vertex = v1) so texture row 0 lands at the quad
        // top — matching Wayland buffers, whose row 0 is the top of the window.
        std::vector<ModelVertex> verts = {
            {{-hw, -hh, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}, {1, 1, 1, 1}}, // bottom-left
            {{ hw, -hh, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}, {1, 1, 1, 1}}, // bottom-right
            {{ hw,  hh, 0.0f}, {0, 0, 1}, {1.0f, 1.0f}, {1, 1, 1, 1}}, // top-right
            {{-hw,  hh, 0.0f}, {0, 0, 1}, {0.0f, 1.0f}, {1, 1, 1, 1}}, // top-left
        };
        // Both windings, so whichever face points at the camera survives the
        // opaque pipeline's back-face cull (the away-facing set is culled).
        std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0,   // front winding
                                         0, 2, 1, 0, 3, 2};  // reverse winding

        m_texW = 512;
        m_texH = 320;
        m_pixels.assign((size_t)m_texW * m_texH * 4, 0);
        fillPattern(m_pixels, m_texW, m_texH, 0);

        m_quad = m_modelRenderer->createModel(verts, indices, m_pixels.data(), m_texW, m_texH);

        // Light the scene: full ambient, no point lights, so the textured quad
        // shows at full brightness (model.frag shades by the light UBO).
        m_modelRenderer->setDayNight(1.0f, 1.0f);
        m_modelRenderer->setLights({});

        // Fixed camera looking at the quad from +Z.
        m_camera.setPosition(glm::vec3(0.0f, 0.0f, 2.2f));
        m_camera.setYaw(-90.0f);
        m_camera.setPitch(0.0f);
        m_camera.setNoClip(true);

        // A LIVING 3D BACKDROP behind the flat console: two rings of spinning
        // cubes, set back in Z so they peek around the window's edges. This is
        // the "3D wallpaper" — the console floats flat in front of a real scene.
        const glm::vec4 pal[] = {
            {0.42f,0.58f,0.71f,1}, {0.85f,0.40f,0.44f,1}, {0.45f,0.72f,0.50f,1},
            {0.90f,0.72f,0.35f,1}, {0.62f,0.48f,0.78f,1}, {0.40f,0.75f,0.78f,1}};
        auto ring = [&](int n, float radius, float z, float sc, float spin) {
            for (int k = 0; k < n; k++) {
                float a = (float)k / n * 6.2831853f;
                addBg(glm::vec3(radius * std::cos(a), radius * std::sin(a), z),
                      pal[k % 6], sc, spin);
            }
        };
        ring(6, 1.9f, -2.2f, 0.30f, 55.0f);
        ring(7, 3.0f, -4.2f, 0.42f, -35.0f);

        // Software mouse pointer (KMS has no OS cursor). Untextured arrow drawn
        // on top of the panel every frame at the live cursor position.
        {
            std::vector<ModelVertex> cv;
            std::vector<uint32_t> ci;
            buildCursor(kCursorL, cv, ci);
            m_cursor = m_modelRenderer->createModel(cv, ci, nullptr, 0, 0);
        }

        std::cout << "Tearsheet 3D phase 1: quad handle " << m_quad
                  << " (" << m_texW << "x" << m_texH << ")\n";

        // Start the embedded headless Wayland server and launch a client. Its
        // surface pixels will replace the placeholder texture on the quad.
        if (!wlhost_init("foot --font=monospace:size=28 sh -c \"echo; echo "
                         "'  TEARSHEET x EDEN — type into this window (a 3D quad)'; "
                         "echo; exec bash\"")) {
            std::cerr << "Wayland host failed to start; showing placeholder only.\n";
        }

        s_instance = this;
        if (isKmsMode()) {
            // ── DRM/KMS backend: input arrives straight from libinput ────────
            KmsPlatform* kp = getKmsPlatform();
            // libinput reports raw evdev keycodes — exactly what wlhost wants.
            kp->onKey    = [this](unsigned evdev, bool pressed) { handleKey(evdev, pressed); };
            kp->onMotion = [this](double x, double y)           { onCursor(x, y); };
            kp->onButton = [this](int btn, bool pressed, int mods) {
                handleButton(btn, pressed, (mods & 0x0004) != 0, s_instance->kmsCurX(), s_instance->kmsCurY());
            };
            kp->onScroll = [this](double dy, bool alt)          { handleScroll(dy, alt); };
            // Start the pointer where libinput has it (screen centre), so it's
            // visible before the first mouse move.
            m_curPx = kp->cursorX();
            m_curPy = kp->cursorY();
        } else {
            // ── GLFW backend (windowed, inside a desktop) ────────────────────
            // NOTE: do NOT use glfwSetWindowUserPointer — EDEN's Window owns it.
            GLFWwindow* win = getWindow().getHandle();
            glfwSetKeyCallback(win, &Tearsheet3D::keyCb);
            glfwSetCursorPosCallback(win, &Tearsheet3D::cursorCb);
            glfwSetMouseButtonCallback(win, &Tearsheet3D::buttonCb);
            glfwSetScrollCallback(win, &Tearsheet3D::scrollCb);
            glfwMaximizeWindow(win);   // fill the desktop
        }
    }

    // Helpers for the KMS lambdas to read the live cursor position.
    double kmsCurX() { return getKmsPlatform()->cursorX(); }
    double kmsCurY() { return getKmsPlatform()->cursorY(); }

    // ── Backend-neutral input handling (fed by GLFW or libinput) ─────────────
    void handleKey(unsigned evdev, bool pressed) { wlhost_key(evdev, pressed); }

    void handleButton(int glfwButton, bool pressed, bool alt, double cx, double cy) {
        // Alt+left drag = MOVE the panel (compositor-side; not sent to the client).
        if (alt && glfwButton == GLFW_MOUSE_BUTTON_LEFT) {
            if (pressed) {
                glm::vec2 h;
                if (worldHitOnPlane(cx, cy, h)) { m_dragging = true; m_lastHit = h; }
            } else {
                m_dragging = false;
            }
            return;
        }
        unsigned b = glfwButton == GLFW_MOUSE_BUTTON_RIGHT  ? BTN_RIGHT
                   : glfwButton == GLFW_MOUSE_BUTTON_MIDDLE ? BTN_MIDDLE
                                                            : BTN_LEFT;
        wlhost_pointer_button(b, pressed);
    }

    void handleScroll(double dy, bool alt) {
        // Alt+scroll = RESIZE (scale) the panel; plain scroll = client scrollback.
        if (alt) {
            m_panelScale *= (float)(1.0 + dy * 0.08);
            m_panelScale = std::max(0.3f, std::min(3.0f, m_panelScale));
            return;
        }
        wlhost_pointer_axis(-dy * 15.0);
    }

    // ── GLFW callback trampolines (thin; forward to the neutral helpers) ─────
    static inline Tearsheet3D* s_instance = nullptr;
    static void keyCb(GLFWwindow*, int, int scancode, int action, int) {
        if (action == GLFW_REPEAT || !s_instance) return;
        // GLFW scancode on X11 is the evdev keycode + 8.
        unsigned evdev = scancode >= 8 ? (unsigned)(scancode - 8) : (unsigned)scancode;
        s_instance->handleKey(evdev, action == GLFW_PRESS);
    }
    static void cursorCb(GLFWwindow*, double x, double y) {
        if (s_instance) s_instance->onCursor(x, y);
    }
    static void buttonCb(GLFWwindow* win, int button, int action, int mods) {
        if (!s_instance) return;
        double x, y; glfwGetCursorPos(win, &x, &y);
        s_instance->handleButton(button, action == GLFW_PRESS, (mods & GLFW_MOD_ALT) != 0, x, y);
    }
    static void scrollCb(GLFWwindow* win, double, double dy) {
        if (!s_instance) return;
        bool alt = glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                   glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        s_instance->handleScroll(dy, alt);
    }

    // Ray from the cursor to the z=0 plane -> world (x,y). No bounds check.
    bool worldHitOnPlane(double px, double py, glm::vec2& out) {
        return worldHitAtZ(px, py, 0.0f, out);
    }

    // Ray from the cursor pixel to the world plane z=zPlane -> world (x,y).
    bool worldHitAtZ(double px, double py, float zPlane, glm::vec2& out) {
        int ww = 0, wh = 0;
        getViewportSize(ww, wh);        // GLFW window or KMS display, whichever is live
        if (ww <= 0 || wh <= 0) return false;
        // This engine renders world +Y at the screen BOTTOM (verified), so
        // screen-bottom (py large) must unproject to world +Y — i.e. no Y flip.
        float nx = (float)(px / ww * 2.0 - 1.0);
        float ny = (float)(py / wh * 2.0 - 1.0);
        float aspect = (float)ww / wh;
        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 100.0f);
        glm::mat4 invVP = glm::inverse(proj * view);
        glm::vec4 pn = invVP * glm::vec4(nx, ny, 0.0f, 1.0f); pn /= pn.w;
        glm::vec4 pf = invVP * glm::vec4(nx, ny, 1.0f, 1.0f); pf /= pf.w;
        glm::vec3 O(pn);
        glm::vec3 D = glm::normalize(glm::vec3(pf) - glm::vec3(pn));
        if (std::fabs(D.z) < 1e-6f) return false;
        float t = (zPlane - O.z) / D.z;
        if (t < 0) return false;
        glm::vec3 P = O + D * t;
        out = glm::vec2(P.x, P.y);
        return true;
    }

    // Cursor -> either drag the panel, or map to the client's surface pixel
    // (accounting for the panel's current position + scale).
    void onCursor(double px, double py) {
        m_curPx = px; m_curPy = py;   // remember the raw pixel for drawing the pointer
        glm::vec2 H;
        if (!worldHitOnPlane(px, py, H)) return;
        if (m_dragging) {
            m_panelPos += (H - m_lastHit);
            m_lastHit = H;
            return;
        }
        if (!m_haveClient || m_clientW <= 0 || m_clientH <= 0) return;
        float hw = kHalfW * m_panelScale, hh = kHalfH * m_panelScale;
        float lx = H.x - m_panelPos.x, ly = H.y - m_panelPos.y;
        if (lx < -hw || lx > hw || ly < -hh || ly > hh) return; // off the panel
        double sx = (lx + hw) / (2.0 * hw) * m_clientW;
        double sy = (ly + hh) / (2.0 * hh) * m_clientH; // world +Y = screen bottom
        wlhost_pointer_motion(sx, sy);
    }

    void onCleanup() override {
        vkDeviceWaitIdle(getContext().getDevice());
        m_modelRenderer.reset();
    }

    void update(float dt) override {
        m_time += dt;
        m_worldAngle = std::fmod(m_worldAngle + dt * 6.0f, 360.0f); // orbit the backdrop
        for (auto& b : m_bg) b.self = std::fmod(b.self + b.spin * dt, 360.0f);

        // Pump the embedded Wayland server, then upload the client's latest
        // frame onto the quad (updateTexture is self-contained: own submit+wait,
        // and recreates the texture when the client's size differs).
        wlhost_pump();
        int w = 0, h = 0;
        const unsigned char* px = nullptr;
        bool dirty = false;
        if (wlhost_frame(&w, &h, &px, &dirty)) {
            m_clientW = w;
            m_clientH = h;
            if (dirty) {
                m_modelRenderer->updateTexture(m_quad, px, w, h);
                if (!m_haveClient) {
                    std::cerr << "[phase1] first client frame " << w << "x" << h << "\n";
                    m_haveClient = true;
                    m_clientTime = m_time;
                }
            }
        } else if (!m_haveClient) {
            // No client frame yet — animate the placeholder so the quad is alive.
            if (++m_tick % 5 == 0) {
                fillPattern(m_pixels, m_texW, m_texH, ++m_frame);
                m_modelRenderer->updateTexture(m_quad, m_pixels.data(), m_texW, m_texH);
            }
        }

        // Opt-in self-test (TEARSHEET_SELFTEST=1): ~1.5s after the client is up,
        // auto-type a command to prove keyboard injection without a human. Normal
        // launches are interactive — type/click into the window directly.
        if (getenv("TEARSHEET_SELFTEST") && m_haveClient && !m_typed &&
            (m_time - m_clientTime) > 1.5f) {
            wlhost_type_ascii("echo tearsheet input works\n");
            m_typed = true;
            std::cerr << "[phase2] auto-typed self-test command\n";
        }
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) override {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &begin);

        auto& sc = getSwapchain();
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = sc.getRenderPass();
        rp.framebuffer = sc.getFramebuffers()[imageIndex];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = sc.getExtent();
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{0.06f, 0.07f, 0.09f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        rp.clearValueCount = static_cast<uint32_t>(clears.size());
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        auto ext = sc.getExtent();

        // EDEN's model pipeline declares viewport/scissor as dynamic state, so
        // they must be set in every command buffer before drawing.
        VkViewport vp{};
        vp.x = 0.0f; vp.y = 0.0f;
        vp.width = static_cast<float>(ext.width);
        vp.height = static_cast<float>(ext.height);
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = ext;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        float aspect = static_cast<float>(ext.width) / ext.height;

        // Auto-fit: pull the camera to the distance where the quad exactly fills
        // the window (whichever of width/height is the binding constraint), so it
        // stays maximized as the window resizes.
        float th = std::tan(glm::radians(m_camera.getFov()) * 0.5f);
        float distV = kHalfH / th;
        float distH = kHalfW / (aspect * th);
        // Pull back so the console floats at ~55% of the view, leaving room for
        // the 3D backdrop to show around its edges.
        float dist = std::max(distV, distH) * 1.9f;
        m_camera.setPosition(glm::vec3(0.0f, 0.0f, dist));

        glm::mat4 view = m_camera.getViewMatrix();
        glm::mat4 proj = m_camera.getProjectionMatrix(aspect, 0.1f, 100.0f);
        glm::mat4 viewProj = proj * view;

        m_modelRenderer->setDayNight(1.0f, 1.0f); // keep full ambient each frame
        m_modelRenderer->setLights({});

        // The living 3D backdrop, set back in Z (behind the console). Orbits in
        // the view plane so nothing ever swings in front of the camera.
        for (auto& b : m_bg) {
            glm::mat4 m = glm::rotate(glm::mat4(1.0f), glm::radians(m_worldAngle), glm::vec3(0, 0, 1)) *
                          glm::translate(glm::mat4(1.0f), b.base) *
                          glm::rotate(glm::mat4(1.0f), glm::radians(b.self), glm::vec3(0.6f, 1.0f, 0.0f));
            m_modelRenderer->render(cmd, viewProj, b.handle, m);
        }

        // The flat console on top (z=0, closest → composites over the backdrop),
        // moved/scaled by the window-management state.
        glm::mat4 panel = glm::translate(glm::mat4(1.0f), glm::vec3(m_panelPos, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(m_panelScale, m_panelScale, 1.0f));
        m_modelRenderer->render(cmd, viewProj, m_quad, panel,
                                /*hue*/ 0.0f, /*sat*/ 1.0f, /*bright*/ 1.0f, /*twoSided*/ false);

        // Software mouse pointer, drawn last and in FRONT of the panel (z=0) so
        // it's never occluded. KMS only — GLFW has a real OS cursor already.
        if (isKmsMode() && m_cursor) {
            glm::vec2 cw;
            if (worldHitAtZ(m_curPx, m_curPy, kCursorZ, cw)) {
                glm::mat4 cur = glm::translate(glm::mat4(1.0f),
                                               glm::vec3(cw, kCursorZ));
                m_modelRenderer->render(cmd, viewProj, m_cursor, cur,
                                        /*hue*/ 0.0f, /*sat*/ 1.0f, /*bright*/ 1.0f,
                                        /*twoSided*/ true);
            }
        }

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

private:
    // A backdrop object: a spinning cube parked behind the console.
    struct Bg { uint32_t handle; glm::vec3 base; float spin; float self = 0.0f; };
    void addBg(glm::vec3 pos, glm::vec4 col, float scale, float spin) {
        std::vector<ModelVertex> v;
        std::vector<uint32_t> i;
        buildBox({scale, scale, scale}, col, v, i);
        m_bg.push_back({m_modelRenderer->createModel(v, i, nullptr, 0, 0), pos, spin});
    }

    // Scrolling checkerboard with a moving accent band — obviously animated.
    static void fillPattern(std::vector<unsigned char>& px, int w, int h, int frame) {
        const int cell = 32;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                bool on = ((((x + frame * 2) / cell) + (y / cell)) & 1) != 0;
                bool band = std::abs(((x + frame * 6) % w) - w / 2) < 10;
                unsigned char* p = &px[((size_t)y * w + x) * 4];
                if (band)      { p[0] = 220; p[1] = 110; p[2] = 120; } // coral accent
                else if (on)   { p[0] = 70;  p[1] = 150; p[2] = 200; } // steel
                else           { p[0] = 20;  p[1] = 24;  p[2] = 32;  } // dark
                p[3] = 255;
            }
        }
    }

    static constexpr float kHalfW = 0.8f, kHalfH = 0.5f; // quad half-extents (1.6:1)
    static constexpr float kCursorL = 0.055f; // pointer length in world units
    static constexpr float kCursorZ = 0.06f;  // in front of the panel (z=0)

    std::unique_ptr<ModelRenderer> m_modelRenderer;
    Camera m_camera;
    uint32_t m_quad = 0;

    std::vector<Bg> m_bg;          // spinning backdrop cubes
    float m_worldAngle = 0.0f;     // slow orbit of the whole backdrop

    // Floating-window state: Alt+drag moves, Alt+scroll resizes.
    glm::vec2 m_panelPos{0.0f, 0.0f};
    float m_panelScale = 1.0f;
    bool m_dragging = false;
    glm::vec2 m_lastHit{0.0f, 0.0f};

    // Software mouse pointer (KMS only) and the live cursor pixel position.
    uint32_t m_cursor = 0;
    double m_curPx = 0.0, m_curPy = 0.0;

    std::vector<unsigned char> m_pixels;
    int m_texW = 0, m_texH = 0;
    bool m_haveClient = false;
    int m_clientW = 0, m_clientH = 0;
    bool m_typed = false;
    float m_clientTime = 0.0f;

    float m_time = 0.0f;
    long m_tick = 0;
    int m_frame = 0;
};

int main() {
    try {
        Tearsheet3D app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
