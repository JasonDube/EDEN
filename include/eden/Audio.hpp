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

    // Stop a looping sound by ID
    void stopLoop(int loopId);

    // Check if a loop is playing
    bool isLoopPlaying(int loopId) const;

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

    std::unordered_map<std::string, int> m_soundCache;
    int m_nextHandle = 1;
};

} // namespace eden
