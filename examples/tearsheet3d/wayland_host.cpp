// Embedded headless Wayland server — implementation. See wayland_host.h.

#include "wayland_host.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <unistd.h>

extern "C" {
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
// wlroots public headers use C99 `type name[static N]` parameter syntax, which
// is invalid C++. Neutralise the `static` array qualifier for these includes.
#define static
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/pixman.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/util/log.h>
#undef static
#include "xdg-shell-protocol.h"
}

namespace {

// The one client toplevel we track, plus the latest captured frame.
struct Host {
    wl_display* display = nullptr;
    wlr_backend* backend = nullptr;
    wlr_renderer* renderer = nullptr;
    wlr_allocator* allocator = nullptr;
    wlr_output* output = nullptr;
    wlr_xdg_shell* xdg_shell = nullptr;
    wlr_seat* seat = nullptr;

    wl_listener new_surface{};
    wl_listener commit{};
    wl_listener map{};
    wl_listener destroy{};

    // Virtual keyboard (input comes from EDEN/GLFW, not a real device).
    wlr_keyboard keyboard{};
    wl_listener kb_key{};
    wl_listener kb_modifiers{};

    wlr_xdg_toplevel* toplevel = nullptr;
    wlr_surface* surface = nullptr;
    bool mapped = false;
    bool ptr_entered = false;

    // Latest client frame as RGBA8.
    std::vector<unsigned char> rgba;
    int width = 0, height = 0;
    bool dirty = false;

    static constexpr int REQUEST_W = 1024;
    static constexpr int REQUEST_H = 640;
} g;

const wlr_keyboard_impl kb_impl = {"tearsheet-virtual", nullptr};

uint32_t now_ms() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

// The virtual keyboard's processed events (xkb state already updated) are
// forwarded to the seat so the focused client receives keys + modifiers.
void on_kb_key(wl_listener*, void* data) {
    auto* ev = static_cast<wlr_keyboard_key_event*>(data);
    wlr_seat_set_keyboard(g.seat, &g.keyboard);
    wlr_seat_keyboard_notify_key(g.seat, ev->time_msec, ev->keycode, ev->state);
}
void on_kb_modifiers(wl_listener*, void*) {
    wlr_seat_keyboard_notify_modifiers(g.seat, &g.keyboard.modifiers);
}

// Copy the surface's committed shm buffer into g.rgba, converting the usual
// BGRA memory order (DRM ARGB8888/XRGB8888, little-endian) to RGBA.
void capture_surface() {
    if (!g.surface || !g.surface->buffer || !g.surface->buffer->source) return;
    wlr_buffer* src = g.surface->buffer->source;

    void* data = nullptr;
    uint32_t format = 0;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(
            src, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
        return;
    }

    int w = src->width, h = src->height;
    bool opaque = (format == DRM_FORMAT_XRGB8888 || format == DRM_FORMAT_XBGR8888);
    g.rgba.resize((size_t)w * h * 4);
    const unsigned char* base = static_cast<const unsigned char*>(data);
    for (int y = 0; y < h; y++) {
        const unsigned char* s = base + (size_t)y * stride;
        unsigned char* d = &g.rgba[(size_t)y * w * 4];
        for (int x = 0; x < w; x++) {
            // src memory: B,G,R,A  ->  dst: R,G,B,A
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = opaque ? 255 : s[3];
            s += 4;
            d += 4;
        }
    }
    wlr_buffer_end_data_ptr_access(src);

    g.width = w;
    g.height = h;
    g.dirty = true;
}

void on_commit(wl_listener*, void*) {
    if (!g.toplevel) return;
    if (g.toplevel->base->initial_commit) {
        fprintf(stderr, "wlhost: initial commit -> set_size %dx%d\n", Host::REQUEST_W, Host::REQUEST_H);
        wlr_xdg_toplevel_set_size(g.toplevel, Host::REQUEST_W, Host::REQUEST_H);
        return;
    }
    if (g.mapped) capture_surface();
}

void on_map(wl_listener*, void*) {
    g.mapped = true;
    fprintf(stderr, "wlhost: surface mapped\n");
    // Give the window keyboard focus so injected keys reach it.
    wlr_seat_keyboard_notify_enter(g.seat, g.surface, g.keyboard.keycodes,
                                   g.keyboard.num_keycodes, &g.keyboard.modifiers);
}

void on_destroy(wl_listener*, void*) {
    g.mapped = false;
    g.surface = nullptr;
    g.toplevel = nullptr;
    wl_list_remove(&g.commit.link);
    wl_list_remove(&g.map.link);
    wl_list_remove(&g.destroy.link);
}

void on_new_surface(wl_listener*, void* data) {
    auto* xdg_surface = static_cast<wlr_xdg_surface*>(data);
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) return;
    if (g.toplevel) return; // spike: track only the first toplevel

    fprintf(stderr, "wlhost: new toplevel\n");
    g.toplevel = xdg_surface->toplevel;
    g.surface = xdg_surface->surface;

    g.commit.notify = on_commit;
    wl_signal_add(&g.surface->events.commit, &g.commit);
    g.map.notify = on_map;
    wl_signal_add(&g.surface->events.map, &g.map);
    g.destroy.notify = on_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &g.destroy);
}

void spawn_client(const char* socket, const char* cmd) {
    if (fork() == 0) {
        setsid();
        setenv("WAYLAND_DISPLAY", socket, 1);
        unsetenv("DISPLAY"); // force Wayland (avoid XWayland)
        execl("/bin/sh", "/bin/sh", "-c", cmd, (char*)nullptr);
        _exit(1);
    }
}

} // namespace

bool wlhost_init(const char* startup_cmd) {
    wlr_log_init(WLR_ERROR, nullptr); // quiet; errors only

    g.display = wl_display_create();
    if (!g.display) return false;

    g.renderer = wlr_pixman_renderer_create(); // software: no GPU device
    if (!g.renderer) { fprintf(stderr, "wlhost: pixman renderer failed\n"); return false; }

    // Headless backend + a virtual output so clients (foot) see a monitor.
    g.backend = wlr_headless_backend_create(g.display);
    if (!g.backend) { fprintf(stderr, "wlhost: headless backend failed\n"); return false; }
    g.allocator = wlr_allocator_autocreate(g.backend, g.renderer);
    if (!g.allocator) { fprintf(stderr, "wlhost: allocator failed\n"); return false; }

    wlr_renderer_init_wl_display(g.renderer, g.display);
    if (!wlr_backend_start(g.backend)) { fprintf(stderr, "wlhost: backend start failed\n"); return false; }

    g.output = wlr_headless_add_output(g.backend, 1280, 800);
    wlr_output_init_render(g.output, g.allocator, g.renderer);
    wlr_output_state ostate;
    wlr_output_state_init(&ostate);
    wlr_output_state_set_enabled(&ostate, true);
    wlr_output_state_set_custom_mode(&ostate, 1280, 800, 0);
    wlr_output_commit_state(g.output, &ostate);
    wlr_output_state_finish(&ostate);
    wlr_output_create_global(g.output); // advertise wl_output to clients

    wlr_compositor_create(g.display, 5, g.renderer);
    wlr_subcompositor_create(g.display);
    wlr_data_device_manager_create(g.display);

    g.xdg_shell = wlr_xdg_shell_create(g.display, 3);
    g.new_surface.notify = on_new_surface;
    wl_signal_add(&g.xdg_shell->events.new_surface, &g.new_surface);

    g.seat = wlr_seat_create(g.display, "seat0");
    wlr_seat_set_capabilities(g.seat,
                              WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

    // Virtual keyboard with a default (US) keymap; injected keys flow through it
    // so xkb tracks modifiers, then on_kb_* forward to the seat.
    wlr_keyboard_init(&g.keyboard, &kb_impl, "tearsheet-virtual");
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(&g.keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    wlr_seat_set_keyboard(g.seat, &g.keyboard);
    g.kb_key.notify = on_kb_key;
    wl_signal_add(&g.keyboard.events.key, &g.kb_key);
    g.kb_modifiers.notify = on_kb_modifiers;
    wl_signal_add(&g.keyboard.events.modifiers, &g.kb_modifiers);

    const char* socket = wl_display_add_socket_auto(g.display);
    if (!socket) { fprintf(stderr, "wlhost: no socket\n"); return false; }

    fprintf(stderr, "wlhost: serving on WAYLAND_DISPLAY=%s\n", socket);
    if (startup_cmd) spawn_client(socket, startup_cmd);
    return true;
}

void wlhost_pump() {
    if (!g.display) return;
    wl_event_loop_dispatch(wl_display_get_event_loop(g.display), 0); // non-blocking
    // Let the client draw the next frame.
    if (g.surface && g.mapped) {
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_surface_send_frame_done(g.surface, &now);
    }
    wl_display_flush_clients(g.display);
}

bool wlhost_frame(int* width, int* height, const unsigned char** rgba, bool* dirty) {
    if (g.rgba.empty() || g.width == 0) return false;
    if (width) *width = g.width;
    if (height) *height = g.height;
    if (rgba) *rgba = g.rgba.data();
    if (dirty) { *dirty = g.dirty; g.dirty = false; }
    return true;
}

// ── Input injection ──────────────────────────────────────────────────────────
void wlhost_pointer_motion(double sx, double sy) {
    if (!g.surface || !g.mapped) return;
    if (!g.ptr_entered) {
        wlr_seat_pointer_notify_enter(g.seat, g.surface, sx, sy);
        g.ptr_entered = true;
    }
    wlr_seat_pointer_notify_motion(g.seat, now_ms(), sx, sy);
    wlr_seat_pointer_notify_frame(g.seat);
}

void wlhost_pointer_button(unsigned linux_button, bool pressed) {
    if (!g.surface || !g.mapped) return;
    wlr_seat_pointer_notify_button(g.seat, now_ms(), linux_button,
                                   pressed ? WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED);
    wlr_seat_pointer_notify_frame(g.seat);
}

void wlhost_pointer_axis(double value) {
    if (!g.surface || !g.mapped) return;
    wlr_seat_pointer_notify_axis(g.seat, now_ms(), WLR_AXIS_ORIENTATION_VERTICAL,
                                 value, (int32_t)value, WLR_AXIS_SOURCE_WHEEL);
    wlr_seat_pointer_notify_frame(g.seat);
}

void wlhost_key(unsigned evdev_keycode, bool pressed) {
    if (!g.surface || !g.mapped) return;
    wlr_keyboard_key_event ev{};
    ev.time_msec = now_ms();
    ev.keycode = evdev_keycode;
    ev.update_state = true;
    ev.state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;
    wlr_keyboard_notify_key(&g.keyboard, &ev); // updates xkb, fires on_kb_*
}

void wlhost_type_ascii(const char* text) {
    // Minimal lowercase US-QWERTY evdev map for the self-test.
    static const int letter[26] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
        KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
        KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    static const int digit[10] = {
        KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
    for (const char* p = text; *p; p++) {
        int code = -1;
        char c = *p;
        if (c >= 'a' && c <= 'z') code = letter[c - 'a'];
        else if (c >= '0' && c <= '9') code = digit[c - '0'];
        else if (c == ' ') code = KEY_SPACE;
        else if (c == '\n') code = KEY_ENTER;
        else if (c == '-') code = KEY_MINUS;
        else if (c == '.') code = KEY_DOT;
        else if (c == '/') code = KEY_SLASH;
        if (code < 0) continue;
        wlhost_key(code, true);
        wlhost_key(code, false);
    }
}
