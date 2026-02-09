// Enable all decoders
#define MA_ENABLE_VORBIS
#define MA_ENABLE_FLAC
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <eden/Audio.hpp>
#include <iostream>
#include <vector>
#include <filesystem>

namespace eden {

struct Audio::Impl {
    ma_engine engine;
    std::vector<ma_sound*> sounds;
    std::unordered_map<int, ma_sound*> loops;
    int nextLoopId = 1;
};

Audio& Audio::getInstance() {
    static Audio instance;
    return instance;
}

Audio::~Audio() {
    shutdown();
}

bool Audio::init() {
    if (m_initialized) return true;

    m_impl = std::make_unique<Impl>();

    ma_result result = ma_engine_init(nullptr, &m_impl->engine);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
        m_impl.reset();
        return false;
    }

    m_initialized = true;
    return true;
}

void Audio::shutdown() {
    if (!m_initialized) return;

    // Clean up sounds
    if (m_impl) {
        for (auto* sound : m_impl->sounds) {
            if (sound) {
                ma_sound_uninit(sound);
                delete sound;
            }
        }
        m_impl->sounds.clear();

        // Clean up loops
        for (auto& pair : m_impl->loops) {
            if (pair.second) {
                ma_sound_uninit(pair.second);
                delete pair.second;
            }
        }
        m_impl->loops.clear();

        ma_engine_uninit(&m_impl->engine);
        m_impl.reset();
    }

    m_soundCache.clear();
    m_initialized = false;
}

int Audio::loadSound(const std::string& filepath) {
    if (!m_initialized) {
        std::cerr << "Audio not initialized" << std::endl;
        return -1;
    }

    // Check cache
    auto it = m_soundCache.find(filepath);
    if (it != m_soundCache.end()) {
        return it->second;
    }

    // Load the sound
    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(
        &m_impl->engine,
        filepath.c_str(),
        MA_SOUND_FLAG_DECODE,  // Decode to memory for faster playback
        nullptr, nullptr,
        sound
    );

    if (result != MA_SUCCESS) {
        std::cerr << "Failed to load sound: " << filepath << std::endl;
        delete sound;
        return -1;
    }

    int handle = m_nextHandle++;
    m_impl->sounds.push_back(sound);
    m_soundCache[filepath] = handle;

    std::cout << "Loaded sound: " << filepath << " (handle " << handle << ")" << std::endl;
    return handle;
}

void Audio::playSound(int handle, float volume) {
    if (!m_initialized || handle < 1) return;

    // Find the sound by handle
    for (const auto& pair : m_soundCache) {
        if (pair.second == handle) {
            playSound(pair.first, volume);
            return;
        }
    }
}

void Audio::playSound(const std::string& filepath, float volume) {
    if (!m_initialized) return;

    // Get absolute path for miniaudio
    std::string absPath = filepath;
    if (!std::filesystem::path(filepath).is_absolute()) {
        absPath = std::filesystem::absolute(filepath).string();
    }

    // Play the sound using the engine's one-shot API
    ma_engine_play_sound(&m_impl->engine, absPath.c_str(), nullptr);
}

int Audio::startLoop(const std::string& filepath, float volume) {
    if (!m_initialized) return -1;

    // Get absolute path
    std::string absPath = filepath;
    if (!std::filesystem::path(filepath).is_absolute()) {
        absPath = std::filesystem::absolute(filepath).string();
    }

    // Create and configure the looping sound
    ma_sound* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(
        &m_impl->engine,
        absPath.c_str(),
        MA_SOUND_FLAG_DECODE,
        nullptr, nullptr,
        sound
    );

    if (result != MA_SUCCESS) {
        delete sound;
        return -1;
    }

    ma_sound_set_looping(sound, MA_TRUE);
    ma_sound_set_volume(sound, volume);
    ma_sound_start(sound);

    int loopId = m_impl->nextLoopId++;
    m_impl->loops[loopId] = sound;
    return loopId;
}

void Audio::stopLoop(int loopId) {
    if (!m_initialized || !m_impl) return;

    auto it = m_impl->loops.find(loopId);
    if (it != m_impl->loops.end()) {
        if (it->second) {
            ma_sound_stop(it->second);
            ma_sound_uninit(it->second);
            delete it->second;
        }
        m_impl->loops.erase(it);
    }
}

bool Audio::isLoopPlaying(int loopId) const {
    if (!m_initialized || !m_impl) return false;

    auto it = m_impl->loops.find(loopId);
    if (it != m_impl->loops.end() && it->second) {
        return ma_sound_is_playing(it->second);
    }
    return false;
}

} // namespace eden
