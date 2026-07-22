#!/bin/sh
# Build the KMS display spike. Safe to run inside your normal desktop.
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall kmsprobe.cpp \
    $(pkg-config --cflags libdrm) \
    -lvulkan $(pkg-config --libs libdrm) \
    -o kmsprobe
echo "built ./kmsprobe"
