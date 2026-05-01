#include "VideoPlayer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <iostream>

namespace eden {

VideoPlayer::VideoPlayer() = default;
VideoPlayer::~VideoPlayer() { close(); }

void VideoPlayer::close() {
    if (m_swsCtx)    { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_packet)    { av_packet_free(&m_packet); }
    if (m_decoded)   { av_frame_free(&m_decoded); }
    if (m_rgba)      { av_frame_free(&m_rgba); }
    if (m_codecCtx)  { avcodec_free_context(&m_codecCtx); }
    if (m_formatCtx) { avformat_close_input(&m_formatCtx); }
    m_videoStream = -1;
    m_timeBase = 0.0;
    m_nextFramePts = 0.0;
    m_width = m_height = 0;
    m_duration = 0.0f;
    m_currentTime = 0.0f;
    m_hasFrame = false;
    m_frameJustChanged = false;
    m_currentFrame.clear();
    m_error.clear();
}

bool VideoPlayer::open(const std::string& filepath) {
    close();

    if (avformat_open_input(&m_formatCtx, filepath.c_str(), nullptr, nullptr) < 0) {
        m_error = "avformat_open_input failed for " + filepath;
        return false;
    }
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        m_error = "avformat_find_stream_info failed";
        close();
        return false;
    }

    // First video stream wins.
    for (unsigned i = 0; i < m_formatCtx->nb_streams; ++i) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStream = static_cast<int>(i);
            break;
        }
    }
    if (m_videoStream < 0) {
        m_error = "No video stream in " + filepath;
        close();
        return false;
    }

    AVStream* stream = m_formatCtx->streams[m_videoStream];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        m_error = "No decoder for codec id " + std::to_string(stream->codecpar->codec_id);
        close();
        return false;
    }
    m_codecCtx = avcodec_alloc_context3(decoder);
    if (!m_codecCtx ||
        avcodec_parameters_to_context(m_codecCtx, stream->codecpar) < 0 ||
        avcodec_open2(m_codecCtx, decoder, nullptr) < 0) {
        m_error = "Failed to open decoder";
        close();
        return false;
    }

    m_width  = m_codecCtx->width;
    m_height = m_codecCtx->height;
    m_timeBase = av_q2d(stream->time_base);
    m_duration = (stream->duration != AV_NOPTS_VALUE)
                     ? static_cast<float>(stream->duration * m_timeBase)
                     : (m_formatCtx->duration > 0
                            ? static_cast<float>(m_formatCtx->duration) / AV_TIME_BASE
                            : 0.0f);

    m_decoded = av_frame_alloc();
    m_rgba    = av_frame_alloc();
    m_packet  = av_packet_alloc();
    if (!m_decoded || !m_rgba || !m_packet) {
        m_error = "av_frame/packet_alloc failed";
        close();
        return false;
    }

    // Allocate a single RGBA destination buffer reused per-frame.
    m_currentFrame.resize(static_cast<size_t>(m_width) * m_height * 4, 0);
    m_rgba->format = AV_PIX_FMT_RGBA;
    m_rgba->width  = m_width;
    m_rgba->height = m_height;
    m_rgba->data[0]     = m_currentFrame.data();
    m_rgba->linesize[0] = m_width * 4;

    m_swsCtx = sws_getContext(m_width, m_height, m_codecCtx->pix_fmt,
                              m_width, m_height, AV_PIX_FMT_RGBA,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        m_error = "sws_getContext failed";
        close();
        return false;
    }

    // Pull the first frame so we have something to display immediately.
    if (decodeNextFrame()) {
        m_hasFrame = true;
        m_currentTime = 0.0f;
    }
    return true;
}

bool VideoPlayer::decodeNextFrame() {
    if (!m_formatCtx || !m_codecCtx) return false;

    while (true) {
        // Try to receive a frame from whatever's already in the decoder.
        int recv = avcodec_receive_frame(m_codecCtx, m_decoded);
        if (recv == 0) {
            sws_scale(m_swsCtx,
                      m_decoded->data, m_decoded->linesize, 0, m_height,
                      m_rgba->data, m_rgba->linesize);
            int64_t pts = (m_decoded->best_effort_timestamp != AV_NOPTS_VALUE)
                              ? m_decoded->best_effort_timestamp
                              : m_decoded->pts;
            m_nextFramePts = (pts == AV_NOPTS_VALUE) ? 0.0 : pts * m_timeBase;
            m_frameJustChanged = true;
            return true;
        }
        if (recv != AVERROR(EAGAIN) && recv != AVERROR_EOF) return false;

        // Need more input: read packets until we hit our video stream or EOF.
        bool gotPacket = false;
        while (true) {
            int rd = av_read_frame(m_formatCtx, m_packet);
            if (rd < 0) {
                avcodec_send_packet(m_codecCtx, nullptr);  // flush
                gotPacket = false;
                break;
            }
            if (m_packet->stream_index == m_videoStream) {
                avcodec_send_packet(m_codecCtx, m_packet);
                av_packet_unref(m_packet);
                gotPacket = true;
                break;
            }
            av_packet_unref(m_packet);
        }
        if (!gotPacket) {
            // EOF — try to drain any remaining frames; if none, signal end.
            int finalRecv = avcodec_receive_frame(m_codecCtx, m_decoded);
            if (finalRecv == 0) {
                sws_scale(m_swsCtx,
                          m_decoded->data, m_decoded->linesize, 0, m_height,
                          m_rgba->data, m_rgba->linesize);
                m_frameJustChanged = true;
                return true;
            }
            return false;
        }
    }
}

void VideoPlayer::update(float deltaTime, bool loop) {
    if (!m_codecCtx || !m_playing) {
        m_frameJustChanged = false;
        return;
    }
    m_frameJustChanged = false;
    m_currentTime += deltaTime;

    // Pull frames as long as the playhead is past the next frame's PTS.
    while (m_currentTime >= m_nextFramePts) {
        if (!decodeNextFrame()) {
            // Reached end.
            if (loop && m_duration > 0.0f) {
                seek(0.0f);
                continue;
            } else {
                m_playing = false;
                return;
            }
        }
        m_hasFrame = true;
    }
}

void VideoPlayer::seek(float t) {
    if (!m_formatCtx || m_videoStream < 0) return;
    t = std::max(0.0f, t);
    if (m_duration > 0.0f) t = std::min(t, m_duration);

    int64_t target = static_cast<int64_t>(t / m_timeBase);
    av_seek_frame(m_formatCtx, m_videoStream, target, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecCtx);
    m_currentTime = t;
    m_nextFramePts = t;  // decodeNextFrame will overwrite once it pulls the next keyframe
    decodeNextFrame();   // prime the buffer with the first frame after the seek
}

} // namespace eden
