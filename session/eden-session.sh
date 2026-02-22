#!/bin/bash
# EDEN OS Session Launcher
# This script is executed by the display manager when "EDEN OS" is selected at login.
# If EDEN crashes, falls back to a basic bash session.

EDEN_DIR="$(dirname "$(readlink -f "$0")")/.."
EDEN_BIN="$EDEN_DIR/build/examples/terrain_editor/terrain_editor"

export EDEN_SESSION=1
export XDG_SESSION_TYPE=x11

# Change to EDEN directory so assets are found
cd "$EDEN_DIR/build/examples/terrain_editor" || cd "$HOME"

# Launch EDEN in session mode; if it exits/crashes, fall back to bash
if [ -x "$EDEN_BIN" ]; then
    "$EDEN_BIN" --session-mode
    EXIT_CODE=$?
    if [ $EXIT_CODE -ne 0 ]; then
        echo "EDEN exited with code $EXIT_CODE, falling back to terminal..."
        exec /usr/bin/bash --login
    fi
else
    echo "EDEN binary not found at $EDEN_BIN"
    exec /usr/bin/bash --login
fi
