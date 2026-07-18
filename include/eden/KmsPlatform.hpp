#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// KmsPlatform — EDEN as its own display server.
//
// A self-contained, opt-in alternative to the GLFW Window: it takes the real
// display straight from DRM/KMS via Vulkan (VK_EXT_acquire_drm_display) and
// reads keyboard/mouse straight from libinput. No X11, no Wayland, no compositor
// underneath. Used only when EDEN_KMS is set; every GLFW example is untouched.
//
// Ownership/order (see VulkanApplicationBase::init):
//   1. instanceExtensions()  — fed to the Vulkan instance (must precede it)
//   2. createSurface(inst)   — acquires the display, returns a presentable surface
//   3. initInput()           — opens libinput
//   4. pollEvents() each frame; shouldClose() to end the loop
// ─────────────────────────────────────────────────────────────────────────────
#include <vulkan/vulkan.h>
#include <vector>
#include <functional>

// opaque — real types live in the .cpp so heavy headers stay out of the include
struct libinput;
struct udev;

namespace eden {

class KmsPlatform {
public:
    KmsPlatform() = default;
    ~KmsPlatform();
    KmsPlatform(const KmsPlatform&) = delete;
    KmsPlatform& operator=(const KmsPlatform&) = delete;

    // The instance extensions the DRM-display path needs (no GLFW).
    static std::vector<const char*> instanceExtensions();

    bool openDrm();                          // open card0, become master, pick connector
    VkSurfaceKHR createSurface(VkInstance);  // acquire display + display-plane surface
    bool initInput();                        // libinput via udev (needs device access)

    int  width()  const { return m_w; }
    int  height() const { return m_h; }

    void pollEvents();                       // drain libinput, dispatch callbacks
    bool shouldClose() const { return m_close; }
    void requestClose()      { m_close = true; }
    void cleanup();

    // ── input callbacks (set by the app; mirror what tearsheet3d needs) ──────
    // evdev keycode (as libinput reports it) + press/release. Modifiers included.
    std::function<void(unsigned evdev, bool pressed)>          onKey;
    // absolute cursor position in pixels (accumulated from relative motion)
    std::function<void(double x, double y)>                    onMotion;
    // GLFW-compatible button index (0=left,1=right,2=middle), press/release, mods
    std::function<void(int glfwButton, bool pressed, int mods)> onButton;
    // scroll delta + whether Alt is held (so the app can branch move/resize)
    std::function<void(double dy, bool altHeld)>              onScroll;

    // queried by the app
    bool   altDown()   const { return m_alt; }
    double cursorX()   const { return m_cx; }
    double cursorY()   const { return m_cy; }

private:
    int          computeMods() const;

    // DRM
    int          m_drmFd = -1;
    unsigned     m_connId = 0;
    int          m_w = 0, m_h = 0;

    // Vulkan display objects (owned by the instance/driver; freed on teardown)
    VkInstance   m_inst    = VK_NULL_HANDLE;
    VkPhysicalDevice m_phys = VK_NULL_HANDLE;
    VkDisplayKHR m_display = VK_NULL_HANDLE;

    // libinput
    ::udev*      m_udev = nullptr;
    ::libinput*  m_li   = nullptr;

    // input state
    double m_cx = 0, m_cy = 0;
    bool   m_alt = false, m_ctrl = false, m_shift = false;
    bool   m_close = false;
};

} // namespace eden
