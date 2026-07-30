#!/bin/sh
# ── EDEN recovery · get your COSMIC desktop back without rebooting ───────────
# If EDEN left you on a black screen, switch to a text console (Ctrl+Alt+F3),
# log in, and run:   ./eden-recover.sh
#
# What it does: makes sure EDEN is dead, then bounces the active console away
# from and back to COSMIC's VT. That forces the compositor to re-take the DRM
# master and re-modeset the display, which is what repaints your screen.

# 1. Make sure nothing is still holding the display.
#
# Every binary that can take the display, not just tearsheet3d -- kms-ted.sh runs
# the editor on the same backend, and a recovery script that does not know about
# the thing currently holding your screen bounces the VT into a display somebody
# else still owns, and you stay black.
for app in tearsheet3d terrain_editor tessara_axiom; do
    pkill -x "$app" 2>/dev/null || true
done
sleep 1
for app in tearsheet3d terrain_editor tessara_axiom; do
    pkill -9 -x "$app" 2>/dev/null || true
done

# 2. Find COSMIC's VT (falls back to tty1).
COSMIC_TTY=$(ps -o tty= -C cosmic-comp 2>/dev/null | grep -o 'tty[0-9]*' | head -1)
COSMIC_VT=$(echo "$COSMIC_TTY" | grep -o '[0-9]*')
[ -n "$COSMIC_VT" ] || COSMIC_VT=1

# 3. A scratch text VT to bounce through (anything that isn't COSMIC's).
SCRATCH_VT=3
[ "$SCRATCH_VT" = "$COSMIC_VT" ] && SCRATCH_VT=2

echo "Recovering display -> COSMIC on VT $COSMIC_VT (bounce via VT $SCRATCH_VT)..."
chvt "$SCRATCH_VT" 2>/dev/null || sudo chvt "$SCRATCH_VT"
sleep 1
chvt "$COSMIC_VT" 2>/dev/null || sudo chvt "$COSMIC_VT"
echo "Done. If it's still black, run it once more, or wait a few seconds for COSMIC to repaint."
