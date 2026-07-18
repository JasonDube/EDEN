#include <eden/KmsPlatform.hpp>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libinput.h>
#include <libudev.h>
#include <linux/input-event-codes.h>

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cerrno>

namespace eden {

// ── libinput needs us to open/close device nodes (we run privileged on a TTY) ─
static int li_open(const char* path, int flags, void* /*user*/) {
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}
static void li_close(int fd, void* /*user*/) { close(fd); }
static const libinput_interface kLiIface = { li_open, li_close };

KmsPlatform::~KmsPlatform() { cleanup(); }

// ── DRM: open the card, become master, find the connected connector ──────────
bool KmsPlatform::openDrm() {
    m_drmFd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (m_drmFd < 0) { perror("[kms] open /dev/dri/card0"); return false; }

    if (drmSetMaster(m_drmFd) != 0)
        fprintf(stderr, "[kms] drmSetMaster: %s (continuing)\n", strerror(errno));

    drmModeRes* res = drmModeGetResources(m_drmFd);
    if (!res) { perror("[kms] drmModeGetResources"); return false; }
    for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnector* c = drmModeGetConnector(m_drmFd, res->connectors[i]);
        if (!c) continue;
        if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            m_connId = c->connector_id;
            m_w = c->modes[0].hdisplay;
            m_h = c->modes[0].vdisplay;
            printf("[kms] connector %u: %dx%d @ %uHz\n", m_connId, m_w, m_h, c->modes[0].vrefresh);
            drmModeFreeConnector(c);
            break;
        }
        drmModeFreeConnector(c);
    }
    drmModeFreeResources(res);
    if (!m_connId) { fprintf(stderr, "[kms] no connected connector\n"); return false; }
    m_cx = m_w * 0.5; m_cy = m_h * 0.5;   // start cursor centred
    return true;
}

std::vector<const char*> KmsPlatform::instanceExtensions() {
    return {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_DISPLAY_EXTENSION_NAME,
        VK_KHR_GET_DISPLAY_PROPERTIES_2_EXTENSION_NAME,
        VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
        VK_EXT_ACQUIRE_DRM_DISPLAY_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
}

// ── acquire the display from DRM and wrap it in a presentable surface ────────
VkSurfaceKHR KmsPlatform::createSurface(VkInstance inst) {
    m_inst = inst;

    // pick the discrete GPU (must match the one VulkanContext will choose)
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst, &n, devs.data());
    for (auto d : devs) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { m_phys = d; break; }
        if (m_phys == VK_NULL_HANDLE) m_phys = d;
    }
    if (!m_phys) { fprintf(stderr, "[kms] no physical device\n"); return VK_NULL_HANDLE; }

    auto pGet = (PFN_vkGetDrmDisplayEXT)     vkGetInstanceProcAddr(inst, "vkGetDrmDisplayEXT");
    auto pAcq = (PFN_vkAcquireDrmDisplayEXT) vkGetInstanceProcAddr(inst, "vkAcquireDrmDisplayEXT");
    if (!pGet || !pAcq) { fprintf(stderr, "[kms] acquire_drm entrypoints missing\n"); return VK_NULL_HANDLE; }

    if (pGet(m_phys, m_drmFd, m_connId, &m_display) != VK_SUCCESS || !m_display) {
        fprintf(stderr, "[kms] vkGetDrmDisplayEXT failed\n"); return VK_NULL_HANDLE; }
    if (pAcq(m_phys, m_drmFd, m_display) != VK_SUCCESS) {
        fprintf(stderr, "[kms] vkAcquireDrmDisplayEXT failed\n"); return VK_NULL_HANDLE; }

    // display mode matching the native resolution
    uint32_t mc = 0; vkGetDisplayModePropertiesKHR(m_phys, m_display, &mc, nullptr);
    std::vector<VkDisplayModePropertiesKHR> modes(mc);
    vkGetDisplayModePropertiesKHR(m_phys, m_display, &mc, modes.data());
    VkDisplayModeKHR mode = VK_NULL_HANDLE; VkExtent2D ext{(uint32_t)m_w, (uint32_t)m_h};
    for (auto& m : modes)
        if (m.parameters.visibleRegion.width == (uint32_t)m_w &&
            m.parameters.visibleRegion.height == (uint32_t)m_h) { mode = m.displayMode; break; }
    if (mode == VK_NULL_HANDLE && mc) { mode = modes[0].displayMode; ext = modes[0].parameters.visibleRegion;
        m_w = ext.width; m_h = ext.height; }
    if (mode == VK_NULL_HANDLE) { fprintf(stderr, "[kms] no display mode\n"); return VK_NULL_HANDLE; }

    // a plane that can drive this display
    uint32_t pc = 0; vkGetPhysicalDeviceDisplayPlanePropertiesKHR(m_phys, &pc, nullptr);
    std::vector<VkDisplayPlanePropertiesKHR> planes(pc);
    vkGetPhysicalDeviceDisplayPlanePropertiesKHR(m_phys, &pc, planes.data());
    uint32_t planeIndex = UINT32_MAX, stackIndex = 0;
    for (uint32_t i = 0; i < pc; ++i) {
        uint32_t sc = 0; vkGetDisplayPlaneSupportedDisplaysKHR(m_phys, i, &sc, nullptr);
        if (!sc) continue;
        std::vector<VkDisplayKHR> sup(sc);
        vkGetDisplayPlaneSupportedDisplaysKHR(m_phys, i, &sc, sup.data());
        for (auto d : sup) if (d == m_display) { planeIndex = i; stackIndex = planes[i].currentStackIndex; break; }
        if (planeIndex != UINT32_MAX) break;
    }
    if (planeIndex == UINT32_MAX) { fprintf(stderr, "[kms] no plane for display\n"); return VK_NULL_HANDLE; }

    VkDisplaySurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    sci.displayMode = mode; sci.planeIndex = planeIndex; sci.planeStackIndex = stackIndex;
    sci.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sci.globalAlpha = 1.0f; sci.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    sci.imageExtent = ext;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateDisplayPlaneSurfaceKHR(inst, &sci, nullptr, &surface) != VK_SUCCESS) {
        fprintf(stderr, "[kms] vkCreateDisplayPlaneSurfaceKHR failed\n"); return VK_NULL_HANDLE; }
    printf("[kms] display-plane surface %dx%d ready\n", m_w, m_h);
    return surface;
}

// libinput logs "failed to open /dev/input/eventN" etc. here — route it to
// stderr so a permission problem (user not in the `input` group) is visible.
static void li_log(libinput* /*li*/, libinput_log_priority /*pri*/,
                   const char* fmt, va_list args) {
    fprintf(stderr, "[kms/libinput] ");
    vfprintf(stderr, fmt, args);
}

// ── libinput over udev (seat0) ───────────────────────────────────────────────
bool KmsPlatform::initInput() {
    m_udev = udev_new();
    if (!m_udev) { fprintf(stderr, "[kms] udev_new failed\n"); return false; }
    m_li = libinput_udev_create_context(&kLiIface, nullptr, m_udev);
    if (!m_li) { fprintf(stderr, "[kms] libinput ctx failed\n"); return false; }
    libinput_log_set_handler(m_li, li_log);
    libinput_log_set_priority(m_li, LIBINPUT_LOG_PRIORITY_INFO);
    if (libinput_udev_assign_seat(m_li, "seat0") != 0) {
        fprintf(stderr, "[kms] assign seat0 failed (need input device access)\n"); return false; }

    // Pump the initial device-added events so we can report what we actually
    // captured. Zero devices == a permission problem (see the [kms/libinput]
    // "failed to open" lines): add your user to the `input` group.
    libinput_dispatch(m_li);
    int nDev = 0;
    libinput_event* ev;
    while ((ev = libinput_get_event(m_li))) {
        if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED) {
            libinput_device* d = libinput_event_get_device(ev);
            fprintf(stderr, "[kms] input device: %s\n", libinput_device_get_name(d));
            ++nDev;
        }
        libinput_event_destroy(ev);
    }
    if (nDev == 0)
        fprintf(stderr, "[kms] WARNING: no input devices opened — keyboard/mouse "
                        "will not work. Add your user to the `input` group.\n");
    else
        printf("[kms] libinput on seat0 (%d device%s)\n", nDev, nDev == 1 ? "" : "s");
    return true;
}

int KmsPlatform::computeMods() const {
    int m = 0;
    if (m_shift) m |= 0x0001; // GLFW_MOD_SHIFT
    if (m_ctrl)  m |= 0x0002; // GLFW_MOD_CONTROL
    if (m_alt)   m |= 0x0004; // GLFW_MOD_ALT
    return m;
}

void KmsPlatform::pollEvents() {
    if (!m_li) return;
    // non-blocking: only work if libinput has something
    pollfd pfd{ libinput_get_fd(m_li), POLLIN, 0 };
    if (poll(&pfd, 1, 0) <= 0) return;
    if (libinput_dispatch(m_li) != 0) return;

    libinput_event* ev;
    while ((ev = libinput_get_event(m_li))) {
        switch (libinput_event_get_type(ev)) {
        case LIBINPUT_EVENT_KEYBOARD_KEY: {
            auto* k = libinput_event_get_keyboard_event(ev);
            unsigned code = libinput_event_keyboard_get_key(k);
            bool pressed = libinput_event_keyboard_get_key_state(k) == LIBINPUT_KEY_STATE_PRESSED;
            // track modifier state ourselves (evdev codes)
            if (code == KEY_LEFTALT   || code == KEY_RIGHTALT)   m_alt   = pressed;
            if (code == KEY_LEFTCTRL  || code == KEY_RIGHTCTRL)  m_ctrl  = pressed;
            if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) m_shift = pressed;
            // Alt+Esc quits (matches the 2D compositor); don't forward that combo
            if (code == KEY_ESC && pressed && m_alt) { m_close = true; break; }
            if (onKey) onKey(code, pressed);
            break;
        }
        case LIBINPUT_EVENT_POINTER_MOTION: {
            auto* p = libinput_event_get_pointer_event(ev);
            m_cx += libinput_event_pointer_get_dx(p);
            m_cy += libinput_event_pointer_get_dy(p);
            if (m_cx < 0) m_cx = 0; if (m_cx > m_w) m_cx = m_w;
            if (m_cy < 0) m_cy = 0; if (m_cy > m_h) m_cy = m_h;
            if (onMotion) onMotion(m_cx, m_cy);
            break;
        }
        case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
            auto* p = libinput_event_get_pointer_event(ev);
            m_cx = libinput_event_pointer_get_absolute_x_transformed(p, m_w);
            m_cy = libinput_event_pointer_get_absolute_y_transformed(p, m_h);
            if (onMotion) onMotion(m_cx, m_cy);
            break;
        }
        case LIBINPUT_EVENT_POINTER_BUTTON: {
            auto* p = libinput_event_get_pointer_event(ev);
            unsigned b = libinput_event_pointer_get_button(p);
            bool pressed = libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED;
            int glfwButton = b == BTN_RIGHT ? 1 : b == BTN_MIDDLE ? 2 : 0;
            if (onButton) onButton(glfwButton, pressed, computeMods());
            break;
        }
        case LIBINPUT_EVENT_POINTER_AXIS: {
            auto* p = libinput_event_get_pointer_event(ev);
            if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
                double v = libinput_event_pointer_get_axis_value(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
                // libinput: +down. Flip so onScroll dy matches GLFW (+up).
                if (onScroll) onScroll(-v / 15.0, m_alt);
            }
            break;
        }
        default: break;
        }
        libinput_event_destroy(ev);
    }
}

void KmsPlatform::cleanup() {
    if (m_li)   { libinput_unref(m_li);   m_li = nullptr; }
    if (m_udev) { udev_unref(m_udev);     m_udev = nullptr; }
    if (m_drmFd >= 0) { drmDropMaster(m_drmFd); close(m_drmFd); m_drmFd = -1; }
}

} // namespace eden
