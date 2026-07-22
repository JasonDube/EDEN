#pragma once

#include <string>
#include <unordered_map>
#include <memory>

namespace eden {

/**
 * Simple audio system using miniaudio
 * Supports playing sound effects (wav, ogg, mp3, flac)
 */
class Audio {
public:
    static Audio& getInstance();

    // Initialize audio system
    bool init();

    // Shutdown audio system
    void shutdown();

    // Load a sound file and return a handle
    int loadSound(const std::string& filepath);

    // Play a sound by handle (returns immediately)
    void playSound(int handle, float volume = 1.0f);

    // Play a sound file directly (loads if not cached)
    void playSound(const std::string& filepath, float volume = 1.0f);

    // Start a looping sound (returns loop ID, or -1 on failure)
    int startLoop(const std::string& filepath, float volume = 1.0f);

    // Play from the start through the end ONCE, then loop back to loopStartSeconds forever
    // (mirrors a video that plays once then loops a tail range). Returns loop ID, or -1.
    int startLoopFrom(const std::string& filepath, float loopStartSeconds, float volume = 1.0f);

    // General ranged playback: begin at startSec; if loopBegSec >= 0, loop the sub-range
    // [loopBegSec, loopEndSec] forever (loopEndSec <= 0 means "to end"); otherwise play once.
    // Mirrors a video that starts somewhere and loops a middle range. Returns loop ID, or -1.
    int startLoopRange(const std::string& filepath, float startSec,
                       float loopBegSec, float loopEndSec, float volume = 1.0f);

    // Start a seamless crossfade loop (two overlapping copies, no gap)
    // Returns loop ID for the pair, or -1 on failure
    int startCrossfadeLoop(const std::string& filepath, float volume = 1.0f);

    // Stop a looping sound by ID
    void stopLoop(int loopId);

    // Set volume on a running loop (for distance attenuation)
    void setLoopVolume(int loopId, float volume);

    // Check if a loop is playing
    bool isLoopPlaying(int loopId) const;

    // Microphone recording (push-to-talk)
    bool startRecording();
    bool stopRecording(const std::string& outputPath);  // saves WAV to outputPath
    bool isRecording() const { return m_recording; }

    // Check if initialized
    bool isInitialized() const { return m_initialized; }

private:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized = false;
    bool m_recording = false;

    std::unordered_map<std::string, int> m_soundCache;
    int m_nextHandle = 1;
};

} // namespace eden
