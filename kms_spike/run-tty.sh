#!/bin/sh
# ── EDEN KMS display spike · bare-TTY runner ─────────────────────────────────
# Proves Vulkan can take the real display straight from DRM (no X11/Wayland).
#
# SAFETY:
#   * Refuses to run inside a graphical session (it needs to OWN the display).
#   * The probe self-exits after KMS_SECONDS (default 8s); a watchdog SIGKILLs
#     it if it somehow hangs. You can never get stuck.
#   * Ctrl+Alt+F1 returns to COSMIC at any time.
#
# HOW TO USE:
#   1. Save your work in COSMIC.
#   2. Ctrl+Alt+F3  -> text console. Log in (username + password).
#   3. cd ~/Desktop/EDEN_GAME_WORK/kms_spike && ./run-tty.sh
#      SUCCESS = your TV pulses through colours for ~8s, then prints SUCCESS.
#      Black screen / [FAIL] line = it didn't take the display; we read the log.
#   4. Back to COSMIC: Ctrl+Alt+F1. Tell Claude; we read $LOG together.
set -e
cd "$(dirname "$0")"

if [ -n "$WAYLAND_DISPLAY" ] || [ -n "$DISPLAY" ]; then
    echo "Refusing to run inside a graphical session (WAYLAND_DISPLAY/DISPLAY set)."
    echo "The Vulkan display path must OWN the screen. Run from a bare TTY:"
    echo "  Ctrl+Alt+F3, log in, then ./run-tty.sh"
    exit 1
fi

[ -x ./kmsprobe ] || ./build.sh

SECONDS_RUN="${KMS_SECONDS:-8}"
LOG=/tmp/eden-kms.log
echo "=================================================================="
echo " EDEN · KMS display spike"
echo " Runs ~${SECONDS_RUN}s then exits.  Recovery any time: Ctrl+Alt+F1"
echo " Log: $LOG"
echo "=================================================================="
sleep 2

# Watchdog: guarantee the display is released even if the probe wedges.
( sleep $((SECONDS_RUN + 10)); pkill -x kmsprobe 2>/dev/null || true
  sleep 2; pkill -9 -x kmsprobe 2>/dev/null || true ) &
WATCHDOG=$!

KMS_SECONDS="$SECONDS_RUN" ./kmsprobe 2>&1 | tee "$LOG" || true

kill "$WATCHDOG" 2>/dev/null || true
echo
echo "Done. Back to COSMIC: Ctrl+Alt+F1.  Then tell Claude to read $LOG"
