/* libveuphoria.c  --  the VEUPHORIA engine core.
 *
 * A small, FLAT command API backed by SDL2.  Euphoria calls these over its C
 * FFI (see veuphoria.e); all the messy window/GPU work stays here in C.  The
 * boundary is deliberately simple -- only ints, floats, and C strings cross
 * it, never SDL structs.  Backend is SDL2's 2D renderer for now; it can be
 * swapped for Vulkan later without changing the Euphoria side.
 *
 * build:  gcc -shared -fPIC -o libveuphoria.so libveuphoria.c `sdl2-config --cflags --libs`
 */
#include <SDL2/SDL.h>
#include "font5x7.h"

static SDL_Window   *win = NULL;
static SDL_Renderer *ren = NULL;

/* open a window; returns 1 on success, 0 on failure */
int veu_open(int w, int h, const char *title) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 0;
    win = SDL_CreateWindow(title ? title : "veuphoria",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           w, h, SDL_WINDOW_SHOWN);
    if (!win) return 0;
    /* try GPU-accelerated + vsync; fall back to software (e.g. headless) */
    ren = SDL_CreateRenderer(win, -1,
              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) return 0;
    return 1;
}

/* clear the whole window to an (r,g,b) colour */
void veu_clear(int r, int g, int b) {
    if (!ren) return;
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    SDL_RenderClear(ren);
}

/* set the colour used by line/rect */
void veu_color(int r, int g, int b) {
    if (!ren) return;
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
}

/* draw a line from (x1,y1) to (x2,y2) in the current colour */
void veu_line(int x1, int y1, int x2, int y2) {
    if (!ren) return;
    SDL_RenderDrawLine(ren, x1, y1, x2, y2);
}

/* draw a rectangle; fill != 0 means solid, 0 means outline */
void veu_rect(int x, int y, int w, int h, int fill) {
    if (!ren) return;
    SDL_Rect rc = { x, y, w, h };
    if (fill) SDL_RenderFillRect(ren, &rc);
    else      SDL_RenderDrawRect(ren, &rc);
}

/* draw text at (x,y) in the current colour, using the built-in 5x7 font.
 * scale is the pixel size (1 = 5x7 px per glyph, 2 = twice as big, ...).
 * lowercase renders as uppercase; unknown chars are blank; '\n' starts a line. */
void veu_text(int x, int y, const char *s, int scale) {
    if (!ren || !s) return;
    if (scale < 1) scale = 1;
    int cx = x;
    for (const char *p = s; *p; ++p) {
        int c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') c -= 32;          /* lowercase -> uppercase */
        if (c == '\n') { y += 8 * scale; cx = x; continue; }
        if (c < 32 || c > 90) { cx += 6 * scale; continue; }
        const unsigned char *g = FONT5X7[c - 32];
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (g[row] & (1 << (4 - col))) {
                    SDL_Rect r = { cx + col*scale, y + row*scale, scale, scale };
                    SDL_RenderFillRect(ren, &r);
                }
            }
        }
        cx += 6 * scale;                            /* 5px glyph + 1px gap */
    }
}

/* show everything drawn since the last present */
void veu_present(void) {
    if (ren) SDL_RenderPresent(ren);
}

/* pump events once.  Returns:
 *   -1  the window/ESC asked to quit
 *    0  nothing this poll
 *   >0  the SDL keycode of a key pressed this poll
 */
int veu_poll(void) {
    SDL_Event e;
    int key = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return -1;
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_ESCAPE) return -1;
            key = e.key.keysym.sym;
        }
    }
    return key;
}

/* return the SDL keycode for a named key id, so Euphoria needn't hardcode
 * SDL's magic numbers: 0=right 1=left 2=down 3=up 4=space 5=enter */
int veu_key(int id) {
    switch (id) {
        case 0: return SDLK_RIGHT;
        case 1: return SDLK_LEFT;
        case 2: return SDLK_DOWN;
        case 3: return SDLK_UP;
        case 4: return SDLK_SPACE;
        case 5: return SDLK_RETURN;
        default: return 0;
    }
}

/* sleep for ms milliseconds (steady frame pacing) */
void veu_delay(int ms) {
    SDL_Delay(ms);
}

/* milliseconds since SDL started (for your own timing) */
int veu_ticks(void) {
    return (int)SDL_GetTicks();
}

/* tear the window down */
void veu_close(void) {
    if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
    if (win) { SDL_DestroyWindow(win);   win = NULL; }
    SDL_Quit();
}
