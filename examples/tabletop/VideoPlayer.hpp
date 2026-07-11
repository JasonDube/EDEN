#pragma once

// Dialogue video playback for the tabletop, ported from Tearsheet.
//
// Wraps one libmpv instance driving the *software* render API. mpv owns demux,
// decode, audio output and A/V sync on its own threads; each tick we ask it to
// paint the current frame into `pixels` (a CPU RGBA buffer) which the app then
// uploads to a GPU texture. Transport (play/pause/loop/volume) is just mpv
// properties and commands.

#include <mpv/client.h>
#include <mpv/render.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Max edge we ask mpv to software-render video frames at. Capped so big videos
// don't thrash the per-frame texture upload; mpv scales to whatever we request.
inline constexpr int kDlgVideoDecode = 1280;

struct VideoPlayer {
    mpv_handle*         mpv  = nullptr;
    mpv_render_context* rctx = nullptr;
    int  w = 0, h = 0;                       // frame size (0 until first frame)
    std::vector<unsigned char> pixels;       // w*h*4, packed "rgb0"
    bool newFrame = false;                   // a fresh frame is in `pixels`

    VideoPlayer() = default;
    ~VideoPlayer() { close(); }
    VideoPlayer(const VideoPlayer&) = delete;             // owns mpv handles
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    bool isOpen() const { return mpv != nullptr; }

    bool open(const std::string& file) {
        close();
        mpv = mpv_create();
        if (!mpv) return false;
        // We render frames ourselves — no mpv window, OSC, or key handling.
        mpv_set_option_string(mpv, "vo", "libmpv");
        mpv_set_option_string(mpv, "hwdec", "no");   // software frames for SW render
        mpv_set_option_string(mpv, "osc", "no");
        mpv_set_option_string(mpv, "input-default-bindings", "no");
        mpv_set_option_string(mpv, "input-vo-keyboard", "no");
        mpv_set_option_string(mpv, "keep-open", "yes");  // hold last frame at EOF
        if (mpv_initialize(mpv) < 0) { close(); return false; }

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
            {MPV_RENDER_PARAM_INVALID,  nullptr},
        };
        if (mpv_render_context_create(&rctx, mpv, params) < 0) { close(); return false; }

        const char* cmd[] = {"loadfile", file.c_str(), nullptr};
        mpv_command(mpv, cmd);
        w = h = 0;
        return true;
    }

    void close() {
        if (rctx) { mpv_render_context_free(rctx); rctx = nullptr; }  // free before mpv
        if (mpv)  { mpv_terminate_destroy(mpv);    mpv  = nullptr; }
        w = h = 0; newFrame = false;
        pixels.clear();
    }

    // Pump events and, when mpv signals a new frame, render it into `pixels`.
    void poll() {
        if (!mpv || !rctx) return;
        while (mpv_event* ev = mpv_wait_event(mpv, 0)) {  // drain so the core advances
            if (ev->event_id == MPV_EVENT_NONE) break;
        }
        if (!(mpv_render_context_update(rctx) & MPV_RENDER_UPDATE_FRAME)) return;

        if (w == 0 || h == 0) {  // size known once the first frame is decoded
            int64_t vw = 0, vh = 0;
            if (mpv_get_property(mpv, "dwidth",  MPV_FORMAT_INT64, &vw) < 0 || vw <= 0) return;
            if (mpv_get_property(mpv, "dheight", MPV_FORMAT_INT64, &vh) < 0 || vh <= 0) return;
            int tw = static_cast<int>(vw), th = static_cast<int>(vh);
            if (tw > kDlgVideoDecode || th > kDlgVideoDecode) {
                if (tw >= th) { th = std::max(1, static_cast<int>(std::lround(static_cast<double>(th) * kDlgVideoDecode / tw))); tw = kDlgVideoDecode; }
                else          { tw = std::max(1, static_cast<int>(std::lround(static_cast<double>(tw) * kDlgVideoDecode / th))); th = kDlgVideoDecode; }
            }
            w = tw; h = th;
            pixels.assign(static_cast<size_t>(w) * h * 4, 0);
        }

        int    sz[2]  = {w, h};
        size_t stride = static_cast<size_t>(w) * 4;
        mpv_render_param rp[] = {
            {MPV_RENDER_PARAM_SW_SIZE,    sz},
            {MPV_RENDER_PARAM_SW_FORMAT,  const_cast<char*>("rgb0")},
            {MPV_RENDER_PARAM_SW_STRIDE,  &stride},
            {MPV_RENDER_PARAM_SW_POINTER, pixels.data()},
            {MPV_RENDER_PARAM_INVALID,    nullptr},
        };
        if (mpv_render_context_render(rctx, rp) >= 0) newFrame = true;
    }

    // ----- transport -----
    bool   paused()         { int f = 0;   mpv_get_property(mpv, "pause",    MPV_FORMAT_FLAG,   &f); return f != 0; }
    void   setPaused(bool p){ int f = p;   mpv_set_property(mpv, "pause",    MPV_FORMAT_FLAG,   &f); }
    void   setVolume(int v) { double d = v; mpv_set_property(mpv, "volume",  MPV_FORMAT_DOUBLE, &d); }
    bool   muted()          { int f = 0;   mpv_get_property(mpv, "mute",     MPV_FORMAT_FLAG,   &f); return f != 0; }
    void   setMuted(bool m) { int f = m;   mpv_set_property(mpv, "mute",     MPV_FORMAT_FLAG,   &f); }
    void   setLoop(bool on) { mpv_set_property_string(mpv, "loop-file", on ? "inf" : "no"); }
    void   restart()        { const char* c[] = {"seek", "0", "absolute", nullptr}; mpv_command(mpv, c); }

    // ----- frame-precise control (for a clip's "rest frame") -----
    // True once playback reaches the end (with keep-open, it holds the last frame).
    bool   eofReached()     { int f = 0; mpv_get_property(mpv, "eof-reached", MPV_FORMAT_FLAG, &f); return f != 0; }
    // Frames per second of the current clip (container rate; falls back to filtered).
    double fps() {
        double f = 0;
        if (mpv_get_property(mpv, "container-fps", MPV_FORMAT_DOUBLE, &f) < 0 || f <= 0)
            mpv_get_property(mpv, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &f);
        return f;
    }
    long   frameCount()     { int64_t n = -1; mpv_get_property(mpv, "estimated-frame-count", MPV_FORMAT_INT64, &n); return (long)n; }
    // Seek to a specific frame index and hold there (paused). Frame -> time via fps.
    void   seekToFramePause(long frame) {
        double f = fps(); if (f <= 0) f = 30.0;
        double t = (frame + 0.5) / f;   // mid-frame so we land inside frame `frame`
        char b[64]; std::snprintf(b, sizeof(b), "%.4f", t);
        const char* c[] = {"seek", b, "absolute", "exact", nullptr};
        mpv_command(mpv, c);
        setPaused(true);
    }
};
