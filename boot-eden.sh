#!/bin/sh
# ── Boot into EDEN OS · the 3D desktop, straight on the hardware ─────────────
# EDEN takes the real display via Vulkan (VK_EXT_acquire_drm_display) and reads
# input straight from libinput. No X11, no Wayland, no compositor underneath —
# EDEN *is* the display server. You boot into a 3D world with a live terminal.
#
# SAFETY:
#   * Refuses to run inside a graphical session (it must OWN the display).
#   * Self-exits after $TIMEOUT seconds; a watchdog SIGKILLs it if it hangs.
#   * On exit it AUTO-RECOVERS the display back to COSMIC (bounces the VT so the
#     compositor re-modesets). On NVIDIA this handoff is flaky, so if the screen
#     is still black, log in on a text console and run ./eden-recover.sh
#
# HOW TO USE:
#   1. Save your work in COSMIC.
#   2. Ctrl+Alt+F3  -> text console. Log in (username + password).
#   3. cd ~/Desktop/EDEN_GAME_WORK && ./boot-eden.sh
#      SUCCESS = your TV shows the 3D scene with a foot terminal you can type in.
#   4. Quit: Alt+Esc (or wait out the timer). It returns you to COSMIC by itself.
#   5. STUCK ON BLACK? Ctrl+Alt+F3, log in, run ./eden-recover.sh   (no reboot).
#   6. Tell Claude; we read $LOG together.
set -e
cd "$(dirname "$0")"

if [ -n "$WAYLAND_DISPLAY" ] || [ -n "$DISPLAY" ]; then
    echo "Refusing to run inside a graphical session (WAYLAND_DISPLAY/DISPLAY set)."
    echo "EDEN's display path must OWN the screen. Run from a bare TTY:"
    echo "  Ctrl+Alt+F3, log in, then ./boot-eden.sh"
    exit 1
fi

BIN=build/examples/tearsheet3d/tearsheet3d
if [ ! -x "$BIN" ]; then
    echo "Building tearsheet3d first..."
    cmake --build build --target tearsheet3d
fi

TIMEOUT="${TIMEOUT:-30}"
LOG=/tmp/eden-boot.log
: > "$LOG" || true   # truncate up front so an empty log means "produced no output"

# Find COSMIC's VT now, while we can still see the process table cleanly.
COSMIC_VT=$(ps -o tty= -C cosmic-comp 2>/dev/null | grep -o '[0-9]*' | head -1)
[ -n "$COSMIC_VT" ] || COSMIC_VT=1
SCRATCH_VT=3
[ "$SCRATCH_VT" = "$COSMIC_VT" ] && SCRATCH_VT=2

# Always run on exit (normal quit, timer, Ctrl+C, or error): kill EDEN and bounce
# the console back to COSMIC so the compositor re-takes and repaints the display.
recover() {
    pkill -x tearsheet3d 2>/dev/null || true
    sleep 1
    pkill -9 -x tearsheet3d 2>/dev/null || true
    echo
    echo "Recovering display -> COSMIC on VT $COSMIC_VT ..."
    chvt "$SCRATCH_VT" 2>/dev/null || true
    sleep 1
    chvt "$COSMIC_VT" 2>/dev/null || true
    echo "If your screen is still black: log in on a text console (Ctrl+Alt+F3)"
    echo "and run ./eden-recover.sh   ($LOG has this run's output)"
}
trap recover EXIT INT TERM

echo "=================================================================="
echo " EDEN OS  ·  boot to the 3D desktop (DRM/KMS + libinput)"
echo " Auto-quits in ${TIMEOUT}s.  In-app quit: Alt+Esc."
echo " COSMIC is on VT ${COSMIC_VT}; on exit this script returns you there."
echo " Log: $LOG"
echo "=================================================================="
sleep 2

# Watchdog: always release the display, even if the app wedges.
( sleep "$TIMEOUT"; pkill -x tearsheet3d 2>/dev/null || true
  sleep 3; pkill -9 -x tearsheet3d 2>/dev/null || true ) &
WATCHDOG=$!

# Run from the binary's dir so the embedded compositor spawns foot with a sane
# working directory. EDEN_KMS flips the engine onto the DRM-display backend.
# `set -e` is off for this line so a nonzero exit still runs the recover trap.
( cd "$(dirname "$BIN")" && EDEN_KMS=1 exec ./tearsheet3d ) 2>&1 | tee "$LOG" || true

kill "$WATCHDOG" 2>/dev/null || true
# recover() runs here via the EXIT trap.
