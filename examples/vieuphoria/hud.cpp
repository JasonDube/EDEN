// In-world console text rasterizer — implementation. See hud.h.

#include "hud.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace {
bool g_ready = false;
std::vector<unsigned char> g_font;
stbtt_fontinfo g_info;

const char* kFonts[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
};

bool load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    g_font.resize((size_t)n);
    size_t got = fread(g_font.data(), 1, (size_t)n, f);
    fclose(f);
    if ((long)got != n) return false;
    return stbtt_InitFont(&g_info, g_font.data(),
                          stbtt_GetFontOffsetForIndex(g_font.data(), 0)) != 0;
}
} // namespace

bool hud_init(const char* path) {
    if (path && *path && load(path)) g_ready = true;
    else
        for (const char* p : kFonts)
            if (load(p)) { g_ready = true; break; }
    return g_ready;
}

void hud_render(std::vector<unsigned char>& out, int W, int H, int px,
                const std::vector<std::string>& lines) {
    out.assign((size_t)W * H * 4, 0);
    const unsigned char bg[3] = {17, 20, 27};
    for (size_t i = 0; i < (size_t)W * H; i++) {
        out[i * 4 + 0] = bg[0];
        out[i * 4 + 1] = bg[1];
        out[i * 4 + 2] = bg[2];
        out[i * 4 + 3] = 255;
    }
    if (!g_ready) return;

    float scale = stbtt_ScaleForPixelHeight(&g_info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_info, &asc, &desc, &gap);
    int lineH = (int)((asc - desc) * scale + 0.5f) + 4;
    int margin = 12;

    // Word-wrap to the panel width (monospace: fixed advance).
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&g_info, 'M', &adv, &lsb);
    int charW = (int)(adv * scale + 0.5f);
    if (charW < 1) charW = 1;
    int maxChars = (W - 2 * margin) / charW;
    if (maxChars < 8) maxChars = 8;
    std::vector<std::string> wrapped;
    for (const std::string& s : lines) {
        if ((int)s.size() <= maxChars) { wrapped.push_back(s); continue; }
        size_t pos = 0;
        while (pos < s.size()) {
            size_t len = std::min((size_t)maxChars, s.size() - pos);
            if (pos + len < s.size()) {
                size_t sp = s.rfind(' ', pos + len);
                if (sp != std::string::npos && sp > pos) len = sp - pos;
            }
            wrapped.push_back(s.substr(pos, len));
            pos += len;
            while (pos < s.size() && s[pos] == ' ') pos++;
        }
    }

    int maxLines = (H - 2 * margin) / lineH;
    if (maxLines < 1) maxLines = 1;
    int start = (int)wrapped.size() - maxLines;
    if (start < 0) start = 0;

    const unsigned char fg[3] = {214, 219, 228};
    int y = margin;
    for (int li = start; li < (int)wrapped.size(); li++) {
        const std::string& s = wrapped[li];
        int baseline = y + (int)(asc * scale + 0.5f);
        float x = (float)margin;
        for (unsigned char c : s) {
            int cp = c;
            int ax, lsb;
            stbtt_GetCodepointHMetrics(&g_info, cp, &ax, &lsb);
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&g_info, cp, scale, scale, &x0, &y0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> gb((size_t)gw * gh);
                stbtt_MakeCodepointBitmap(&g_info, gb.data(), gw, gh, gw, scale, scale, cp);
                int ox = (int)(x + 0.5f) + x0, oy = baseline + y0;
                for (int gy = 0; gy < gh; gy++) {
                    int dy = oy + gy;
                    if (dy < 0 || dy >= H) continue;
                    for (int gx = 0; gx < gw; gx++) {
                        int dx = ox + gx;
                        if (dx < 0 || dx >= W) continue;
                        unsigned char cov = gb[(size_t)gy * gw + gx];
                        if (!cov) continue;
                        float a = cov / 255.0f, inv = 1.0f - a;
                        unsigned char* d = &out[((size_t)dy * W + dx) * 4];
                        d[0] = (unsigned char)(fg[0] * a + d[0] * inv);
                        d[1] = (unsigned char)(fg[1] * a + d[1] * inv);
                        d[2] = (unsigned char)(fg[2] * a + d[2] * inv);
                    }
                }
            }
            x += ax * scale;
        }
        // Caret block at the end of the final (input) line.
        if (li == (int)wrapped.size() - 1) {
            int cw = (int)(px * 0.55f), ch = (int)(asc * scale);
            int cx = (int)(x + 2.0f), cy = baseline - ch + 2;
            for (int yy = 0; yy < ch; yy++) {
                int dy = cy + yy;
                if (dy < 0 || dy >= H) continue;
                for (int xx = 0; xx < cw; xx++) {
                    int dx = cx + xx;
                    if (dx < 0 || dx >= W) continue;
                    unsigned char* d = &out[((size_t)dy * W + dx) * 4];
                    d[0] = 150; d[1] = 190; d[2] = 120;
                }
            }
        }
        y += lineH;
    }
}
