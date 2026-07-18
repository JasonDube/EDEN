// Embedded headless Wayland server for the Tearsheet × EDEN spike (Phase 1b/1c).
//
// Runs wlroots on a software (Pixman) renderer with no display of its own and
// no dmabuf, so a client (foot) falls back to wl_shm and its surface buffer is
// directly CPU-readable. We expose the latest client frame as RGBA8 so EDEN can
// upload it onto a 3D quad.
//
// Header is intentionally free of wlroots/Wayland types so it can be included
// from the EDEN translation unit without header clashes.
#pragma once

// Create the server, bind a socket, and spawn `startup_cmd` as a client of it.
// Returns true on success. Call once, after EDEN's window/Vulkan are up.
bool wlhost_init(const char* startup_cmd);

// Pump the Wayland event loop and send frame callbacks. Call once per frame.
void wlhost_pump();

// Fetch the latest committed client frame as RGBA8 (row-major, tightly packed).
// Returns false until a frame exists. When it returns true, *dirty is set to
// whether the frame changed since the last call (and the internal flag clears).
bool wlhost_frame(int* width, int* height, const unsigned char** rgba, bool* dirty);

// ── Input injection (Phase 2) ────────────────────────────────────────────────
// Pointer position in surface-local coordinates (pixels; origin top-left).
void wlhost_pointer_motion(double sx, double sy);
void wlhost_pointer_button(unsigned linux_button, bool pressed); // BTN_LEFT etc.
void wlhost_pointer_axis(double value);                          // vertical scroll
// A key press/release by Linux evdev keycode (xkb handles modifiers/keymap).
void wlhost_key(unsigned evdev_keycode, bool pressed);
// Convenience: type a lowercase ASCII string (a-z, 0-9, space, '\n'=Enter).
void wlhost_type_ascii(const char* text);
