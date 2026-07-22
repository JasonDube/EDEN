// In-world console text rasterizer for VIEUPHORIA: renders lines of text into an
// RGBA8 canvas that gets uploaded onto a HUD quad in the 3D scene.
#pragma once

#include <string>
#include <vector>

// Load a monospace TTF (falls back to a system font). Returns true on success.
bool hud_init(const char* font_path);

// Render `lines` into out (sized W*H*4, RGBA8, opaque dark background). Shows the
// last lines that fit; draws a caret block after the final line (the input line).
void hud_render(std::vector<unsigned char>& out, int W, int H, int px_size,
                const std::vector<std::string>& lines);
