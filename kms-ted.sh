#!/bin/sh
# ── TED on the bare hardware · does the compositor cause the stall? ──────────
#
# WHY THIS EXISTS
#
# In your COSMIC session the editor does 2.4 ms of work per frame and yet the
# frame takes 19. The missing time is not ours: vkAcquireNextImageKHR blocks for
# fifty-odd milliseconds about three times a second, and it does that with no
# level loaded, with update() at 0.02 ms, windowed, fullscreen, vsync on and
# vsync off. Everything above the present is innocent.
#
# The suspect is the path the frame takes to the screen. GLFW here is the system
# 3.3 build, which is X11-only, so the frame goes app -> X server -> XWayland ->
# COSMIC -> display. This runs it with EDEN_KMS=1 instead, where the engine takes
# the display directly (VK_EXT_acquire_drm_display) and there is no compositor at
# all. If the stall is gone, it was never ours. If it is still there, it is the
# driver and we stop blaming Wayland.
#
# HOW TO USE
#   1. Save your work in COSMIC.
#   2. Ctrl+Alt+F3   -> text console. Log in (username + password).
#   3. cd ~/Desktop/EDEN_GAME_WORK && ./kms-ted.sh
#   4. It quits itself after ${TIMEOUT:-30}s and prints the verdict.
#   5. STUCK ON BLACK? Ctrl+Alt+F3, log in, ./eden-recover.sh   (no reboot.)
#
# SAFETY, same rails as boot-eden.sh
#   * Refuses to run inside a graphical session -- it must OWN the display.
#   * Self-exits after the timeout; a watchdog SIGKILLs it if it wedges.
#   * On exit -- normal, timer, Ctrl+C or crash -- it bounces the VT so COSMIC
#     re-takes DRM master and repaints. Your desktop session is not restarted and
#     nothing you had open is lost; a black screen here is a missed repaint, not a
#     crash.
#
# EXPECT ROUGH EDGES: the editor has never been run on the KMS backend. Input
# comes from libinput there rather than GLFW, so the controls may misbehave. That
# is fine for this -- the frame log answers the question whether or not the mouse
# does anything.
set -e
cd "$(dirname "$0")"

if [ -n "$WAYLAND_DISPLAY" ] || [ -n "$DISPLAY" ]; then
    echo "Refusing to run inside a graphical session (WAYLAND_DISPLAY/DISPLAY set)."
    echo "The whole point is to take the display away from the compositor, so this"
    echo "has to start from a bare console:"
    echo "  Ctrl+Alt+F3, log in, then ./kms-ted.sh"
    exit 1
fi

BIN=build/examples/terrain_editor/terrain_editor
if [ ! -x "$BIN" ]; then
    echo "Building terrain_editor first..."
    cmake --build build --target terrain_editor -j
fi

LEVEL="${LEVEL:-red_planet}"
TIMEOUT="${TIMEOUT:-30}"
LOG=/tmp/eden-ted-kms.log
: > "$LOG" || true   # truncate, so an empty log means "produced no output at all"

# COSMIC's VT, found now while the process table is still easy to read.
COSMIC_VT=$(ps -o tty= -C cosmic-comp 2>/dev/null | grep -o '[0-9]*' | head -1)
[ -n "$COSMIC_VT" ] || COSMIC_VT=1
SCRATCH_VT=3
[ "$SCRATCH_VT" = "$COSMIC_VT" ] && SCRATCH_VT=2

recover() {
    pkill -x terrain_editor 2>/dev/null || true
    sleep 1
    pkill -9 -x terrain_editor 2>/dev/null || true
    echo
    echo "Recovering display -> COSMIC on VT $COSMIC_VT ..."
    chvt "$SCRATCH_VT" 2>/dev/null || true
    sleep 1
    chvt "$COSMIC_VT" 2>/dev/null || true

    # The verdict, printed HERE on the console, so the answer is readable without
    # getting back to a desktop first.
    echo
    echo "=================== what the frames did ==================="
    if grep -q '^\[perf\] [0-9]' "$LOG" 2>/dev/null; then
        echo "last few seconds:"
        grep '^\[perf\] [0-9]' "$LOG" | tail -5
        echo
        SPIKES=$(grep -c '^\[spike\]' "$LOG" 2>/dev/null || echo 0)
        echo "frames over 30 ms: $SPIKES"
        if [ "$SPIKES" -lt 5 ]; then
            echo "-> The stall is GONE without a compositor. It was the present path."
        else
            echo "-> Still stalling with no compositor at all. Not Wayland's fault;"
            echo "   the next suspect is the driver or the swapchain itself."
            grep '^\[spike\]' "$LOG" | tail -3
        fi
    else
        echo "No frame data. It did not get as far as rendering -- read $LOG;"
        echo "the KMS display or libinput probably refused, and the first error"
        echo "in that file will say which."
    fi
    echo "=========================================================="
    echo "full log: $LOG"
    echo "If your screen is still black: Ctrl+Alt+F3, log in, ./eden-recover.sh"
}
trap recover EXIT INT TERM

echo "=================================================================="
echo " TED on DRM/KMS  ·  no X, no Wayland, no compositor"
echo " Level: $LEVEL, straight into play mode, frame cost logged each second."
echo " Auto-quits in ${TIMEOUT}s.  COSMIC is on VT ${COSMIC_VT}."
echo " Log: $LOG"
echo "=================================================================="
sleep 2

# Watchdog: release the display even if the app wedges completely.
( sleep "$TIMEOUT"; pkill -x terrain_editor 2>/dev/null || true
  sleep 3; pkill -9 -x terrain_editor 2>/dev/null || true ) &
WATCHDOG=$!

# From the binary's own directory, or it will not find shaders/ or levels/.
# `|| true` so a nonzero exit still reaches the recover trap.
( cd "$(dirname "$BIN")" \
  && EDEN_KMS=1 exec ./terrain_editor --level "$LEVEL" --play --perf-log ) 2>&1 \
  | tee "$LOG" || true

kill "$WATCHDOG" 2>/dev/null || true
# recover() runs here via the EXIT trap.
