#!/usr/bin/env bash
# build the VEUPHORIA engine
cd "$(dirname "$0")"
gcc -shared -fPIC -o libveuphoria.so libveuphoria.c $(sdl2-config --cflags --libs) \
    && echo "built libveuphoria.so" || { echo "build failed"; exit 1; }
