#pragma once

// VideoPlayer — minimal CPU video decoder built on libav (ffmpeg).
//
// Opens a video file, advances along its presentation timeline as you call
// update(deltaTime), and exposes the most-recently-decoded frame as a
// tightly-packed RGBA buffer ready to push straight to a Vulkan texture
// (e.g. via ModelRenderer::updateTexture).
//
// One frame is decoded only when the playback time has advanced past the
// next packet's PTS — so update() is cheap when nothing new is needed and
// expensive only when there's actually a new frame to deliver.

#include <cstdint>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace eden {

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // Open a video file. Returns true on success; on failure errorMessage()
    // is populated. Calling open again on a live player closes the old file first.
    bool open(const std::string& filepath);
    void close();

    // Advance playback time. Decodes the next frame only when the new time
    // crosses the next frame's PTS. Loops to start by default; pass false
    // to stop playback at the end (isPlaying() goes false).
    void update(float deltaTime, bool loop = true);

    // Tightly-packed RGBA8 buffer of the current frame, sized
    // width() * height() * 4 bytes. Empty until at least one frame decoded.
    const std::vector<uint8_t>& currentFrame() const { return m_currentFrame; }
    bool                        hasFrame()      const { return m_hasFrame; }
    bool                        frameJustChanged() const { return m_frameJustChanged; }

    int   width()    const { return m_width; }
    int   height()   const { return m_height; }
    float duration() const { return m_duration; }
    float currentTime() const { return m_currentTime; }
    bool  isPlaying()   const { return m_playing; }

    void play()  { m_playing = true; }
    void pause() { m_playing = false; }
    void seek(float t);  // Seconds from start — clamped to [0, duration].

    const std::string& errorMessage() const { return m_error; }

private:
    bool decodeNextFrame();   // Pulls one packet, decodes, sw-scales to RGBA.

    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext*  m_codecCtx  = nullptr;
    AVFrame*         m_decoded   = nullptr;
    AVFrame*         m_rgba      = nullptr;
    AVPacket*        m_packet    = nullptr;
    SwsContext*      m_swsCtx    = nullptr;
    int              m_videoStream = -1;
    double           m_timeBase = 0.0;          // av_q2d(stream->time_base)
    double           m_nextFramePts = 0.0;      // PTS of the most recently decoded frame, in seconds

    int    m_width  = 0;
    int    m_height = 0;
    float  m_duration = 0.0f;
    float  m_currentTime = 0.0f;
    bool   m_playing = true;
    bool   m_hasFrame = false;
    bool   m_frameJustChanged = false;

    std::vector<uint8_t> m_currentFrame;
    std::string          m_error;
};

} // namespace eden
